// Native tests for the BMP -> R565 streaming converter (SonosBmp.cpp): the
// variants a Mac's sips or another tool may write, fed in uneven pieces the
// way an upload arrives, checked pixel by pixel.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "SonosBmp.h"

static int g_fail = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); g_fail++; } } while (0)

struct Px { uint8_t r, g, b, a; };
static Px pixelAt(int x, int y) {   // a pattern with every channel varying
  return {(uint8_t)(x * 37 + y * 3), (uint8_t)(y * 29 + x), (uint8_t)((x ^ y) * 11), (uint8_t)(x * 255 / 9)};
}
static void put16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(x & 0xFF); v.push_back(x >> 8); }
static void put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; i++) v.push_back((x >> (8 * i)) & 0xFF); }

// dib: 40, 108 or 124. comp: 0 = BI_RGB, 3 = BI_BITFIELDS. topDown: negative height.
static std::vector<uint8_t> makeBmp(int w, int h, int bpp, int dib, int comp, bool topDown, bool alpha) {
  std::vector<uint8_t> v;
  int masks = (comp == 3 && dib == 40) ? 12 : 0;
  uint32_t off = 14 + dib + masks;
  uint32_t stride = ((uint32_t)bpp * w + 31) / 32 * 4;
  v.push_back('B'); v.push_back('M'); put32(v, off + stride * h); put32(v, 0); put32(v, off);
  put32(v, dib); put32(v, w); put32(v, topDown ? -h : h); put16(v, 1); put16(v, bpp);
  put32(v, comp); put32(v, stride * h); put32(v, 2835); put32(v, 2835); put32(v, 0); put32(v, 0);
  if (comp == 3 || dib > 40) {   // masks: R G B (A)
    put32(v, 0x00FF0000); put32(v, 0x0000FF00); put32(v, 0x000000FF);
    if (dib > 40) put32(v, alpha ? 0xFF000000 : 0);
  }
  while (v.size() < off) v.push_back(0);   // rest of a V4/V5 header
  for (int r = 0; r < h; r++) {
    int y = topDown ? r : h - 1 - r;
    size_t start = v.size();
    for (int x = 0; x < w; x++) {
      Px p = pixelAt(x, y);
      v.push_back(p.b); v.push_back(p.g); v.push_back(p.r);
      if (bpp == 32) v.push_back(alpha ? p.a : (comp == 0 ? 0 : 0xFF));   // BI_RGB: the usual zero
    }
    while (v.size() - start < stride) v.push_back(0);
  }
  return v;
}

static bool sink(void* ctx, const uint8_t* d, size_t n) {
  auto* out = (std::vector<uint8_t>*)ctx;
  out->insert(out->end(), d, d + n);
  return true;
}

static const uint16_t BG = 0x18E3;
// zeroOpaque: a BI_RGB file, where alpha 0 means "not set" (see SonosBmp.cpp).
static uint16_t expect565(Px p, bool alpha, bool zeroOpaque) {
  uint8_t c[3] = {p.r, p.g, p.b};
  if (alpha && !(zeroOpaque && p.a == 0)) {
    uint8_t bg[3] = {(uint8_t)((BG >> 8) & 0xF8), (uint8_t)((BG >> 3) & 0xFC), (uint8_t)(BG << 3)};
    for (int i = 0; i < 3; i++) c[i] = (uint8_t)((c[i] * p.a + bg[i] * (255 - p.a) + 127) / 255);
  }
  return (uint16_t)((c[0] & 0xF8) << 8 | (c[1] & 0xFC) << 3 | c[2] >> 3);
}

static void run(const char* name, int w, int h, int bpp, int dib, int comp, bool topDown, bool alpha) {
  std::vector<uint8_t> bmp = makeBmp(w, h, bpp, dib, comp, topDown, alpha);
  std::vector<uint8_t> out;
  BmpTo565 c(sink, &out, BG);
  srand(w * 31 + h);
  bool ok = true;
  for (size_t i = 0; i < bmp.size() && ok;) {
    size_t n = 1 + rand() % 97;
    if (i + n > bmp.size()) n = bmp.size() - i;
    ok = c.feed(bmp.data() + i, n);
    i += n;
  }
  char msg[96];
  snprintf(msg, sizeof msg, "%s: converts (%s)", name, c.error() ? c.error() : "-");
  CHECK(ok && c.finished(), msg);
  CHECK(out.size() == (size_t)R565_HEADER + 2u * w * h, name);
  if (out.size() < R565_HEADER) return;
  CHECK(out[0] == 'R' && out[1] == '5', "magic");
  CHECK((out[2] & 1) == (topDown ? 0 : 1), "row order flag");
  CHECK(out[4] + (out[5] << 8) == w && out[6] + (out[7] << 8) == h, "size in header");
  int bad = 0;
  for (int r = 0; r < h && out.size() >= (size_t)R565_HEADER + 2u * w * h; r++) {
    int y = topDown ? r : h - 1 - r;   // file rows are in the BMP's order
    for (int x = 0; x < w; x++) {
      size_t o = R565_HEADER + 2u * (r * w + x);
      uint16_t got = out[o] | out[o + 1] << 8;
      if (got != expect565(pixelAt(x, y), alpha, comp == 0)) bad++;
    }
  }
  snprintf(msg, sizeof msg, "%s: every pixel (%d wrong)", name, bad);
  CHECK(bad == 0, msg);
}

int main() {
  run("24-bit bottom-up, odd width (row padding)", 7, 5, 24, 40, 0, false, false);
  run("24-bit top-down", 10, 4, 24, 40, 0, true, false);
  run("32-bit BI_RGB", 9, 6, 32, 40, 0, false, false);
  run("32-bit BITFIELDS, 40-byte info + masks", 9, 6, 32, 40, 3, false, false);
  run("32-bit V4 header with alpha", 10, 10, 32, 108, 3, false, true);
  run("32-bit V5 header with alpha, top-down", 10, 3, 32, 124, 3, true, true);
  run("32-bit BI_RGB with alpha in the spare byte (Pillow)", 10, 4, 32, 40, 0, false, true);
  run("160x160 V5, the size the Mac sends", 160, 160, 32, 124, 3, false, true);

  // Refusals: a PNG, a JPEG, compressed or too big BMPs, 16-bit.
  std::vector<uint8_t> o;
  const uint8_t png[] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
  BmpTo565 a(sink, &o, BG);
  bool r = true;
  for (int i = 0; i < 80 && r; i++) r = a.feed(png, sizeof png);
  CHECK(!r && a.error(), "PNG refused");
  std::vector<uint8_t> big = makeBmp(200, 10, 24, 40, 0, false, false);
  BmpTo565 b(sink, &o, BG);
  CHECK(!b.feed(big.data(), big.size()), "wider than the cover box refused");
  std::vector<uint8_t> b16 = makeBmp(8, 8, 24, 40, 0, false, false);
  b16[28] = 16;
  BmpTo565 c(sink, &o, BG);
  CHECK(!c.feed(b16.data(), b16.size()), "16-bit refused");
  std::vector<uint8_t> rle = makeBmp(8, 8, 24, 40, 0, false, false);
  rle[30] = 1;
  BmpTo565 d(sink, &o, BG);
  CHECK(!d.feed(rle.data(), rle.size()), "RLE refused");

  if (g_fail) { printf("%d check(s) failed\n", g_fail); return 1; }
  printf("bmp converter tests: all passed\n");
  return 0;
}
