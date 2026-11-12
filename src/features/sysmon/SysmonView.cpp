#include "SysmonView.h"
#include <Arduino_GFX_Library.h>
#include <U8g2lib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "Gfx.h"

// ---- palette (RGB565 from 8-bit RGB; gfxTint applies the panel correction) --
static constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
struct Accent { uint16_t fg, track; };
static const Accent kCpu  = {rgb(0x5E, 0xD6, 0xCB), rgb(0x1B, 0x3B, 0x3A)};
static const Accent kMem  = {rgb(0xB3, 0x5C, 0xF2), rgb(0x32, 0x1E, 0x45)};
static const Accent kDisk = {rgb(0xFF, 0xA0, 0x0A), rgb(0x45, 0x2D, 0x08)};
static const Accent kNet  = {rgb(0x34, 0xD0, 0x5A), rgb(0x12, 0x3D, 0x1C)};
static const uint16_t kLabel = rgb(0xA8, 0xA8, 0xA8);
static const uint16_t kBg = 0x0000;

// ---- layout (240x240) ---------------------------------------------------------
// Top: two rings. Bottom: icon + label, a big number, a bar. Each quarter is
// 120 px wide; x0 is its left edge.
static const int kRingCy = 78, kRingOut = 37, kRingIn = 30;
static const int kTopLabelBase = 25;
static const int kBotLabelBase = 152;
static const int kBotValueBase = 194;
static const int kBarY = 209, kBarH = 11, kBarX = 12, kBarW = 96;

// ---- text helpers -----------------------------------------------------------------
static int textW(Arduino_GFX* g, const char* s) {
  int16_t x1, y1;
  uint16_t w, h;
  g->getTextBounds(s, 0, 100, &x1, &y1, &w, &h);
  return w;
}

static void setFont(Arduino_GFX* g, const uint8_t* font, uint16_t color) {
  g->setFont(font);
  g->setUTF8Print(false);
  g->setTextWrap(false);
  g->setTextSize(1);
  g->setTextColor(color);
}

// Small caps-style label with 1 px of extra tracking, like the reference.
static int trackedW(Arduino_GFX* g, const char* s) {
  int w = 0;
  char one[2] = {0, 0};
  for (const char* p = s; *p; p++) {
    if (*p == ' ') { w += 4; continue; }
    one[0] = *p;
    int16_t x1, y1;
    uint16_t cw, ch;
    g->getTextBounds(one, 0, 100, &x1, &y1, &cw, &ch);
    w += cw + 3;
  }
  return w > 0 ? w - 3 : 0;
}
static void drawTracked(Arduino_GFX* g, const char* s, int x, int base) {
  char one[2] = {0, 0};
  for (const char* p = s; *p; p++) {
    one[0] = *p;
    if (*p == ' ') { x += 4; continue; }
    int16_t x1, y1;
    uint16_t cw, ch;
    g->getTextBounds(one, x, base, &x1, &y1, &cw, &ch);
    g->setCursor(x - (x1 - x), base);   // align the glyph's ink to x
    g->print(one);
    x += cw + 3;
  }
}

// "42" + small "%" (or "48" + "Mb/s"), the pair centred on cx or starting at x.
static void drawValue(Arduino_GFX* g, const char* num, const char* unit,
                      const uint8_t* numFont, const uint8_t* unitFont,
                      int x, int base, bool centre, uint16_t color) {
  setFont(g, numFont, color);
  int nw = textW(g, num);
  setFont(g, unitFont, color);
  int uw = textW(g, unit);
  int total = nw + 2 + uw;
  int left = centre ? x - total / 2 : x;
  setFont(g, numFont, color);
  int16_t x1, y1;
  uint16_t w, h;
  g->getTextBounds(num, left, base, &x1, &y1, &w, &h);
  g->setCursor(left - (x1 - left), base);
  g->print(num);
  setFont(g, unitFont, color);
  g->getTextBounds(unit, left + nw + 2, base, &x1, &y1, &w, &h);
  g->setCursor(left + nw + 2 - (x1 - (left + nw + 2)), base);
  g->print(unit);
}

void sysmonPercentText(float pct, char* out, unsigned outLen) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  snprintf(out, outLen, "%d", (int)lroundf(pct));
}

