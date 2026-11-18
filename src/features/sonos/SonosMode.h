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
  // Where the cover on screen came from: "saved logo", "speaker/web",
  // "from Mac" or "none".
  const char* artVia() const;
  // The cover the cube could not fetch itself and would like the Mac to
  // relay: its URL and key (artKeyOf). False when nothing is wanted.
  bool artWanted(const char** url, uint32_t* key) const;

 private:
  void renderFull(const Settings& s);
  void renderArt(const Settings& s);
  void renderBadge();

  uint32_t seenVersion_ = 0xFFFFFFFF;
  bool     needFull_ = true;
  bool     shownPaused_ = false;
  uint8_t  shownKind_ = 0xFF;    // which screen is up: track / idle / status
  uint8_t  shownStage_ = 0xFF;
  uint8_t  artResult_ = 0;
  uint32_t shownKey_ = 0;        // sonosTrackKey() of the track on screen
  uint8_t  artVia_ = 0;          // ART_VIA_* in SonosMode.cpp
  uint32_t wantedKey_ = 0;       // artKeyOf() of a cover the Mac should relay, 0 = none
  uint32_t seenRelaySeq_ = 0;    // artRelaySeq() last looked at
};

extern SonosMode g_sonosMode;
