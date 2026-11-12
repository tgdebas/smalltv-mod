// Native tests for the Sonos response parsing (src/features/sonos/SonosXml.cpp,
// SonosParse.cpp): the streaming scanner, the two levels of XML escaping
// Sonos uses, group following, radio metadata and art URL resolution.
// Responses are fed through a Client that hands out bytes in small, uneven
// pieces, with "nothing available yet" gaps, the way a socket does.
#include <algorithm>
#include <cstdio>
#include <string>
#include "SonosParse.h"

static uint32_t g_now = 0;
uint32_t millis() { return g_now += 1; }

static int g_fail = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); g_fail++; } } while (0)
#define CHECK_STR(a, b, msg) do { if (strcmp((a), (b))) { printf("FAIL %s:%d %s: got \"%s\" want \"%s\"\n", __FILE__, __LINE__, msg, (a), (b)); g_fail++; } } while (0)

class FakeClient : public Client {
 public:
  explicit FakeClient(const std::string& body, unsigned step = 7) : b_(body), step_(step) {}
  int available() override {
    if (pos_ >= b_.size()) return 0;
    if (++tick_ % 5 == 0) return 0;             // a gap: data not here yet
    size_t n = 1 + (tick_ * step_) % 23;        // uneven chunk sizes
    return int(std::min(n, b_.size() - pos_));
  }
  int read(uint8_t* buf, size_t n) override {
    size_t c = std::min(n, b_.size() - pos_);
    memcpy(buf, b_.data() + pos_, c);
    pos_ += c;
    return int(c);
  }
  uint8_t connected() override { return pos_ < b_.size(); }

 private:
  std::string b_;
  size_t pos_ = 0;
  unsigned tick_ = 0, step_;
};

static std::string envelope(const std::string& inner) {
  return "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
         "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>" + inner +
         "</s:Body></s:Envelope>";
}

// ---- GetPositionInfo ---------------------------------------------------------------
static const char* kSpotify =
    "<u:GetPositionInfoResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
    "<Track>3</Track><TrackDuration>0:04:15</TrackDuration>"
    "<TrackMetaData>&lt;DIDL-Lite xmlns:dc=&quot;http://purl.org/dc/elements/1.1/&quot; "
    "xmlns:upnp=&quot;urn:schemas-upnp-org:metadata-1-0/upnp/&quot; "
    "xmlns:r=&quot;urn:schemas-rinconnetworks-com:metadata-1-0/&quot; "
    "xmlns=&quot;urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/&quot;&gt;"
    "&lt;item id=&quot;-1&quot; parentID=&quot;-1&quot; restricted=&quot;true&quot;&gt;"
    "&lt;res protocolInfo=&quot;sonos.com-spotify:*:audio/x-spotify:*&quot; duration=&quot;0:04:15&quot;&gt;"
    "x-sonos-spotify:spotify%3atrack%3a2SiXAy?sid=12&amp;amp;flags=8224&amp;amp;sn=7&lt;/res&gt;"
    "&lt;r:streamContent&gt;&lt;/r:streamContent&gt;"
    "&lt;upnp:albumArtURI&gt;/getaa?s=1&amp;amp;u=x-sonos-spotify%3aspotify%253atrack%253a2SiXAy%3fsid%3d12%26flags%3d8224%26sn%3d7&lt;/upnp:albumArtURI&gt;"
    "&lt;dc:title&gt;Rock &amp;amp; Roll Ain&amp;apos;t Noise Pollution&lt;/dc:title&gt;"
    "&lt;upnp:class&gt;object.item.audioItem.musicTrack&lt;/upnp:class&gt;"
    "&lt;dc:creator&gt;Beyonc\xc3\xa9 &amp;amp; Friends&lt;/dc:creator&gt;"
    "&lt;upnp:album&gt;Back In Black&lt;/upnp:album&gt;"
    "&lt;/item&gt;&lt;/DIDL-Lite&gt;</TrackMetaData>"
    "<TrackURI>x-sonos-spotify:spotify%3atrack%3a2SiXAy?sid=12&amp;flags=8224&amp;sn=7</TrackURI>"
    "<RelTime>0:01:02</RelTime><AbsTime>NOT_IMPLEMENTED</AbsTime>"
    "</u:GetPositionInfoResponse>";

