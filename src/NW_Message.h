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
 * @details Ordered by band, which is a convenience for reading the list and not
 * a rule the code relies on: the band comes from the table, never from the value.
 */
enum NW_Message : uint8_t {
  //Fine: no action.
  NW_MSG_READY = 0,            ///< Self-tests passed and the first row reached the card.
  NW_MSG_LOG_STARTED,          ///< Logging began, at power-up or by the button.
  NW_MSG_NEW_FILE,             ///< A new data and status file pair was opened.
  NW_MSG_INTERVAL_LENGTHENED,  ///< The logging interval was lengthened.
  NW_MSG_INTERVAL_SHORTENED,   ///< The logging interval was shortened.
  NW_MSG_CLOCK_SET,            ///< The clock was set from a stamp over the serial line.

  //Caught: act at the next visit.
  NW_MSG_BATTERY_LOW,      ///< Below the warning threshold, with cells fitted.
  NW_MSG_BME_MISSING,      ///< The on-board environment sensor did not answer; the logger still logs.
  NW_MSG_ROW_NOT_WRITTEN,  ///< A data row could not be written to the card.
  NW_MSG_NOTE_TRUNCATED,   ///< The row's Note column did not hold every word.
  NW_MSG_CYCLE_OVERRAN,    ///< A cycle ran past its own interval and a slot was lost.
  NW_MSG_BACKUP_WOKE,      ///< The backup alarm woke the logger, so the primary was missed.

  //Actively bad: act now.
  NW_MSG_CLOCK_NOT_SET,      ///< The clock did not answer, or its oscillator is stopped.
  NW_MSG_CARD_MISSING,       ///< No card is inserted.
  NW_MSG_CARD_UNREADABLE,    ///< A card is inserted and would not mount.
  NW_MSG_CARD_WRITE_FAILED,  ///< The boot write-and-read-back on the card failed.
  NW_MSG_SENSOR_MISSING,     ///< An address the sketch declared does not answer.
  NW_MSG_NO_SCHEDULE,        ///< No interval holds this cycle inside the watchdog: it cannot log.

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
static const char nwMsgBatteryLow[] PROGMEM = "BatteryLow";
static const char nwMsgBmeMissing[] PROGMEM = "BME280Missing";
static const char nwMsgRowNotWritten[] PROGMEM = "RowNotWritten";
static const char nwMsgNoteTruncated[] PROGMEM = "NoteTruncated";
static const char nwMsgCycleOverran[] PROGMEM = "CycleOverran";
static const char nwMsgBackupWoke[] PROGMEM = "BackupWoke";
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
  { NW_BAND_CAUGHT, nwMsgBatteryLow },
  { NW_BAND_CAUGHT, nwMsgBmeMissing },
  { NW_BAND_CAUGHT, nwMsgRowNotWritten },
  { NW_BAND_CAUGHT, nwMsgNoteTruncated },
  { NW_BAND_CAUGHT, nwMsgCycleOverran },
  { NW_BAND_CAUGHT, nwMsgBackupWoke },
  { NW_BAND_BAD, nwMsgClockNotSet },
  { NW_BAND_BAD, nwMsgCardMissing },
  { NW_BAND_BAD, nwMsgCardUnreadable },
  { NW_BAND_BAD, nwMsgCardWriteFailed },
  { NW_BAND_BAD, nwMsgSensorMissing },
  { NW_BAND_BAD, nwMsgNoSchedule }
};

/// @brief The band a message belongs to, or NW_BAND_FINE for a value out of range.
inline uint8_t nwMessageBand(uint8_t message) {
  if (message >= NW_MSG_COUNT) return NW_BAND_FINE;
  return pgm_read_byte(&NW_MESSAGES[message].band);
}

/// @brief A message's word for the Note column and the serial line, from flash.
inline const __FlashStringHelper* nwMessageWord(uint8_t message) {
  if (message >= NW_MSG_COUNT) return F("Unknown");
  return (const __FlashStringHelper*)pgm_read_ptr(&NW_MESSAGES[message].word);
}

#endif
