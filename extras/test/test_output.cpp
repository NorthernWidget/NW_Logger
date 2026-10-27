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
#include "../../src/NW_Message.h"

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
  1, 2, 3, 4, 5, 6, 10, 12, 15, 20, 30,                    //the divisors of 60, as seconds
  60, 120, 180, 240, 300, 360, 600, 720, 900, 1200, 1800,  //the minutes dividing an hour
  3600, 7200, 10800, 14400, 21600, 28800, 43200, 86400     //the hours dividing a day
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

  // 5. The guard's floor and its price. A step under the guard is refused rather
  //    than kept at the guard's cadence: at a 1 s step every wake is pushed, so
  //    the logger would log at 2 s while the sketch asked for 1 s.
  printf("floor:    guard %lu s; 1 s step pushed to +%lu s, held %d;  2 s step +%lu s, held %d\n",
         (unsigned long)NW_ALARM_GUARD_SECONDS,
         (unsigned long)nwNextWake(0, 1), nwScheduleHolds(1, 4200, 0),
         (unsigned long)nwNextWake(0, 2), nwScheduleHolds(2, 4200, 0));
  printf("cost:     slots pushed by the guard, over every arrival phase of a step\n");
  for (uint8_t i = 0; i < N_STEPS; i++) {
    uint32_t step = STEPS[i];
    if (step > 900) continue;
    uint32_t pushed = 0;
    for (uint32_t phase = 0; phase < step; phase++) {
      if (nwNextWake(phase, step) - phase > step) pushed++;
    }
    printf("            step %4lu s   pushed %lu of %lu phases\n",
           (unsigned long)step, (unsigned long)pushed, (unsigned long)step);
  }

  // 6. The overrun, measured from the two clock reads a cycle makes anyway
  //    rather than from an alarm flag.
  printf("overran:  a 60 s step: 3 s cycle %d, 59 s %d, 60 s %d, 125 s %d; across midnight (86395 -> 10) %d\n",
         nwCycleOverran(1000, 1003, 60), nwCycleOverran(1000, 1059, 60),
         nwCycleOverran(1000, 1060, 60), nwCycleOverran(1000, 1125, 60),
         nwCycleOverran(86395, 10, 60));
  printf("overran:  across midnight, a 75 s cycle under a 60 s step (86395 -> 70): %d\n",
         nwCycleOverran(86395, 70, 60));

  // 7. The interval the logger keeps when a cycle has outgrown the one it was
  //    given. The step generator is checked against the table it must reproduce,
  //    then the lengthening is walked over a range of cycle durations.
  printf("legible:  ");
  uint32_t walk = 0;
  uint8_t seen = 0;
  while (true) {
    walk = nwNextLegibleStep(walk);
    if (walk == NW_SCHEDULE_NONE) break;
    printf("%lu ", (unsigned long)walk);
    if (seen < N_STEPS && walk != STEPS[seen]) printf("(EXPECTED %lu) ", (unsigned long)STEPS[seen]);
    seen++;
  }
  printf("\n          generated %d of %d tabulated steps, in order\n", seen, N_STEPS);

  printf("lengthen: a 10 s interval, no watchdog declared\n");
  const uint32_t CYCLES[] = { 0, 1, 2, 9, 10, 11, 25, 31, 59, 61, 3599 };
  for (uint8_t i = 0; i < sizeof(CYCLES) / sizeof(CYCLES[0]); i++) {
    printf("            cycle %4lu s -> keeps %lu s\n", (unsigned long)CYCLES[i],
           (unsigned long)nwLengthenStep(10, CYCLES[i], 0));
  }
  printf("lengthen: the watchdog's ceiling, at 4200 s\n");
  printf("            a 900 s interval with a 2 s cycle   -> %lu s\n", (unsigned long)nwLengthenStep(900, 2, 4200));
  printf("            a 900 s interval with a 1000 s cycle -> %lu s\n", (unsigned long)nwLengthenStep(900, 1000, 4200));
  printf("            a 900 s interval with a 5000 s cycle -> none: %d\n",
         nwLengthenStep(900, 5000, 4200) == NW_SCHEDULE_NONE);
  printf("            the same 5000 s cycle, no watchdog   -> %lu s\n", (unsigned long)nwLengthenStep(900, 5000, 0));
}

