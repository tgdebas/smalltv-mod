#include "PixelRefresh.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "Gfx.h"

static uint32_t g_until = 0;      // millis() the run ends at, 0 = idle
static uint32_t g_nextMs = 0;
static uint8_t  g_step = 0;

// Raw panel colours on purpose (no gfxTint): the point is to drive every
// subpixel to its extremes.
static const uint16_t kSeq[] = {0xFFFF, 0x0000, 0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000, 0x8410};
static const uint32_t kStepMs = 1500;

void pixelRefreshStart(uint16_t minutes) {
  if (minutes == 0) { g_until = 0; return; }
  if (minutes > PIXEL_REFRESH_MAX_MIN) minutes = PIXEL_REFRESH_MAX_MIN;
  g_until = millis() + (uint32_t)minutes * 60000UL;
  if (!g_until) g_until = 1;
  g_nextMs = millis();
  g_step = 0;
}

bool pixelRefreshActive() {
  if (!g_until) return false;
  if ((int32_t)(millis() - g_until) >= 0) { g_until = 0; return false; }
  return true;
}

uint32_t pixelRefreshRemainingSec() {
  return pixelRefreshActive() ? (g_until - millis()) / 1000 : 0;
}

void pixelRefreshService() {
  Arduino_GFX* g = gfxDev();
  if (!g || (int32_t)(millis() - g_nextMs) < 0) return;
  g_nextMs = millis() + kStepMs;
  g->fillScreen(kSeq[g_step]);
  g_step = (uint8_t)((g_step + 1) % (sizeof(kSeq) / sizeof(kSeq[0])));
}
