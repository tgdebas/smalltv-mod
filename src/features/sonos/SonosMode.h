// SonosMode.h — Sonos now playing: album art with the track title under it.
//
// Follows one Sonos room (its group coordinator, so grouped rooms show the
// group's track) and repaints only when the track changes. Play/pause only
// redraws a small badge, so pausing does not re-download the cover.
#pragma once
#include "Mode.h"
#include "config.h"

class SonosMode : public DisplayMode {
 public:
  const char* id() const override { return "sonos"; }
  uint8_t     modeConst() const override { return MODE_SONOS; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override { needFull_ = true; }   // repaint from cache

  // Outcome of the last album-art draw, for /api/status.
  const char* artStatus() const;

 private:
  void renderFull(const Settings& s);
  void renderBadge();

  uint32_t seenVersion_ = 0xFFFFFFFF;
  bool     needFull_ = true;
  bool     shownPaused_ = false;
  uint8_t  shownKind_ = 0xFF;    // which screen is up: track / idle / status
  uint8_t  shownStage_ = 0xFF;
  uint8_t  artResult_ = 0;
  char     shownTitle_[96] = "";
  char     shownArtist_[64] = "";
  char     shownArt_[320] = "";
};

extern SonosMode g_sonosMode;