// The message table, every row: the enum's order and the table's must agree, and
// worstMessage() relies on the order within a band, so a reordering that moves
// one and not the other shows here as a word beside the wrong value or band.
static void messages() {
  printf("\n-- the messages --\n");
  static const char* const BANDS[] = { "fine", "caught", "bad" };
  for (uint8_t m = 0; m < NW_MSG_COUNT; m++) {
    printf("%2u  %-6s  %s\n", m, BANDS[nwMessageBand(m)], (const char*)nwMessageWord(m));
  }
  printf("count %u; out of range -> band %u, word %s\n", NW_MSG_COUNT,
         nwMessageBand(NW_MSG_COUNT), (const char*)nwMessageWord(NW_MSG_COUNT));
}

// What the LED shows for a set of raised messages. The property fix 5 changed is
// here as a line: with no card AND an unset clock, the card wins, because without
// a card the LED is the only channel the logger has left.
static void verdict(const char* what, uint32_t set) {
  uint8_t m = nwWorstMessage(set);
  printf("%-44s -> %-18s (%u)\n", what, (const char*)nwMessageWord(m), m);
}
#define BIT(m) ((uint32_t)1 << (m))
static void verdicts() {
  printf("\n-- the verdict --\n");
  verdict("nothing raised", 0);
  verdict("fine only: started, new file, lengthened", BIT(NW_MSG_LOG_STARTED) | BIT(NW_MSG_NEW_FILE) | BIT(NW_MSG_INTERVAL_LENGTHENED));
  verdict("one caught: BME missing", BIT(NW_MSG_BME_MISSING));
  verdict("caught beats fine", BIT(NW_MSG_LOG_STARTED) | BIT(NW_MSG_ROW_NOT_WRITTEN));
  verdict("two caught: first in list order", BIT(NW_MSG_ROW_NOT_WRITTEN) | BIT(NW_MSG_BME_MISSING));
  verdict("bad beats caught", BIT(NW_MSG_BME_MISSING) | BIT(NW_MSG_SENSOR_MISSING));
  verdict("no card AND clock not set", BIT(NW_MSG_CLOCK_NOT_SET) | BIT(NW_MSG_CARD_MISSING));
  verdict("every bad at once", BIT(NW_MSG_CARD_MISSING) | BIT(NW_MSG_CARD_UNREADABLE) | BIT(NW_MSG_CARD_WRITE_FAILED) | BIT(NW_MSG_NO_SCHEDULE) | BIT(NW_MSG_SENSOR_MISSING) | BIT(NW_MSG_CLOCK_NOT_SET));
  verdict("no schedule AND clock not set", BIT(NW_MSG_NO_SCHEDULE) | BIT(NW_MSG_CLOCK_NOT_SET));
  verdict("sensor missing AND clock not set", BIT(NW_MSG_SENSOR_MISSING) | BIT(NW_MSG_CLOCK_NOT_SET));
  verdict("write failed AND sensor missing", BIT(NW_MSG_CARD_WRITE_FAILED) | BIT(NW_MSG_SENSOR_MISSING));
  verdict("a bit past the end, alone", BIT(NW_MSG_COUNT));
  verdict("a bit past the end, with a caught", BIT(NW_MSG_COUNT) | BIT(NW_MSG_NOTE_TRUNCATED));
}

