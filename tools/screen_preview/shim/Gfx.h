// Host stand-in for src/Gfx.h: no panel colour correction in a preview.
#pragma once
#include <stdint.h>
inline uint16_t gfxTint(uint16_t c) { return c; }
#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_GRAY   0x8410
#define C_DGRAY  0x4208
#define C_YELLOW 0xFFE0
