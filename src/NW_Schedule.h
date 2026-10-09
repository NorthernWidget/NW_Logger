/*
NW_Schedule: when a logger wakes, and when its backup wakes.
Licensed: GNU GPL v3

The arithmetic of an absolute, drift-free logging schedule, kept apart from the
clock and the alarm registers because it is the only part with anything to get
wrong: extras/test exercises every step and every overrun, where arming an alarm
needs a board. See NW-Status/QUEUE.md, "the alarm must be absolute".

A wake is computed from the clock and snapped to the schedule. Computing it from
the clock is what keeps it in the future, so the logger cannot deadlock; snapping
it to the schedule is what keeps it from drifting, so rows land on round times
however long a cycle takes. Taking only one of the two gives the two schemes this
replaces: `now + interval` is always ahead and always wandering, and
`previous + interval` holds the grid and can land in the past.
*/

#ifndef NW_Schedule_h
#define NW_Schedule_h

#include <stdint.h>

/// Seconds in a day: every schedule is a time of day, because an alarm carries no month.
#define NW_SCHEDULE_DAY 86400UL

/// Returned by nwBackupWake() when no backup can be expressed.
#define NW_SCHEDULE_NONE 0xFFFFFFFFUL

/**
 * @brief The seconds of lead a wake needs, so the alarm is armed before its instant passes.
 *
 * @details **Derived, not chosen.** The guard must cover everything that can
 * elapse between the clock read that produced the wake and the moment alarm 1 is
 * live in the clock's comparator. An alarm whose instant has already gone by does
 * not fire: with the hour-minute-second mask it waits a day, and with the date
 * mask a month. Four terms, at the DS3231 library's own traffic and Wire's
 * default 100 kHz (no setClock() anywhere in the logger path):
 *
 * 1. **Read quantisation: 1000 ms.** The seconds register counts whole seconds,
 *    so a read of 15:00:00 means the true time lies anywhere in
 *    [15:00:00.000, 15:00:01.000). This term dominates the other three by two
 *    orders of magnitude, and nothing short of reading the 32 kHz output removes
 *    it.
 * 2. **Arming traffic: 3.6 ms** to get alarm 1 live, 6.1 ms for both alarms.
 *    Counted at 90 us per byte (8 bits and an acknowledge at 10 us a bit) and
 *    10 us per start or stop: readTime() 0.94 ms (two transactions, 2 bytes then
 *    8), turnOffAlarm() 0.69 ms (a control read at 0.40 and a write at 0.29),
 *    setA1Time() 0.56 ms (a pointer and four alarm bytes), checkIfAlarm() 0.69 ms,
 *    turnOnAlarm() 0.69 ms. Alarm 2 adds 2.54 ms, after alarm 1 is already live.
 * 3. **Delays in the arming path: 10 ms budgeted.** ALog carries delay(2) after
 *    setA1Time and delay(1) after each turnOnAlarm, 6 ms in all. Nothing else
 *    belongs between the clock read and the arming: no card access, no sensor.
 * 4. **Interrupt service: 1 ms budgeted.** The tipping-bucket counter and the
 *    pin-change handler can preempt the sequence by a few microseconds each.
 *
 * 1000 + 4 + 10 + 1 = 1015 ms, and the alarm's own resolution is one second, so
 * the guard rounds up to **2 s**. One second is not enough: the quantisation
 * alone consumes it and leaves nothing for the traffic. Raise this if the arming
 * path ever grows; correctness does not rest on the number, because the caller
 * reads the clock back after arming and re-arms if the instant passed anyway.
 *
 * **What it costs the schedule.** A wake is pushed to the following slot whenever
 * a cycle ends within the guard of the next grid point, which for whole seconds
 * is `guard - 1` of every `step` arrival phases: one slot in 900 at a 15-minute
 * interval, one in 10 at a 10-second interval, one in 3 at a 3-second interval.
 * So the guard is free at the intervals a logger uses and costly only at the
 * shortest ones. Below the guard itself it is worse than costly: every wake would
 * be pushed and the logger would keep the guard's cadence rather than the one
 * asked for, which is why nwScheduleHolds() refuses a step under the guard
 * instead of quietly halving the rate (Andy, 2026-10-09).
 */
#define NW_ALARM_GUARD_SECONDS 2UL

/**
 * @brief A step the schedule can hold: it must divide the day evenly.
 * @details Otherwise the last slot before midnight is short and the grid breaks
 * at the day boundary. The legible set is the divisors of 60 applied to seconds
 * (1, 2, 3, 4, 5, 6, 10, 12, 15, 20, 30), the minutes dividing an hour applied
 * to minutes, and the hours dividing a day.
 */
inline bool nwStepDividesDay(uint32_t step) {
  if (step == 0) return false;
  if (step > NW_SCHEDULE_DAY) return false;
  return NW_SCHEDULE_DAY % step == 0;
}

