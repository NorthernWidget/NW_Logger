/*
NW_Logger: the logger core that Margay_Library and Okapi_Library share.
Licensed: GNU GPL v3

Extracted 2026-09-23 from Margay_Library, whose authors wrote every function here:
Bobby Schulz
Andy Wickert
*/

#include <NW_Logger.h>
#include <Arduino.h>

volatile bool manualLog = false; // Global for interrupt access

volatile uint8_t extIntPin = 255; // external interrupt pin; 255 = not set
const char* ext_int_header_entry = "nInterrupts,";
volatile bool extIntTripped = false; // Global for the external interrupt
volatile uint16_t extIntCount = 0; // Global for the external interrupt

NW_Logger* NW_Logger::selfPointer;

ISR (PCINT0_vect) { // handle pin change interrupt for D24-D31 (Port A) on ATmega1284p
  // NOTE: PCINT fires on both rising and falling edges. The current
  // implementation sets manualLog unconditionally. If the button is still
  // held when the logger finishes processing and re-enters sleep, the
  // rising edge on release will trigger a second log entry. Consider
  // checking pin state to fire only on the falling edge (button press):
  //   if (!(PINA & digitalPinToBitMask(28))) manualLog = true;
  manualLog = true;
}

// --- begin() in pieces: a board's begin() calls these in order around its own steps ---

void NW_Logger::acceptAddresses(uint8_t *vals, uint8_t numVals) {
  i2cTruncated = (numVals > sizeof(_i2cAdr));
  _numAdr = min(numVals, (uint8_t)sizeof(_i2cAdr));
  for (uint8_t i = 0; i < _numAdr; i++) _i2cAdr[i] = vals[i];
}

bool NW_Logger::readIdentity() {
  Serial.print("SN = ");
  int EEPROMLen = EEPROM.length(); //Copy value for faster access
  int val = 0; //Value to read temp EEPROM values into
  int pos = 0; //used to keep track of position in SN string
  // NW-Device-Specification: the stored image (Page 0 identity, Page 1 calibration) occupies the top 64 bytes of EEPROM, Page 0 first.
  // Schema 1 (NW-Provision): serial number = Block 2 (offset 0x10-0x17).
  // Schema 0 (MargaySetup): serial number = the last 8 bytes.
  int page0 = EEPROMLen - 64; //Schema 1 stored image: Page 0 identity, then Page 1 calibration, at the top of EEPROM (2026-09-23 renumbering)
  _pages.loadStored(page0); //The logger's own pages: 0 and 1 from EEPROM; 2 and 3 filled at each reading
  const uint8_t* p0 = _pages.page;
  bool schema1 = _pages.page0Valid();
  int snStart = schema1 ? page0 + 0x10 : EEPROMLen - 8;
  for (int i = snStart; i < snStart + 8; i++) {  //Read out Serial Number
    val = EEPROM.read(i);  //Read SN values as individual bytes from EEPROM
    // Load upper and lower nibbles of each EEPROM byte into SN string,
    // post-incrementing the position index each time
    _sn[pos++] = HEX_MAP[(val >> 4)];
    _sn[pos++] = HEX_MAP[(val % 0x10)];
    if ((i - snStart) % 2 == 1 && i < snStart + 7) {
      _sn[pos++] = '-';  //Place - between each SN category, post inc pos
    }
    _sn[19] = '\0'; //Null terminate string
  }

  Serial.print(_sn); //Print compiled string
  if (schema1) {
    Serial.print("  (Schema 1, HW v");
    Serial.print(p0[0x08]); Serial.print("."); Serial.print(p0[0x09]); Serial.print(")");
    snprintf(_hwVersion, sizeof(_hwVersion), "%u.%u", p0[0x08], p0[0x09]); //For the status file's boot row
  }
  if (!schema1) _pages.latchFault(0xE3); //Page 0 invalid: unprovisioned or corrupt
  if (strcmp(_sn, "FFFF-FFFF-FFFF-FFFF") == 0)
    Serial.println("WARNING: no serial number programmed in EEPROM");
  return schema1;
}

