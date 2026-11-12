#include "SonosArt.h"
#include "Platform.h"
#include "Gfx.h"
#include <Arduino_GFX_Library.h>
#include <memory>
#include <new>
#include "tjpgd.h"

// TJpgDec's documented minimum for JD_FASTDECODE 1 is 3500 bytes; the margin
// covers JPEGs with four Huffman tables and 2x2 chroma subsampling.
#define ART_POOL_BYTES 4096

namespace {
struct ArtSession {
  Client*  in;
  int32_t  left;        // Content-Length still to read, -1 = unknown
  uint32_t deadline;
  int      x0, y0;      // panel position of the decoded image's top-left
  int      clipX, clipY, clipW, clipH;
};

// Input callback: fill buf (or skip when buf is null) with exactly n bytes,
// fewer only at the end of the stream.
size_t artInput(JDEC* jd, uint8_t* buf, size_t n) {
  ArtSession* s = (ArtSession*)jd->device;
  size_t done = 0;
  uint8_t sink[64];
  while (done < n) {
    if (s->left == 0) break;
    int avail = s->in->available();
    if (avail <= 0) {
      if (!s->in->connected() || (int32_t)(millis() - s->deadline) >= 0) break;
      delay(1);
      continue;
    }
    size_t want = n - done;
    if ((size_t)avail < want) want = avail;
    if (s->left > 0 && (int32_t)want > s->left) want = s->left;
    uint8_t* dst = buf ? buf + done : sink;
    if (!buf && want > sizeof(sink)) want = sizeof(sink);
    int got = s->in->read(dst, want);
    if (got <= 0) break;
    done += got;
    if (s->left > 0) s->left -= got;
  }
  yield();
  return done;
}

// Output callback: one decoded block, RGB565. Clips to the art box and
// applies the panel colour correction the rest of the UI gets via gfxTint.
int artOutput(JDEC* jd, void* bitmap, JRECT* r) {
  ArtSession* s = (ArtSession*)jd->device;
  uint16_t* px = (uint16_t*)bitmap;
  int w = r->right - r->left + 1;
  int h = r->bottom - r->top + 1;
  int x = s->x0 + r->left, y = s->y0 + r->top;

  for (int i = 0; i < w * h; i++) px[i] = gfxTint(px[i]);

  if (x >= s->clipX && y >= s->clipY &&
      x + w <= s->clipX + s->clipW && y + h <= s->clipY + s->clipH) {
    gfxDev()->draw16bitRGBBitmap(x, y, px, w, h);
  } else {
    // Rare: a huge image even at 1/8 scale. Draw only what falls in the box.
    for (int row = 0; row < h; row++) {
      int yy = y + row;
      if (yy < s->clipY || yy >= s->clipY + s->clipH) continue;
      for (int col = 0; col < w; col++) {
        int xx = x + col;
        if (xx < s->clipX || xx >= s->clipX + s->clipW) continue;
        gfxDev()->drawPixel(xx, yy, px[row * w + col]);
      }
    }
  }
  yield();
  return 1;   // continue
}
}  // namespace

const char* sonosArtResultName(ArtResult r) {
  switch (r) {
    case ART_OK:           return "ok";
    case ART_NO_URL:       return "no art";
    case ART_LOW_HEAP:     return "skipped, low heap";
    case ART_CONNECT_FAIL: return "connect failed";
    case ART_HTTP_ERROR:   return "http error";
    case ART_UNSUPPORTED:  return "unsupported image (not baseline JPEG)";
    default:               return "decode error";
  }
}

ArtResult sonosDrawArt(const char* url, int x, int y, int box, uint16_t timeoutMs) {
  if (!url || !url[0]) return ART_NO_URL;
  bool https = !strncmp(url, "https://", 8);

  std::unique_ptr<NetClient> client;
  if (https) {
    // Art from the speaker's /getaa proxy is plain HTTP; only some radio
    // stations hand out https logos. Same TLS heap floor as the other fetches.
    if (platformMaxFreeBlock() < 16000 + ART_POOL_BYTES) return ART_LOW_HEAP;
    client.reset(platformMakeSecureClient(4096));
  } else {
    if (platformMaxFreeBlock() < ART_POOL_BYTES + 4096) return ART_LOW_HEAP;
    client.reset(new WiFiClient());
  }

  HTTPClient http;
  http.useHTTP10(true);   // no chunked framing in the JPEG stream
  http.setReuse(false);
  http.setTimeout(timeoutMs);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(*client, url)) return ART_CONNECT_FAIL;
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return code < 0 ? ART_CONNECT_FAIL : ART_HTTP_ERROR;
  }

  std::unique_ptr<uint8_t[]> pool(new (std::nothrow) uint8_t[ART_POOL_BYTES]);
  if (!pool) { http.end(); return ART_LOW_HEAP; }

  ArtSession s;
  s.in = &http.getStream();
  s.left = http.getSize();
  s.deadline = millis() + (uint32_t)timeoutMs * 2;   // a cover is ~50-150 KB
  s.clipX = x; s.clipY = y; s.clipW = box; s.clipH = box;

  JDEC jd;
  jd.swap = 0;
  JRESULT jr = jd_prepare(&jd, artInput, pool.get(), ART_POOL_BYTES, &s);
  if (jr != JDR_OK) {
    http.end();
    return (jr == JDR_FMT3 || jr == JDR_FMT1 || jr == JDR_MEM1) ? ART_UNSUPPORTED : ART_DECODE_ERROR;
  }

  // Largest of 1/1, 1/2, 1/4, 1/8 that fits the box.
  uint8_t scale = 0;
  while (scale < 3 && ((jd.width >> scale) > box || (jd.height >> scale) > box)) scale++;
  int ow = jd.width >> scale, oh = jd.height >> scale;
  s.x0 = x + (box - ow) / 2;
  s.y0 = y + (box - oh) / 2;

  jr = jd_decomp(&jd, artOutput, scale);
  http.end();
  return jr == JDR_OK ? ART_OK : ART_DECODE_ERROR;
}
