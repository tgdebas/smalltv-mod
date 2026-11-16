#include "SonosMode.h"
#include <Arduino_GFX_Library.h>
#include <U8g2lib.h>
#include "Gfx.h"
#include "SonosArt.h"
#include "SonosClient.h"

extern void appReleaseHeap(uint32_t needFree, uint32_t needLargestBlock);

SonosMode g_sonosMode;

// Layout (240x240): the cover in a square at the top, title and artist under
// it, a small pause badge in the top-right corner outside the cover.
static const int ART = SONOS_ART_BOX;
static const int ART_X = (TFT_WIDTH - ART) / 2;
static const int ART_Y = 8;
static const int TITLE_BASE = ART_Y + ART + 26;    // text baselines
static const int ARTIST_BASE = TITLE_BASE + 24;
static const int TEXT_W = TFT_WIDTH - 16;

#define C_ART_BG  gfxTint(0x18E3)   // placeholder square
#define C_SUBTLE  gfxTint(0x9CF3)   // artist line

enum : uint8_t { K_TRACK, K_IDLE, K_STATUS };

// ---- text helpers (u8g2 fonts: proportional, UTF-8, Latin-1 glyphs) ---------
static int textWidth(Arduino_GFX* g, const char* s) {
  int16_t x1, y1;
  uint16_t w, h;
  g->getTextBounds(s, 0, 100, &x1, &y1, &w, &h);
  return w;
}

// Shortens s in place with a trailing "..." until it fits maxW.
static void ellipsize(Arduino_GFX* g, char* s, int maxW) {
  if (textWidth(g, s) <= maxW) return;
  size_t n = strlen(s);
  while (n > 0) {
    do { n--; } while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80);   // whole UTF-8 chars
    while (n > 0 && s[n - 1] == ' ') n--;
    s[n] = 0;
    char probe[104];
    snprintf(probe, sizeof(probe), "%s...", s);
    if (textWidth(g, probe) <= maxW) { strlcpy(s, probe, 100); return; }
  }
}

static void drawLine(Arduino_GFX* g, const char* text, const uint8_t* font,
                     int baseline, uint16_t color) {
  if (!text || !text[0]) return;
  char buf[100];
  strlcpy(buf, text, sizeof(buf));
  g->setFont(font);
  g->setUTF8Print(true);
  g->setTextWrap(false);
  g->setTextSize(1);
  ellipsize(g, buf, TEXT_W);
  g->setTextColor(color);
  g->setCursor((TFT_WIDTH - textWidth(g, buf)) / 2, baseline);
  g->print(buf);
}

static void resetFont(Arduino_GFX* g) {
  g->setUTF8Print(false);
  g->setFont((const GFXfont*)nullptr);   // back to the built-in font for other modes
}

// A radio (body, speaker grille, dial, antenna), for streams without a cover.
static void drawRadio(Arduino_GFX* g, int cx, int cy) {
  uint16_t c = C_GRAY;
  g->drawLine(cx - 18, cy - 24, cx + 14, cy - 40, c);          // antenna
  g->drawLine(cx - 18, cy - 23, cx + 14, cy - 39, c);
  g->fillRoundRect(cx - 40, cy - 24, 80, 50, 8, c);             // body
  g->fillCircle(cx - 14, cy + 1, 15, C_ART_BG);                 // speaker
  for (int r = 3; r <= 12; r += 4) g->drawCircle(cx - 14, cy + 1, r, c);
  g->fillRoundRect(cx + 6, cy - 14, 26, 12, 3, C_ART_BG);       // tuning window
  g->fillRect(cx + 17, cy - 13, 2, 10, c);
  g->fillCircle(cx + 19, cy + 13, 6, C_ART_BG);                 // dial
}

// A simple eighth note, for covers that could not be drawn.
static void drawNote(Arduino_GFX* g, int cx, int cy) {
  uint16_t c = C_GRAY;
  g->fillCircle(cx - 12, cy + 22, 12, c);
  g->fillRect(cx - 2, cy - 30, 5, 52, c);
  g->fillTriangle(cx + 3, cy - 30, cx + 3, cy - 12, cx + 24, cy - 8, c);
}

// ---- DisplayMode ------------------------------------------------------------
void SonosMode::begin(const Settings& s) {
  sonosInit(s);
  needFull_ = true;
}