static void testSpotify() {
  FakeClient c(envelope(kSpotify));
  XmlScanner x(c, -1, 5000);
  SonosTrack t = {};
  bool redirect = true;
  CHECK(sonosParsePosition(x, "192.168.1.20", t, redirect), "spotify parses");
  CHECK(!redirect, "spotify not a redirect");
  CHECK_STR(t.title, "Rock & Roll Ain't Noise Pollution", "title, second-level entities");
  CHECK_STR(t.artist, "Beyonc\xc3\xa9 & Friends", "artist keeps UTF-8");
  CHECK_STR(t.artUrl,
            "http://192.168.1.20:1400/getaa?s=1&u=x-sonos-spotify%3aspotify%253atrack%253a2SiXAy%3fsid%3d12%26flags%3d8224%26sn%3d7",
            "art via the coordinator's getaa proxy");
  CHECK(t.hasTrack, "has track");
}

static void testContentLengthStopsEarly() {
  // With a Content-Length the scanner stops there, even if more bytes follow
  // on the socket (keep-alive junk, the next response).
  std::string body = envelope(kSpotify);
  FakeClient c(body + "<dc:title>junk</dc:title>");
  XmlScanner x(c, (int32_t)body.size(), 5000);
  SonosTrack t = {};
  bool redirect;
  sonosParsePosition(x, "10.0.0.2", t, redirect);
  CHECK_STR(t.title, "Rock & Roll Ain't Noise Pollution", "content-length bounded");
}

static void testRadio() {
  const char* r =
      "<u:GetPositionInfoResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
      "<Track>1</Track><TrackMetaData>&lt;DIDL-Lite xmlns:dc=&quot;http://purl.org/dc/elements/1.1/&quot;&gt;"
      "&lt;item id=&quot;-1&quot;&gt;&lt;res protocolInfo=&quot;x-rincon-mp3radio:*:*:*&quot;&gt;x-rincon-mp3radio://stream.example/live&lt;/res&gt;"
      "&lt;r:streamContent&gt;Daft Punk - Get Lucky&lt;/r:streamContent&gt;"
      "&lt;dc:title&gt;Radio Paradise&lt;/dc:title&gt;"
      "&lt;upnp:albumArtURI&gt;http://img.example/rp.jpg&lt;/upnp:albumArtURI&gt;"
      "&lt;/item&gt;&lt;/DIDL-Lite&gt;</TrackMetaData>"
      "<TrackURI>x-rincon-mp3radio://stream.example/live</TrackURI></u:GetPositionInfoResponse>";
  FakeClient c(envelope(r), 3);
  XmlScanner x(c, -1, 5000);
  SonosTrack t = {};
  bool redirect;
  CHECK(sonosParsePosition(x, "192.168.1.20", t, redirect), "radio parses");
  CHECK_STR(t.title, "Get Lucky", "radio title from streamContent");
  CHECK_STR(t.artist, "Daft Punk", "radio artist from streamContent");
  CHECK_STR(t.artUrl, "http://img.example/rp.jpg", "absolute art kept");

  // Station still connecting: keep the station name.
  std::string conn = r;
  conn.replace(conn.find("Daft Punk - Get Lucky"), strlen("Daft Punk - Get Lucky"), "ZPSTR_CONNECTING");
  FakeClient c2(envelope(conn));
  XmlScanner x2(c2, -1, 5000);
  SonosTrack t2 = {};
  sonosParsePosition(x2, "192.168.1.20", t2, redirect);
  CHECK_STR(t2.title, "Radio Paradise", "placeholder streamContent ignored");
}

