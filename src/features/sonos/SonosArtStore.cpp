#include "SonosArtStore.h"
#include "Platform.h"
#include <LittleFS.h>
#include <memory>
#include <new>
#include "SonosBmp.h"

static const char kDir[] = "/art";
static const char kTmp[] = "/art/.up";

static File     g_up;
static bool     g_upOk = false;
static bool     g_upRelay = false;
static size_t   g_upBytes = 0;
static uint8_t  g_upHead[2];
static char     g_upPath[32];
static uint32_t g_relaySeq = 0;
// A BMP upload is converted to R565 as it streams in (see SonosBmp.h).
static std::unique_ptr<BmpTo565> g_bmp;
static size_t   g_upIn = 0;               // bytes received (a BMP is larger than what is stored)
static const size_t kMaxBmpIn = 200000;   // 164x164 at 32 bpp plus any header

static bool writeStored(void*, const uint8_t* d, size_t n) {
  if (g_upBytes + n > ART_STORE_MAX_BYTES || g_up.write(d, n) != n) return false;
  g_upBytes += n;
  return true;
}

uint32_t artRelaySeq() { return g_relaySeq; }

uint32_t artKeyOf(const char* url) {
  uint32_t h = 2166136261u;
  for (const char* p = url; p && *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
  return h ? h : 1;
}

void artStationPath(uint32_t uriHash, char* out, size_t n) {
  snprintf(out, n, "/art/s_%08lx.jpg", (unsigned long)uriHash);
}
void artRelayPath(uint32_t key, char* out, size_t n) {
  snprintf(out, n, "/art/r_%08lx.jpg", (unsigned long)key);
}

bool artStoreHas(const char* path) { return LittleFS.exists(path); }
bool artStoreRemove(const char* path) { return LittleFS.exists(path) && LittleFS.remove(path); }

// Removes the files in /art whose name starts with `prefix`, except `keep`
// (may be null). Returns how many were removed.
static uint16_t removeWithPrefix(const char* prefix, const char* keep) {
  uint16_t removed = 0;
  size_t pl = strlen(prefix);
  for (;;) {   // in batches: deleting while a directory is open is not safe
    char names[8][32];
    uint8_t count = 0;
#if defined(SMALLTV_FAMILY_ESP32)
    File root = LittleFS.open(kDir);
    for (File f = root ? root.openNextFile() : File(); f && count < 8; f = root.openNextFile()) {
      const char* base = strrchr(f.name(), '/');
      base = base ? base + 1 : f.name();
      if (strncmp(base, prefix, pl)) continue;
      snprintf(names[count], sizeof(names[0]), "%s/%s", kDir, base);
      if (!keep || strcmp(names[count], keep)) count++;
    }
#else
    Dir d = LittleFS.openDir(kDir);
    while (d.next() && count < 8) {
      String name = d.fileName();
      if (!name.startsWith(prefix)) continue;
      snprintf(names[count], sizeof(names[0]), "%s/%s", kDir, name.c_str());
      if (!keep || strcmp(names[count], keep)) count++;
    }
#endif
    if (!count) return removed;
    for (uint8_t i = 0; i < count; i++)
      if (LittleFS.remove(names[i])) removed++;
  }
}

static void pruneRelays(const char* keep) { removeWithPrefix("r_", keep); }

uint16_t artRemoveAllStationLogos() { return removeWithPrefix("s_", nullptr); }

bool artUploadBegin(const char* finalPath, bool relay) {
  artUploadAbort();
  if (!LittleFS.exists(kDir)) LittleFS.mkdir(kDir);
  g_up = LittleFS.open(kTmp, "w");
  g_upOk = (bool)g_up;
  g_upRelay = relay;
  g_upBytes = 0;
  g_upIn = 0;
  g_bmp.reset();
  strlcpy(g_upPath, finalPath, sizeof(g_upPath));
  return g_upOk;
}

void artUploadWrite(const uint8_t* data, size_t len) {
  if (!g_upOk || !len) return;
  for (size_t i = 0; i < len && g_upIn + i < 2; i++) g_upHead[g_upIn + i] = data[i];
  // The format shows in the first two bytes ("BM", or FF D8 for a JPEG); an
  // upload's first piece is far longer than that.
  if (g_upIn == 0 && len >= 2 && data[0] == 'B' && data[1] == 'M') {
    // Black art background (SonosMode's C_ART_BG) under transparent pixels.
    g_bmp.reset(new (std::nothrow) BmpTo565(writeStored, nullptr, 0x18E3));
    if (!g_bmp) { g_upOk = false; return; }
  }
  g_upIn += len;
  if (g_bmp) {
    if (g_upIn > kMaxBmpIn || !g_bmp->feed(data, len)) g_upOk = false;
    return;
  }
  if (!writeStored(nullptr, data, len)) g_upOk = false;   // too big, or the filesystem is full
}

bool artUploadEnd() {
  bool ok = g_upOk && (g_bmp ? g_bmp->finished()
                             : g_upBytes >= 4 && g_upHead[0] == 0xFF && g_upHead[1] == 0xD8);   // JPEG SOI
  g_bmp.reset();
  if (g_up) g_up.close();
  g_upOk = false;
  if (!ok) { LittleFS.remove(kTmp); return false; }
  LittleFS.remove(g_upPath);
  if (!LittleFS.rename(kTmp, g_upPath)) { LittleFS.remove(kTmp); return false; }
  if (g_upRelay) {
    pruneRelays(g_upPath);
    g_relaySeq++;
  }
  return true;
}

void artUploadAbort() {
  if (g_up) { g_up.close(); LittleFS.remove(kTmp); }
  g_upOk = false;
  g_bmp.reset();
}
