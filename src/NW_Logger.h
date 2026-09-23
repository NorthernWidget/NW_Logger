/*
NW_Logger: the logger core that Margay_Library and Okapi_Library share.
Licensed: GNU GPL v3

Extracted 2026-09-23 from Margay_Library, whose authors wrote every function here:
Bobby Schulz
Andy Wickert
*/

#ifndef NW_LOGGER_h
#define NW_LOGGER_h

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include <EEPROM.h>
#include <NW_Core.h>   // NW_Sensor: the view the status file takes of a sensor; NW_Pages: the logger's own pages
#include "DS3231_Logger.h"
#include "SdFat.h"
#include <NW_BME280.h>

// Build identity: this library's version (held equal to library.properties by
// NW-Tests/version_check.py) and its build commit, set by the NW-Build wrapper from
// git and blank in an Arduino IDE build.
#define NW_LOGGER_LIBRARY_VERSION "0.0.0"
#ifndef NW_LOGGER_LIBRARY_COMMIT
#define NW_LOGGER_LIBRARY_COMMIT ""
#endif

/// @defgroup colors LED color constants
/// Packed 32-bit LED color values. Format: 0xLLRRGGBB where LL = luminosity,
/// RR = red, GG = green, BB = blue. Pass to LED_Color().
/// @{
#define RED         0xFFFF0000L
#define GREEN       0xFF00FF00L
#define BLUE        0xFF0000FFL
#define MAROON      0xFF800000L
#define GOLD        0xFFFFD700L
#define ORANGE      0xFFFFA500L
#define PURPLE      0xFF800080L
#define CYAN        0xFF00FFFFL
#define BLACK_ALERT 0x802019FFL ///< Deep blue-violet; used internally for error states.
/// @}

#define ON  1
#define OFF 0

//Define CBI macro
#ifndef cbi
#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
#endif


/**
 * @brief What a Northern Widget data logger is, apart from its board.
 * @details Margay and Okapi share one MCU (ATmega1284P), one SD library, one
 * clock, one BME280 and one way of logging: a serial-numbered folder on the
 * card, a numbered pair of data and status files, the run() loop with its
 * three triggers, the interrupt plumbing, the self-tests and the LED. That
 * is this class. A board's library inherits it and fills in the hooks: how
 * its power rails switch, what its on-board columns are, and what it reads
 * of itself. Every function here was written in Margay_Library and moved
 * without change; the Doxygen comments came with them.
 *
 * The logger is itself a Schema 1 device (NW-Device-Specification): it keeps
 * its Pages 0 and 1 from EEPROM and fills Pages 2 and 3 with a reading of
 * itself, and its rows in the status file decode like any sensor's.
 */
// Interrupt-shared state, defined in NW_Logger.cpp: the ISRs set them, run()
// clears them, and a board's begin() reads the external-interrupt pin and header.
extern volatile bool manualLog;
extern volatile uint8_t ExtIntPin;
extern String ext_int_header_entry;
extern volatile bool ExtIntTripped;
extern volatile uint16_t ExtInt_count;

class NW_Logger : public NW_Sensor
{
  public:
    /**
     * @brief Initialise the logger with a list of external I2C sensor addresses.
     * @details The board's library implements this: power, pins, on-board
     * chips, the serial number, self-tests, the LED report and the interrupts.
     * @param vals Pointer to array of 7-bit I2C addresses of external sensors.
     * @param numVals Number of addresses in vals. Silently truncated to 128.
     * @param header_ Comma-separated column header string for the log file,
     *                matching the CSV data returned by the user's update()
     *                function.
     * @return true when the self-tests found nothing wrong (Okapi's convention;
     *         Margay sketches may ignore it as before).
     */
    virtual bool begin(uint8_t *vals, uint8_t numVals, String header_) = 0;

