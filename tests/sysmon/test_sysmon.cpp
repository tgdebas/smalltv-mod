// Native tests for the Mac stats data store (src/features/sysmon/SysmonData.cpp)
// and its /api/status member.
#include <cstdio>
#include <string>
#include "SysmonData.h"
#include "StatusJson.h"

static int g_fail = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); g_fail++; } } while (0)
static bool near(float a, float b) { return a > b - 0.01f && a < b + 0.01f; }

struct StringSink : smalltv::Sink {
  std::string text;
  bool write(const char* d, size_t n) override { text.append(d, n); return true; }
};

int main() {
  CHECK(!sysmonFresh(1000), "nothing pushed yet: not fresh");
  CHECK(sysmonLastMs() == 0, "never pushed");

  CHECK(sysmonApply("42.5", "68", "65", "40", "8", 5000), "full push accepted");
  const SysmonSample& s = sysmonSample();
  CHECK(near(s.cpu, 42.5f) && near(s.mem, 68) && near(s.disk, 65), "percentages stored");
  CHECK(near(s.rxMbps, 40) && near(s.txMbps, 8), "rates stored");
  CHECK(sysmonFresh(5000 + SYSMON_STALE_MS - 1), "fresh until the stale limit");
  CHECK(!sysmonFresh(5000 + SYSMON_STALE_MS), "stale after it");
  CHECK(near(sysmonNetScale(), 48), "net scale follows the peak");

  uint32_t v = sysmonVersion();
  CHECK(!sysmonApply("", "abc", nullptr, "1x", "", 6000), "nothing readable: refused");
  CHECK(sysmonVersion() == v && sysmonLastMs() == 5000, "a refused push changes nothing");

  CHECK(sysmonApply("150", "-3", "", "", "", 7000), "partial push accepted");
  CHECK(near(s.cpu, 100) && near(s.mem, 0), "clamped to 0..100");
  CHECK(near(s.disk, 65) && near(s.rxMbps, 40), "missing fields keep their values");

  // The scale decays towards quiet traffic but never below 10 Mbit/s.
  for (int i = 0; i < 300; i++) sysmonApply(nullptr, nullptr, nullptr, "0.1", "0.1", 8000 + i);
  CHECK(near(sysmonNetScale(), 10), "scale floor");

  CHECK(sysmonApply("1", "1", "1", "1", "1", 0), "a push at millis()==0");
  CHECK(sysmonLastMs() != 0, "still counts as pushed");

  smalltv::StatusSysmon st;
  st.fresh = true; st.ago = 3; st.cpu = 42.5f; st.mem = 68; st.disk = 65; st.rx = 40; st.tx = 8;
  StringSink sink;
  smalltv::PieceWriter<16> out(sink);
  smalltv::jsonout::raw(out, "{\"x\":1");
  smalltv::writeStatusSysmon(out, st);
  smalltv::writeStatusEnd(out);
  JsonDocument doc;
  CHECK(!deserializeJson(doc, sink.text), "status is JSON");
  CHECK(doc["sysmon"]["fresh"] == true && doc["sysmon"]["ago"] == 3, "status fresh/ago");
  CHECK(near(doc["sysmon"]["cpu"].as<float>(), 42.5f), "status cpu");

  smalltv::StatusSysmon never;
  StringSink sink2;
  smalltv::PieceWriter<16> out2(sink2);
  smalltv::jsonout::raw(out2, "{");
  smalltv::writeStatusSysmon(out2, never);
  smalltv::writeStatusEnd(out2);
  std::string t = "{" + sink2.text.substr(2);   // drop the leading ","
  JsonDocument d2;
  CHECK(!deserializeJson(d2, t), "never-pushed status is JSON");
  CHECK(d2["sysmon"]["ago"] == -1 && d2["sysmon"]["cpu"].isNull(), "no numbers before the first push");

  if (g_fail) { printf("%d check(s) failed\n", g_fail); return 1; }
  printf("sysmon tests: all passed\n");
  return 0;
}