void NW_Logger::serialTimeSet() {
  Serial.print("\n\n");
  Serial.println("\nInitializing...\n"); //DEBUG!
  delay(100);
  if (Serial.available()) {  //If time setting info available
    String dateTimeTemp = Serial.readString();
    Serial.println(dateTimeTemp);  //DEBUG!
    int dateTimeVals[6] = {0};
    for (int i = 0; i < 6; i++) {
      dateTimeVals[i] = dateTimeTemp.substring(2*i, 2*(i+1)).toInt();
      Serial.print(i); Serial.print("  "); //DEBUG!
      Serial.println(dateTimeVals[i]); //DEBUG!
    }
    _rtc.setTime(2000 + dateTimeVals[0], dateTimeVals[1], dateTimeVals[2],
                dateTimeVals[3], dateTimeVals[4], dateTimeVals[5]);
    _pages.latchNotice(0x30); //ClockSet
  }

  getTime(); //Get time to pass to computer
  Serial.print("\nTimestamp = ");
  Serial.println(_logTimeDate);
}

void NW_Logger::attachLoggerInterrupts(bool buttonOnPCINT) {
  //Sets up basic initialization required for the system
  selfPointer = this;


  pinMode(RedLED, OUTPUT);
  pinMode(GreenLED, OUTPUT);
  pinMode(BlueLED, OUTPUT);

  LED_Color(OFF);

  pinMode(SD_CS, OUTPUT);

  SdFile::dateTimeCallback(dateTimeSD); //Setup SD file time setting
  // Attach ISR driven by RTC interrupt; triggers data logging each interval
  attachInterrupt(digitalPinToInterrupt(RTCInt), NW_Logger::isr1, FALLING);
  if (!buttonOnPCINT) {
    // Attach ISR driven by manual log button, sets logging flag and logs data
    attachInterrupt(digitalPinToInterrupt(LogInt), NW_Logger::isr0, FALLING);
  }
  else { //Margay v2.0 and up, Okapi: PCINT for log button (LogInt = D28, PA4); enable pin first
    *digitalPinToPCMSK(LogInt) |= bit(digitalPinToPCMSKbit(LogInt)); // enable
    PCIFR |= bit(digitalPinToPCICRbit(LogInt)); // clear outstanding interrupt
    PCICR |= bit(digitalPinToPCICRbit(LogInt)); // enable interrupt group
  }
  pinMode(RTCInt, INPUT_PULLUP);
  pinMode(LogInt, INPUT);
}

void NW_Logger::ledReport() {
  digitalWrite(AuxLED, HIGH);

  if (_onBoardError) {
    LED_Color(RED); //On board failure
    delay(2000);
  }
  if (_sensorError) {
    LED_Color(ORANGE);  //Sensor failure
    delay(2000);
  }
  if (_timeError) {
    LED_Color(CYAN); //Time set error
    delay(2000);
  }
  if (_sdCardMissing) {
    LED_Color(PURPLE); //Sd card not inserted
    delay(2000);
  }
  // Battery voltage is below level where hardware functionality
  // can be guaranteed
  if (_batError) {
    for (int i = 0; i < 10; i++) {
      LED_Color(RED);
      delay(100);
      LED_Color(OFF);
      delay(100);
    }
  }

  // Battery charge % is at a concerning level; recommend replacing batteries
  if (_batWarning && !_batError) {
    for (int i = 0; i < 10; i++) {
      LED_Color(GOLD); //Low battery charge warning
      delay(100);
      LED_Color(OFF);
      delay(100);
    }
  }
  //Include battery error in test??
  if (!_onBoardError && !_sensorError && !_timeError && !_sdCardMissing) {
    LED_Color(GREEN);
    delay(2000);
  }

  Serial.print("\nReady to Log...\n\n");
}

void NW_Logger::attachExtInt() {
  if (extIntPin != 255) {
    pinMode(extIntPin, INPUT);
    digitalWrite(extIntPin, HIGH);
    attachInterrupt(digitalPinToInterrupt(extIntPin), NW_Logger::isr2, FALLING);
  }
}

bool NW_Logger::begin() {
  //No address list passed, so take one from the sensors watch() was given, in
  //watch order. A sketch used to keep a second list of its own, which had to
  //stay in step with the sensors it constructed and the header it composed
  //(Andy, 2026-10-03). Watch nothing and this tests nothing, as before.
  uint8_t adr[MAX_SENSORS];
  uint8_t n = 0;
  for (uint8_t i = 0; i < _numSensors; i++) {
    if (_sensors[i] == this) continue;     //a logger is not found at an address
    if (_sensorAddress[i]) adr[n++] = _sensorAddress[i];
  }
  bool ok = beginBoard(adr, n); //The board's own hardware start
  _begun = true;                //The sensor rail and the bus switch are up
  return ok;
}

