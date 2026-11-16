// PixelRefresh.h — clears LCD image retention ("ghosting").
//
// An IPS panel that showed one layout for a long time (the stock weather
// clock, say) can keep a faint copy of it. That is retention, not OLED-style
// burn-in, and it fades faster when every pixel is driven through full-range
// changes: this cycles the whole screen through white, black and the
// primaries for a set time, then hands the screen back to the active mode.
#pragma once
#include <stdint.h>

void     pixelRefreshStart(uint16_t minutes);   // 0 stops
bool     pixelRefreshActive();
uint32_t pixelRefreshRemainingSec();
// Draws the next colour when due. Call from loop() while active.
void     pixelRefreshService();

#define PIXEL_REFRESH_MAX_MIN 240