/**
 * @brief The next wake: the first grid point after now, with the guard's lead.
 * @param secondsIntoDay the clock's time of day, in seconds
 * @param step the logging interval, in seconds; must divide the day
 * @param guard the lead a wake needs, NW_ALARM_GUARD_SECONDS unless measured otherwise
 * @return the wake's time of day, or NW_SCHEDULE_NONE for a step the schedule cannot hold
 *
 * @details Always strictly ahead of secondsIntoDay, because integer division
 * floors: ((s / step) + 1) * step > s for every s. Always a multiple of step
 * past midnight, so it does not depend on when the last cycle ended. A cycle
 * that overruns loses its slot and resumes on the grid rather than pushing every
 * later wake sideways.
 *
 * At a step of a whole day the grid holds one point, so the result is that point
 * and the difference from now is a date rather than a time of day. The alarm
 * still fires correctly, because the hour-minute-second mask repeats daily.
 */
inline uint32_t nwNextWake(uint32_t secondsIntoDay, uint32_t step, uint32_t guard = NW_ALARM_GUARD_SECONDS) {
  if (!nwStepDividesDay(step)) return NW_SCHEDULE_NONE;
  uint32_t s = secondsIntoDay % NW_SCHEDULE_DAY;
  uint32_t next = ((s / step) + 1) * step;
  if (next - s < guard) next += step;  //Too close to arm: take the following slot
  return next % NW_SCHEDULE_DAY;
}

/**
 * @brief The backup wake: one cycle after the primary, on a whole minute.
 * @param next the primary wake, from nwNextWake()
 * @param step the logging interval, in seconds
 * @return the backup's time of day, or NW_SCHEDULE_NONE when none can be expressed
 *
 * @details Alarm 2 carries hours and minutes only and matches at seconds = 00, so
 * it can be placed on a whole minute and nowhere else. For every step the
 * schedule can hold, a whole minute is itself a grid point, which is what lets
 * the backup land on the grid: a rescued row is then indistinguishable from an
 * ordinary one, and a lost interrupt costs exactly one row.
 *
 * The instant is **the earliest grid point at or after one full increment past
 * the primary that alarm 2 can express**, which is `next + step` exactly for
 * every step of 30 s or more. It is never earlier than one full increment,
 * because rounding to a whole minute only ever moves it later (by up to 58 s at
 * the sub-minute steps). So the backup cannot fire inside the cycle that armed
 * it unless one increment is itself shorter than a cycle takes, which is the
 * configuration nwScheduleHolds() refuses.
 *
 * **The backup therefore inherits the primary's validity and needs no condition
 * of its own** (Andy, 2026-10-09): an increment the primary can keep, the backup
 * can keep, because the backup is that same increment measured from the primary.
 *
 * **No backup exists at a step of a whole day**: alarm 2 carries no date, so one
 * cycle later is the same time of day and the backup would collapse onto the
 * primary.
 */
inline uint32_t nwBackupWake(uint32_t next, uint32_t step) {
  if (!nwStepDividesDay(step)) return NW_SCHEDULE_NONE;
  if (step >= NW_SCHEDULE_DAY) return NW_SCHEDULE_NONE;
  uint32_t backup = next + step;
  if (backup % 60) backup = ((backup / 60) + 1) * 60;  //A sub-minute step: the next whole minute
  return backup % NW_SCHEDULE_DAY;
}

/**
 * @brief A step this board can actually keep.
 * @param step the logging interval, in seconds
 * @param wdtSeconds the watchdog's period, from the hardware (Margay: a 100k resistor on the TPL5010's DELAY pin); **0 when there is no watchdog or its period is not known**
 * @param cycleSeconds how long a logging cycle takes, measured by the logger
 * @param guard the arming lead, NW_ALARM_GUARD_SECONDS unless measured otherwise
 * @return true when the schedule holds
 *
 * @details Four conditions, and the backup is not one of them. (1) The step
 * divides the day. (2) The step is at least the guard, or every wake would be
 * pushed. (3) The step, plus a cycle, stays inside the watchdog period: the
 * watchdog is fed once per wake, so an interval longer than its period resets the
 * board instead of logging. (4) The step exceeds a cycle, or the next wake
 * arrives while this one is still running.
 *
 * **A watchdog period of 0 means the condition is not applied**, because a bound
 * nobody has measured must not refuse a schedule that works. Margay and Okapi
 * both carry a TPL5010 whose period is set by a 100k resistor, and that period
 * has not been read off the datasheet's curve: assume it too short and an hourly
 * logger is refused for no reason, too long and the board resets mid-cycle. So
 * the number is the board's to supply once measured, and until then only the
 * other three conditions apply.
 *
 * The backup is armed whatever the watchdog does. Where the watchdog would fire
 * first, the backup sits behind it as the only recovery left if the watchdog
 * itself has failed - a wrong resistor, a dead part, a DONE pin stuck high - and
 * arming it costs 2.5 ms of bus traffic per cycle (Andy, 2026-10-09).
 */