bool NW_Logger::begin(NW_Sensor** candidates, uint8_t n) {
  //The board first, then the bus. Discovery needs the sensor rail and the bus
  //switch, and bringing those up is what begin() does, so the order is the
  //logger's to keep rather than the sketch's to remember: a bus asked before
  //begin() answers nothing at all. Nothing in begin() depends on knowing the
  //sensors beforehand except I2Ctest(), and discovery is the stronger test -
  //every device it watched answered to its name. The data file's header row is
  //written by initLogFile() at the first run(), which is after this.
  bool ok = begin();
  discover(candidates, n);
  return ok;
}

void NW_Logger::I2Ctest() {
  bool initialStateExternalI2C = digitalRead(I2C_SW);

  switchExternalI2C(ON);

  int error = 0;
  bool i2cTest = true;

  Serial.print("I2C: ");
  if (i2cTruncated)
    Serial.println(F("WARNING: address list truncated to 128"));
  for (int i = 0; i < _numAdr; i++) {
    Wire.beginTransmission(_i2cAdr[i]);
    error = Wire.endTransmission();
    if (error != 0) {
      if (i2cTest) Serial.println(" Fail");
      Serial.print("   Fail At: ");
      Serial.println(_i2cAdr[i], HEX);
      i2cTest = false;
      _sensorError = true;
    }
  }

  //Switch to connect to onboard I2C!
  switchExternalI2C(OFF);

  for (int i = 0; i < _numAdrOb; i++) {
    Wire.beginTransmission(_i2cAdrOb[i]);
    error = Wire.endTransmission();
    if (error != 0) {
      if (i2cTest) Serial.println(" Fail");
      Serial.print("   Fail At: ");
      Serial.println(_i2cAdrOb[i], HEX);
      i2cTest = false;
      _onBoardError = true;
    }
  }

  if (i2cTest) Serial.println("PASS");

  // make sure I2C Bus is returned to initial state
  farmGateI2C(initialStateExternalI2C);
}


void NW_Logger::SDtest() {
  bool sdTestFailed = false;

  // SD_CD is pulled up: HIGH=1 if not present
  // SD card being inserted closes a switch to pull it LOW
  pinMode(SD_CD, INPUT);
  bool cardNotPresent = digitalRead(SD_CD);

  Serial.print("SD: ");
  delay(5); //DEBUG!
  if (cardNotPresent) {
    Serial.println(F(" NO CARD"));
    sdTestFailed = true;
    _sdTestFailed = true;
    _sdCardMissing = true; //Card not inserted
  }
  else if (!_sd.begin(SD_CS)) {
    _onBoardError = true;
    sdTestFailed = true;
    _sdTestFailed = true;
  }

  // If card is present and initialised successfully, do the following:
  if (!cardNotPresent && !sdTestFailed) {
    _sd.chdir("/"); //The card's root
    _sd.mkdir(_sn); //Make directory with serial number as name: everything this logger writes lives in it
    _sd.chdir(_sn); //Move into this directory
    String fileNameTest = "HWTest";
    (fileNameTest + ".txt").toCharArray(_fileNameTestC, 11);
    _sd.remove(_fileNameTestC); //Remove any previous files

    // Seed with a random process to ensure randomness
    randomSeed(analogRead(A7));
    // Generate a random number between 1 and 30557
    // (the number of words in Hamlet); start at 1 to avoid log10(0)
    int randVal = random(1, 30557);
    char randDigits[6] = {0};
    // Convert randVal into a series of digits
    sprintf(randDigits, "%d", randVal);
    // +1: println appends \r; loop uses randLength-1 to skip it
    int randLength = strlen(randDigits) + 1;
    File dataWrite = _sd.open(_fileNameTestC, FILE_WRITE);
    if (dataWrite) {
      dataWrite.println(randVal);
      dataWrite.println("\nHe was a man. Take him for all in all.");
      dataWrite.println("I shall not look upon his like again.");
      dataWrite.println("-Hamlet, Act 1, Scene 2");
    }
    dataWrite.close();
    char testDigits[6] = {0};
    File dataRead = _sd.open(_fileNameTestC, FILE_READ);
    if (dataRead) {
      dataRead.read(testDigits, randLength);
      for (int i = 0; i < randLength - 1; i++){ //Test random value string
        if (testDigits[i] != randDigits[i]) {
          sdTestFailed = true;
          _sdTestFailed = true;
          _onBoardError = true;
        }
      }
    }
    dataRead.close();

    keep_SPCR=SPCR;
  }

  // If card is inserted and still does not connect properly, throw error
  if (sdTestFailed && !cardNotPresent) Serial.println("FAIL");
  // If card is inserted AND connects properly, return success
  else if (!sdTestFailed && !cardNotPresent) Serial.println("PASS");
}

