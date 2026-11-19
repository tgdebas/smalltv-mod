#include "SonosBmp.h"
#include <string.h>

static uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

static uint8_t lowBit(uint32_t m) {
  uint8_t s = 0;
  while (m && !(m & 1)) { m >>= 1; s++; }
  return s;
}

bool BmpTo565::parseHeader() {
  if (hdr_[0] != 'B' || hdr_[1] != 'M') return fail("not a BMP");
  dataOff_ = le32(hdr_ + 10);
  uint32_t dib = le32(hdr_ + 14);
  if (dib < 40) return fail("old BMP header");
  int32_t w = (int32_t)le32(hdr_ + 18);
  int32_t h = (int32_t)le32(hdr_ + 22);
  uint16_t bpp = le16(hdr_ + 28);
  uint32_t comp = le32(hdr_ + 30);
  if (w <= 0 || w > SONOS_BMP_MAX || h == 0 || h > SONOS_BMP_MAX || h < -SONOS_BMP_MAX)
    return fail("size");
  if (bpp != 24 && bpp != 32) return fail("bit depth");
  bottomUp_ = h > 0;
  width_ = (uint16_t)w;
  height_ = (uint16_t)(h > 0 ? h : -h);
  bpp_ = (uint8_t)bpp;
  stride_ = ((uint32_t)bpp * width_ + 31) / 32 * 4;
  if (comp == 3 || comp == 6) {   // BI_BITFIELDS / BI_ALPHABITFIELDS: masks right after the 40-byte info
    if (bpp != 32) return fail("bitfields depth");
    mask_[0] = le32(hdr_ + 54);
    mask_[1] = le32(hdr_ + 58);
    mask_[2] = le32(hdr_ + 62);
    mask_[3] = (dib >= 56 || comp == 6) ? le32(hdr_ + 66) : 0;
  } else if (comp == 0) {         // BI_RGB: bytes are B, G, R (, "unused")
    mask_[0] = 0x00FF0000; mask_[1] = 0x0000FF00; mask_[2] = 0x000000FF;
    // The fourth byte is officially unused, yet many writers (Pillow among
    // them) keep alpha there while others leave it 0. So it is read as
    // alpha, except that 0 counts as opaque: a zeroed byte must not black
    // out the image, and a truly transparent pixel shows its own colour.
    mask_[3] = bpp == 32 ? 0xFF000000 : 0;
    zeroAlphaOpaque_ = true;
  } else {
    return fail("compressed BMP");
  }
  for (int i = 0; i < 4; i++) shift_[i] = lowBit(mask_[i]);
  if (dataOff_ < 54) return fail("pixel offset");
  uint8_t h565[R565_HEADER] = {'R', '5', (uint8_t)(bottomUp_ ? 1 : 0), 0,
                               (uint8_t)(width_ & 0xFF), (uint8_t)(width_ >> 8),
                               (uint8_t)(height_ & 0xFF), (uint8_t)(height_ >> 8)};
  if (!out_(ctx_, h565, sizeof(h565))) return fail("write");
  started_ = true;
  return true;
}

// One byte of pixel data (row padding included).
bool BmpTo565::pixelByte(uint8_t b) {
  if (rowsDone_ >= height_) return true;   // trailing bytes after the last row
  uint32_t used = (uint32_t)width_ * (bpp_ / 8);
  if (inRow_ < used) {
    px_[pxFill_++] = b;
    if (pxFill_ == bpp_ / 8) {
      pxFill_ = 0;
      uint32_t v = px_[0] | px_[1] << 8 | px_[2] << 16 | (bpp_ == 32 ? (uint32_t)px_[3] << 24 : 0);
      uint8_t c[3];
      for (int i = 0; i < 3; i++) c[i] = (uint8_t)((v & mask_[i]) >> shift_[i]);
      if (mask_[3]) {
        uint8_t a = (uint8_t)((v & mask_[3]) >> shift_[3]);
        if (a == 0 && zeroAlphaOpaque_) a = 255;
        uint8_t bgc[3] = {(uint8_t)((bg_ >> 8) & 0xF8), (uint8_t)((bg_ >> 3) & 0xFC), (uint8_t)(bg_ << 3)};
        for (int i = 0; i < 3; i++) c[i] = (uint8_t)((c[i] * a + bgc[i] * (255 - a) + 127) / 255);
      }
      uint16_t rgb = (uint16_t)((c[0] & 0xF8) << 8 | (c[1] & 0xFC) << 3 | c[2] >> 3);
      rowOut_[2 * col_] = (uint8_t)(rgb & 0xFF);
      rowOut_[2 * col_ + 1] = (uint8_t)(rgb >> 8);
      col_++;
    }
  }
  if (++inRow_ == stride_) {
    if (!out_(ctx_, rowOut_, 2u * width_)) return fail("write");
    inRow_ = 0;
    col_ = 0;
    rowsDone_++;
  }
  return true;
}

bool BmpTo565::feed(const uint8_t* data, size_t len) {
  if (err_) return false;
  for (size_t i = 0; i < len; i++, pos_++) {
    uint8_t b = data[i];
    if (!started_) {
      if (pos_ < sizeof(hdr_)) hdr_[pos_] = b;
      // The header is read once 70 bytes are in, or at the pixel offset if a
      // file puts its pixels earlier than that (a plain 40-byte info header).
      bool atData = pos_ >= 14 && pos_ + 1 >= 54 && pos_ + 1 == le32(hdr_ + 10);
      if (pos_ + 1 == sizeof(hdr_) || atData) {
        if (!parseHeader()) return false;
        if (pos_ + 1 > dataOff_) {   // already inside the pixels: replay them
          for (uint32_t k = dataOff_; k <= pos_; k++)
            if (!pixelByte(hdr_[k])) return false;
        }
      }
      continue;
    }
    if (pos_ < dataOff_) continue;   // colour table, gap
    if (!pixelByte(b)) return false;
  }
  return true;
}
