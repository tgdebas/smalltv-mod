#include "SonosParse.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>

// Literals go through PSTR: on the ESP8266 a plain string constant takes RAM
// for good, and this file is all tag and attribute names.

bool sonosHostFromUrl(const char* url, char* out, size_t outLen) {
  const char* p = strstr(url, "://");
  if (!p) return false;
  p += 3;
  size_t n = 0;
  while (p[n] && p[n] != ':' && p[n] != '/' && n + 1 < outLen) { out[n] = p[n]; n++; }
  out[n] = 0;
  return n > 0;
}

SonosPlay sonosParseTransport(XmlScanner& x) {
  bool inState = false;
  char name[32];
  XmlScanner::Kind k;
  while ((k = x.next()) != XmlScanner::END) {
    if (k == XmlScanner::TAG) {
      xmlTagName(x.value(), name, sizeof(name));
      inState = x.value()[0] != '/' && !strcmp_P(name, PSTR("CurrentTransportState"));
    } else if (inState) {
      const char* v = x.value();
      return !strcmp_P(v, PSTR("PLAYING"))         ? SONOS_PLAYING
           : !strcmp_P(v, PSTR("PAUSED_PLAYBACK")) ? SONOS_PAUSED
           : !strcmp_P(v, PSTR("TRANSITIONING"))   ? SONOS_TRANSITIONING
                                           : SONOS_STOPPED;
    }
  }
  return SONOS_STOPPED;
}

// Fields of GetPositionInfo we keep. The DIDL-Lite in TrackMetaData arrives
// escaped; XmlScanner's first unescape exposes its tags, xmlUnescape the rest.
enum PosField : uint8_t { PF_NONE, PF_URI, PF_TITLE, PF_CREATOR, PF_ALBUM, PF_ART, PF_STREAM };

