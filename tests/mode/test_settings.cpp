// The "mode" key of a save or an import. A save that does not name a mode the
// build knows must leave the stored mode alone. Before, an empty or unknown
// text turned the mode into the stock ticker. Built with the stand-in core
// headers of the fuzz target (see tests/mode/run.sh).
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include "Settings.h"
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

FSClass LittleFS;
EspClass ESP;

static int g_failed = 0, g_checks = 0;
#define CHECK(cond, ...)                                                   \
  do {                                                                     \
    g_checks++;                                                            \
    if (!(cond)) {                                                         \
      g_failed++;                                                          \
      printf("FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond);               \
      printf(__VA_ARGS__);                                                 \
      printf("\n");                                                        \
    }                                                                      \
  } while (0)

static void apply(Settings& s, const char* json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) { printf("bad test json: %s\n", json); g_failed++; return; }
  settingsApplyJson(s, doc.as<JsonObjectConst>());
}

// Start from a device that stored `mode`, then apply a save.
static uint8_t after(uint8_t stored, const char* json) {
  Settings s;
  s.setDefaults();
  s.mode = stored;
  apply(s, json);
  return s.mode;
}

static void testNamesSetTheMode() {
  CHECK(after(MODE_STOCKS, "{\"mode\":\"theme\"}") == MODE_THEME, "theme");
  CHECK(after(MODE_STOCKS, "{\"mode\":\"radar\"}") == MODE_RADAR, "radar");
  CHECK(after(MODE_STOCKS, "{\"mode\":\"usage\"}") == MODE_USAGE, "usage");
  CHECK(after(MODE_STOCKS, "{\"mode\":\"ha\"}") == MODE_HA, "ha");
  CHECK(after(MODE_STOCKS, "{\"mode\":\"carousel\"}") == MODE_CAROUSEL, "carousel");
  CHECK(after(MODE_RADAR, "{\"mode\":\"stocks\"}") == MODE_STOCKS, "stocks");
  CHECK(after(MODE_RADAR, "{\"mode\":\"Theme\"}") == MODE_THEME, "case is ignored");
}

static void testEmptyMode() {
  // The page sends "" when its mode list lacks the stored mode (a build
  // without that feature). The stored mode stays.
  CHECK(after(MODE_RADAR, "{\"mode\":\"\"}") == MODE_RADAR, "radar kept");
  CHECK(after(MODE_THEME, "{\"mode\":\"\"}") == MODE_THEME, "theme kept");
  CHECK(after(MODE_HA, "{\"mode\":\"\"}") == MODE_HA, "ha kept");
  CHECK(after(MODE_CAROUSEL, "{\"mode\":\"\"}") == MODE_CAROUSEL, "carousel kept");
}

static void testUnknownMode() {
  CHECK(after(MODE_USAGE, "{\"mode\":\"nonsense\"}") == MODE_USAGE, "unknown text kept");
  CHECK(after(MODE_USAGE, "{\"mode\":\"theme2\"}") == MODE_USAGE, "near miss kept");
}

static void testNoModeOrOtherType() {
  CHECK(after(MODE_HA, "{\"brightness\":40}") == MODE_HA, "no mode key");
  CHECK(after(MODE_HA, "{\"mode\":7}") == MODE_HA, "number");
  CHECK(after(MODE_HA, "{\"mode\":null}") == MODE_HA, "null");
}

static void testImport() {
  // An import goes through the same parser, onto the stored settings: an
  // export from a lean build with an empty mode does not move the mode.
  Settings s;
  s.setDefaults();
  apply(s, "{\"mode\":\"radar\",\"hostname\":\"smalltv-test\"}");
  CHECK(s.mode == MODE_RADAR, "stored radar");
  apply(s, "{\"mode\":\"\",\"hostname\":\"smalltv-test-2\"}");
  CHECK(s.mode == MODE_RADAR, "import with empty mode keeps radar");
  CHECK(s.hostname == "smalltv-test-2", "the rest of the import applies");
}

// Sonos: the mode token round-trips, and the speaker field keeps only the host
// when a URL is pasted into it.
static void testSonos() {
  CHECK(after(MODE_STOCKS, "{\"mode\":\"sonos\"}") == MODE_SONOS, "sonos");
  CHECK(after(MODE_SONOS, "{\"mode\":\"\"}") == MODE_SONOS, "sonos kept");

  Settings s;
  s.setDefaults();
  CHECK(s.sonos.room == "" && s.sonos.host == "", "defaults: follow first group, discover a speaker");
  CHECK(s.carouselSonos, "now playing joins the carousel by default");
  apply(s, "{\"sonos\":{\"room\":\"  Living Room \",\"host\":\"http://192.168.1.20:1400/xml/x.xml\",\"pollSec\":1}}");
  CHECK(s.sonos.room == "Living Room", "room trimmed: %s", s.sonos.room.c_str());
  CHECK(s.sonos.host == "192.168.1.20", "host from URL: %s", s.sonos.host.c_str());
  CHECK(s.sonos.pollSec == 2, "poll clamped to 2 s: %u", s.sonos.pollSec);
  apply(s, "{\"sonos\":{\"showArtist\":false}}");
  CHECK(s.sonos.room == "Living Room" && !s.sonos.showArtist, "partial save keeps the rest");

  JsonDocument doc;
  settingsToJson(s, doc.to<JsonObject>(), false);
  CHECK(strcmp(doc["sonos"]["host"] | "", "192.168.1.20") == 0, "exported");
  s.mode = MODE_SONOS;
  JsonDocument doc2;
  settingsToJson(s, doc2.to<JsonObject>(), false);
  CHECK(strcmp(doc2["mode"] | "", "sonos") == 0, "mode exported as sonos");
}

int main() {
  testSonos();
  testNamesSetTheMode();
  testEmptyMode();
  testUnknownMode();
  testNoModeOrOtherType();
  testImport();
  printf("%d checks, %d failed\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
