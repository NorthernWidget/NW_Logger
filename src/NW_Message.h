/*
NW_Message: what a logger has to tell the user, apart from how a board tells it.
Licensed: GNU GPL v3

One flat list in the logger's own namespace, with the severity a column in a
table rather than a pattern in the number. The compiler assigns the values, so
nothing renumbers when a message is added and there is no reserved range to dodge.

This is deliberately NOT the sensor Report register of NW-Device-Specification.
That byte packs a chip into its top three bits and a kind into its bottom five,
because a peripheral serves 32 bytes to a polling controller; a logger has a
file, a serial port and an LED, and none of those constraints. The logger reads a
sensor's report register to learn about the sensor - the opposite direction - and
does not imitate one.

Three readings of every message: the LED at deployment, the Note column in the
data file, and the serial line. They come from this one table, so a new message
cannot reach one of them and miss another.
*/

#ifndef NW_Message_h
#define NW_Message_h

#include <Arduino.h>

/**
 * @brief How much the user has to do about a message.
 * @details Named for what it means rather than for a colour, because the colour
 * is one renderer's choice: a board with several LEDs may carry the band as a
 * position, the Note column carries it as a word, and a telemetered record as a
 * number. The stoplight mapping is the natural default on a single RGB LED and
 * not the definition.
 */
enum NW_Band : uint8_t {
  NW_BAND_FINE = 0,    ///< Nothing to do. The logger is doing what it was asked.
  NW_BAND_CAUGHT = 1,  ///< Act at the next visit. Something went wrong and was handled, or a limit is near.
  NW_BAND_BAD = 2      ///< Act now. It is not logging as asked.
};

/**
 * @brief Everything a logger says about itself, hardware and runtime alike.
 * @details Ordered by band. The band itself comes from the table, never from the
 * value - but worstMessage() shows the FIRST message of the highest band, so the
 * order WITHIN a band is a rule the code relies on. See the BAD block.
 */
enum NW_Message : uint8_t {
  //Fine: no action.
  NW_MSG_READY = 0,            ///< Self-tests passed and the first row reached the card.
  NW_MSG_LOG_STARTED,          ///< Logging began, at power-up or by the button.
  NW_MSG_NEW_FILE,             ///< A new data and status file pair was opened.
  NW_MSG_INTERVAL_LENGTHENED,  ///< The logging interval was lengthened.
  NW_MSG_INTERVAL_SHORTENED,   ///< The logging interval was shortened.
  NW_MSG_CLOCK_SET,            ///< The clock was set from a stamp over the serial line.

  //Caught: act at the next visit. Battery state is not a message: it is the
  //colour of the READY chirp at boot, and the voltage column in every row.
  NW_MSG_BME_MISSING,      ///< The on-board environment sensor did not answer; the logger still logs.
  NW_MSG_ROW_NOT_WRITTEN,  ///< A data row could not be written to the card.
  NW_MSG_NOTE_TRUNCATED,   ///< The row's Note column did not hold every word.
  NW_MSG_CYCLE_OVERRAN,    ///< A cycle ran past its own interval and a slot was lost.
  NW_MSG_BACKUP_WOKE,      ///< The backup alarm woke the logger, so the primary was missed.
  NW_MSG_ONBOARD_MISSING,  ///< An on-board chip other than the clock did not answer (the battery ADC on BUILD_B/C).
  NW_MSG_PAGE0_INVALID,    ///< Page 0 is unprovisioned or corrupt: the board has no identity.
  NW_MSG_ON_BACKUP_POWER,  ///< The main battery is absent or could not hold the bus; the backup is carrying the logger (Okapi).

