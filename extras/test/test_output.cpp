// Output-regression test for NW_Logger: the twelve-digit stamp a terminal sends
// to set the clock. run.sh diffs the result against baseline.txt.
//
// This path is not in the NW-Sim transcripts, because nothing sends the firmware
// a stamp there, and it is the one part of the logger's String removal with
// behaviour rather than form in it: the old code read a malformed stamp as
// zeroes through String::toInt() and set the clock to the year 2000 in silence.
#include "Arduino.h"
#include "Wire.h"
TwoWire Wire;  // the shared runner links NW_Device.cpp, which wants one
#include "../../src/NW_TimeStamp.h"

static void parse(const char* what, const char* s) {
  int v[6] = { -1, -1, -1, -1, -1, -1 };
  bool ok = nwParseTimeStamp(s, v);
  printf("%-34s ok=%d  ", what, ok);
  if (ok) printf("20%02d-%02d-%02d %02d:%02d:%02d\n", v[0], v[1], v[2], v[3], v[4], v[5]);
  else printf("(untouched: %d %d %d %d %d %d)\n", v[0], v[1], v[2], v[3], v[4], v[5]);
}

int main() {
  // 1. What each sender in use actually sends. SetTime_GUI appends an 'x', a
  //    terminal appends CR LF, and a hand-typed stamp ends with nothing. The
  //    'x' is from LoggerSetTime() in NW_Logger_TimeSet.pde, and refusing it was
  //    a live regression until this test went in.
  parse("SetTime_GUI: 12 digits then 'x'", "261107140509x");
  parse("261107140509", "261107140509");
  parse("with CR LF", "261107140509\r\n");
  parse("with a newline alone", "261107140509\n");
  parse("with spaces around it", "  261107140509  ");

  // 2. The fields at their edges: midnight on the first of January, and a
  //    second before midnight on the last of December.
  parse("260101000000", "260101000000");
  parse("261231235959", "261231235959");

  // 3. What must be refused rather than half-read. Every one of these set the
  //    clock before, through toInt() answering 0 for what it could not read.
  parse("empty", "");
  parse("eleven digits", "26110714050");
  parse("a letter in the middle", "2611o7140509");
  parse("a space in the middle", "261107 40509");
  parse("thirteen digits", "2611071405099");        // a field of the wrong length, not a terminator
  parse("a stamp and a word", "261107140509 now");  // twelve good digits; the rest is terminator
  parse("a sentence", "set the clock please");
  parse("the null pointer", nullptr);

  return 0;
}
