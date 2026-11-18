#include "SonosArtStore.h"
#include "Platform.h"
#include <LittleFS.h>

static const char kDir[] = "/art";
static const char kTmp[] = "/art/.up";

static File     g_up;
static bool     g_upOk = false;
static bool     g_upRelay = false;
static size_t   g_upBytes = 0;
static uint8_t  g_upHead[2];
static char     g_upPath[32];
static uint32_t g_relaySeq = 0;

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

// Removes every relayed cover but `keep`.
static void pruneRelays(const char* keep) {
  char names[8][32];
  uint8_t count = 0;
#if defined(SMALLTV_FAMILY_ESP32)
  File root = LittleFS.open(kDir);
  for (File f = root.openNextFile(); f && count < 8; f = root.openNextFile()) {
    const char* base = strrchr(f.name(), '/');
    base = base ? base + 1 : f.name();
    if (!strncmp(base, "r_", 2)) snprintf(names[count++], sizeof(names[0]), "%s/%s", kDir, base);
  }
#else
  Dir d = LittleFS.openDir(kDir);
  while (d.next() && count < 8) {
    String name = d.fileName();
    if (name.startsWith("r_")) snprintf(names[count++], sizeof(names[0]), "%s/%s", kDir, name.c_str());
  }
#endif
  for (uint8_t i = 0; i < count; i++)
    if (strcmp(names[i], keep)) LittleFS.remove(names[i]);
}

bool artUploadBegin(const char* finalPath, bool relay) {
  artUploadAbort();
  if (!LittleFS.exists(kDir)) LittleFS.mkdir(kDir);
  g_up = LittleFS.open(kTmp, "w");
  g_upOk = (bool)g_up;
  g_upRelay = relay;
  g_upBytes = 0;
  strlcpy(g_upPath, finalPath, sizeof(g_upPath));
  return g_upOk;
}

void artUploadWrite(const uint8_t* data, size_t len) {
  if (!g_upOk) return;
  for (size_t i = 0; i < len && g_upBytes + i < 2; i++) g_upHead[g_upBytes + i] = data[i];
  if (g_upBytes + len > ART_STORE_MAX_BYTES || g_up.write(data, len) != len) {
    g_upOk = false;   // too big, or the filesystem is full
    return;
  }
  g_upBytes += len;
}

bool artUploadEnd() {
  bool ok = g_upOk && g_upBytes >= 4 && g_upHead[0] == 0xFF && g_upHead[1] == 0xD8;   // JPEG SOI
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
}
