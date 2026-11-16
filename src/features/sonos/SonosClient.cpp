#include "SonosClient.h"
#include "Platform.h"
#include "SonosXml.h"
#include "SonosParse.h"
#include <string.h>

// ---- state ------------------------------------------------------------------
static SonosTrack g_track;
static uint32_t   g_version = 0;
static uint32_t   g_publishedKey = 0;   // fingerprint of what g_version last announced
// Radio: the station logo from GetMediaInfo, read once per station (keyed by
// the TrackURI hash), not on every poll.
static char       g_stationArt[SONOS_ART_LEN] = "";
static uint32_t   g_stationUri = 0;
static bool       g_error = false;
static SonosStage g_stage = SONOS_IDLE;
static uint32_t   g_lastPlayingMs = 0;

static char     g_seed[MAX_SONOS_HOST_LEN] = "";   // any speaker we can ask
static char     g_coord[MAX_SONOS_HOST_LEN] = "";  // the room's group coordinator
static char     g_room[SONOS_ROOM_NAME] = "";      // room actually followed
static uint32_t g_nextPollMs = 0;
static uint32_t g_nextTopoMs = 0;
static uint8_t  g_failStreak = 0;

static char    g_rooms[SONOS_MAX_ROOMS][SONOS_LIST_NAME];
static uint8_t g_roomCount = 0;

// SSDP discovery (non-blocking: send, then collect replies over later ticks).
static WiFiUDP  g_udp;
static bool     g_ssdpOpen = false;
static uint32_t g_ssdpUntil = 0;
static uint32_t g_nextSsdpMs = 0;

const SonosTrack& sonosTrack()    { return g_track; }
uint32_t    sonosVersion()        { return g_version; }
bool        sonosError()          { return g_error; }
SonosStage  sonosStage()          { return g_stage; }
const char* sonosCoordinator()    { return g_coord; }
const char* sonosRoomName()       { return g_room; }
uint8_t     sonosRoomCount()      { return g_roomCount; }
const char* sonosRoomAt(uint8_t i){ return i < g_roomCount ? g_rooms[i] : ""; }

bool sonosActive() {
  if (!g_track.hasTrack) return false;
  if (g_track.state == SONOS_PLAYING || g_track.state == SONOS_TRANSITIONING) return true;
  // Paused or stopped: keep the page in the rotation for a few minutes, so a
  // quick pause does not make it vanish, then let the carousel drop it.
  return g_lastPlayingMs && millis() - g_lastPlayingMs < 5UL * 60UL * 1000UL;
}

const char* sonosStageName() {
  switch (g_stage) {
    case SONOS_DISCOVERING:  return "searching for speakers";
    case SONOS_NO_SPEAKER:   return "no speaker found";
    case SONOS_NO_ROOM:      return "room not found";
    case SONOS_CONNECT_FAIL: return "speaker not answering";
    case SONOS_HTTP_ERROR:   return "http error";
    case SONOS_OK:           return "ok";
    default:                 return "idle";
  }
}

static void closeSsdp() {
  if (g_ssdpOpen) g_udp.stop();
  g_ssdpOpen = false;
}

void sonosInit(const Settings& s) {
  memset(&g_track, 0, sizeof(g_track));
  g_version++;
  g_publishedKey = 0;
  g_stationArt[0] = 0;
  g_stationUri = 0;
  g_error = false;
  g_stage = SONOS_IDLE;
  g_lastPlayingMs = 0;
  strlcpy(g_seed, s.sonos.host.c_str(), sizeof(g_seed));
  g_coord[0] = 0;
  g_room[0] = 0;
  g_roomCount = 0;
  g_failStreak = 0;
  g_nextPollMs = g_nextTopoMs = g_nextSsdpMs = millis();
  closeSsdp();
}

void sonosForceRefresh() { g_nextPollMs = millis(); }

// ---- SOAP -------------------------------------------------------------------
// One POST to a speaker; on 200 the caller scans the body. HTTP/1.0 keeps the
// body unchunked, so the scanner reads raw XML.
struct Soap {
  WiFiClient client;
  HTTPClient http;
  int        code = 0;

