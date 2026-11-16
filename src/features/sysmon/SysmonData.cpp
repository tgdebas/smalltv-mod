#include "SysmonData.h"
#include <stdlib.h>
#include <math.h>

static SysmonSample g_s = {0, 0, 0, 0, 0};
static uint32_t g_version = 0;
static uint32_t g_lastMs = 0;
static float    g_peak = 10.0f;    // Mbit/s; never below 10 so idle noise stays low

const SysmonSample& sysmonSample() { return g_s; }
uint32_t sysmonVersion()           { return g_version; }
uint32_t sysmonLastMs()            { return g_lastMs; }
float    sysmonNetScale()          { return g_peak; }

bool sysmonFresh(uint32_t nowMs) {
  return g_lastMs != 0 && nowMs - g_lastMs < SYSMON_STALE_MS;
}

// Parses a non-negative number; false for null, empty, junk or NaN.
static bool num(const char* s, float lo, float hi, float& out) {
  if (!s || !*s) return false;
  char* end = nullptr;
  float v = strtof(s, &end);
  if (end == s || *end != 0 || v != v) return false;
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  out = v;
  return true;
}

bool sysmonApply(const char* cpu, const char* mem, const char* disk,
                 const char* rx, const char* tx, uint32_t nowMs) {
  bool any = false;
  any |= num(cpu, 0, 100, g_s.cpu);
  any |= num(mem, 0, 100, g_s.mem);
  any |= num(disk, 0, 100, g_s.disk);
  any |= num(rx, 0, 100000, g_s.rxMbps);
  any |= num(tx, 0, 100000, g_s.txMbps);
  if (!any) return false;

  float total = g_s.rxMbps + g_s.txMbps;
  g_peak *= 0.98f;                  // ~halves in 2 minutes of 3 s pushes
  if (total > g_peak) g_peak = total;
  if (g_peak < 10.0f) g_peak = 10.0f;

  g_lastMs = nowMs ? nowMs : 1;     // 0 means "never"
  g_version++;
  return true;
}