void SonosMode::invalidate(const Settings& s) {
  sonosInit(s);
  sonosForceRefresh();
  needFull_ = true;
  shownKey_ = 0;
}

const char* SonosMode::artStatus() const {
  return sonosArtResultName((ArtResult)artResult_);
}

void SonosMode::renderBadge() {
  Arduino_GFX* g = gfxDev();
  const int cx = TFT_WIDTH - 17, cy = 17;
  g->fillRect(cx - 12, cy - 12, 25, 25, C_BLACK);
  if (!shownPaused_) return;
  g->fillCircle(cx, cy, 11, C_DGRAY);
  g->fillRect(cx - 5, cy - 6, 3, 13, C_WHITE);
  g->fillRect(cx + 2, cy - 6, 3, 13, C_WHITE);
}

void SonosMode::renderFull(const Settings& s) {
  Arduino_GFX* g = gfxDev();
  const SonosTrack& t = sonosTrack();

  if (shownKind_ != K_TRACK) {
    resetFont(g);
    if (shownKind_ == K_IDLE) {
      g->fillScreen(C_BLACK);
      drawNote(g, TFT_WIDTH / 2, 92);
      drawLine(g, "Nothing playing", u8g2_font_helvB14_tf, 176, C_WHITE);
      drawLine(g, sonosRoomName(), u8g2_font_helvR12_tf, 202, C_SUBTLE);
      resetFont(g);
    } else {
      const char* msg = (sonosStage() == SONOS_NO_ROOM) ? "Room not found"
                      : (sonosStage() == SONOS_NO_SPEAKER) ? "No speaker found"
                      : (sonosStage() == SONOS_CONNECT_FAIL || sonosStage() == SONOS_HTTP_ERROR)
                          ? "Speaker not answering"
                          : "Looking for speakers";
      gfxMessage("Sonos", msg, C_YELLOW);
    }
    return;
  }

  g->fillScreen(C_BLACK);
  g->fillRect(ART_X, ART_Y, ART, ART, C_ART_BG);
  // Text first: it shows at once, and the cover streams in over it.
  drawLine(g, t.title, u8g2_font_helvB14_tf, TITLE_BASE, C_WHITE);
  if (s.sonos.showArtist) drawLine(g, t.artist, u8g2_font_helvR12_tf, ARTIST_BASE, C_SUBTLE);
  resetFont(g);
  renderBadge();

  // The decoder needs ~10 KB in one piece (6 KB work area plus the socket).
  // A theme loaded for the carousel can hold most of the heap; it steps
  // aside while the cover streams in.
  appReleaseHeap(16384, 10240);
  ArtResult r = sonosDrawArt(t.artUrl, ART_X, ART_Y, ART, s.httpTimeout);
  artResult_ = r;
  if (r != ART_OK) {
    g->fillRect(ART_X, ART_Y, ART, ART, C_ART_BG);
    if (t.radio) drawRadio(g, TFT_WIDTH / 2, ART_Y + ART / 2 + 6);
    else         drawNote(g, TFT_WIDTH / 2, ART_Y + ART / 2);
  }
}

void SonosMode::service(const Settings& s) {
  sonosService(s);

  const SonosTrack& t = sonosTrack();
  uint8_t kind = t.hasTrack ? K_TRACK : (sonosStage() == SONOS_OK ? K_IDLE : K_STATUS);
  bool paused = t.state == SONOS_PAUSED || t.state == SONOS_STOPPED;

  uint8_t stage = sonosStage();
  bool changed = needFull_ || sonosVersion() != seenVersion_ || kind != shownKind_ ||
                 (kind == K_STATUS && stage != shownStage_);
  if (!changed) return;
  seenVersion_ = sonosVersion();
  shownStage_ = stage;

  uint32_t key = sonosTrackKey(t);
  bool sameTrack = kind == K_TRACK && shownKind_ == K_TRACK && key == shownKey_;
  if (!needFull_ && sameTrack) {
    if (paused != shownPaused_) { shownPaused_ = paused; renderBadge(); }
    return;
  }
  if (!needFull_ && kind == shownKind_ && kind == K_IDLE) return;

  needFull_ = false;
  shownKind_ = kind;
  shownPaused_ = paused;
  shownKey_ = key;
  renderFull(s);
}