    /**
     * @brief Initialise the logger with no external I2C sensors.
     * @details Convenience overload; equivalent to calling
     * begin(empty_array, 0, header_). Logs only on-board sensor values.
     * @param header_ Optional column header string (default empty).
     * @return what begin(vals, numVals, header_) returns.
     */
    bool begin(String header_ = "");

    /**
     * @brief Write a string to the SD card log file and echo it to Serial.
     * @param val String to log.
     * @return 0 on success, -1 if the log file could not be opened.
     */
    int logStr(String val);
    /**
     * @brief Append one row to the status file (sta<n>.csv beside log<n>.csv).
     * @details Columns: Time,Trigger,Device,Serial,HW,FW,FWCommit,Lib,LibCommit,Code,Note,Page0,Page1,Page2.
     * The logger writes its own boot row at every new file; a sketch writes a
     * device's row with the logger's time, a trigger word, and the device's
     * printStatus() line (NW_Core), whenever the device's reportKind() is not 0.
     * @return 0 written, -1 the file could not be opened
     */
    int statusStr(String val);
    /**
     * @brief Register a sensor whose reports the status file should carry.
     * @details Call once per sensor in setup(). After every reading the logger
     * writes a row for each watched sensor whose report says something happened:
     * a report captured with the reading (trigger "report"), and a report the
     * sensor captured at its boot other than the reset a logger expects when it
     * powers the rail (trigger "boot"). The first reading writes a boot row for
     * every watched sensor, whatever it says, so the file records each device's
     * identity and versions. Up to MaxWatched sensors.
     * @return false if the list is full
     */
    bool watch(NW_Sensor& sensor);

    // --- NW_Sensor: the logger is a Schema 1 device and watches itself ---
    /** @brief Kind of the logger's own report latched during the last reading (0 = none). */
    uint8_t reportKind() override;
    bool reportIsFault() override;
    /** @brief The report at boot: LoggingStarted (0xF0), or the first fault begin() found. */
    uint8_t bootReportKind() override;
    void clearBootReport() override;

    /**
     * @brief Note a one-word condition for the current log row.
     * @details The word goes in the Note column, the last column of every
     * row, written without a comma after it so the row ends cleanly. It is
     * also printed to Serial and shown as an orange pulse on the LED. Several
     * notes in one interval are joined with ';'. Cleared after each row.
     * Typical words: NotAnswering, OldFirmware, NotSchema1, LiDARTimeout.
     * @param word One word (no commas) naming the condition.
     */
    void note(const String& word);

    /**
     * @brief Set the on-board RGB LED to a packed color value.
     * @details The color format is 0xLLRRGGBB: byte 3 = luminosity,
     * byte 2 = red, byte 1 = green, byte 0 = blue. Use the predefined
     * color constants (RED, GREEN, BLUE, etc.) or OFF to turn the LED off.
     * @param val Packed 32-bit color value.
     */
    void LED_Color(unsigned long val);

    /**
     * @brief Main logging loop; call from Arduino loop().
     * @details Handles three logging triggers:
     *   - RTC alarm (every logInterval seconds): logs a data point and
     *     resets the alarm.
     *   - Manual log button press: logs an additional data point immediately.
     *   - External interrupt (if configured via setExtInt()): increments the
     *     event counter.
     * Puts the MCU into SLEEP_MODE_PWR_DOWN between events to minimise
     * power consumption. Turns the SD card and auxiliary power rail off
     * during sleep and restores them on wake.
     * @param f Pointer to the user's update() function, which must return a
     *          comma-separated String of sensor readings with a trailing comma.
     * @param logInterval Logging interval in seconds.
     */
    void run(String (*f)(void), unsigned long logInterval);

    /**
     * @brief Log one data point immediately, outside the normal run() cycle.
     * @details The board's library implements this: it switches the I2C bus
     * to external, calls the user's update() function, restores the bus,
     * prepends its on-board values and writes the row.
     * @param update Pointer to the user's update() function.
     */
    virtual void addDataPoint(String (*update)(void)) = 0;

