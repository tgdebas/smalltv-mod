// SonosParse.h — turns Sonos SOAP response bodies into the client's state.
//
// Kept apart from SonosClient's networking so the native tests can feed it
// recorded speaker responses (tests/sonos/).
#pragma once
#include "SonosClient.h"
#include "SonosXml.h"

#define SONOS_IP_LEN 40

// GetTransportInfo -> play state.
SonosPlay sonosParseTransport(XmlScanner& x);

// GetPositionInfo -> title / artist / absolute art URL. `coordIp` resolves
// relative /getaa art URIs. Returns false, with redirect set, when this
// speaker is a group member whose transport points at its coordinator.
bool sonosParsePosition(XmlScanner& x, const char* coordIp, SonosTrack& t, bool& redirect);

// GetZoneGroupState -> the coordinator IP of the group holding `room`
// (case-insensitive). An empty `room` picks the first group. Every visible
// room name is passed to onRoom (may be null).
struct SonosTopology {
  char coordIp[SONOS_IP_LEN];
  char room[SONOS_ROOM_NAME];
};
bool sonosParseTopology(XmlScanner& x, const char* room, SonosTopology& out,
                        void (*onRoom)(const char* name));

// "http://192.168.1.20:1400/xml/..." -> "192.168.1.20"
bool sonosHostFromUrl(const char* url, char* out, size_t outLen);