inline bool nwScheduleHolds(uint32_t step, uint32_t wdtSeconds, uint32_t cycleSeconds,
                            uint32_t guard = NW_ALARM_GUARD_SECONDS) {
  if (!nwStepDividesDay(step)) return false;
  if (step < guard) return false;  //The guard would push every wake: see NW_ALARM_GUARD_SECONDS
  if (wdtSeconds && step + cycleSeconds >= wdtSeconds) return false;
  if (step <= cycleSeconds) return false;
  return true;
}

/**
 * @brief The next interval the grid can hold, after the one given.
 * @param after return the smallest legible step strictly greater than this
 * @return the step, or NW_SCHEDULE_NONE when nothing larger is legible
 *
 * @details The legible set is generated rather than tabulated, so it costs no
 * RAM on a board that has 16 kB of it: the divisors of 60 as seconds, sixty
 * times the divisors of 60 as minutes dividing an hour, and 3600 times the
 * divisors of 24 as hours dividing a day. Those are the same thirty values the
 * harness sweeps, and every one of them divides the day.
 */
inline uint32_t nwNextLegibleStep(uint32_t after) {
  uint32_t best = NW_SCHEDULE_NONE;
  for (uint32_t s = 1; s <= 60; s++) {
    if (60 % s) continue;
    if (s > after && s < best) best = s;  //Seconds: the divisors of 60
    uint32_t m = s * 60;
    if (m > after && m < best) best = m;  //Minutes: the same divisors, as minutes
  }
  for (uint32_t h = 1; h <= 24; h++) {
    if (24 % h) continue;
    uint32_t v = h * 3600;
    if (v > after && v < best) best = v;  //Hours: the divisors of 24
  }
  return best;
}

/**
 * @brief The interval this board can actually keep, lengthening the one asked for if it must.
 * @param step the interval the sketch asked for, in seconds
 * @param cycleSeconds the longest logging cycle seen so far, in whole seconds
 * @param wdtSeconds the watchdog's period, from the hardware
 * @param guard the arming lead, NW_ALARM_GUARD_SECONDS unless measured otherwise
 * @return the interval to keep: `step` when it holds, a longer legible step when
 * it does not, or NW_SCHEDULE_NONE when no interval can hold this cycle
 *
 * @details Returns `step` untouched whenever the schedule holds, so the normal
 * path costs one comparison. Where a cycle has outgrown its interval the logger
 * takes the next legible step that holds rather than a multiple of the one asked
 * for, because a multiple need not divide the day (20 s doubles to 40 s, which
 * does not) and the grid is what keeps rows on round times.
 *
 * **It never shortens.** The interval is sized to the longest cycle seen and not
 * to the last one, because an interval that cannot hold the worst cycle will
 * overrun again; a single slow card write therefore lengthens the schedule for
 * the rest of the deployment. That is the conservative direction, and the
 * alternative - lengthening only after several consecutive overruns - is a
 * threshold for Andy rather than one to pick here.
 *
 * NW_SCHEDULE_NONE means the hardware cannot log at all: the cycle plus any
 * interval exceeds the watchdog, so the board would reset mid-cycle whatever the
 * schedule. No interval fixes that and the caller must say so loudly.
 */
inline uint32_t nwLengthenStep(uint32_t step, uint32_t cycleSeconds, uint32_t wdtSeconds,
                               uint32_t guard = NW_ALARM_GUARD_SECONDS) {
  if (nwScheduleHolds(step, wdtSeconds, cycleSeconds, guard)) return step;
  uint32_t next = step;
  while (true) {
    next = nwNextLegibleStep(next);
    if (next == NW_SCHEDULE_NONE) return NW_SCHEDULE_NONE;
    if (nwScheduleHolds(next, wdtSeconds, cycleSeconds, guard)) return next;
  }
}

/**
 * @brief A cycle ran past its own interval, so a slot was lost.
 * @param startSeconds the clock's time of day when the cycle began
 * @param endSeconds the clock's time of day when it finished
 * @param step the logging interval, in seconds
 *
 * @details The direct measurement, taken from the two clock reads a cycle makes
 * anyway, rather than from an alarm flag: a flag says only whether an alarm
 * happened to be armed inside the window, where the two times say what the
 * schedule actually cares about. An overrun recovers by itself, because the next
 * wake is computed from the clock and lands on the first grid point still ahead.
 * What it must not do is pass unrecorded, so a caller that sees this flashes the
 * LED and latches a report.
 */
inline bool nwCycleOverran(uint32_t startSeconds, uint32_t endSeconds, uint32_t step) {
  uint32_t took = (endSeconds + NW_SCHEDULE_DAY - startSeconds) % NW_SCHEDULE_DAY;
  return took >= step;
}

#endif