bool sonosParsePosition(XmlScanner& x, const char* coordIp, SonosTrack& t, bool& redirect) {
  redirect = false;
  char uri[200] = "", album[48] = "", stream[SONOS_TITLE_LEN] = "", art[SONOS_ART_LEN] = "";
  t.title[0] = t.artist[0] = t.artUrl[0] = 0;

  PosField f = PF_NONE;
  char name[32];
  XmlScanner::Kind k;
  while ((k = x.next()) != XmlScanner::END) {
    if (k == XmlScanner::TAG) {
      const char* tag = x.value();
      size_t tl = strlen(tag);
      if (tag[0] == '/' || (tl && tag[tl - 1] == '/')) { f = PF_NONE; continue; }
      xmlTagName(tag, name, sizeof(name));
      f = !strcmp_P(name, PSTR("TrackURI"))         ? PF_URI
        : !strcmp_P(name, PSTR("dc:title"))         ? PF_TITLE
        : !strcmp_P(name, PSTR("dc:creator"))       ? PF_CREATOR
        : !strcmp_P(name, PSTR("upnp:album"))       ? PF_ALBUM
        : !strcmp_P(name, PSTR("upnp:albumArtURI")) ? PF_ART
        : !strcmp_P(name, PSTR("r:streamContent"))  ? PF_STREAM
                                            : PF_NONE;
      continue;
    }
    char* dst = nullptr;
    size_t cap = 0;
    switch (f) {
      case PF_URI:     dst = uri;      cap = sizeof(uri);      break;
      case PF_TITLE:   dst = t.title;  cap = sizeof(t.title);  break;
      case PF_CREATOR: dst = t.artist; cap = sizeof(t.artist); break;
      case PF_ALBUM:   dst = album;    cap = sizeof(album);    break;
      case PF_ART:     dst = art;      cap = sizeof(art);      break;
      case PF_STREAM:  dst = stream;   cap = sizeof(stream);   break;
      default: break;
    }
    if (dst && !dst[0]) {   // first occurrence wins
      strlcpy(dst, x.value(), cap);
      xmlUnescape(dst);
    }
    f = PF_NONE;
  }

  // A grouped room's own transport points at its coordinator.
  if (!strncmp_P(uri, PSTR("x-rincon:"), 9)) { redirect = true; return false; }

  // Inputs with no track metadata.
  if (!strncmp_P(uri, PSTR("x-sonos-htastream:"), 18)) { xmlCopyP(t.title, PSTR("TV"), sizeof(t.title)); t.artist[0] = 0; }
  else if (!strncmp_P(uri, PSTR("x-rincon-stream:"), 16)) { xmlCopyP(t.title, PSTR("Line-In"), sizeof(t.title)); t.artist[0] = 0; }

  // Radio: the live "Artist - Title" is in streamContent; dc:title is then
  // the station or the stream URL. ZPSTR_* are Sonos status placeholders
  // ("connecting", "buffering").
  if (stream[0] && strncmp_P(stream, PSTR("ZPSTR_"), 6)) {
    char* dash = strstr(stream, " - ");
    if (dash) {
      *dash = 0;
      strlcpy(t.artist, stream, sizeof(t.artist));
      strlcpy(t.title, dash + 3, sizeof(t.title));
    } else {
      strlcpy(t.artist, t.title, sizeof(t.artist));   // station name
      strlcpy(t.title, stream, sizeof(t.title));
    }
  }
  if (!t.artist[0] && album[0]) strlcpy(t.artist, album, sizeof(t.artist));

  // Album art: relative URIs go through the speaker's /getaa proxy, which
  // fetches from the music service and serves it over plain HTTP.
  // A URL that does not fit is dropped, not cut: a truncated one only 404s.
  if (art[0] == '/') {
    char host[64];
    int hn = snprintf_P(host, sizeof(host), PSTR("http://%s:%d"), coordIp, 1400);
    if (hn > 0 && (size_t)hn < sizeof(host) && hn + strlen(art) < sizeof(t.artUrl)) {
      memcpy(t.artUrl, host, hn);
      strlcpy(t.artUrl + hn, art, sizeof(t.artUrl) - hn);
    }
  } else if (!strncmp_P(art, PSTR("http"), 4) && strlen(art) < sizeof(t.artUrl)) {
    strlcpy(t.artUrl, art, sizeof(t.artUrl));
  }
  t.artSource = t.artUrl[0] ? SONOS_ART_METADATA : SONOS_ART_NONE;
  strlcpy(t.trackUri, uri, sizeof(t.trackUri));
  // Radio: a stream added by URL (x-rincon-mp3radio, also aac: and
  // hls-radio:) carries no cover and /getaa has none for it (HTTP 404);
  // a station from a Sonos radio service (x-sonosapi-stream) usually has a
  // logo there, so that one still gets the /getaa request below.
  bool directStream = !strncmp_P(uri, PSTR("x-rincon-mp3radio:"), 18) ||
                      !strncmp_P(uri, PSTR("aac:"), 4) || !strncmp_P(uri, PSTR("hls-radio:"), 10);
  t.radio = directStream || !strncmp_P(uri, PSTR("x-sonosapi-stream:"), 18) ||
            !strncmp_P(uri, PSTR("x-sonosapi-radio:"), 17);

  // No cover in the track info (some AirPlay and Spotify Connect sessions):
  // ask the speaker for the track's own art the way the Sonos apps do,
  // /getaa?s=1&u=<TrackURI, percent-encoded>. TV and line-in have none.
  if (!t.artUrl[0] && uri[0] && !directStream && strncmp_P(uri, PSTR("x-sonos-htastream:"), 18) &&
      strncmp_P(uri, PSTR("x-rincon-stream:"), 16)) {
    int n = snprintf_P(t.artUrl, sizeof(t.artUrl), PSTR("http://%s:%d/getaa?s=1&u="), coordIp, 1400);
    bool fits = n > 0 && (size_t)n < sizeof(t.artUrl);
    static const char kHex[] = "0123456789abcdef";
    for (const char* p = uri; fits && *p; p++) {
      unsigned char c = (unsigned char)*p;
      bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                   c == '-' || c == '_' || c == '.' || c == '~';
      if ((size_t)n + (plain ? 1 : 3) >= sizeof(t.artUrl)) { fits = false; break; }
      if (plain) { t.artUrl[n++] = (char)c; }
      else { t.artUrl[n++] = '%'; t.artUrl[n++] = kHex[c >> 4]; t.artUrl[n++] = kHex[c & 15]; }
    }
    if (fits) { t.artUrl[n] = 0; t.artSource = SONOS_ART_FROM_TRACK; }
    else t.artUrl[0] = 0;
  }
  t.hasTrack = t.title[0] || t.artUrl[0];
  return true;
}