static void testGroupedMemberRedirects() {
  const char* r =
      "<u:GetPositionInfoResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
      "<Track>1</Track><TrackMetaData></TrackMetaData>"
      "<TrackURI>x-rincon:RINCON_000E58AAAAAA01400</TrackURI></u:GetPositionInfoResponse>";
  FakeClient c(envelope(r));
  XmlScanner x(c, -1, 5000);
  SonosTrack t = {};
  bool redirect = false;
  CHECK(!sonosParsePosition(x, "192.168.1.21", t, redirect), "member returns false");
  CHECK(redirect, "member asks for the coordinator");
}

static void testTvAndEmpty() {
  const char* tv =
      "<u:GetPositionInfoResponse><TrackMetaData>NOT_IMPLEMENTED</TrackMetaData>"
      "<TrackURI>x-sonos-htastream:RINCON_000E58BBBBBB01400:spdif</TrackURI></u:GetPositionInfoResponse>";
  FakeClient c(envelope(tv));
  XmlScanner x(c, -1, 5000);
  SonosTrack t = {};
  bool redirect;
  sonosParsePosition(x, "1.2.3.4", t, redirect);
  CHECK_STR(t.title, "TV", "tv input");
  CHECK(t.hasTrack, "tv counts as playing something");

  const char* empty =
      "<u:GetPositionInfoResponse><Track>0</Track><TrackMetaData></TrackMetaData>"
      "<TrackURI></TrackURI></u:GetPositionInfoResponse>";
  FakeClient c2(envelope(empty));
  XmlScanner x2(c2, -1, 5000);
  SonosTrack t2 = {};
  sonosParsePosition(x2, "1.2.3.4", t2, redirect);
  CHECK(!t2.hasTrack, "empty queue has no track");
}

// ---- GetTransportInfo --------------------------------------------------------------
static void testTransport() {
  const char* states[][2] = {{"PLAYING", "p"}, {"PAUSED_PLAYBACK", "a"}, {"STOPPED", "s"}, {"TRANSITIONING", "t"}};
  SonosPlay want[] = {SONOS_PLAYING, SONOS_PAUSED, SONOS_STOPPED, SONOS_TRANSITIONING};
  for (int i = 0; i < 4; i++) {
    std::string r = "<u:GetTransportInfoResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
                    "<CurrentTransportState>" + std::string(states[i][0]) + "</CurrentTransportState>"
                    "<CurrentTransportStatus>OK</CurrentTransportStatus><CurrentSpeed>1</CurrentSpeed>"
                    "</u:GetTransportInfoResponse>";
    FakeClient c(envelope(r));
    XmlScanner x(c, -1, 5000);
    CHECK(sonosParseTransport(x) == want[i], states[i][0]);
  }
}

// ---- GetZoneGroupState --------------------------------------------------------------
// Kitchen is grouped with Living Room and listed BEFORE the coordinator; the
// Living Room has a surround satellite with the same room name; one room name
// carries an escaped ampersand; the long attribute tail overflows the tag buffer.
static std::string topology() {
  std::string tail;
  for (int i = 0; i < 40; i++) tail += " Attr" + std::to_string(i) + "=&quot;value-value&quot;";
  return "<u:GetZoneGroupStateResponse xmlns:u=\"urn:schemas-upnp-org:service:ZoneGroupTopology:1\">"
         "<ZoneGroupState>&lt;ZoneGroupState&gt;&lt;ZoneGroups&gt;"
         "&lt;ZoneGroup Coordinator=&quot;RINCON_A01400&quot; ID=&quot;RINCON_A01400:3&quot;&gt;"
         "&lt;ZoneGroupMember UUID=&quot;RINCON_B01400&quot; Location=&quot;http://192.168.1.21:1400/xml/device_description.xml&quot; "
         "ZoneName=&quot;Kitchen&quot;" + tail + "/&gt;"
         "&lt;ZoneGroupMember UUID=&quot;RINCON_A01400&quot; Location=&quot;http://192.168.1.20:1400/xml/device_description.xml&quot; "
         "ZoneName=&quot;Living Room&quot; Icon=&quot;x-rincon-roomicon:living&quot;&gt;"
         "&lt;Satellite UUID=&quot;RINCON_S01400&quot; Location=&quot;http://192.168.1.25:1400/xml/device_description.xml&quot; "
         "ZoneName=&quot;Living Room&quot; Invisible=&quot;1&quot;/&gt;"
         "&lt;/ZoneGroupMember&gt;&lt;/ZoneGroup&gt;"
         "&lt;ZoneGroup Coordinator=&quot;RINCON_C01400&quot; ID=&quot;RINCON_C01400:9&quot;&gt;"
         "&lt;ZoneGroupMember UUID=&quot;RINCON_C01400&quot; Location=&quot;http://192.168.1.22:1400/xml/device_description.xml&quot; "
         "ZoneName=&quot;Kids &amp;amp; Play&quot;/&gt;"
         "&lt;/ZoneGroup&gt;&lt;/ZoneGroups&gt;&lt;VanishedDevices&gt;&lt;Device UUID=&quot;RINCON_X01400&quot; "
         "ZoneName=&quot;Old&quot;/&gt;&lt;/VanishedDevices&gt;&lt;/ZoneGroupState&gt;</ZoneGroupState>"
         "</u:GetZoneGroupStateResponse>";
}

