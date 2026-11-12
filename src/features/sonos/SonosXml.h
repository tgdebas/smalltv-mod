// SonosXml.h — a tiny streaming tag/text scanner for Sonos SOAP responses.
//
// Sonos nests a second XML document inside the SOAP body as escaped text
// (TrackMetaData's DIDL-Lite, ZoneGroupState's topology). Unescaping one level
// while reading turns both the outer envelope and the inner document into a
// single run of tags and text, so one scanner handles either, without ever
// holding the response (the topology can run to tens of KB) in RAM.
//
// After that first unescape, text and attribute values still carry their own
// entities ("Rock &amp; Roll"); xmlUnescape() strips that second level from
// the short values a caller keeps.
#pragma once
#include <Arduino.h>
#include <Client.h>

// In-place: &lt; &gt; &quot; &apos; &amp; and numeric &#NN; / &#xNN; (UTF-8 out).
void xmlUnescape(char* s);

// Copies attribute `name` of a tag (the text between < and >) into out,
// unescaped. Returns false if the attribute is absent.
bool xmlAttr(const char* tag, const char* name, char* out, size_t outLen);

// Element name of a tag ("dc:title" from `dc:title id="x"`), without a leading
// '/'. Writes at most outLen-1 chars.
void xmlTagName(const char* tag, char* out, size_t outLen);

class XmlScanner {
 public:
  // `limit` = Content-Length (or -1 = until the peer closes); `timeoutMs`
  // bounds the whole read.
  XmlScanner(Client& s, int32_t limit, uint32_t timeoutMs);

  enum Kind : uint8_t { END = 0, TAG, TEXT };
  // Next token. TAG: the tag body without < >, possibly truncated to the
  // buffer. TEXT: the run of text up to the next tag, possibly truncated.
  // Whitespace-only text runs are skipped.
  Kind next();
  const char* value() const { return buf_; }
  bool timedOut() const { return timedOut_; }

 private:
  int rawByte();     // one byte off the wire, -1 at end/timeout
  int unescByte();   // one byte with the first level of entities decoded

  Client&  s_;
  int32_t  left_;
  uint32_t deadline_;
  bool     timedOut_ = false;
  int      pending_ = -1;       // a byte read ahead (the '<' that ended a text run)
  uint8_t  in_[96];
  uint8_t  inLen_ = 0, inPos_ = 0;
  char     ent_[8];             // decoded multi-byte entity spill (UTF-8)
  uint8_t  entLen_ = 0, entPos_ = 0;
  char     buf_[400];
};
