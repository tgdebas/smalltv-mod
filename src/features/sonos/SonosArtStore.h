// SonosArtStore.h — covers kept in flash (LittleFS /art), so they cost no RAM.
//
//  - Station logos: /art/s_<uriHash>.jpg, saved from the Sonos tab for the
//    radio station playing. They win over every other source.
//  - Relayed covers: /art/r_<key>.jpg, sent by tools/mac/smalltv-stats.sh for
//    a cover the cube could not fetch itself (https, PNG, short heap). <key> is
//    artKeyOf(url). Only the newest is kept, so a station's logo stays until a
//    different cover replaces it.
// Files are baseline JPEG, or R565 (see SonosBmp.h) converted from a BMP
// upload, at most ART_STORE_MAX_BYTES.
#pragma once
#include <Arduino.h>

#define ART_STORE_MAX_BYTES 54000   // a 164x164 R565 cover is 53,800 bytes

uint32_t artKeyOf(const char* url);               // FNV-1a, never 0
void     artStationPath(uint32_t uriHash, char* out, size_t n);
void     artRelayPath(uint32_t key, char* out, size_t n);
bool     artStoreHas(const char* path);
bool     artStoreRemove(const char* path);
// Deletes every saved station logo (not the relayed cover); returns how many.
uint16_t artRemoveAllStationLogos();

// Streaming upload into a temporary file, committed with a rename. A file
// that is too big or does not start like a JPEG is refused.
bool     artUploadBegin(const char* finalPath, bool relay);
void     artUploadWrite(const uint8_t* data, size_t len);
bool     artUploadEnd();      // true when the file was committed
void     artUploadAbort();

// Bumps whenever a relayed cover is committed, so the screen can pick it up.
uint32_t artRelaySeq();
