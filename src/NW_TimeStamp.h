/*
NW_TimeStamp: the twelve-digit stamp a logger's clock is set from.
Licensed: GNU GPL v3

A terminal sends "YYMMDDHHMMSS" and the logger sets its clock from it. The parse
is here, apart from the serial read, because it is the only part with anything to
get wrong: extras/test exercises it, where the rest of the logger needs a card,
a clock and a board.
*/

#ifndef NW_TimeStamp_h
#define NW_TimeStamp_h

#include <stdint.h>
#include <stddef.h>

/// The characters a stamp has, "YYMMDDHHMMSS": six two-digit fields.
#define NW_TIMESTAMP_DIGITS 12

/**
 * @brief Read a "YYMMDDHHMMSS" stamp into six integers.
 * @details Year (two digits, the century is the caller's), month, day, hour,
 * minute, second.
 *
 * **Twelve digits, then anything that is not a digit.** The senders in use end
 * the stamp differently and the protocol never said which: SetTime_GUI's
 * NW_Logger_TimeSet appends an `x` (`LoggerSetTime()` in its `.pde`), a terminal
 * appends a carriage return and a newline, and a hand-typed stamp ends with
 * nothing at all. All three are accepted, because what a sender puts after the
 * field is its own business.
 *
 * A thirteenth digit is refused: that is a field of the wrong length rather than
 * a terminator, and reading the first twelve of it would drop a digit in
 * silence. So is a non-digit inside the field, and a field shorter than twelve.
 * The fields are not range-checked here; the clock rejects what it cannot hold.
 * @param s The received characters, zero-terminated.
 * @param out Six values, written only when the whole stamp is good.
 * @return True when `s` held exactly a stamp.
 */
inline bool nwParseTimeStamp(const char* s, int out[6]) {
  if (!s) return false;
  while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;  //What a terminal adds
  int values[6] = { 0 };
  for (uint8_t i = 0; i < NW_TIMESTAMP_DIGITS; i++) {
    char c = s[i];
    if (c < '0' || c > '9') return false;  //Short, or not a stamp at all
    int digit = c - '0';
    if (i % 2 == 0) values[i / 2] = 10 * digit;
    else values[i / 2] += digit;
  }
  const char* rest = s + NW_TIMESTAMP_DIGITS;
  while (*rest == ' ' || *rest == '\t' || *rest == '\r' || *rest == '\n') rest++;
  //Whatever follows the field is the sender's terminator, whichever it chose.
  //A digit is the one thing it may not be: that is a field of the wrong length.
  if (*rest >= '0' && *rest <= '9') return false;  //Thirteen digits: not a stamp
  for (uint8_t i = 0; i < 6; i++) out[i] = values[i];
  return true;
}

#endif
