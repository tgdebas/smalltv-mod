// SonosArt.h — streams album art from a URL straight onto the panel.
//
// The JPEG is never held in RAM: ChaN's TJpgDec pulls bytes off the socket as
// it decodes (a ~4 KB work area), and hands back 8x8..16x16 pixel blocks that
// go straight to the display. Its built-in 1/2, 1/4, 1/8 downscale is how a
// 640x640 cover becomes 160x160 on a chip with ~40 KB of free heap.
// Baseline JPEG only: progressive JPEGs and PNGs return ART_UNSUPPORTED and the
// caller draws a placeholder instead.
#pragma once
#include <Arduino.h>

enum ArtResult : uint8_t {
  ART_OK = 0,
  ART_NO_URL,
  ART_LOW_HEAP,
  ART_CONNECT_FAIL,
  ART_HTTP_ERROR,
  ART_UNSUPPORTED,   // not a baseline JPEG
  ART_DECODE_ERROR,  // truncated stream, timeout, corrupt data
};

// Draw the image centred in the box (x, y, box, box). Pixels outside the
// decoded image are left as they were, so fill the box first.
ArtResult sonosDrawArt(const char* url, int x, int y, int box, uint16_t timeoutMs);
const char* sonosArtResultName(ArtResult r);