    /**
     * @brief Create a new sequentially numbered log file on the SD card.
     * @details The pair logNNNNN.csv and staNNNNN.csv lives in SD:/<SN>/.
     * Searches for the next unused number, writes the status file's header
     * row and the data file's header row (dataHeader()).
     * Called automatically by run() when a new log is started; can also be
     * called directly (e.g. from HighSpeed_NoSleep sketches).
     */
    void initLogFile();

    /**
     * @brief Configure a pin as an external interrupt event counter.
     * @details Attaches a falling-edge interrupt to the given pin. Each
     * falling edge increments an internal counter accessible via
     * getExtIntCount(). Intended for pulse-output sensors such as tipping
     * bucket rain gauges and anemometers.
     * Must be called before begin().
     * @param n Arduino pin number for the external interrupt.
     * @param header_entry CSV column label for the counter, including trailing
     *                     comma (default "nInterrupts,").
     */
    void setExtInt(uint8_t n, String header_entry = "nInterrupts,");

    /**
     * @brief Atomically read the external interrupt event count.
     * @details Disables interrupts while reading and optionally resetting the
     * 16-bit counter to prevent torn reads on the 8-bit AVR. Any interrupt
     * that arrives during the critical section is deferred, not lost.
     * @param reset0 If true (default), reset the counter to zero after reading.
     * @return Number of external interrupt events since the last reset.
     */
    uint16_t getExtIntCount(bool reset0 = true);

    /**
     * @brief Atomically set the external interrupt counter to a given value.
     * @details Disables interrupts during the write to prevent a torn store
     * on the 8-bit AVR. Any interrupt that arrives during the critical section
     * is deferred, not lost.
     * @param start Value to set the counter to (default 0).
     */
    void resetExtIntCount(uint16_t start = 0);

    /**
     * @brief Pulse the external watchdog timer's DONE pin.
     * @details Call periodically to prevent a hardware watchdog reset.
     * Does nothing when WDHold is 255 (no watchdog on this board model).
     */
    void resetWDT();

    // -----------------------------------------------------------------------
    // Public pin definitions shared by every logger
    // A board's constructor sets these to its own values; do not modify
    // after begin() is called.
    // -----------------------------------------------------------------------
    uint8_t SD_CS  = 4;  ///< SD card SPI chip-select pin.
    uint8_t AuxLED = 20; ///< Auxiliary single-color LED pin.
    uint8_t RedLED = 13; ///< Red channel of on-board RGB LED (active low).
    uint8_t GreenLED = 15; ///< Green channel of on-board RGB LED (active low).
    uint8_t BlueLED  = 14; ///< Blue channel of on-board RGB LED (active low).
    uint8_t SD_CD       = 1; ///< SD card detect pin (LOW when card is present).
    uint8_t I2C_SW     = 21; ///< I2C bus switch (HIGH = external bus, LOW = internal bus).
    uint8_t RTCInt     = 10; ///< RTC alarm interrupt pin.
    uint8_t LogInt     =  2; ///< Manual log button interrupt pin.
    uint8_t WDHold     = 23; ///< Watchdog timer DONE pin (255 = not present on this model).

  protected:
    // --- the hooks a board's library fills in ---
    virtual String dataHeader() = 0; ///< The data file's header row: the on-board columns, Header, Note.
    virtual void sleepNow() = 0;     ///< Power down between events and come back with the card ready.
    virtual void afterLogEvent() {}  ///< Called by run() after an alarm-driven row is written (Okapi: the backhaul).

