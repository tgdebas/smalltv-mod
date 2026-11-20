// c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc tests/test_crash_record.cpp -o /tmp/smalltv-crash-tests && /tmp/smalltv-crash-tests
#include "CrashRecord.h"
#include <cassert>
#include <cstring>
#include <iostream>

static CrashRtc sample() {
  CrashRtc r;
  memset(&r, 0, sizeof(r));
  r.magic = CRASH_MAGIC;
  r.exccause = 28; r.epc1 = 0x40201234; r.excvaddr = 0; r.uptimeS = 77;
  r.rstReason = CRASH_RST_EXCEPTION; r.uptimeKnown = 1;
  strcpy(r.fw, "3.2.0");
  r.check = crashCheck(r);
  return r;
}

int main() {
  // The record is 16 words and must end inside the 128 user words from word 96.
  assert(sizeof(CrashRtc) == 64);
  {
    // Callers: code addresses only, nearest first, no repeats, at most four.
    const uint32_t stack[] = {0x3ffef000u, 0x40212345u, 0u, 0x40212345u, 0x40101000u,
                              0x4000df48u, 0x40230000u, 0x40240000u, 0x40250000u};
    CrashRtc r;
    memset(&r, 0, sizeof(r));
    crashPickCallers(r, stack, stack + 9);
    assert(r.caller[0] == 0x40212345u && r.caller[1] == 0x40101000u);
    assert(r.caller[2] == 0x40230000u && r.caller[3] == 0x40240000u);
    CrashRtc e;
    memset(&e, 0, sizeof(e));
    crashPickCallers(e, stack, stack + 1);
    assert(e.caller[0] == 0);
  }
  assert(96 + sizeof(CrashRtc) / 4 <= 128);

  // Labels: 253/254 get their own, the hardware watchdog is only REASON_WDT_RST.
  assert(!strcmp(crashReasonLabel(2), "Exception"));
  assert(!strcmp(crashReasonLabel(3), "Software watchdog"));
  assert(!strcmp(crashReasonLabel(1), "Hardware watchdog"));
  assert(!strcmp(crashReasonLabel(254), "Abort/out of memory"));
  assert(!strcmp(crashReasonLabel(253), "Stack overflow"));
  assert(!strcmp(crashReasonLabel(0), "Unknown"));
  assert(!strcmp(crashReasonLabel(4), "Unknown"));
  assert(!strcmp(crashReasonLabel(255), "Unknown"));
  assert(strlen(crashReasonLabel(254)) < 24);   // PlatformCrashRecord::reason[24]

  // The checksum covers every byte after the two header words, and not the checksum itself.
  CrashRtc a = sample();
  assert(a.check == crashCheck(a));
  CrashRtc b = a; b.epc1++;
  assert(b.check != crashCheck(b));
  b = a; b.fw[0] = 'x';
  assert(b.check != crashCheck(b));
  b = a; b.flags = CRASH_PENDING;
  assert(b.check != crashCheck(b));

  // A firmware field with no terminator is cut, and the copy always ends in one.
  {
    CrashRtc u; memset(&u, 'x', sizeof(u));
    char out[16]; memset(out, 'y', sizeof(out));
    crashCopyFw(out, sizeof(out), u);
    assert(strlen(out) == 15 && out[15] == 0);
    char small[4];
    crashCopyFw(small, sizeof(small), sample());
    assert(!strcmp(small, "3.2"));
    crashCopyFw(out, sizeof(out), sample());
    assert(!strcmp(out, "3.2.0"));
  }

  // Abort and stack overflow carry no registers; the other reasons do.
  assert(!crashHasRegisters(254) && !crashHasRegisters(253));
  assert(crashHasRegisters(1) && crashHasRegisters(2) && crashHasRegisters(3) && crashHasRegisters(0));

  // Boot decisions.
  assert(crashBootAction(true, true, 4)  == CRASH_ADOPT);    // abort(): comes back as a software restart
  assert(crashBootAction(true, true, 2)  == CRASH_ADOPT);    // exception
  assert(crashBootAction(true, true, 3)  == CRASH_ADOPT);    // software watchdog
  assert(crashBootAction(true, false, 1) == CRASH_CAPTURE);  // hardware watchdog: no callback ran
  assert(crashBootAction(false, false, 1) == CRASH_CAPTURE);
  assert(crashBootAction(true, false, 2) == CRASH_CAPTURE);  // exception whose callback write was lost
  assert(crashBootAction(true, false, 4) == CRASH_KEEP);     // normal software restart keeps the record
  assert(crashBootAction(true, false, 6) == CRASH_KEEP);     // external reset
  assert(crashBootAction(false, false, 0) == CRASH_KEEP);    // power on, nothing stored
  assert(crashBootAction(true, false, 0) == CRASH_KEEP);

  std::cout << "crash record tests passed\n";
  return 0;
}
