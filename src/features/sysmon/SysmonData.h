// SysmonData.h — the Mac's CPU, memory, disk and network numbers (features/sysmon).
//
// The device cannot measure another computer, so a script on the Mac
// (tools/mac/smalltv-stats.sh) samples them every few seconds and POSTs them
// to /api/sysmon. This holds the latest sample and says whether it is fresh.
#pragma once
#include <stdint.h>

struct SysmonSample {
  float cpu;      // % busy, 0..100
  float mem;      // % used, 0..100
  float disk;     // % of the startup disk used, 0..100
  float rxMbps;   // network in, Mbit/s
  float txMbps;   // network out, Mbit/s
};

// Apply one push. Values arrive as text ("42.5"); a missing or unreadable one
// keeps its previous value. Returns false when none of them could be read.
bool sysmonApply(const char* cpu, const char* mem, const char* disk,
                 const char* rx, const char* tx, uint32_t nowMs);

const SysmonSample& sysmonSample();
uint32_t sysmonVersion();          // bumps on every accepted push
uint32_t sysmonLastMs();           // millis() of the last push, 0 = never
bool     sysmonFresh(uint32_t nowMs);   // a push arrived within SYSMON_STALE_MS

// The network bar's full scale: the recent peak of in+out, decaying slowly so
// the bar stays readable on a quiet link and after a big download alike.
float    sysmonNetScale();

#ifndef SYSMON_STALE_MS
#define SYSMON_STALE_MS 20000UL    // the script pushes every 5 s; 4 missed = stale
#endif