  //Actively bad: act now. Ordered by how little the data file can tell you,
  //because worstMessage() shows the FIRST of the highest band on the one LED:
  //with no card the LED is the only channel left, so the card outranks everything.
  NW_MSG_CARD_MISSING,       ///< No card is inserted.
  NW_MSG_CARD_UNREADABLE,    ///< A card is inserted and would not mount.
  NW_MSG_CARD_WRITE_FAILED,  ///< The boot write-and-read-back on the card failed.
  NW_MSG_NO_SCHEDULE,        ///< No interval holds this cycle inside the watchdog: it cannot log.
  NW_MSG_SENSOR_MISSING,     ///< An address the sketch declared does not answer.
  NW_MSG_CLOCK_NOT_SET,      ///< The clock did not answer, or its oscillator is stopped.

  NW_MSG_COUNT
};

//One row per message, in flash. The words are what the Note column prints, so
//they are one token each with no spaces: a reader greps them and a parser splits
//the column on ';'.
static const char nwMsgReady[] PROGMEM = "Ready";
static const char nwMsgLogStarted[] PROGMEM = "LoggingStarted";
static const char nwMsgNewFile[] PROGMEM = "NewLogFile";
static const char nwMsgIntervalLengthened[] PROGMEM = "IntervalLengthened";
static const char nwMsgIntervalShortened[] PROGMEM = "IntervalShortened";
static const char nwMsgClockSet[] PROGMEM = "ClockSet";
static const char nwMsgBmeMissing[] PROGMEM = "BME280Missing";
static const char nwMsgRowNotWritten[] PROGMEM = "RowNotWritten";
static const char nwMsgNoteTruncated[] PROGMEM = "NoteTruncated";
static const char nwMsgCycleOverran[] PROGMEM = "CycleOverran";
static const char nwMsgBackupWoke[] PROGMEM = "BackupWoke";
static const char nwMsgOnBoardMissing[] PROGMEM = "OnBoardMissing";
static const char nwMsgPage0Invalid[] PROGMEM = "Page0Invalid";
static const char nwMsgOnBackupPower[] PROGMEM = "OnBackupPower";
static const char nwMsgClockNotSet[] PROGMEM = "ClockNotSet";
static const char nwMsgCardMissing[] PROGMEM = "CardMissing";
static const char nwMsgCardUnreadable[] PROGMEM = "CardUnreadable";
static const char nwMsgCardWriteFailed[] PROGMEM = "CardWriteFailed";
static const char nwMsgSensorMissing[] PROGMEM = "SensorMissing";
static const char nwMsgNoSchedule[] PROGMEM = "NoSchedule";

static_assert(NW_MSG_COUNT <= 32, "the message set is a uint32_t: one bit per message");

/// @brief A message's band and its word, the one place both are decided.
struct NW_MessageInfo {
  uint8_t band;
  const char* word;  ///< In PROGMEM: read with nwMessageWord(), not directly
};

static const NW_MessageInfo NW_MESSAGES[NW_MSG_COUNT] PROGMEM = {
  { NW_BAND_FINE, nwMsgReady },
  { NW_BAND_FINE, nwMsgLogStarted },
  { NW_BAND_FINE, nwMsgNewFile },
  { NW_BAND_FINE, nwMsgIntervalLengthened },
  { NW_BAND_FINE, nwMsgIntervalShortened },
  { NW_BAND_FINE, nwMsgClockSet },
  { NW_BAND_CAUGHT, nwMsgBmeMissing },
  { NW_BAND_CAUGHT, nwMsgRowNotWritten },
  { NW_BAND_CAUGHT, nwMsgNoteTruncated },
  { NW_BAND_CAUGHT, nwMsgCycleOverran },
  { NW_BAND_CAUGHT, nwMsgBackupWoke },
  { NW_BAND_CAUGHT, nwMsgOnBoardMissing },
  { NW_BAND_CAUGHT, nwMsgPage0Invalid },
  { NW_BAND_CAUGHT, nwMsgOnBackupPower },
  { NW_BAND_BAD, nwMsgCardMissing },
  { NW_BAND_BAD, nwMsgCardUnreadable },
  { NW_BAND_BAD, nwMsgCardWriteFailed },
  { NW_BAND_BAD, nwMsgNoSchedule },
  { NW_BAND_BAD, nwMsgSensorMissing },
  { NW_BAND_BAD, nwMsgClockNotSet },
};