static std::string g_rooms;
static void collect(const char* n) { g_rooms += n; g_rooms += "|"; }

static void testTopology() {
  struct { const char* want; const char* ip; const char* room; } cases[] = {
      {"Kitchen", "192.168.1.20", "Kitchen"},          // member -> coordinator listed later
      {"living room", "192.168.1.20", "Living Room"},  // case-insensitive
      {"Kids & Play", "192.168.1.22", "Kids & Play"},  // entity in the name
      {"", "192.168.1.20", "Living Room"},             // none set: first group's coordinator
  };
  for (auto& k : cases) {
    FakeClient c(envelope(topology()), 5);
    XmlScanner x(c, -1, 5000);
    SonosTopology t;
    g_rooms.clear();
    CHECK(sonosParseTopology(x, k.want, t, collect), k.want);
    CHECK_STR(t.coordIp, k.ip, k.want);
    CHECK_STR(t.room, k.room, k.want);
  }
  CHECK_STR(g_rooms.c_str(), "Kitchen|Living Room|Kids & Play|", "visible rooms only, satellites skipped");

  FakeClient c(envelope(topology()));
  XmlScanner x(c, -1, 5000);
  SonosTopology t;
  CHECK(!sonosParseTopology(x, "Garage", t, nullptr), "unknown room");
  CHECK(!t.coordIp[0], "no coordinator for an unknown room");
}

// ---- helpers --------------------------------------------------------------------------
static void testHelpers() {
  char s[64];
  strcpy(s, "a &amp; b &lt;c&gt; &#233; &#x263A; &bogus; &");
  xmlUnescape(s);
  CHECK_STR(s, "a & b <c> \xc3\xa9 \xe2\x98\xba &bogus; &", "xmlUnescape");

  char v[32];
  CHECK(xmlAttr("ZoneGroupMember UUID=\"R1\" XUUID=\"no\"", "UUID", v, sizeof v), "attr found");
  CHECK_STR(v, "R1", "whole attribute name only");
  CHECK(!xmlAttr("a Coordinatorx=\"1\"", "Coordinator", v, sizeof v), "prefix is not a match");

  char ip[40];
  CHECK(sonosHostFromUrl("http://10.0.0.7:1400/xml/x.xml", ip, sizeof ip), "host from url");
  CHECK_STR(ip, "10.0.0.7", "host from url value");
}

int main() {
  testSpotify();
  testContentLengthStopsEarly();
  testRadio();
  testGroupedMemberRedirects();
  testTvAndEmpty();
  testTransport();
  testTopology();
  testHelpers();
  if (g_fail) { printf("%d check(s) failed\n", g_fail); return 1; }
  printf("sonos parse tests: all passed\n");
  return 0;
}