bool sonosParseTopology(XmlScanner& x, const char* want, SonosTopology& out,
                        void (*onRoom)(const char* name)) {
  char groupCoord[40] = "";    // Coordinator UUID of the group being read
  char groupCoordIp[SONOS_IP_LEN] = "";
  bool groupHasRoom = false;
  char firstCoordIp[SONOS_IP_LEN] = "";
  char firstRoom[SONOS_ROOM_NAME] = "";
  char foundRoom[SONOS_ROOM_NAME] = "";
  out.coordIp[0] = out.room[0] = 0;

  char name[24], attr[160];
  XmlScanner::Kind k;
  while ((k = x.next()) != XmlScanner::END) {
    if (k != XmlScanner::TAG) continue;
    const char* tag = x.value();
    bool closing = tag[0] == '/';
    xmlTagName(tag, name, sizeof(name));

    if (!strcmp_P(name, PSTR("ZoneGroup"))) {
      if (!closing) {
        xmlAttr(tag, PSTR("Coordinator"), groupCoord, sizeof(groupCoord));
        groupCoordIp[0] = 0;
        groupHasRoom = false;
      } else if (groupHasRoom && groupCoordIp[0] && !out.coordIp[0]) {
        strlcpy(out.coordIp, groupCoordIp, sizeof(out.coordIp));
        strlcpy(out.room, foundRoom, sizeof(out.room));
      }
      continue;
    }
    if (closing || strcmp_P(name, PSTR("ZoneGroupMember"))) continue;   // Satellites etc.

    char uuid[40], ip[SONOS_IP_LEN], zone[SONOS_ROOM_NAME];
    xmlAttr(tag, PSTR("UUID"), uuid, sizeof(uuid));
    xmlAttr(tag, PSTR("Location"), attr, sizeof(attr));
    if (!sonosHostFromUrl(attr, ip, sizeof(ip))) ip[0] = 0;
    xmlAttr(tag, PSTR("ZoneName"), zone, sizeof(zone));
    bool invisible = xmlAttr(tag, PSTR("Invisible"), attr, sizeof(attr)) && attr[0] == '1';

    if (!strcmp(uuid, groupCoord)) {
      strlcpy(groupCoordIp, ip, sizeof(groupCoordIp));
      if (!firstCoordIp[0]) {   // fallback when no room is configured
        strlcpy(firstCoordIp, ip, sizeof(firstCoordIp));
        strlcpy(firstRoom, zone, sizeof(firstRoom));
      }
    }
    if (!invisible && onRoom && zone[0]) onRoom(zone);
    if (want[0] && !strcasecmp(zone, want) && !groupHasRoom) {
      groupHasRoom = true;
      strlcpy(foundRoom, zone, sizeof(foundRoom));
    }
  }

  if (!want[0] && firstCoordIp[0]) {
    strlcpy(out.coordIp, firstCoordIp, sizeof(out.coordIp));
    strlcpy(out.room, firstRoom, sizeof(out.room));
  }
  return out.coordIp[0] != 0;
}