void NW_Logger::clockTest() {
  int error = 1;
  uint8_t testSeconds = 0;
  bool oscStop = false;

  Serial.print("Clock: ");
  Wire.beginTransmission(_i2cAdrOb[0]);
  Wire.write(0xFF);
  error = Wire.endTransmission();

  if (error == 0) {
    getTime(); //FIX!
    testSeconds = _rtc.getValue(5);
    delay(1100);
    if (_rtc.getValue(5) == testSeconds) {
      _onBoardError = true; // If clock is not incrementing
      _clockError = true;
      oscStop = true;      // Oscillator not running
      Serial.println(" FAIL (oscillator stopped)");
    }
    if (!oscStop) {
      // DS3231 powers on at Jan 1, 2000 (year register = 0). If the year
      // is still 0, the clock has not been set — timestamps will read as
      // 2000, which is an obviously wrong but identifiable sentinel value.
      unsigned int yearNow = _rtc.getValue(0);
      if (yearNow == 0) {
        _timeError = true;
        Serial.println(" PASS, BAD TIME");
      } else {
        Serial.println(" PASS");
      }
    }
  } else {
    Serial.println(" FAIL");
    _onBoardError = true;
    _clockError = true;
  }
}

void NW_Logger::initLogFile() {
  _sd.chdir("/");  //The card's root
  _sd.chdir(_sn);  //Move into this logger's folder, named by its serial number
  //Find the first unused file number in "SD:/sn/"
  char numCharArray[6];
  String fileName = "log";
  int fileNum = 1;
  sprintf(numCharArray, "%05d", fileNum);
  (fileName + String(numCharArray) + ".csv").toCharArray(_fileNameC, 13);
  while (_sd.exists(_fileNameC)) {
    fileNum += 1;
    sprintf(numCharArray, "%05d", fileNum);
    (fileName + String(numCharArray) + ".csv").toCharArray(_fileNameC, 13);
  }
  ("sta" + String(numCharArray) + ".csv").toCharArray(_fileNameStaC, 13); //The status file, same number
  if (_fileNum != 0) _pages.latchNotice(0xF1); //NewLogFile: a later pair, not the first
  _fileNum = fileNum;
  Serial.print("FileNameC: ");
  Serial.println(_fileNameC);
  // The status file: one row per report, boot and check, from this logger and
  // from every device on it (NW-Device-Specification Report register). Its
  // boot row carries what the data file's first line used to: library
  // version and serial number, with the hardware version beside them.
  //The status file's header row. The logger's own boot row follows at the first
  //reading, because it watches itself.
  _sd.chdir("/");
  _sd.chdir(_sn);
  File StatusHeader = _sd.open(_fileNameStaC, FILE_WRITE);
  if (StatusHeader) {
    StatusHeader.println(F("Time,Trigger,Device,Serial,HW,FW,FWCommit,Lib,LibCommit,Code,Note,Page0,Page1,Page2"));
    StatusHeader.close();
  }
  Serial.println(F("Time,Trigger,Device,Serial,HW,FW,FWCommit,Lib,LibCommit,Code,Note,Page0,Page1,Page2"));
  // The data file starts with its header row, which the logger builds from
  // its own columns, then every watched sensor's (printFileHeader()).
  //The data file's header row, written from the sensors watch() was given.
  _sd.chdir("/");
  _sd.chdir(_sn);
  File HeaderFile = _sd.open(_fileNameC, FILE_WRITE);
  if (HeaderFile) {
    printFileHeader(HeaderFile);
    HeaderFile.println();
    HeaderFile.close();
  }
  printFileHeader(Serial);
  Serial.println();
}



void NW_Logger::LED_Color(unsigned long val) { //Set color of onboard led
  int red = 0; //red led color
  int green = 0;  //green led color
  int blue = 0;  //blue led color
  int lum = 0;  //Luminosity

  //Parse all values from single val
  blue = val & 0xFF;
  green = (val >> 8) & 0xFF;
  red = (val >> 16) & 0xFF;
  lum = (val >> 24) & 0xFF;
  //  lum = 255 - lum; //Invert since LEDs are open drain

  analogWrite(RedLED, 255 - (red * lum)/0xFF);
  analogWrite(GreenLED, 255 - (green * lum)/0xFF);
  analogWrite(BlueLED, 255 - (blue * lum)/0xFF);
}

