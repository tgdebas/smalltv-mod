// SonosClient.h — reads what a Sonos room is playing, straight from the speakers.
//
// No cloud, no bridge: Sonos speakers answer UPnP SOAP on port 1400 over plain
// HTTP, which the ESP8266 handles comfortably. The client
//   1. finds a speaker to ask (the configured host, or SSDP discovery),
//   2. reads the zone topology to map the configured room to its group
//      coordinator (a grouped room's own transport only says "x-rincon:..."),
//   3. polls the coordinator's AVTransport for the transport state and the
//      current track's title, artist and album-art URI.
// Whatever the Sonos app on the Mac (or a phone, or Spotify Connect) starts on
// that room is what the coordinator reports, so the screen follows it.
#pragma once
#include <Arduino.h>
#include "Settings.h"

#define SONOS_TITLE_LEN   96
#define SONOS_ARTIST_LEN  64
#define SONOS_ART_LEN    320
#define SONOS_MAX_ROOMS   12
#define SONOS_LIST_NAME   32   // room picker entries (matching uses the full name)
#define SONOS_ROOM_NAME   48   // = MAX_SONOS_ROOM_LEN, so a saved name always fits

enum SonosPlay : uint8_t {
  SONOS_STOPPED = 0,
  SONOS_PLAYING,
  SONOS_PAUSED,
  SONOS_TRANSITIONING,
};

enum : uint8_t {
  SONOS_ART_NONE = 0,       // the track info named no cover and none could be derived
  SONOS_ART_METADATA,       // upnp:albumArtURI from the track info
  SONOS_ART_FROM_TRACK,     // derived: the speaker's /getaa for the TrackURI
};

struct SonosTrack {
  char      title[SONOS_TITLE_LEN];
  char      artist[SONOS_ARTIST_LEN];
  char      artUrl[SONOS_ART_LEN];   // absolute http(s) URL, empty = no art
  uint8_t   artSource;               // SONOS_ART_* below: where artUrl came from
  char      trackUri[48];            // start of TrackURI, for diagnostics
  SonosPlay state;
  bool      hasTrack;                // something is loaded (title or art known)
};

// Why the last poll produced what it did (reported by /api/status).
enum SonosStage : uint8_t {
  SONOS_IDLE = 0,        // nothing has run yet
  SONOS_DISCOVERING,     // SSDP search in flight, no speaker known yet
  SONOS_NO_SPEAKER,      // SSDP found nothing (set a speaker IP)
  SONOS_NO_ROOM,         // topology read, but the room name matched nothing
  SONOS_CONNECT_FAIL,    // a speaker did not answer
  SONOS_HTTP_ERROR,      // a speaker answered with a non-200
  SONOS_OK,
};

void sonosInit(const Settings& s);        // reset; next service() starts over
// Call often; self-times all requests. background = the page is not on
// screen: poll less often (at most every SONOS_BG_POLL_SEC), just enough to
// notice music starting.
void sonosService(const Settings& s, bool background = false);
void sonosForceRefresh();                 // poll on the next service() call

const SonosTrack& sonosTrack();
uint32_t    sonosVersion();               // bumps whenever title/artist/art/state changes
// Fingerprint of a track's title, artist and art URL (not its play state).
uint32_t    sonosTrackKey(const SonosTrack& t);
bool        sonosError();                 // most recent request failed
SonosStage  sonosStage();
const char* sonosStageName();
const char* sonosCoordinator();           // coordinator IP being polled ("" = none)
const char* sonosRoomName();              // the room actually followed

// Room names seen in the last topology read, for the web UI's picker.
uint8_t     sonosRoomCount();
const char* sonosRoomAt(uint8_t i);

// True while the room is playing, or was within the last few minutes. The
// carousel uses this to skip the Now Playing page when nothing is on.
bool        sonosActive();
