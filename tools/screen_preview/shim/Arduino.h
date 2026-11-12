// Host stand-in for the Arduino core: just what Arduino_GFX's drawing code
// and the screen renderers need to compile on a PC.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "Print.h"
#define PROGMEM
#define pgm_read_byte(a) (*(const uint8_t*)(a))
#define pgm_read_word(a) (*(const uint16_t*)(a))
#define pgm_read_dword(a) (*(const uint32_t*)(a))
#define pgm_read_ptr(a) (*(void* const*)(a))
#define F_CPU 80000000L
#define GFX_SKIP_OUTPUT_BEGIN
typedef bool boolean;
typedef uint8_t byte;
inline unsigned long millis() { return 0; }
inline void delay(unsigned long) {}
inline void yield() {}
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#define _swap_int16_t(a, b) { int16_t t = a; a = b; b = t; }
