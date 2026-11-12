// SysmonView.h — draws the Mac stats screen (features/sysmon).
//
// The look follows the "system monitor" dashboard of adrienbrault/geekmagic-hacs:
// CPU and memory as rings across the top, disk and network as large numbers
// with a bar underneath. Each quarter redraws on its own, so a new sample only
// repaints the numbers that changed and the screen never flashes.
//
// Only Arduino_GFX and the u8g2 fonts are used here, so tools/preview can
// compile this file on a PC and write the same pixels to a PNG.
#pragma once
#include <stdint.h>
#include "SysmonData.h"

class Arduino_GFX;

enum SysmonPart : uint8_t {
  SYSMON_CPU  = 1,
  SYSMON_MEM  = 2,
  SYSMON_DISK = 4,
  SYSMON_NET  = 8,
  SYSMON_ALL  = 15,
};

// The whole screen: background, labels, icons and all four values.
void sysmonDrawAll(Arduino_GFX* g, const SysmonSample& s, float netScale);
// Only the given parts (SysmonPart bits), over a screen sysmonDrawAll drew.
void sysmonDrawParts(Arduino_GFX* g, const SysmonSample& s, float netScale, uint8_t parts);
// No data, or data gone stale: what to do about it. secondsAgo < 0 = never.
void sysmonDrawWaiting(Arduino_GFX* g, long secondsAgo);
// The waiting screen's second line; it changes at most once a minute, so the
// mode repaints that screen only when this text changes.
void sysmonWaitingLine(long secondsAgo, char* out, unsigned outLen);

// The text a value is drawn as ("42", "4.8", "1.2"), and its unit. Shared with
// the mode, which repaints a part only when its text changes.
void sysmonPercentText(float pct, char* out, unsigned outLen);
void sysmonNetText(float mbps, char* out, unsigned outLen, const char** unit);