  bool call(const char* host, const __FlashStringHelper* path, const __FlashStringHelper* service,
            const __FlashStringHelper* action, const __FlashStringHelper* args, uint16_t timeoutMs) {
    String url = F("http://");
    url += host;
    url += ':';
    url += SONOS_PORT;
    url += path;
    http.useHTTP10(true);
    http.setReuse(false);
    http.setTimeout(timeoutMs);
    if (!http.begin(client, url)) { code = -1; return false; }
    String soapAction = F("\"urn:schemas-upnp-org:service:");
    soapAction += service;
    soapAction += F(":1#");
    soapAction += action;
    soapAction += '"';
    http.addHeader(F("Content-Type"), F("text/xml; charset=\"utf-8\""));
    http.addHeader(F("SOAPACTION"), soapAction);

    String body = F("<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                    "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                    "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:");
    body += action;
    body += F(" xmlns:u=\"urn:schemas-upnp-org:service:");
    body += service;
    body += F(":1\">");
    body += args;
    body += F("</u:");
    body += action;
    body += F("></s:Body></s:Envelope>");

    code = http.POST(body);
    return code == HTTP_CODE_OK;
  }
  ~Soap() { http.end(); }
};

static void noteFailure(int code) {
  g_error = true;
  g_stage = (code < 0) ? SONOS_CONNECT_FAIL : SONOS_HTTP_ERROR;
  // A coordinator that stops answering has usually handed the group to another
  // speaker (or lost its DHCP lease): re-read the topology soon.
  if (++g_failStreak >= 2) {
    g_coord[0] = 0;
    g_nextTopoMs = millis();
  }
}

// ---- SSDP discovery -----------------------------------------------------------
static void ssdpService() {
  if (!g_ssdpOpen) {
    if ((int32_t)(millis() - g_nextSsdpMs) < 0) return;
    if (!g_udp.begin(0)) { g_nextSsdpMs = millis() + 10000; return; }
    g_ssdpOpen = true;
    g_ssdpUntil = millis() + 2500;
    g_stage = SONOS_DISCOVERING;
    static const char kSearch[] PROGMEM =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 1\r\n"
        "ST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n\r\n";
    char pkt[sizeof(kSearch)];
    strcpy_P(pkt, kSearch);
    for (uint8_t i = 0; i < 2; i++) {   // UDP: send twice, cheap insurance
#if defined(SMALLTV_FAMILY_ESP8266)
      g_udp.beginPacketMulticast(IPAddress(239, 255, 255, 250), 1900, WiFi.localIP());
#else
      g_udp.beginPacket(IPAddress(239, 255, 255, 250), 1900);
#endif
      g_udp.write((const uint8_t*)pkt, strlen(pkt));
      g_udp.endPacket();
    }
    return;
  }

  int len = g_udp.parsePacket();
  if (len > 0) {
    char buf[384];
    int n = g_udp.read(buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = 0;
      // Header names are case-insensitive; Sonos sends "LOCATION:".
      char* loc = strstr(buf, "LOCATION:");
      if (!loc) loc = strstr(buf, "Location:");
      if (loc && sonosHostFromUrl(loc, g_seed, sizeof(g_seed))) {
        closeSsdp();
        g_nextTopoMs = millis();
        return;
      }
    }
  }
  if ((int32_t)(millis() - g_ssdpUntil) >= 0) {
    closeSsdp();
    g_stage = SONOS_NO_SPEAKER;
    g_nextSsdpMs = millis() + 30000;   // try again in a while
  }
}

// ---- topology: room -> coordinator --------------------------------------------
static void addRoom(const char* name) {
  for (uint8_t i = 0; i < g_roomCount; i++)
    if (!strncasecmp(g_rooms[i], name, SONOS_LIST_NAME - 1)) return;
  if (g_roomCount >= SONOS_MAX_ROOMS) return;
  strlcpy(g_rooms[g_roomCount++], name, SONOS_LIST_NAME);
}

static void readTopology(const Settings& s) {
  g_nextTopoMs = millis() + SONOS_TOPOLOGY_MS;
  Soap soap;
  if (!soap.call(g_seed, F("/ZoneGroupTopology/Control"), F("ZoneGroupTopology"),
                 F("GetZoneGroupState"), F(""), s.httpTimeout)) {
    noteFailure(soap.code);
    // A seed found by SSDP may be gone; search again next time round.
    if (s.sonos.host.length() == 0) g_seed[0] = 0;
    return;
  }
  g_roomCount = 0;
  XmlScanner x(soap.http.getStream(), soap.http.getSize(), 6000);
  SonosTopology topo;
  if (!sonosParseTopology(x, s.sonos.room.c_str(), topo, addRoom)) {
    g_coord[0] = 0;
    g_error = x.timedOut();
    g_stage = x.timedOut() ? SONOS_CONNECT_FAIL : SONOS_NO_ROOM;
    g_nextTopoMs = millis() + 15000;
    return;
  }
  strlcpy(g_coord, topo.coordIp, sizeof(g_coord));
  strlcpy(g_room, topo.room, sizeof(g_room));
  g_failStreak = 0;
  g_nextPollMs = millis();
}