/// @brief The band a message belongs to, or NW_BAND_FINE for a value out of range.
inline uint8_t nwMessageBand(uint8_t message) {
  if (message >= NW_MSG_COUNT) return NW_BAND_FINE;
  return pgm_read_byte(&NW_MESSAGES[message].band);
}

/**
 * @brief The message the LED shows for a set: the first of the highest band, or READY.
 * @details A set is one bit per message. The highest band present wins; within it,
 * the first in list order - which is why the order within BAD is a rule and not a
 * convenience. A set that is empty or merely FINE answers READY, so a clean boot
 * gets the chirp rather than whichever housekeeping message happened to be raised.
 * Bits at or above NW_MSG_COUNT are ignored. Pure, so the host harness tests it.
 */
inline uint8_t nwWorstMessage(uint32_t set) {
  uint8_t worst = NW_MSG_READY;
  uint8_t band = NW_BAND_FINE;
  for (uint8_t m = 0; m < NW_MSG_COUNT; m++) {
    if (!(set & ((uint32_t)1 << m))) continue;
    if (nwMessageBand(m) > band) {
      band = nwMessageBand(m);
      worst = m;
    }
  }
  return band == NW_BAND_FINE ? NW_MSG_READY : worst;
}

/// @brief How many raised messages of a set belong to a band.
inline uint8_t nwCountBand(uint32_t set, uint8_t band) {
  uint8_t n = 0;
  for (uint8_t m = 0; m < NW_MSG_COUNT; m++) {
    if (!(set & ((uint32_t)1 << m))) continue;
    if (nwMessageBand(m) == band) n++;
  }
  return n;
}

/**
 * @brief The forms the status LED can play: one per distinct rhythm, not one per message.
 * @details Identity rides on the rhythm as a shape (Andy, 2026-10-09), so two messages
 * that share a form are one thing to the eye and the file tells them apart. The
 * verdict plays each form raised once, worst to least (Andy, 2026-10-21).
 */
enum NW_Rhythm : uint8_t {
  NW_RHYTHM_NONE = 0,     ///< FINE: the file alone, no observer in the field
  NW_RHYTHM_CHIRP,        ///< READY: blue long-short-short, ALog's LEDgood()
  NW_RHYTHM_FLUTTER,      ///< the card, any of its three: red 60/60
  NW_RHYTHM_SOLID,        ///< NO_SCHEDULE, and any BAD message without a form of its own: red, steady
  NW_RHYTHM_PIPS,         ///< SENSOR_MISSING: red 100/300 x8
  NW_RHYTHM_SYNCOPATION,  ///< CLOCK_NOT_SET: red 250/100/100/100, ALog's LEDtimeWrong()
  NW_RHYTHM_AMBER,        ///< the CAUGHT band: one blip per message raised, then 500/500 x3
  NW_RHYTHM_COUNT
};

/// @brief The rhythm a message plays, decided here so signal() and the sequence agree.
inline uint8_t nwMessageRhythm(uint8_t message) {
  switch (message) {
    case NW_MSG_READY:
      return NW_RHYTHM_CHIRP;
    case NW_MSG_CARD_MISSING:
    case NW_MSG_CARD_UNREADABLE:
    case NW_MSG_CARD_WRITE_FAILED:
      return NW_RHYTHM_FLUTTER;
    case NW_MSG_SENSOR_MISSING:
      return NW_RHYTHM_PIPS;
    case NW_MSG_CLOCK_NOT_SET:
      return NW_RHYTHM_SYNCOPATION;
    default:
      switch (nwMessageBand(message)) {
        case NW_BAND_BAD:
          return NW_RHYTHM_SOLID;
        case NW_BAND_CAUGHT:
          return NW_RHYTHM_AMBER;
        default:
          return NW_RHYTHM_NONE;
      }
  }
}