// The segments the verdict plays, worst to least, one per distinct rhythm, and
// the count the amber segment opens with (Andy, 2026-10-21). The first segment
// is always what the one-message verdict showed, so nothing a Margay user learnt
// changes; the rest is what the one LED could not say before.
static void sequence(const char* what, uint32_t set) {
  uint8_t seq[NW_RHYTHM_COUNT];
  uint8_t n = nwVerdictSequence(set, seq);
  printf("%-44s -> %u segment(s), %u caught:", what, n, nwCountBand(set, NW_BAND_CAUGHT));
  for (uint8_t i = 0; i < n; i++) printf(" %s", (const char*)nwMessageWord(seq[i]));
  static const char* const CHIRPS[] = { " (no chirp)", " + long-and-buzz", " + chirp on backup (3 shorts)", " + chirp" };
  printf("%s\n", CHIRPS[nwChirpForm(set)]);
}
static void sequences() {
  printf("\n-- the sequence --\n");
  sequence("nothing raised", 0);
  sequence("fine only: started, new file, lengthened", BIT(NW_MSG_LOG_STARTED) | BIT(NW_MSG_NEW_FILE) | BIT(NW_MSG_INTERVAL_LENGTHENED));
  sequence("one caught: BME missing", BIT(NW_MSG_BME_MISSING));
  sequence("two caught: one amber, counted two", BIT(NW_MSG_ROW_NOT_WRITTEN) | BIT(NW_MSG_BME_MISSING));
  sequence("all eight caught", BIT(NW_MSG_ON_BACKUP_POWER) | BIT(NW_MSG_BME_MISSING) | BIT(NW_MSG_ROW_NOT_WRITTEN) | BIT(NW_MSG_NOTE_TRUNCATED) | BIT(NW_MSG_CYCLE_OVERRAN) | BIT(NW_MSG_BACKUP_WOKE) | BIT(NW_MSG_ONBOARD_MISSING) | BIT(NW_MSG_PAGE0_INVALID));
  sequence("bad then caught", BIT(NW_MSG_BME_MISSING) | BIT(NW_MSG_SENSOR_MISSING));
  sequence("no card AND clock not set", BIT(NW_MSG_CLOCK_NOT_SET) | BIT(NW_MSG_CARD_MISSING));
  sequence("three card messages: one flutter", BIT(NW_MSG_CARD_MISSING) | BIT(NW_MSG_CARD_UNREADABLE) | BIT(NW_MSG_CARD_WRITE_FAILED));
  sequence("every bad at once", BIT(NW_MSG_CARD_MISSING) | BIT(NW_MSG_CARD_UNREADABLE) | BIT(NW_MSG_CARD_WRITE_FAILED) | BIT(NW_MSG_NO_SCHEDULE) | BIT(NW_MSG_SENSOR_MISSING) | BIT(NW_MSG_CLOCK_NOT_SET));
  sequence("everything at once", ((uint32_t)1 << NW_MSG_COUNT) - 1);
  sequence("the dead bus: clock, battery ADC, BME", BIT(NW_MSG_CLOCK_NOT_SET) | BIT(NW_MSG_ONBOARD_MISSING) | BIT(NW_MSG_BME_MISSING));
  sequence("a bit past the end, with a caught", BIT(NW_MSG_COUNT) | BIT(NW_MSG_NOTE_TRUNCATED));
  sequence("clock not set alone: runs, so chirps", BIT(NW_MSG_CLOCK_NOT_SET));
  sequence("sensor missing alone: runs, so chirps", BIT(NW_MSG_SENSOR_MISSING));
  sequence("no schedule alone: cannot run", BIT(NW_MSG_NO_SCHEDULE));
  sequence("no card AND no schedule: none outranks", BIT(NW_MSG_CARD_MISSING) | BIT(NW_MSG_NO_SCHEDULE));
  sequence("on backup power alone", BIT(NW_MSG_ON_BACKUP_POWER));
  sequence("on backup AND card missing: warning wins", BIT(NW_MSG_ON_BACKUP_POWER) | BIT(NW_MSG_CARD_MISSING));
  sequence("on backup AND clock not set", BIT(NW_MSG_ON_BACKUP_POWER) | BIT(NW_MSG_CLOCK_NOT_SET));
  sequence("card write failed and BME missing", BIT(NW_MSG_CARD_WRITE_FAILED) | BIT(NW_MSG_BME_MISSING));
  // The rules, over every set of the twenty bits: the first segment is the worst
  // message, no rhythm plays twice, the sequence never exceeds the five rhythms
  // that have a fault to name, and the chirp's form is decided by the set: none
  // with NO_SCHEDULE, the long-and-buzz with a card message and no NO_SCHEDULE,
  // the full chirp otherwise.
  uint32_t checked = 0;
  uint32_t broke = 0;
  for (uint32_t set = 0; set < ((uint32_t)1 << NW_MSG_COUNT); set++) {
    uint8_t seq[NW_RHYTHM_COUNT];
    uint8_t n = nwVerdictSequence(set, seq);
    uint8_t worst = nwWorstMessage(set);
    bool ok = true;
    if (n == 0) {
      ok = (worst == NW_MSG_READY);
    } else {
      ok = (seq[0] == worst) && (n <= 5);
      uint8_t seen = 0;
      for (uint8_t i = 0; i < n && ok; i++) {
        uint8_t r = nwMessageRhythm(seq[i]);
        if (seen & (1 << r)) ok = false;
        seen |= 1 << r;
      }
    }
    uint32_t card = BIT(NW_MSG_CARD_MISSING) | BIT(NW_MSG_CARD_UNREADABLE) | BIT(NW_MSG_CARD_WRITE_FAILED);
    uint8_t expected = (set & BIT(NW_MSG_NO_SCHEDULE)) ? NW_CHIRP_NONE : ((set & card) ? NW_CHIRP_WARNING : ((set & BIT(NW_MSG_ON_BACKUP_POWER)) ? NW_CHIRP_BACKUP : NW_CHIRP_FULL));
    if (nwChirpForm(set) != expected) ok = false;
    if (!ok) broke++;
    checked++;
  }
  printf("every set: %lu checked, %lu broke a rule\n", (unsigned long)checked, (unsigned long)broke);
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
  messages();
  verdicts();
  sequences();
  return 0;
}