uint32_t NW_Logger::clockUnix() {
  //Clock: Unix seconds from the DS3231's fields (days from civil, proleptic Gregorian)
  int y = _rtc.getValue(0), mo = _rtc.getValue(1), d = _rtc.getValue(2);
  int32_t yy = y - (mo <= 2 ? 1 : 0);
  int32_t era = (yy >= 0 ? yy : yy - 399) / 400;
  uint32_t yoe = (uint32_t)(yy - era * 400);
  uint32_t doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  uint32_t days = (uint32_t)(era * 146097 + (int32_t)doe - 719468);
  return days * 86400UL + (uint32_t)_rtc.getValue(3) * 3600UL + (uint32_t)_rtc.getValue(4) * 60UL + (uint32_t)_rtc.getValue(5);
}

void NW_Logger::getTime() {
  //Update global time string
  //getTime() answers a String; copy it into the buffer the rows print from.
  strlcpy(_logTimeDate, _rtc.getTime(0).c_str(), sizeof(_logTimeDate));
}

void NW_Logger::blinkGood() {
  // Peppy blinky pattern to show that the logger has successfully initialized
  digitalWrite(BlueLED,LOW);
  delay(651);
  digitalWrite(BlueLED,HIGH);
  delay(300);
  digitalWrite(BlueLED,LOW);
  delay(100);
  digitalWrite(BlueLED,HIGH);
  delay(200);
  digitalWrite(BlueLED,LOW);
  delay(100);
  digitalWrite(BlueLED,HIGH);
}

// Pass in function which returns string of data

void NW_Logger::run(unsigned long logInterval) {
  _logInterval = logInterval; //Served on Page 3
  // Print note that that logging has started
  // Serial.println("Log Started!"); //DEBUG!
  // Serial.println(millis()); //DEBUG!
  if (_newLog) {
    // LogEvent = true;
    _rtc.setAlarm(logInterval);
    initLogFile(); //Start a new file each time log button is pressed
    //Add inital data point
    addDataPoint();
    _newLog = false;  //Clear flag once log is started
    blinkGood();  //Alert user to start of log
    resetWDT(); //Clear alarm
  }

  if (_logEvent) {
    // Serial.println("Log Event!"); //DEBUG!
    // RTC.setAlarm(logInterval);  //Set/reset alarm //DEBUG!
    addDataPoint(); //Write values to SD
    afterLogEvent(); //A board's follow-up to the alarm-driven row (Okapi's backhaul)
    _logEvent = false; //Clear log flag
    _rtc.setAlarm(logInterval);  //Set/reset alarm
    resetWDT(); //Clear alarm
  }

  // Write data to SD card without interrupting existing timing cycle
  if (manualLog) {
    // Serial.println("Click!"); //DEBUG!
    addDataPoint(); //write values to SD
    manualLog = false; //Clear log flag
    resetWDT(); //Clear alarm
  }

  if (extIntTripped) {  // Defaults to just counter for now
    // Serial.println("TIP!"); //DEBUG!
    extIntCount ++;
    extIntTripped = false; // Clear interrupt flag
    resetWDT(); //Clear alarm
    delay(150); //Hard-code for now; tipping bucket "debounce"
    attachInterrupt(digitalPinToInterrupt(extIntPin), NW_Logger::isr2, FALLING);
  }

  if (!digitalRead(RTCInt)) {  //Catch alarm if not reset properly
    Serial.println("Reset Alarm"); //DEBUG!
    _rtc.setAlarm(logInterval); //Turn alarm back on
  }

  _awakeCount++;

  // @bschulz1701: AwakeCount was designed to give ~5 run() iterations after
  // an RTC wake before returning to sleep, reset to 0 by the RTC ISR
  // (writeDataToSD). Since addDataPoint() blocks within a single run() call,
  // the 5-count may be unnecessary. Consider simplifying or removing.
  if (_awakeCount > 5) {
    sleepNow();
  }
  delay(1);
}

// Send a pulse to "feed" the watchdog timer
void NW_Logger::resetWDT() {
  if (WDHold == 255) return; // No watchdog timer on this board model
  digitalWrite(WDHold, HIGH); //Set DONE pin high
  delayMicroseconds(5); //Wait a short pulse
  digitalWrite(WDHold, LOW);
}

void NW_Logger::switchExternalI2C(bool desiredState) {
  /*
  Must be ON to read off-board sensors, OFF to read on-board sensors and RTC
  Serves to isolate these s.t. I2C addresses may not clash.
  */
  pinMode(I2C_SW, OUTPUT);

  if ( desiredState == ON ) {
    digitalWrite(I2C_SW, HIGH);
    externalI2COn = digitalRead(I2C_SW);
  }
  else {
    digitalWrite(I2C_SW, LOW);
    externalI2COn = digitalRead(I2C_SW);
  }
  delay(1); // Any time needed to switch states; may not be necessary
}