// ---- transport + track ----------------------------------------------------------
static bool readTransport(const Settings& s, SonosPlay& out) {
  Soap soap;
  if (!soap.call(g_coord, F("/MediaRenderer/AVTransport/Control"), F("AVTransport"),
                 F("GetTransportInfo"), F("<InstanceID>0</InstanceID>"), s.httpTimeout)) {
    noteFailure(soap.code);
    return false;
  }
  XmlScanner x(soap.http.getStream(), soap.http.getSize(), 3000);
  out = sonosParseTransport(x);
  return true;
}

static bool readPosition(const Settings& s, SonosTrack& t, bool& redirect) {
  redirect = false;
  Soap soap;
  if (!soap.call(g_coord, F("/MediaRenderer/AVTransport/Control"), F("AVTransport"),
                 F("GetPositionInfo"), F("<InstanceID>0</InstanceID>"), s.httpTimeout)) {
    noteFailure(soap.code);
    return false;
  }
  XmlScanner x(soap.http.getStream(), soap.http.getSize(), 3000);
  return sonosParsePosition(x, g_coord, t, redirect);
}

static bool readStationArt(const Settings& s) {
  Soap soap;
  if (!soap.call(g_coord, F("/MediaRenderer/AVTransport/Control"), F("AVTransport"),
                 F("GetMediaInfo"), F("<InstanceID>0</InstanceID>"), s.httpTimeout))
    return false;   // not a poll failure: the track itself was read fine
  XmlScanner x(soap.http.getStream(), soap.http.getSize(), 3000);
  sonosParseMediaInfo(x, g_coord, g_stationArt, sizeof(g_stationArt));
  return true;
}

// ---------------------------------------------------------------------------------
void sonosService(const Settings& s, bool background) {
  if (WiFi.status() != WL_CONNECTED) return;

  if (!g_seed[0]) { ssdpService(); return; }

  if (!g_coord[0] || (int32_t)(millis() - g_nextTopoMs) >= 0) {
    if ((int32_t)(millis() - g_nextTopoMs) < 0) return;
    readTopology(s);
    if (!g_coord[0]) return;
  }

  if ((int32_t)(millis() - g_nextPollMs) < 0) return;
  uint16_t every = s.sonos.pollSec;
  if (background && every < SONOS_BG_POLL_SEC) every = SONOS_BG_POLL_SEC;
  g_nextPollMs = millis() + (uint32_t)every * 1000UL;

  SonosPlay state;
  if (!readTransport(s, state)) return;

  // Parsed straight into g_track: a second ~480-byte copy is heap the
  // ESP8266 cannot spare. A change is found by comparing fingerprints with
  // what was last published, so a failed or redirected poll publishes nothing.
  SonosTrack& t = g_track;
  t.state = state;
  bool redirect = false;
  if (!readPosition(s, t, redirect)) {
    if (redirect) {   // the room joined a group: find the new coordinator now
      g_coord[0] = 0;
      g_nextTopoMs = millis();
    }
    return;
  }

  // Radio has no per-track cover; the station's logo is in the media info,
  // the same place the Sonos app takes it from.
  if (t.radio && !t.artUrl[0]) {
    if (t.uriHash != g_stationUri) {
      g_stationArt[0] = 0;
      if (readStationArt(s)) g_stationUri = t.uriHash;   // retried next poll if it failed
    }
    if (g_stationArt[0]) {
      strlcpy(t.artUrl, g_stationArt, sizeof(t.artUrl));
      t.artSource = SONOS_ART_STATION;
      t.hasTrack = true;
    }
  }

  g_error = false;
  g_failStreak = 0;
  g_stage = SONOS_OK;
  if (state == SONOS_PLAYING || state == SONOS_TRANSITIONING) g_lastPlayingMs = millis();
  if (state == SONOS_STOPPED && !t.title[0]) t.hasTrack = false;

  uint32_t key = sonosTrackKey(t) ^ ((uint32_t)t.state << 24) ^ (t.hasTrack ? 0x5A5A5A5Au : 0);
  if (key != g_publishedKey) {
    g_publishedKey = key;
    g_version++;
  }
}

uint32_t sonosTrackKey(const SonosTrack& t) {
  uint32_t h = 2166136261u;   // FNV-1a over title, artist and art URL
  const char* parts[] = {t.title, t.artist, t.artUrl};
  for (const char* p : parts) {
    for (; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    h = (h ^ 0xFF) * 16777619u;   // field separator
  }
  return h;
}
