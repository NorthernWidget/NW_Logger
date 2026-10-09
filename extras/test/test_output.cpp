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
#include "../../src/NW_Schedule.h"

static void parse(const char* what, const char* s) {
  int v[6] = { -1, -1, -1, -1, -1, -1 };
  bool ok = nwParseTimeStamp(s, v);
  printf("%-34s ok=%d  ", what, ok);
  if (ok) printf("20%02d-%02d-%02d %02d:%02d:%02d\n", v[0], v[1], v[2], v[3], v[4], v[5]);
  else printf("(untouched: %d %d %d %d %d %d)\n", v[0], v[1], v[2], v[3], v[4], v[5]);
}


// The schedule: every step the grid can hold, swept for the two properties it
// claims, and the backup swept for the three it claims. No board is needed, so
// this runs here rather than in a transcript.
static const uint32_t STEPS[] = {
  1, 2, 3, 4, 5, 6, 10, 12, 15, 20, 30,                     //the divisors of 60, as seconds
  60, 120, 180, 240, 300, 360, 600, 720, 900, 1200, 1800,   //the minutes dividing an hour
  3600, 7200, 10800, 14400, 21600, 28800, 43200, 86400      //the hours dividing a day
};
static const uint8_t N_STEPS = sizeof(STEPS) / sizeof(STEPS[0]);

static void schedule() {
  printf("\n-- the schedule --\n");

  // 1. The primary, walked forward with the cycle taking anything from nothing to
  //   more than a whole interval. A wake must be strictly ahead of the clock
  //   that produced it and must sit on the grid, at every step.
  //   A step of a whole day is left out of the "ahead" half: its grid holds one
  //   point, so the next wake is the same time of day and only the clock's date
  //   tells them apart. The alarm fires correctly - the hour-minute-second mask
  //   repeats daily - though a time of day cannot express the difference, which
  //   is the same boundary that leaves such a step without a backup.
  uint32_t notAhead = 0, offGrid = 0, total = 0;
  for (uint8_t i = 0; i < N_STEPS; i++) {
    uint32_t step = STEPS[i];
    if (step >= NW_SCHEDULE_DAY) continue;
    uint32_t at = 0;
    for (uint16_t k = 0; k < 400; k++) {
      uint32_t awake = (k % 7 == 0) ? step * 2 + 5 : (k % 3);  //every seventh cycle overruns
      uint32_t armed = (at + awake) % NW_SCHEDULE_DAY;
      uint32_t next = nwNextWake(armed, step);
      total++;
      uint32_t ahead = (next + NW_SCHEDULE_DAY - armed) % NW_SCHEDULE_DAY;
      if (ahead == 0 || ahead > 3 * step) notAhead++;
      if (next % step) offGrid++;
      at = next;
    }
  }
  printf("primary:  %lu wakes   not ahead %lu   off the grid %lu\n",
         (unsigned long)total, (unsigned long)notAhead, (unsigned long)offGrid);

  // 2. The backup. It must land on the grid, never before one full cycle after the
  //   primary (the case Andy raised: a primary at :59 whose cycle runs past the
  //   minute), and never more than one cycle or one minute out, whichever is
  //   larger.
  uint32_t bOffGrid = 0, bTooEarly = 0, bTooLate = 0, bTotal = 0, bNone = 0;
  for (uint8_t i = 0; i < N_STEPS; i++) {
    uint32_t step = STEPS[i];
    for (uint32_t k = 0; k < NW_SCHEDULE_DAY; k += (step > 60 ? step : 60)) {
      uint32_t a1 = k - (k % step);
      uint32_t a2 = nwBackupWake(a1, step);
      if (a2 == NW_SCHEDULE_NONE) {
        bNone++;
        continue;
      }
      bTotal++;
      uint32_t ahead = (a2 + NW_SCHEDULE_DAY - a1) % NW_SCHEDULE_DAY;
      if (a2 % step) bOffGrid++;
      if (ahead < step) bTooEarly++;
      if (ahead > (step > 60 ? step : 60)) bTooLate++;
    }
  }
  printf("backup:   %lu backups   off the grid %lu   earlier than one cycle %lu   later than one minute %lu   not expressible %lu\n",
         (unsigned long)bTotal, (unsigned long)bOffGrid, (unsigned long)bTooEarly,
         (unsigned long)bTooLate, (unsigned long)bNone);

  // 3. The worked cases a reader wants to see: an overrun, the guard at a minute
  //   boundary, and the primary at :59 with a sub-minute step.
  printf("overrun:  a 900 s step armed 3 s late at 14:45:00 wakes at %lu s;  950 s late, %lu s\n",
         (unsigned long)nwNextWake(14 * 3600 + 45 * 60 + 3, 900),
         (unsigned long)nwNextWake(14 * 3600 + 45 * 60 + 950, 900));
  printf("guard:    14:59:59 with a 900 s step: guard 0 gives %lu s, guard 2 gives %lu s\n",
         (unsigned long)nwNextWake(14 * 3600 + 59 * 60 + 59, 900, 0),
         (unsigned long)nwNextWake(14 * 3600 + 59 * 60 + 59, 900));
  printf("at :59:   a 15 s step at 23:59:45 -> backup %lu s;  a 1 s step at 23:59:59 -> backup %lu s\n",
         (unsigned long)nwBackupWake(23 * 3600 + 59 * 60 + 45, 15),
         (unsigned long)nwBackupWake(23 * 3600 + 59 * 60 + 59, 1));
  printf("backup:   a 900 s step at 15:00:00 -> %lu s (ALog's fixed two minutes would give %lu s)\n",
         (unsigned long)nwBackupWake(15 * 3600, 900), (unsigned long)(15 * 3600 + 120));

  // 4. What the board will keep, at a watchdog of 4200 s and a 2 s cycle. The
  //   watchdog period is hardware (a 100k resistor on the TPL5010's DELAY pin);
  //   4200 s stands in here for Andy's "a little over an hour" until it is read
  //   off the datasheet's curve.
  printf("holds:    ");
  for (uint8_t i = 0; i < N_STEPS; i++) {
    if (nwScheduleHolds(STEPS[i], 4200, 2)) printf("%lu ", (unsigned long)STEPS[i]);
  }
  printf("\nrefused:  ");
  for (uint8_t i = 0; i < N_STEPS; i++) {
    if (!nwScheduleHolds(STEPS[i], 4200, 2)) printf("%lu ", (unsigned long)STEPS[i]);
  }
  printf("\n");
  printf("refused:  a 7 s step divides nothing: %d;  a 0 s step: %d\n",
         nwScheduleHolds(7, 4200, 2), nwScheduleHolds(0, 4200, 2));
  printf("daily:    an 86400 s step wakes at %lu s and has no backup: %d\n",
         (unsigned long)nwNextWake(12 * 3600, 86400),
         nwBackupWake(0, 86400) == NW_SCHEDULE_NONE);
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

  // 4. The ALog's format, which is a different device's and must not be read as
  //    ours. ALogTalk/usbserial.py's DS3231_set() sends yymmdd D hhmmss x, with a
  //    day-of-week digit between the date and the time (Eric Ayars' DS3231
  //    convention). Taking its first twelve characters would put the day of week
  //    into the hour and shift everything after it, so a thirteenth digit is
  //    refused: better a clock that did not move than one that moved wrongly.
  parse("ALog: yymmddDhhmmssx", "2611071140509x");
  parse("the null pointer", nullptr);

  schedule();
  return 0;
}
