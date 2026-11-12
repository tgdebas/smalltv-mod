// Just enough of the Arduino core for the Sonos parser (SonosXml/SonosParse)
// to build and run on the host.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

uint32_t millis();
inline void delay(unsigned long) {}
inline void yield() {}
#ifndef __GLIBC_PREREQ
#define __GLIBC_PREREQ(a, b) 0
#endif
#if !__GLIBC_PREREQ(2, 38)
inline size_t strlcpy(char* d, const char* s, size_t n) {
  size_t l = strlen(s);
  if (n) { size_t c = l < n - 1 ? l : n - 1; memcpy(d, s, c); d[c] = 0; }
  return l;
}
#endif

// Flash-string helpers: on the host every string is in RAM already.
#define PSTR(s) (s)
#define strcmp_P strcmp
#define strncmp_P strncmp
#define strncpy_P strncpy
#define snprintf_P snprintf
