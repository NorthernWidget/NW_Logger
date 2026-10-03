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
 * minute, second. Leading and trailing whitespace, carriage returns and
 * newlines are ignored, which is what a terminal adds. Nothing else is: a stamp
 * with a non-digit in it, or with fewer than twelve digits, is refused rather
 * than read as far as it goes. The fields are not range-checked here; the clock
 * rejects what it cannot hold.
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
  if (*rest != '\0') return false;  //More than a stamp was sent
  for (uint8_t i = 0; i < 6; i++) out[i] = values[i];
  return true;
}

#endif