void sysmonNetText(float mbps, char* out, unsigned outLen, const char** unit) {
  if (mbps >= 999.5f) {
    *unit = "Gb/s";
    snprintf(out, outLen, "%.1f", mbps / 1000.0f);
  } else if (mbps < 9.95f) {
    *unit = "Mb/s";
    snprintf(out, outLen, "%.1f", mbps);
  } else {
    *unit = "Mb/s";
    snprintf(out, outLen, "%d", (int)lroundf(mbps));
  }
}

// ---- shapes -------------------------------------------------------------------------
// Arc from 12 o'clock, clockwise, with round caps. Arduino_GFX measures angles
// clockwise from 3 o'clock, so 12 o'clock is 270.
static void drawRing(Arduino_GFX* g, int cx, float pct, const Accent& a) {
  int cy = kRingCy;
  g->fillCircle(cx, cy, kRingIn - 1, kBg);   // clears the old number
  g->fillArc(cx, cy, kRingOut, kRingIn, 0, 360, gfxTint(a.track));
  if (pct <= 0.5f) return;
  if (pct > 100) pct = 100;
  float sweep = pct * 3.6f;
  uint16_t fg = gfxTint(a.fg);
  g->fillArc(cx, cy, kRingOut, kRingIn, 270.0f, 270.0f + sweep, fg);
  // Round caps at both ends of the filled arc.
  float rMid = (kRingOut + kRingIn) / 2.0f;
  int capR = (kRingOut - kRingIn) / 2;
  float aStart = 270.0f * (float)M_PI / 180.0f;
  float aEnd = (270.0f + sweep) * (float)M_PI / 180.0f;
  g->fillCircle(cx + (int)lroundf(rMid * cosf(aStart)), cy + (int)lroundf(rMid * sinf(aStart)), capR, fg);
  g->fillCircle(cx + (int)lroundf(rMid * cosf(aEnd)), cy + (int)lroundf(rMid * sinf(aEnd)), capR, fg);
}

static void drawBar(Arduino_GFX* g, int x0, float frac, const Accent& a) {
  int x = x0 + kBarX, r = kBarH / 2;
  g->fillRoundRect(x, kBarY, kBarW, kBarH, r, gfxTint(a.track));
  if (frac <= 0) return;
  if (frac > 1) frac = 1;
  int w = (int)lroundf(kBarW * frac);
  if (w < kBarH) w = kBarH;   // a short fill still reads as a rounded pill
  g->fillRoundRect(x, kBarY, w, kBarH, r, gfxTint(a.fg));
}

// A small hard-drive glyph (16x18) with its top-left at x, y.
static void iconDisk(Arduino_GFX* g, int x, int y, uint16_t c) {
  g->fillRoundRect(x, y, 14, 17, 3, c);
  g->fillCircle(x + 7, y + 7, 4, kBg);
  g->fillCircle(x + 7, y + 7, 1, c);
  g->drawLine(x + 6, y + 8, x + 3, y + 13, kBg);
  g->fillRect(x + 3, y + 14, 8, 1, kBg);
}

// Wi-Fi glyph: three arcs over a dot, about 20x16, centred on cx with its
// dot's centre at base.
static void iconWifi(Arduino_GFX* g, int cx, int base, uint16_t c) {
  g->fillArc(cx, base, 14, 11, 225, 315, c);
  g->fillArc(cx, base, 8, 5, 225, 315, c);
  g->fillCircle(cx, base - 1, 2, c);
}

// ---- parts ----------------------------------------------------------------------------
static void drawTopLabels(Arduino_GFX* g) {
  setFont(g, u8g2_font_helvB08_tr, gfxTint(kLabel));
  drawTracked(g, "CPU", 60 - trackedW(g, "CPU") / 2, kTopLabelBase);
  drawTracked(g, "MEMORY", 180 - trackedW(g, "MEMORY") / 2, kTopLabelBase);
}

// Icon and label centred together in each bottom half.
static void drawBottomLabels(Arduino_GFX* g) {
  setFont(g, u8g2_font_helvB08_tr, gfxTint(kLabel));
  const int iconW = 16, gap = 8;
  int w = iconW + gap + trackedW(g, "DISK");
  int x = 60 - w / 2;
  iconDisk(g, x, kBotLabelBase - 15, gfxTint(kDisk.fg));
  drawTracked(g, "DISK", x + iconW + gap, kBotLabelBase);
  w = iconW + 4 + gap + trackedW(g, "NETWORK");
  x = 180 - w / 2;
  iconWifi(g, x + 10, kBotLabelBase, gfxTint(kNet.fg));
  drawTracked(g, "NETWORK", x + iconW + 4 + gap, kBotLabelBase);
}

