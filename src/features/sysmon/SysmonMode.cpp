#include "SysmonMode.h"
#include <Arduino_GFX_Library.h>
#include <math.h>
#include "Gfx.h"
#include "SysmonData.h"
#include "SysmonView.h"

SysmonMode g_sysmonMode;

static uint32_t hashText(const char* s, uint32_t h = 2166136261u) {
  for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
  return h;
}

// What a quarter shows, as one number: the value text plus the bar or arc
// length in pixels. Equal keys mean the pixels would not change.
static void partKeys(const SysmonSample& s, float netScale, uint32_t out[4]) {
  char num[8];
  sysmonPercentText(s.cpu, num, sizeof(num));
  out[0] = hashText(num);
  sysmonPercentText(s.mem, num, sizeof(num));
  out[1] = hashText(num);
  sysmonPercentText(s.disk, num, sizeof(num));
  out[2] = hashText(num) ^ (uint32_t)lroundf(s.disk * 0.96f);
  const char* unit;
  float total = s.rxMbps + s.txMbps;
  sysmonNetText(total, num, sizeof(num), &unit);
  float frac = netScale > 0 ? total / netScale : 0;
  if (frac > 1) frac = 1;
  out[3] = hashText(unit, hashText(num)) ^ ((uint32_t)lroundf(frac * 96) << 20);
}

void SysmonMode::service(const Settings& s) {
  Arduino_GFX* g = gfxDev();
  if (!g) return;
  uint32_t now = millis();

  if (!sysmonFresh(now)) {
    long ago = sysmonLastMs() ? (long)((now - sysmonLastMs()) / 1000) : -1;
    char line[40];
    sysmonWaitingLine(ago, line, sizeof(line));
    uint32_t key = hashText(line);
    if (needFull_ || shown_ != 2 || key != waitingKey_) {
      sysmonDrawWaiting(g, ago);
      shown_ = 2;
      waitingKey_ = key;
      needFull_ = false;
    }
    return;
  }

  const SysmonSample& smp = sysmonSample();
  float scale = sysmonNetScale();
  if (needFull_ || shown_ != 1) {
    sysmonDrawAll(g, smp, scale);
    partKeys(smp, scale, partKey_);
    shown_ = 1;
    seenVersion_ = sysmonVersion();
    needFull_ = false;
    return;
  }
  if (sysmonVersion() == seenVersion_) return;
  seenVersion_ = sysmonVersion();

  uint32_t keys[4];
  partKeys(smp, scale, keys);
  uint8_t parts = 0;
  for (uint8_t i = 0; i < 4; i++)
    if (keys[i] != partKey_[i]) { parts |= (uint8_t)(1u << i); partKey_[i] = keys[i]; }
  if (parts) sysmonDrawParts(g, smp, scale, parts);
}
