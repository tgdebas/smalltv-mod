#include "SonosXml.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// ---- entity helpers ---------------------------------------------------------
// Decodes one entity body (without & and ;) to UTF-8 in out (>= 5 bytes).
// Returns the byte count, 0 if unknown.
static uint8_t decodeEntity(const char* e, char* out) {
  if (!strcmp(e, "lt"))   { out[0] = '<';  return 1; }
  if (!strcmp(e, "gt"))   { out[0] = '>';  return 1; }
  if (!strcmp(e, "amp"))  { out[0] = '&';  return 1; }
  if (!strcmp(e, "quot")) { out[0] = '"';  return 1; }
  if (!strcmp(e, "apos")) { out[0] = '\''; return 1; }
  if (e[0] != '#') return 0;
  unsigned long cp = (e[1] == 'x' || e[1] == 'X') ? strtoul(e + 2, nullptr, 16)
                                                  : strtoul(e + 1, nullptr, 10);
  if (cp == 0 || cp > 0x10FFFF) return 0;
  if (cp < 0x80)    { out[0] = (char)cp; return 1; }
  if (cp < 0x800)   { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
  if (cp < 0x10000) { out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                      out[2] = (char)(0x80 | (cp & 0x3F)); return 3; }
  out[0] = (char)(0xF0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

void xmlUnescape(char* s) {
  char* w = s;
  for (const char* r = s; *r;) {
    if (*r == '&') {
      const char* semi = strchr(r, ';');
      if (semi && semi - r <= 9) {
        char e[10];
        size_t n = semi - r - 1;
        memcpy(e, r + 1, n);
        e[n] = 0;
        char u[5];
        uint8_t k = decodeEntity(e, u);
        if (k) {               // decoded output is never longer than the entity
          memcpy(w, u, k);
          w += k;
          r = semi + 1;
          continue;
        }
      }
    }
    *w++ = *r++;
  }
  *w = 0;
}

bool xmlAttr(const char* tag, const char* nameP, char* out, size_t outLen) {
  char name[24];
  xmlCopyP(name, nameP, sizeof(name));
  size_t nl = strlen(name);
  for (const char* p = tag; (p = strstr(p, name)) != nullptr; p += nl) {
    // Whole attribute name only: preceded by whitespace, followed by =".
    if (p == tag || !isspace((unsigned char)p[-1])) continue;
    if (p[nl] != '=' || (p[nl + 1] != '"' && p[nl + 1] != '\'')) continue;
    char q = p[nl + 1];
    const char* v = p + nl + 2;
    const char* end = strchr(v, q);
    if (!end) end = v + strlen(v);   // truncated tag: take what we have
    size_t n = end - v;
    if (n >= outLen) n = outLen - 1;
    memcpy(out, v, n);
    out[n] = 0;
    xmlUnescape(out);
    return true;
  }
  if (outLen) out[0] = 0;
  return false;
}

void xmlTagName(const char* tag, char* out, size_t outLen) {
  if (*tag == '/') tag++;
  size_t n = 0;
  while (tag[n] && !isspace((unsigned char)tag[n]) && tag[n] != '/' && n + 1 < outLen) {
    out[n] = tag[n];
    n++;
  }
  out[n] = 0;
}

// ---- streaming scanner ------------------------------------------------------
XmlScanner::XmlScanner(Client& s, int32_t limit, uint32_t timeoutMs)
    : s_(s), left_(limit), deadline_(millis() + timeoutMs) {
  buf_[0] = 0;
}

int XmlScanner::rawByte() {
  if (inPos_ < inLen_) return in_[inPos_++];
  if (left_ == 0) return -1;
  for (;;) {
    int avail = s_.available();
    if (avail > 0) {
      size_t want = sizeof(in_);
      if ((size_t)avail < want) want = avail;
      if (left_ > 0 && (int32_t)want > left_) want = left_;
      int got = s_.read(in_, want);
      if (got <= 0) return -1;
      if (left_ > 0) left_ -= got;
      inLen_ = got;
      inPos_ = 0;
      return in_[inPos_++];
    }
    if ((int32_t)(millis() - deadline_) >= 0) { timedOut_ = true; return -1; }
    // No Content-Length: the response ends when the speaker closes (we ask
    // for HTTP/1.0, so it does). With one, left_ ends the read without waiting.
    if (!s_.connected()) return -1;
    delay(1);
  }
}

int XmlScanner::unescByte() {
  if (entPos_ < entLen_) return (uint8_t)ent_[entPos_++];
  int c = rawByte();
  if (c != '&') return c;
  // Read up to ';' (bounded); unknown or overlong entities pass through as-is.
  char e[10];
  uint8_t n = 0;
  int d;
  while ((d = rawByte()) >= 0 && d != ';' && n < sizeof(e) - 1) e[n++] = (char)d;
  e[n] = 0;
  char u[5];
  uint8_t k = (d == ';') ? decodeEntity(e, u) : 0;
  if (k) {
    memcpy(ent_, u, k);
  } else {
    // Not an entity we know: replay "&" + what we read (+ the terminator).
    k = 0;
    ent_[k++] = '&';
    for (uint8_t i = 0; i < n && k < sizeof(ent_) - 1; i++) ent_[k++] = e[i];
    if (d >= 0 && k < sizeof(ent_)) ent_[k++] = (char)d;
  }
  entLen_ = k;
  entPos_ = 1;
  return (uint8_t)ent_[0];
}

XmlScanner::Kind XmlScanner::next() {
  for (;;) {
    int c = (pending_ >= 0) ? pending_ : unescByte();
    pending_ = -1;
    if (c < 0) return END;
    size_t n = 0;
    if (c == '<') {
      while ((c = unescByte()) >= 0 && c != '>') {
        if (n < sizeof(buf_) - 1) buf_[n++] = (char)c;
      }
      buf_[n] = 0;
      // Comments, declarations and processing instructions carry nothing we use.
      if (buf_[0] == '?' || buf_[0] == '!') continue;
      return TAG;
    }
    bool blank = true;
    do {
      if (n < sizeof(buf_) - 1) buf_[n++] = (char)c;
      if (!isspace(c)) blank = false;
    } while ((c = unescByte()) >= 0 && c != '<');
    pending_ = c;   // '<' (or -1) starts the next token
    buf_[n] = 0;
    if (!blank) return TEXT;
  }
}
