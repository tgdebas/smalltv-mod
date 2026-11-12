// Renders a screen with the firmware's own drawing code into a 240x240 image,
// on a PC. Arduino_GFX and the u8g2 fonts are compiled from the build's
// libdeps; only the panel is replaced by a framebuffer. See run.sh.
//
//   preview sysmon OUT.ppm CPU MEM DISK RX TX [NETSCALE]
//   preview sysmon-waiting OUT.ppm SECONDS_AGO
#include <Arduino.h>
#include "Arduino_GFX.h"
#include "SysmonView.h"

class Framebuffer : public Arduino_GFX {
 public:
  Framebuffer() : Arduino_GFX(240, 240) {}
  bool begin(int32_t) override { return true; }
  void writePixelPreclipped(int16_t x, int16_t y, uint16_t c) override { px[y * 240 + x] = c; }
  bool save(const char* path) const {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n240 240\n255\n");
    for (int i = 0; i < 240 * 240; i++) {
      uint16_t c = px[i];
      unsigned char rgb[3] = {(unsigned char)(((c >> 11) & 31) * 255 / 31),
                              (unsigned char)(((c >> 5) & 63) * 255 / 63),
                              (unsigned char)((c & 31) * 255 / 31)};
      fwrite(rgb, 1, 3, f);
    }
    return fclose(f) == 0;
  }
  uint16_t px[240 * 240] = {};
};

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: preview SCREEN OUT.ppm ARGS...\n"); return 2; }
  static Framebuffer fb;
  const char* screen = argv[1];
  if (!strcmp(screen, "sysmon") && argc >= 8) {
    SysmonSample s = {strtof(argv[3], 0), strtof(argv[4], 0), strtof(argv[5], 0),
                      strtof(argv[6], 0), strtof(argv[7], 0)};
    float scale = argc >= 9 ? strtof(argv[8], 0) : 100;
    sysmonDrawAll(&fb, s, scale);
  } else if (!strcmp(screen, "sysmon-waiting") && argc >= 4) {
    sysmonDrawWaiting(&fb, atol(argv[3]));
  } else {
    fprintf(stderr, "unknown screen or missing arguments\n");
    return 2;
  }
  return fb.save(argv[2]) ? 0 : 1;
}