void NW_Logger::farmGateI2C(bool initStateI2C) {
  // This closes the farm gate at the end of a function that uses the
  // external I2C bus. It works by checking if the current state of I2C
  // comms (class variable) matches the starting state (argument to this
  // function). If they do not match (XOR), and it was on at the start,
  // then turn on. Otherwise, turn off.

  switchExternalI2C(initStateI2C);
}

uint8_t NW_Logger::reportKind()    { return _pages.report().kind(); }
bool    NW_Logger::reportIsFault() { return _pages.report().isFault(); }
uint8_t NW_Logger::bootReportKind() { return _bootReport.kind(); }
void    NW_Logger::clearBootReport() {
  _bootReport.code = 0;
  _bootReport.status = 0;
}

//Does anything hold that address? One empty transmission, which is what
//I2Ctest() has always asked the bus.
bool NW_Logger::answers(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

//Page 0 in one transaction: the firmware serves it from EEPROM, so it is valid
//before the device has taken a reading. True when the page is there and passes
//the schema, magic and CRC-8 gates, which is what makes a device self-naming.
bool NW_Logger::readPage0(uint8_t address, NW_Pages& pages) {
  Wire.beginTransmission(address);
  Wire.write((uint8_t)0x00);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(address, (uint8_t)32) != 32) return false;
  for (uint8_t i = 0; i < 32; i++) pages.page[i] = Wire.read();
  return pages.page0Valid();
}

//Is the name in this Page 0 the name this library answers to? The field is
//seven bytes at 0x01, padded with zeros, and a library's name() is the same
//spelling as a compile-time literal.
static bool nameMatches(const uint8_t* page, const char* name) {
  uint8_t i = 0;
  for (; i < 7 && name[i]; i++) {
    if ((char)page[1 + i] != name[i]) return false;
  }
  for (; i < 7; i++) {
    if (page[1 + i] != 0) return false;   //the rest of the field is padding
  }
  return true;
}

bool NW_Logger::watching(NW_Sensor& sensor) {
  for (uint8_t i = 0; i < _numSensors; i++) {
    if (_sensors[i] == &sensor) return true;
  }
  return false;
}

uint8_t NW_Logger::scan(Print& out) {
  //The sensor bus, address by address. A device that answers gets its Page 0
  //read and checked; one that passes has named itself, which is what lets a
  //logger be told what is attached rather than told what to expect.
  bool initialStateExternalI2C = digitalRead(I2C_SW);
  switchExternalI2C(ON);

  NW_Pages p;                        //borrowed for page0Valid() and its CRC-8
  uint8_t answered = 0, identified = 0;
  out.println(F("I2C scan:"));
  for (uint8_t adr = 0x08; adr <= 0x77; adr++) {
    if (!answers(adr)) continue;
    answered++;
    out.print(F("  0x"));
    nwPrintHex(out, &adr, 1);
    out.print(F("  "));

    if (!readPage0(adr, p)) {
      out.println(F("(no valid Page 0)"));
      continue;
    }
    identified++;
    for (uint8_t i = 1; i <= 7 && p.page[i]; i++) out.print((char)p.page[i]);
    out.print(F("  HW "));
    out.print(p.page[0x08]);
    out.print('.');
    out.print(p.page[0x09]);
    out.print(F("  FW patch "));
    out.print(p.page[0x0A]);
    out.print(F("  SN "));
    for (uint8_t i = 0; i < 4; i++) {
      if (i) out.print('-');
      nwPrintHex(out, p.page + 0x10 + 2 * i, 2);
    }
    //The address Page 0 was provisioned with, when it is not the one answering.
    if (p.page[0x1F] != 0xFF && p.page[0x1F] != adr) {
      out.print(F("  (Page 0 says 0x"));
      nwPrintHex(out, &p.page[0x1F], 1);
      out.print(')');
    }
    out.println();
  }
  out.print(answered);
  out.print(F(" answered, "));
  out.print(identified);
  out.println(F(" Schema 1"));

  farmGateI2C(initialStateExternalI2C);
  return answered;
}

