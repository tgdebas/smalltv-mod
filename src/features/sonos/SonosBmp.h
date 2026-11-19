// SonosBmp.h — streams a BMP into the cube's own cover format, "R565".
//
// Covers the Mac relays arrive as BMP (sips -s format bmp): uncompressed, so
// no JPEG decoder is involved (sips keeps a progressive source progressive,
// which the decoder cannot read). This converts while the upload streams in,
// a few bytes at a time, into RGB565 rows the panel takes as they are.
//
// R565 file: 8-byte header  'R' '5' flags 0  width(u16 LE) height(u16 LE),
// then height rows of width RGB565 pixels (u16 LE), in the BMP's own row
// order: flags bit 0 set = bottom-up (the first row is the bottom one).
//
// Accepted: 24 or 32 bits per pixel, BI_RGB or BI_BITFIELDS, any header
// version, either row order, up to SONOS_BMP_MAX x SONOS_BMP_MAX. An alpha
// channel is blended over `bg`. No Arduino dependency (native tests).
#pragma once
#include <stddef.h>
#include <stdint.h>

#define SONOS_BMP_MAX 164
#define R565_HEADER   8

class BmpTo565 {
 public:
  // `out` receives the converted bytes; return false to abort.
  typedef bool (*Sink)(void* ctx, const uint8_t* data, size_t len);
  BmpTo565(Sink out, void* ctx, uint16_t bg565) : out_(out), ctx_(ctx), bg_(bg565) {}

  bool feed(const uint8_t* data, size_t len);   // false: not a usable BMP, or the sink failed
  bool finished() const { return rowsDone_ == height_ && height_ > 0; }
  uint16_t width() const { return width_; }
  uint16_t height() const { return height_; }
  const char* error() const { return err_; }

 private:
  bool parseHeader();
  bool pixelByte(uint8_t b);
  bool fail(const char* why) { err_ = why; return false; }

  Sink     out_;
  void*    ctx_;
  uint16_t bg_;
  const char* err_ = nullptr;

  uint8_t  hdr_[70];          // file header + the DIB fields we read (masks end at 70)
  uint32_t pos_ = 0;          // bytes consumed so far
  uint32_t dataOff_ = 0;      // where the pixels start
  bool     started_ = false;  // header parsed, R565 header written
  uint16_t width_ = 0, height_ = 0;
  bool     bottomUp_ = true;
  uint8_t  bpp_ = 0;          // 24 or 32
  uint32_t mask_[4] = {0, 0, 0, 0};   // R, G, B, A
  uint8_t  shift_[4] = {0, 0, 0, 0};
  bool     zeroAlphaOpaque_ = false;   // BI_RGB 32-bit: alpha 0 means "not set"
  uint32_t stride_ = 0;       // bytes per BMP row, padded to 4
  uint32_t inRow_ = 0;        // bytes of the current BMP row seen
  uint16_t rowsDone_ = 0;
  uint8_t  px_[4];            // the current pixel's bytes
  uint8_t  pxFill_ = 0;
  uint16_t col_ = 0;
  uint8_t  rowOut_[2 * SONOS_BMP_MAX];
};
