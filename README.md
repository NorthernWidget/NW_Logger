# NW_Logger

The logger core that [Margay_Library](https://github.com/NorthernWidget/Margay_Library) and [Okapi_Library](https://github.com/NorthernWidget/Okapi_Library) share. Both loggers run the same MCU, the same SD library, the same clock and the same way of logging, and this class is that way of logging: a serial-numbered folder on the card, a numbered pair of data and status files in it, the `run()` loop with its three triggers (the clock alarm, the log button, an external pulse), the interrupt plumbing, the self-tests at boot and the LED. A board's library inherits `NW_Logger` and fills in the hooks: how its power rails switch, what its on-board columns are, and what it reads of itself.

You do not include this library in a sketch. Include `Margay.h` or `Okapi.h` as before: every public method a sketch calls (`begin`, `run`, `logStr`, `watch`, `note`, `setExtInt`, `LED_Color`) is unchanged, and now lives here. Every function was written in Margay_Library and moved without change (the design and the survey behind it are section 12 of [LIBRARY-DESIGN.md](https://github.com/NorthernWidget/NW-Device-Specification/blob/master/LIBRARY-DESIGN.md)).

## What a logger is

The logger is itself a Schema 1 device ([NW-Device-Specification](https://github.com/NorthernWidget/NW-Device-Specification)). It keeps its Pages 0 and 1 from EEPROM, fills Pages 2 and 3 with a reading of itself at every log, and writes its own rows to the status file beside the rows of the sensors it watches (`watch(sensor)`), so that a card holds one record of everything on the bus and what each device reported. The columns are `Time,Trigger,Device,Serial,HW,FW,FWCommit,Lib,LibCommit,Code,Note,Page0,Page1,Page2`.

## The hooks

A board's library implements three things: `begin(vals, n, header)` (its power, pins, on-board chips, serial number, self-tests, LED report and interrupts), `addDataPoint(update)` (its on-board values in front of the sketch's), and `sleepNow()` (its rails off and on around the sleep). Furthermore, it returns its data file's header row from `dataHeader()`. Everything else it inherits.

Unversioned (0.0.0) and unregistered until the 2026 overhaul ends.