uint8_t NW_Logger::discover(NW_Sensor** candidates, uint8_t n) {
  if (!_begun) {
    //The sensor rail is off and the bus switch is an input until begin() has
    //run, so the bus would answer nothing and this would watch nothing. Report
    //it as the sensor fault it is, which the LED and the boot row both carry,
    //rather than logging a file of on-board columns and saying nothing.
    _sensorError = true;
    return 0;
  }
  //The same walk scan() makes, answered with watch() instead of a line of text.
  //A device that passes its Page 0 checks has named itself; a candidate whose
  //name() is that name is the library that reads it, and it is watched at the
  //address it answered on rather than at its default.
  bool initialStateExternalI2C = digitalRead(I2C_SW);
  switchExternalI2C(ON);

  NW_Pages p;                        //borrowed for page0Valid() and its CRC-8
  uint8_t found = 0;
  //Address order, which is therefore column order, and the bus is walked only
  //as far as there are slots left to hold what it finds.
  for (uint8_t adr = 0x08; adr <= 0x77 && _numSensors < MAX_SENSORS; adr++) {
    if (!answers(adr)) continue;
    if (!readPage0(adr, p)) continue;
    for (uint8_t i = 0; i < n; i++) {
      if (!nameMatches(p.page, candidates[i]->name())) continue;
      //One object holds one address at a time, so a second board of the same
      //kind needs a second slot in the table rather than the same one twice.
      if (watching(*candidates[i])) continue;
      watch(*candidates[i], adr);
      found++;
      break;
    }
  }

  farmGateI2C(initialStateExternalI2C);
  return found;
}

size_t NW_Logger::printFileHeader(Print& out) {
  //The logger's own columns first, then each watched sensor's in watch order,
  //which is also column order. Note is always the last column and carries no
  //comma after it: every sensor ends its fields with a comma for the next, so
  //this ends the row.
  size_t n = printDataHeader(out);
  for (uint8_t i = 0; i < _numSensors; i++) {
    //A sketch may watch the logger for the status file. Its columns are
    //already written above, and printing them twice would be silent.
    if (_sensors[i] == this) continue;
    n += _sensors[i]->printDataHeader(out);
  }
  if (extIntPin != 255) n += out.print(ext_int_header_entry);
  n += out.print("Note");
  return n;
}



void NW_Logger::noteFrom(NW_Sensor& sensor, bool beginFailed) {
  //One sensor's word into the Note column, semicolon-separated as note() does.
  NW_BufferPrint p(_note, NOTE_CAPACITY, true);   //append to this row's notes
  if (p.length() > 0) p.print(';');
  sensor.printNote(p, beginFailed);
  if (p.truncated()) _pages.latchNotice(0xF3);   //NoteTruncated: the row's notes did not fit
  Serial.print(F("Note: "));
  Serial.println(_note);
}

void NW_Logger::readSensors() {
  //The two acquisition calls on every watched sensor, in watch order. A Margay
  //cuts the sensor rail at every sleep, so each one is begun again before it is
  //asked for a reading, and whichever step refuses puts its word in the Note
  //column: that is what the sketch's update() used to do by hand.
  for (uint8_t i = 0; i < _numSensors; i++) {
    if (_sensors[i] == this) continue;   //the logger reads its own channels below
    NW_Sensor& s = *_sensors[i];
    if (!s.wake(_sensorAddress[i])) {
      noteFrom(s, true);
      continue;
    }
    if (!s.acquire()) noteFrom(s, false);
    else if (s.reportKind() != 0) noteFrom(s, false);
  }
  acquire();                          //this logger's own channels, last
}

int NW_Logger::logRow() {
  //One row, written straight into the file and echoed to the monitor: this
  //logger's own columns, then each watched sensor's in watch order, then Note.
  //Nothing is composed in RAM (LIBRARY-DESIGN.md section 14).
  _sd.chdir("/");
  _sd.chdir(_sn);
  File DataFile = _sd.open(_fileNameC, FILE_WRITE);
  if (!DataFile) return -1;
  printDataRow(DataFile);
  for (uint8_t i = 0; i < _numSensors; i++) {
    if (_sensors[i] == this) continue;
    _sensors[i]->printDataRow(DataFile);
  }
  DataFile.print(_note);
  DataFile.println();
  _sdIndex = DataFile.position();     //Where the next row starts
  DataFile.close();

  //The monitor gets the same row. printDataRow() prints stored values, so a
  //second pass costs formatting and no bus traffic.
  printDataRow(Serial);
  for (uint8_t i = 0; i < _numSensors; i++) {
    if (_sensors[i] == this) continue;
    _sensors[i]->printDataRow(Serial);
  }
  Serial.println(_note);
  _note[0] = '\0';                     //One row's worth of notes
  return 0;
}

bool NW_Logger::watch(NW_Sensor& sensor, uint8_t address) {
  if (_numSensors >= MAX_SENSORS) return false;
  _sensorAddress[_numSensors] = address ? address : sensor.defaultAddress();
  _sensors[_numSensors++] = &sensor;
  return true;
}


