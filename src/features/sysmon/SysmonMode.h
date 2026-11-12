// SysmonMode.h — the Mac's CPU, memory, disk and network on screen.
//
// Fed by tools/mac/smalltv-stats.sh through POST /api/sysmon. A new sample
// repaints only the quarters whose number or bar changed; with no sample for
// SYSMON_STALE_MS the screen says it is waiting for the Mac, and the carousel
// leaves the page out.
#pragma once
#include "Mode.h"
#include "config.h"

class SysmonMode : public DisplayMode {
 public:
  const char* id() const override { return "sysmon"; }
  uint8_t     modeConst() const override { return MODE_SYSMON; }

  void begin(const Settings& s) override { needFull_ = true; }
  void invalidate(const Settings& s) override { needFull_ = true; }
  void wake(const Settings& s) override { needFull_ = true; }
  void service(const Settings& s) override;

 private:
  bool     needFull_ = true;
  uint8_t  shown_ = 0;            // 0 nothing, 1 stats, 2 waiting
  uint32_t seenVersion_ = 0;
  uint32_t partKey_[4] = {0, 0, 0, 0};   // what each quarter shows now
  uint32_t waitingKey_ = 0;       // hash of the waiting screen's text
};

extern SysmonMode g_sysmonMode;
