#include "SonosArt.h"
#include "Platform.h"
#include "Gfx.h"
#include <Arduino_GFX_Library.h>
#include <memory>
#include <new>
#include "tjpgd.h"
#include <LittleFS.h>

// TJpgDec's documented minimum for JD_FASTDECODE 1 is 3500 bytes, but the
// tables it builds grow with the image (four quantisation tables alone take
// 1 KB), so ask for more when the heap has it and fall back to the minimum.
#define ART_POOL_BYTES     4096
#define ART_POOL_PREFERRED 6144

namespace {
struct ArtSession {
  Client*  in = nullptr;     // network source, or
  File*    file = nullptr;   // a stored JPEG in LittleFS
  int32_t  left;        // Content-Length still to read, -1 = unknown
  uint32_t deadline;
  int      x0, y0;      // panel position of the decoded image's top-left
  int      clipX, clipY, clipW, clipH;
};

// Input callback: fill buf (or skip when buf is null) with exactly n bytes,
// fewer only at the end of the stream.
size_t artInput(JDEC* jd, uint8_t* buf, size_t n) {
  ArtSession* s = (ArtSession*)jd->device;
  if (s->file) {   // stored cover: read or skip straight from flash
    yield();
    if (!buf) {
      size_t left = s->file->size() - s->file->position();
      if (n > left) n = left;
      s->file->seek(n, SeekCur);
      return n;
    }
    int got = s->file->read(buf, n);
    return got > 0 ? (size_t)got : 0;
  }
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

// What the last attempt saw, for the Sonos tab: which URL form, the HTTP
// answer, the image and the heap. One line, overwritten on every draw.
static char g_detail[120] = "";
const char* sonosArtDetail() { return g_detail; }

// Copies `url` into out, rewritten for this chip:
//  - https becomes http. Album-art CDNs (Spotify's i.scdn.co, Apple's
//    mzstatic) serve the same files over plain HTTP, and a TLS handshake needs
//    ~20 KB of contiguous heap the ESP8266 rarely has.
//  - The largest cover size becomes a ~300 px one where the URL names a size,
//    so there is less to download and decode for a 164 px box.
static void artFetchUrl(const char* url, char* out, size_t n) {
  if (!strncmp(url, "https://", 8)) snprintf(out, n, "http://%s", url + 8);
  else strlcpy(out, url, n);
  // Spotify: ab67616d0000b273<id> is 640 px, ab67616d00001e02<id> is 300 px.
  char* p = strstr(out, "ab67616d0000b273");
  if (p) memcpy(p + 8, "00001e02", 8);
  // Apple: .../600x600bb.jpg (any WxH) -> 300x300bb.jpg.
  char* bb = strstr(out, "bb.jpg");
  if (bb) {
    char* slash = bb;
    while (slash > out && *slash != '/') slash--;
    if (*slash == '/' && strchr(slash, 'x') && strchr(slash, 'x') < bb) {
      char tailBuf[64];
      strlcpy(tailBuf, bb, sizeof(tailBuf));
      size_t room = n - (size_t)(slash + 1 - out);
      snprintf(slash + 1, room, "300x300%s", tailBuf);
    }
  }
}

// Decodes the JPEG the session reads and draws it centred in the box.
// Appends to g_detail from position n.
static ArtResult decodeJpeg(ArtSession& s, int x, int y, int box, int& n) {
  size_t poolSize = ART_POOL_PREFERRED;
  std::unique_ptr<uint8_t[]> pool(new (std::nothrow) uint8_t[poolSize]);
  if (!pool) {
    poolSize = ART_POOL_BYTES;
    pool.reset(new (std::nothrow) uint8_t[poolSize]);
  }
  if (!pool) return ART_LOW_HEAP;
  s.clipX = x; s.clipY = y; s.clipW = box; s.clipH = box;

  JDEC jd;
  jd.swap = 0;
  JRESULT jr = jd_prepare(&jd, artInput, pool.get(), poolSize, &s);
  if (jr != JDR_OK) {
    n += snprintf(g_detail + n, sizeof(g_detail) - n, ", jpeg prepare %d", (int)jr);
    return (jr == JDR_FMT3 || jr == JDR_FMT1 || jr == JDR_MEM1) ? ART_UNSUPPORTED : ART_DECODE_ERROR;
  }
  // Largest of 1/1, 1/2, 1/4, 1/8 that fits the box.
  uint8_t scale = 0;
  while (scale < 3 && ((jd.width >> scale) > box || (jd.height >> scale) > box)) scale++;
  int ow = jd.width >> scale, oh = jd.height >> scale;
  s.x0 = x + (box - ow) / 2;
  s.y0 = y + (box - oh) / 2;
  n += snprintf(g_detail + n, sizeof(g_detail) - n, ", %ux%u 1/%u", jd.width, jd.height, 1u << scale);
  jr = jd_decomp(&jd, artOutput, scale);
  n += snprintf(g_detail + n, sizeof(g_detail) - n, ", %s", jr == JDR_OK ? "drawn" : "decode err");
  return jr == JDR_OK ? ART_OK : ART_DECODE_ERROR;
}

static ArtResult fetchAndDraw(const char* url, int x, int y, int box, uint16_t timeoutMs) {
  bool https = !strncmp(url, "https://", 8);
  uint32_t t0 = millis();
  uint32_t blk = platformMaxFreeBlock();
  int n = snprintf(g_detail, sizeof(g_detail), "%s, heap blk %lu", https ? "https" : "http",
                   (unsigned long)blk);

  std::unique_ptr<NetClient> client;
  if (https) {
    if (blk < 16000 + ART_POOL_BYTES) {
      snprintf(g_detail + n, sizeof(g_detail) - n, ", https needs ~20 KB");
      return ART_LOW_HEAP;
    }
    client.reset(platformMakeSecureClient(4096));
  } else {
    if (blk < ART_POOL_BYTES + 4096) return ART_LOW_HEAP;
    client.reset(new WiFiClient());
  }

  HTTPClient http;
  http.useHTTP10(true);   // no chunked framing in the JPEG stream
  http.setReuse(false);
  http.setTimeout(timeoutMs);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  const char* hdrs[] = {"Content-Type"};
  http.collectHeaders(hdrs, 1);
  if (!http.begin(*client, url)) return ART_CONNECT_FAIL;
  int code = http.GET();
  n += snprintf(g_detail + n, sizeof(g_detail) - n, ", HTTP %d", code);
  if (code != HTTP_CODE_OK) {
    http.end();
    return code < 0 ? ART_CONNECT_FAIL : ART_HTTP_ERROR;
  }
  String type = http.header("Content-Type");
  n += snprintf(g_detail + n, sizeof(g_detail) - n, ", %.20s, %d B", type.c_str(), http.getSize());

  ArtSession s;
  s.in = &http.getStream();
  s.left = http.getSize();
  s.deadline = millis() + (uint32_t)timeoutMs * 2;   // a cover is ~30-150 KB
  ArtResult r = decodeJpeg(s, x, y, box, n);
  http.end();
  snprintf(g_detail + n, sizeof(g_detail) - n, ", %lu ms", (unsigned long)(millis() - t0));
  return r;
}

ArtResult sonosDrawArt(const char* url, int x, int y, int box, uint16_t timeoutMs) {
  if (!url || !url[0]) { g_detail[0] = 0; return ART_NO_URL; }
  // The rewritten form first (plain HTTP, smaller image); the original only
  // when it differs and the first could not even be fetched.
  char alt[SONOS_ART_URL_MAX];
  artFetchUrl(url, alt, sizeof(alt));
  ArtResult r = fetchAndDraw(alt, x, y, box, timeoutMs);
  if ((r == ART_CONNECT_FAIL || r == ART_HTTP_ERROR) && strcmp(alt, url)) {
    char first[sizeof(g_detail)];
    strlcpy(first, g_detail, sizeof(first));
    r = fetchAndDraw(url, x, y, box, timeoutMs);
    if (r != ART_OK) {   // keep both stories
      char both[sizeof(g_detail)];
      snprintf(both, sizeof(both), "%.55s | %.60s", first, g_detail);
      strlcpy(g_detail, both, sizeof(g_detail));
    }
  }
  return r;
}

// Walks a stored JPEG's markers up to its frame header, to name what the
// decoder refused: "SOF2" is progressive, "Y12" an unsupported chroma
// layout, and so on. Appends e.g. ", SOF0 3c Y22 T00/11/11" to g_detail.
static void describeJpeg(File& f, int& n) {
  f.seek(2, SeekSet);
  uint8_t h[4];
  char sof[24] = "";
  char tables[16] = "";
  for (int guard = 0; guard < 40 && f.read(h, 4) == 4; guard++) {
    if (h[0] != 0xFF) break;
    uint16_t len = (uint16_t)(h[2] << 8 | h[3]);
    uint32_t next = f.position() + len - 2;
    uint8_t m = h[1];
    if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
      uint8_t b[12];
      int got = f.read(b, sizeof(b));
      if (got >= 9) snprintf(sof, sizeof(sof), "SOF%u %uc Y%02X", m - 0xC0, b[5], b[7]);
      else snprintf(sof, sizeof(sof), "SOF%u", m - 0xC0);
    } else if (m == 0xDA) {   // scan: the table numbers of each component
      uint8_t b[7];
      int got = f.read(b, sizeof(b));
      if (got >= 7 && b[0] == 3) snprintf(tables, sizeof(tables), "T%02X/%02X/%02X", b[2], b[4], b[6]);
      break;
    }
    f.seek(next, SeekSet);
  }
  n += snprintf(g_detail + n, sizeof(g_detail) - n, ", %s %s", sof[0] ? sof : "no SOF", tables);
}

ArtResult sonosDrawArtFile(const char* path, int x, int y, int box, const char* label) {
  File f = LittleFS.open(path, "r");
  if (!f) return ART_NO_URL;
  int n = snprintf(g_detail, sizeof(g_detail), "%s, %u B", label, (unsigned)f.size());
  ArtSession s;
  s.file = &f;
  s.left = f.size();
  s.deadline = millis() + 10000;
  ArtResult r = decodeJpeg(s, x, y, box, n);
  if (r == ART_UNSUPPORTED || r == ART_DECODE_ERROR) describeJpeg(f, n);
  f.close();
  return r;
}