int NW_Logger::statusRow(const char* trigger, NW_Sensor& sensor, bool boot) {
  _sd.chdir("/");  //The card's root
  _sd.chdir(_sn);  //Move into this logger's folder, named by its serial number
  File StatusFile = _sd.open(_fileNameStaC, FILE_WRITE);
  if (!StatusFile) return -1;
  StatusFile.print(_logTimeDate);
  StatusFile.print(',');
  StatusFile.print(trigger); StatusFile.print(',');
  sensor.printStatus(StatusFile, boot);
  StatusFile.println();
  StatusFile.close();
  return 0;
}

void NW_Logger::reportRows() {
  //A reset seen at a sensor's boot (kind 6) is what this logger causes by
  //powering the rail for the reading: no row. Any other boot report, and any
  //report captured with the reading, gets one. The first reading writes every
  //watched sensor's boot row regardless, as the record of what is on the bus.
  for (int8_t i = -1; i < (int8_t)_numSensors; i++) {
    NW_Sensor& s = (i < 0) ? *this : *_sensors[i]; //The logger itself first
    uint8_t bootKind = s.bootReportKind();
    if (!_deviceBootRows || (bootKind != 0 && bootKind != 6)) statusRow("boot", s, true);
    s.clearBootReport();
    if (s.reportKind() != 0) statusRow("report", s, false);
  }
  _pages.acknowledge(); //The logger's own report is written; the next one may latch
  _deviceBootRows = true;
}

void NW_Logger::note(const char* word) {
  NW_BufferPrint p(_note, NOTE_CAPACITY, true);
  if (p.length() > 0) p.print(';');
  p.print(word);
  if (p.truncated()) _pages.latchNotice(0xF3);   //NoteTruncated
  Serial.print(F("Note: "));
  Serial.println(word);
  LED_Color(ORANGE);
  delay(300);
  LED_Color(OFF);
}

void NW_Logger::buttonLog() {
  // ISR to respond to pressing log button and waking device from sleep
  // and starting log
  manualLog = true; //Set flag to manually record an additional data point
}

void NW_Logger::extIntCounter() {
  // ISR for an external event waking the logger
  detachInterrupt(digitalPinToInterrupt(extIntPin));
  // Set flag to just increment the counter and return to sleep
  extIntTripped = true;
}

void NW_Logger::writeDataToSD() {
  //Write global data to SD
  _logEvent = true; //Set flag for a log event
  _awakeCount = 0;
}

// ExtInt functions
void NW_Logger::setExtInt(uint8_t n, const char* header_entry) {
  extIntPin = n;
  ext_int_header_entry = header_entry;
}

uint16_t NW_Logger::getExtIntCount(bool reset0) {
  // ExtInt_count is a 16-bit volatile shared with isr2. On 8-bit AVR, a
  // 16-bit read is two instructions; if isr2 fires between them the read
  // is torn (e.g. low byte 0xFF before + high byte 0x01 after = 0x01FF
  // instead of the correct 0x0100). Folding the optional reset into the
  // same cli/sei block closes a second race: a tip arriving between a
  // separate read and reset would be silently zeroed out and lost.
  //
  // cli() defers rather than discards interrupts — any tip that arrives
  // during the critical section is held pending and fires immediately
  // after sei(), so it is correctly counted in the next reading.
  cli();
  uint16_t out = extIntCount;
  if (reset0) extIntCount = 0;
  sei();
  return out;
}

void NW_Logger::resetExtIntCount(uint16_t start) {
  // Protect the 16-bit write with cli/sei so isr2 cannot increment
  // ExtInt_count between the two store bytes. As with getExtIntCount,
  // any interrupt arriving during cli is deferred, not dropped.
  cli();
  extIntCount = start;
  sei();
}

void NW_Logger::dateTimeSD(uint16_t* date, uint16_t* time) {
  // return date using FAT_DATE macro to format fields
  *date = FAT_DATE(selfPointer->_rtc.getValue(0) + 2000,
                   selfPointer->_rtc.getValue(1),
                   selfPointer->_rtc.getValue(2));

  // return time using FAT_TIME macro to format fields
  *time = FAT_TIME(selfPointer->_rtc.getValue(3),
                   selfPointer->_rtc.getValue(4),
                   selfPointer->_rtc.getValue(5));
}

void NW_Logger::isr0() { selfPointer->buttonLog(); }
void NW_Logger::isr1() { selfPointer->writeDataToSD(); }
void NW_Logger::isr2() { selfPointer->extIntCounter(); }