    // --- begin() in pieces: a board's begin() calls these in order around its own steps ---
    void acceptAddresses(uint8_t *vals, uint8_t numVals, String header_); ///< The sketch's sensor addresses (truncated to 128) and header, plus the ext-int column
    bool readIdentity();     ///< Pages 0-1 from EEPROM; SN and HWVersion from Page 0 (Schema 1) or the last 8 bytes (Schema 0). Returns whether Page 0 is valid; latches Page0Invalid if not
    void serialTimeSet();    ///< A YYMMDDHHMMSS string waiting on Serial sets the clock (notice ClockSet); prints the timestamp
    void attachLoggerInterrupts(bool buttonOnPCINT); ///< LED pins, SD chip select, file times, the alarm ISR and the log button (INT0 or PCINT)
    void ledReport();        ///< The self-test flags on the RGB LED, then "Ready to Log"
    void attachExtInt();     ///< The external-interrupt counter, if setExtInt() named a pin

    void blinkGood();
    virtual void writeDataToSD();
    virtual void buttonLog();
    static void isr0();
    static void isr1();
    static void isr2();
    static NW_Logger* selfPointer;
    static void dateTimeSD(uint16_t* date, uint16_t* time);
    void switchExternalI2C(bool desiredState);
    void getTime();
    void I2Ctest();
    void SDtest();
    void clockTest();
    void extIntCounter();
    void farmGateI2C(bool initialStateExternalI2C);

    DS3231_Logger RTC;
    BME bme280;

    String LogTimeDate = "2063/04/05 20:00:00";
    bool i2cTruncated = false; // true if numVals passed to begin() exceeded I2C_ADR capacity
    bool OnBoardError = false;
    bool SensorError = false;
    bool TimeError = false;
    bool SDCardMissing = false;
    bool BatError = false;
    bool BatWarning = false;
    String Header = "";
    String Note = ""; // pending word(s) for the Note column of the next row
    const char HexMap[16] = {
      '0', '1', '2', '3', '4', '5', '6', '7',
      '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
    }; // hex digit lookup table
    char SN[20] = {0}; // serial number: 19 chars + null terminator
    uint8_t NumADR = 0;
    uint8_t I2C_ADR[128] = {0}; // one slot per usable 7-bit I2C address
    uint8_t NumADR_OB = 1;
    uint8_t I2C_ADR_OB[6] = {0x68}; // on-board chips, the clock first; a board fills the rest (Margay two, Okapi six)

    volatile bool LogEvent = false; //Used to test if logging should begin yet
    volatile bool NewLog = false; //Used to tell system to start a new log
    volatile int AwakeCount = 0;

    char FileNameC[13]; // "logNNNNN.csv" (12 chars) + null terminator
    char FileNameStaC[13]; // "staNNNNN.csv", the status file with the same number
    static const uint8_t MaxWatched = 8;
    NW_Pages Pages;         // the logger's own Schema 1 pages: 0-1 from EEPROM, 2-3 its reading of itself
    NW_Report BootReport;   // what the logger reported at boot, until its row is written
    bool SDTestFailed = false; // the boot write-and-read-back on the card failed
    bool ClockError = false; // the DS3231 did not answer, or its oscillator is stopped
    bool BMEError = false;   // the BME280 did not answer
    unsigned long LogInterval = 0; // seconds, from run(); served on Page 3
    uint16_t FileNum = 0;   // the number of the current log and status file pair
    NW_Sensor* Watched[MaxWatched]; // sensors whose reports go to the status file (watch())
    uint8_t NumWatched = 0;
    bool DeviceBootRows = false; // the first reading's boot rows have been written
    int statusRow(const char* trigger, NW_Sensor& sensor, bool boot); // one device row: time, trigger, printStatus()
    void reportRows(); // after a reading: the rows the watched sensors' reports call for
    String HWVersion = ""; // "3.0" from Page 0 (Schema 1), else the model number; for the status file's boot row
    char FileNameTestC[11]; // "HWTest.txt" (10 chars) + null terminator
    bool externalI2COn = false;
    SdFat SD;
    byte  keep_SPCR;
    byte keep_ADCSRA;
    uint32_t SDIndex = 0; // the byte position in the data file after the last row, for a logger that reads rows back (Okapi's backhaul)
    uint32_t clockUnix(); // Unix seconds from the DS3231's fields, for Page 2 Block 3
};

#endif