/**
 * @brief The segments the verdict plays for a set, worst to least: one message per distinct rhythm.
 * @details BAD first in list order, then the CAUGHT band once; a rhythm already in
 * the sequence is not added again, so the three card messages or eight CAUGHT
 * messages give one segment each. FINE messages add nothing. Returns the number of
 * segments written to out, which must hold NW_RHYTHM_COUNT entries; zero means
 * nothing worse than FINE was raised and the caller plays READY. When the count is
 * not zero, out[0] is nwWorstMessage(set): the harness checks that over every set.
 */
inline uint8_t nwVerdictSequence(uint32_t set, uint8_t* out) {
  uint8_t n = 0;
  uint8_t seen = 0;  //one bit per NW_Rhythm
  for (int8_t band = NW_BAND_BAD; band >= NW_BAND_CAUGHT; band--) {
    for (uint8_t m = 0; m < NW_MSG_COUNT; m++) {
      if (!(set & ((uint32_t)1 << m))) continue;
      if (nwMessageBand(m) != band) continue;
      uint8_t rhythm = nwMessageRhythm(m);
      if (seen & (1 << rhythm)) continue;
      seen |= 1 << rhythm;
      out[n] = m;
      n++;
    }
  }
  return n;
}

/// @brief What the chirp says after a verdict's fault segments: the logger's running state.
enum NW_Chirp : uint8_t {
  NW_CHIRP_NONE = 0,  ///< not cycling: NO_SCHEDULE, so no chirp at all
  NW_CHIRP_WARNING,   ///< cycling but not saving: a card message; long, then the card's fast pulses
  NW_CHIRP_BACKUP,    ///< cycling and saving on the backup battery: long-short-short-short
  NW_CHIRP_FULL       ///< cycling and saving: long-short-short
};

/**
 * @brief Which chirp follows the fault segments.
 * @details The chirp means the logger is running, so it plays after the faults; its
 * form says how (Andy, 2026-10-26). A fault that stops rows reaching the card but
 * not the cycle - any card message, the FLUTTER rhythm - keeps the chirp's long and
 * replaces the two shorts with the card's fast pulses: "running, not saving". A fault
 * that stops the cycle - NO_SCHEDULE, the SOLID rhythm - leaves no chirp. An unset
 * clock, a missing sensor, anything CAUGHT: the full chirp - except ON_BACKUP_POWER,
 * which keeps the full chirp's meaning (running and saving) and adds a beat to say
 * the backup battery is carrying it (Andy, 2026-10-27). The test is on the rhythm
 * and not on a list of messages; NONE outranks WARNING, which outranks BACKUP.
 */
inline uint8_t nwChirpForm(uint32_t set) {
  uint8_t form = NW_CHIRP_FULL;
  for (uint8_t m = 0; m < NW_MSG_COUNT; m++) {
    if (!(set & ((uint32_t)1 << m))) continue;
    uint8_t rhythm = nwMessageRhythm(m);
    if (rhythm == NW_RHYTHM_SOLID) return NW_CHIRP_NONE;
    if (rhythm == NW_RHYTHM_FLUTTER) form = NW_CHIRP_WARNING;
    if (m == NW_MSG_ON_BACKUP_POWER && form == NW_CHIRP_FULL) form = NW_CHIRP_BACKUP;
  }
  return form;
}

/// @brief A message's word for the Note column and the serial line, from flash.
inline const __FlashStringHelper* nwMessageWord(uint8_t message) {
  if (message >= NW_MSG_COUNT) return (const __FlashStringHelper*)F("Unknown");  //The cast is for the host stub, where F() is a plain string
  return (const __FlashStringHelper*)pgm_read_ptr(&NW_MESSAGES[message].word);
}

#endif