static void drawRingPart(Arduino_GFX* g, int cx, float pct, const Accent& a) {
  drawRing(g, cx, pct, a);
  char num[8];
  sysmonPercentText(pct, num, sizeof(num));
  drawValue(g, num, "%", u8g2_font_fub20_tn, u8g2_font_fub11_tr, cx, kRingCy + 10, true,
            gfxTint(a.fg));
}

static void drawBottomPart(Arduino_GFX* g, int x0, const char* num, const char* unit,
                           float frac, const Accent& a) {
  g->fillRect(x0, kBotLabelBase + 6, 120, kBarY - kBotLabelBase - 8, kBg);   // old number
  // "0.3 Mb/s" or "100 %" in the big size would touch the edges; step down.
  const uint8_t* numFont = u8g2_font_fub30_tn;
  const uint8_t* unitFont = u8g2_font_fub14_tr;
  setFont(g, numFont, 0);
  int w = textW(g, num);
  setFont(g, unitFont, 0);
  w += 2 + textW(g, unit);
  if (w > 100) { numFont = u8g2_font_fub25_tn; unitFont = u8g2_font_fub11_tr; }
  drawValue(g, num, unit, numFont, unitFont, x0 + 60, kBotValueBase, true, gfxTint(a.fg));
  drawBar(g, x0, frac, a);
}

void sysmonDrawParts(Arduino_GFX* g, const SysmonSample& s, float netScale, uint8_t parts) {
  if (parts & SYSMON_CPU) drawRingPart(g, 60, s.cpu, kCpu);
  if (parts & SYSMON_MEM) drawRingPart(g, 180, s.mem, kMem);
  if (parts & SYSMON_DISK) {
    char num[8];
    sysmonPercentText(s.disk, num, sizeof(num));
    drawBottomPart(g, 0, num, "%", s.disk / 100.0f, kDisk);
  }
  if (parts & SYSMON_NET) {
    char num[8];
    const char* unit;
    float total = s.rxMbps + s.txMbps;
    sysmonNetText(total, num, sizeof(num), &unit);
    drawBottomPart(g, 120, num, unit, netScale > 0 ? total / netScale : 0, kNet);
  }
  g->setFont((const GFXfont*)nullptr);   // other modes use the built-in font
}

void sysmonDrawAll(Arduino_GFX* g, const SysmonSample& s, float netScale) {
  g->fillScreen(kBg);
  drawTopLabels(g);
  drawBottomLabels(g);
  sysmonDrawParts(g, s, netScale, SYSMON_ALL);
}

void sysmonWaitingLine(long secondsAgo, char* out, unsigned outLen) {
  if (secondsAgo < 0)          snprintf(out, outLen, "Run smalltv-stats.sh on it");
  else if (secondsAgo < 60)    snprintf(out, outLen, "Last update under a minute ago");
  else if (secondsAgo < 120)   snprintf(out, outLen, "Last update 1 min ago");
  else if (secondsAgo < 7200)  snprintf(out, outLen, "Last update %ld min ago", secondsAgo / 60);
  else if (secondsAgo < 172800) snprintf(out, outLen, "Last update %ld h ago", secondsAgo / 3600);
  else                          snprintf(out, outLen, "Last update %ld days ago", secondsAgo / 86400);
}

void sysmonDrawWaiting(Arduino_GFX* g, long secondsAgo) {
  g->fillScreen(kBg);
  // The empty rings, so the screen still reads as the stats page.
  g->fillArc(60, kRingCy, kRingOut, kRingIn, 0, 360, gfxTint(kCpu.track));
  g->fillArc(180, kRingCy, kRingOut, kRingIn, 0, 360, gfxTint(kMem.track));
  drawTopLabels(g);

  setFont(g, u8g2_font_helvB14_tr, gfxTint(kLabel));
  const char* t = "Waiting for the Mac";
  g->setCursor((240 - textW(g, t)) / 2, 168);
  g->print(t);

  char line[40];
  sysmonWaitingLine(secondsAgo, line, sizeof(line));
  setFont(g, u8g2_font_helvR10_tr, gfxTint(rgb(0x90, 0x90, 0x90)));
  g->setCursor((240 - textW(g, line)) / 2, 194);
  g->print(line);
  g->setFont((const GFXfont*)nullptr);
}
