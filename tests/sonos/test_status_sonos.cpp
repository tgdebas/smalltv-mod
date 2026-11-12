// The "sonos" member of /api/status: valid JSON, titles escaped, rooms listed.
#include <cstdio>
#include <string>
#include "StatusJson.h"

using namespace smalltv;

struct StringSink : Sink {
  std::string text;
  bool write(const char* data, size_t count) override { text.append(data, count); return true; }
};

static const char* rooms[] = {"Kitchen", "Kids & \"Play\""};
static const char* roomAt(uint8_t i) { return rooms[i]; }

int main() {
  StatusSonos so;
  so.stage = "ok";
  so.room = "Kitchen";
  so.coordinator = "192.168.1.20";
  so.state = "playing";
  so.title = "Say \"Hi\" \\ Bye";
  so.artist = "Beyonc\xc3\xa9";
  so.art = "ok";
  so.roomCount = 2;
  so.roomAt = roomAt;

  StringSink sink;
  PieceWriter<16> out(sink);
  jsonout::raw(out, "{\"x\":1");
  writeStatusSonos(out, so);
  writeStatusEnd(out);

  JsonDocument doc;
  if (deserializeJson(doc, sink.text)) { printf("FAIL not JSON: %s\n", sink.text.c_str()); return 1; }
  JsonObject s = doc["sonos"];
  bool ok = std::string(s["title"] | "") == "Say \"Hi\" \\ Bye" &&
            std::string(s["state"] | "") == "playing" &&
            s["rooms"].size() == 2 && std::string(s["rooms"][1] | "") == "Kids & \"Play\"" &&
            s["error"] == false;
  if (!ok) { printf("FAIL %s\n", sink.text.c_str()); return 1; }
  printf("sonos status tests: all passed\n");
  return 0;
}
