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

volatile uint8_t ExtIntPin = 255; // external interrupt pin; 255 = not set
String ext_int_header_entry;
volatile bool ExtIntTripped = false; // Global for the external interrupt
volatile uint16_t ExtInt_count = 0; // Global for the external interrupt

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

void NW_Logger::acceptAddresses(uint8_t *vals, uint8_t numVals, String header_) {
  i2cTruncated = (numVals > sizeof(I2C_ADR));
  NumADR = min(numVals, (uint8_t)sizeof(I2C_ADR));
  for (uint8_t i = 0; i < NumADR; i++) I2C_ADR[i] = vals[i];
  if (ExtIntPin == 255) {
    Header = header_; //Copy user defined header
  }
  else {
    Header = header_ + ext_int_header_entry;
  }
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
  Pages.loadStored(page0); //The logger's own pages: 0 and 1 from EEPROM; 2 and 3 filled at each reading
  const uint8_t* p0 = Pages.page;
  bool schema1 = Pages.page0Valid();
  int snStart = schema1 ? page0 + 0x10 : EEPROMLen - 8;
  for (int i = snStart; i < snStart + 8; i++) {  //Read out Serial Number
    val = EEPROM.read(i);  //Read SN values as individual bytes from EEPROM
    // Load upper and lower nibbles of each EEPROM byte into SN string,
    // post-incrementing the position index each time
    SN[pos++] = HexMap[(val >> 4)];
    SN[pos++] = HexMap[(val % 0x10)];
    if ((i - snStart) % 2 == 1 && i < snStart + 7) {
      SN[pos++] = '-';  //Place - between each SN category, post inc pos
    }
    SN[19] = '\0'; //Null terminate string
  }

  Serial.print(SN); //Print compiled string
  if (schema1) {
    Serial.print("  (Schema 1, HW v");
    Serial.print(p0[0x08]); Serial.print("."); Serial.print(p0[0x09]); Serial.print(")");
    HWVersion = String(p0[0x08]) + "." + String(p0[0x09]); //For the status file's boot row
  }
  if (!schema1) Pages.latchFault(0xE3); //Page 0 invalid: unprovisioned or corrupt
  if (strcmp(SN, "FFFF-FFFF-FFFF-FFFF") == 0)
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
    RTC.setTime(2000 + dateTimeVals[0], dateTimeVals[1], dateTimeVals[2],
                dateTimeVals[3], dateTimeVals[4], dateTimeVals[5]);
    Pages.latchNotice(0x30); //ClockSet
  }

  getTime(); //Get time to pass to computer
  Serial.print("\nTimestamp = ");
  Serial.println(LogTimeDate);
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

  if (OnBoardError) {
    LED_Color(RED); //On board failure
    delay(2000);
  }
  if (SensorError) {
    LED_Color(ORANGE);  //Sensor failure
    delay(2000);
  }
  if (TimeError) {
    LED_Color(CYAN); //Time set error
    delay(2000);
  }
  if (SDCardMissing) {
    LED_Color(PURPLE); //Sd card not inserted
    delay(2000);
  }
  // Battery voltage is below level where hardware functionality
  // can be guaranteed
  if (BatError) {
    for (int i = 0; i < 10; i++) {
      LED_Color(RED);
      delay(100);
      LED_Color(OFF);
      delay(100);
    }
  }

  // Battery charge % is at a concerning level; recommend replacing batteries
  if (BatWarning && !BatError) {
    for (int i = 0; i < 10; i++) {
      LED_Color(GOLD); //Low battery charge warning
      delay(100);
      LED_Color(OFF);
      delay(100);
    }
  }
  //Include battery error in test??
  if (!OnBoardError && !SensorError && !TimeError && !SDCardMissing) {
    LED_Color(GREEN);
    delay(2000);
  }

  Serial.print("\nReady to Log...\n\n");
}

void NW_Logger::attachExtInt() {
  if (ExtIntPin != 255) {
    pinMode(ExtIntPin, INPUT);
    digitalWrite(ExtIntPin, HIGH);
    attachInterrupt(digitalPinToInterrupt(ExtIntPin), NW_Logger::isr2, FALLING);
  }
}

bool NW_Logger::begin(String header_) {
  uint8_t dummy[1] = {0};
  return begin(dummy, 0, header_); //Call generalized begin function
}

void NW_Logger::I2Ctest() {
  bool initialStateExternalI2C = digitalRead(I2C_SW);

  switchExternalI2C(ON);

  int error = 0;
  bool i2cTest = true;

  Serial.print("I2C: ");
  if (i2cTruncated)
    Serial.println(F("WARNING: address list truncated to 128"));
  for (int i = 0; i < NumADR; i++) {
    Wire.beginTransmission(I2C_ADR[i]);
    error = Wire.endTransmission();
    if (error != 0) {
      if (i2cTest) Serial.println(" Fail");
      Serial.print("   Fail At: ");
      Serial.println(I2C_ADR[i], HEX);
      i2cTest = false;
      SensorError = true;
    }
  }

  //Switch to connect to onboard I2C!
  switchExternalI2C(OFF);

  for (int i = 0; i < NumADR_OB; i++) {
    Wire.beginTransmission(I2C_ADR_OB[i]);
    error = Wire.endTransmission();
    if (error != 0) {
      if (i2cTest) Serial.println(" Fail");
      Serial.print("   Fail At: ");
      Serial.println(I2C_ADR_OB[i], HEX);
      i2cTest = false;
      OnBoardError = true;
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
    SDTestFailed = true;
    SDCardMissing = true; //Card not inserted
  }
  else if (!SD.begin(SD_CS)) {
    OnBoardError = true;
    sdTestFailed = true;
    SDTestFailed = true;
  }

  // If card is present and initialised successfully, do the following:
  if (!cardNotPresent && !sdTestFailed) {
    SD.chdir("/"); //The card's root
    SD.mkdir(SN); //Make directory with serial number as name: everything this logger writes lives in it
    SD.chdir(SN); //Move into this directory
    String fileNameTest = "HWTest";
    (fileNameTest + ".txt").toCharArray(FileNameTestC, 11);
    SD.remove(FileNameTestC); //Remove any previous files

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
    File dataWrite = SD.open(FileNameTestC, FILE_WRITE);
    if (dataWrite) {
      dataWrite.println(randVal);
      dataWrite.println("\nHe was a man. Take him for all in all.");
      dataWrite.println("I shall not look upon his like again.");
      dataWrite.println("-Hamlet, Act 1, Scene 2");
    }
    dataWrite.close();
    char testDigits[6] = {0};
    File dataRead = SD.open(FileNameTestC, FILE_READ);
    if (dataRead) {
      dataRead.read(testDigits, randLength);
      for (int i = 0; i < randLength - 1; i++){ //Test random value string
        if (testDigits[i] != randDigits[i]) {
          sdTestFailed = true;
          SDTestFailed = true;
          OnBoardError = true;
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
  Wire.beginTransmission(I2C_ADR_OB[0]);
  Wire.write(0xFF);
  error = Wire.endTransmission();

  if (error == 0) {
    getTime(); //FIX!
    testSeconds = RTC.getValue(5);
    delay(1100);
    if (RTC.getValue(5) == testSeconds) {
      OnBoardError = true; // If clock is not incrementing
      ClockError = true;
      oscStop = true;      // Oscillator not running
      Serial.println(" FAIL (oscillator stopped)");
    }
    if (!oscStop) {
      // DS3231 powers on at Jan 1, 2000 (year register = 0). If the year
      // is still 0, the clock has not been set — timestamps will read as
      // 2000, which is an obviously wrong but identifiable sentinel value.
      unsigned int yearNow = RTC.getValue(0);
      if (yearNow == 0) {
        TimeError = true;
        Serial.println(" PASS, BAD TIME");
      } else {
        Serial.println(" PASS");
      }
    }
  } else {
    Serial.println(" FAIL");
    OnBoardError = true;
    ClockError = true;
  }
}

void NW_Logger::initLogFile() {
  SD.chdir("/");  //The card's root
  SD.chdir(SN);  //Move into this logger's folder, named by its serial number
  //Find the first unused file number in "SD:/sn/"
  char numCharArray[6];
  String fileName = "log";
  int fileNum = 1;
  sprintf(numCharArray, "%05d", fileNum);
  (fileName + String(numCharArray) + ".csv").toCharArray(FileNameC, 13);
  while (SD.exists(FileNameC)) {
    fileNum += 1;
    sprintf(numCharArray, "%05d", fileNum);
    (fileName + String(numCharArray) + ".csv").toCharArray(FileNameC, 13);
  }
  ("sta" + String(numCharArray) + ".csv").toCharArray(FileNameStaC, 13); //The status file, same number
  if (FileNum != 0) Pages.latchNotice(0xF1); //NewLogFile: a later pair, not the first
  FileNum = fileNum;
  Serial.print("FileNameC: ");
  Serial.println(FileNameC);
  // The status file: one row per report, boot and check, from this logger and
  // from every device on it (NW-Device-Specification Report register). Its
  // boot row carries what the data file's first line used to: library
  // version and serial number, with the hardware version beside them.
  statusStr("Time,Trigger,Device,Serial,HW,FW,FWCommit,Lib,LibCommit,Code,Note,Page0,Page1,Page2"); //The logger's own boot row follows at the first reading (it watches itself)
  // The data file starts with its header row, which the logger builds from
  // its on-board columns and the sketch's Header (dataHeader()).
  logStr(dataHeader());
}

int NW_Logger::logStr(String val) {
  Serial.println(val); //Echo to serial monitor
  SD.chdir("/");  //The card's root
  SD.chdir(SN);  //Move into this logger's folder, named by its serial number
  File DataFile = SD.open(FileNameC, FILE_WRITE);

  // if the file is available, write to it:
  if (DataFile) {
    DataFile.println(val);
    SDIndex = DataFile.position(); //Where the next row starts, for a logger that reads rows back
    DataFile.close();
    return 0;
  }
  // if the file isn't open, pop up an error:
  else {
    return -1;
  }
}

int NW_Logger::statusStr(String val) {
  Serial.println(val); //Echo to serial monitor
  SD.chdir("/");  //The card's root
  SD.chdir(SN);  //Move into this logger's folder, named by its serial number
  File StatusFile = SD.open(FileNameStaC, FILE_WRITE);
  if (StatusFile) {
    StatusFile.println(val);
    StatusFile.close();
    return 0;
  }
  else {
    return -1;
  }
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
  int y = RTC.getValue(0), mo = RTC.getValue(1), d = RTC.getValue(2);
  int32_t yy = y - (mo <= 2 ? 1 : 0);
  int32_t era = (yy >= 0 ? yy : yy - 399) / 400;
  uint32_t yoe = (uint32_t)(yy - era * 400);
  uint32_t doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  uint32_t days = (uint32_t)(era * 146097 + (int32_t)doe - 719468);
  return days * 86400UL + (uint32_t)RTC.getValue(3) * 3600UL + (uint32_t)RTC.getValue(4) * 60UL + (uint32_t)RTC.getValue(5);
}

void NW_Logger::getTime() {
  //Update global time string
  LogTimeDate = RTC.getTime(0);
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
void NW_Logger::run(String (*update)(void), unsigned long logInterval) {
  LogInterval = logInterval; //Served on Page 3
  // Print note that that logging has started
  // Serial.println("Log Started!"); //DEBUG!
  // Serial.println(millis()); //DEBUG!
  if (NewLog) {
    // LogEvent = true;
    RTC.setAlarm(logInterval);
    initLogFile(); //Start a new file each time log button is pressed
    //Add inital data point
    addDataPoint(update);
    NewLog = false;  //Clear flag once log is started
    blinkGood();  //Alert user to start of log
    resetWDT(); //Clear alarm
  }

  if (LogEvent) {
    // Serial.println("Log Event!"); //DEBUG!
    // RTC.setAlarm(logInterval);  //Set/reset alarm //DEBUG!
    addDataPoint(update); //Write values to SD
    afterLogEvent(); //A board's follow-up to the alarm-driven row (Okapi's backhaul)
    LogEvent = false; //Clear log flag
    RTC.setAlarm(logInterval);  //Set/reset alarm
    resetWDT(); //Clear alarm
  }

  // Write data to SD card without interrupting existing timing cycle
  if (manualLog) {
    // Serial.println("Click!"); //DEBUG!
    addDataPoint(update); //write values to SD
    manualLog = false; //Clear log flag
    resetWDT(); //Clear alarm
  }

  if (ExtIntTripped) {  // Defaults to just counter for now
    // Serial.println("TIP!"); //DEBUG!
    ExtInt_count ++;
    ExtIntTripped = false; // Clear interrupt flag
    resetWDT(); //Clear alarm
    delay(150); //Hard-code for now; tipping bucket "debounce"
    attachInterrupt(digitalPinToInterrupt(ExtIntPin), NW_Logger::isr2, FALLING);
  }

  if (!digitalRead(RTCInt)) {  //Catch alarm if not reset properly
    Serial.println("Reset Alarm"); //DEBUG!
    RTC.setAlarm(logInterval); //Turn alarm back on
  }

  AwakeCount++;

  // @bschulz1701: AwakeCount was designed to give ~5 run() iterations after
  // an RTC wake before returning to sleep, reset to 0 by the RTC ISR
  // (writeDataToSD). Since addDataPoint() blocks within a single run() call,
  // the 5-count may be unnecessary. Consider simplifying or removing.
  if (AwakeCount > 5) {
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

uint8_t NW_Logger::reportKind()    { return Pages.report().kind(); }
bool    NW_Logger::reportIsFault() { return Pages.report().isFault(); }
uint8_t NW_Logger::bootReportKind() { return BootReport.kind(); }
void    NW_Logger::clearBootReport() { BootReport.code = 0; BootReport.status = 0; }

size_t NW_Logger::printFileHeader(Print& out) {
  // The logger's own columns first, then each watched sensor's in watch order,
  // which is also column order. Note is always the last column and carries no
  // comma after it: every sensor ends its fields with a comma for the next, so
  // this ends the row.
  size_t n = printDataHeader(out);
  for (uint8_t i = 0; i < NumWatched; i++) {
    // A sketch may watch the logger for the status file. Its columns are
    // already written above, and printing them twice would be silent.
    if (Watched[i] == this) continue;
    n += Watched[i]->printDataHeader(out);
  }
  if (ExtIntPin != 255) n += out.print(ext_int_header_entry);
  n += out.print("Note");
  return n;
}

String NW_Logger::dataHeader() {
  // Note is always the last column and carries no comma after it: every
  // sensor ends its fields with a comma for the next, so this ends the row.
  String h;
  NW_StringPrint p(h);
  printDataHeader(p);
  h += Header;
  h += "Note";
  return h;
}

String NW_Logger::getOnBoardVals() {
  // The reading, then the row: printDataRow() prints what readOnBoard() left,
  // which is what lets the same row reach two sinks without reading twice.
  readOnBoard();
  String s;
  NW_StringPrint p(s);
  printDataRow(p);
  return s;
}

bool NW_Logger::watch(NW_Sensor& sensor) {
  if (NumWatched >= MaxWatched) return false;
  Watched[NumWatched++] = &sensor;
  return true;
}

int NW_Logger::statusRow(const char* trigger, NW_Sensor& sensor, bool boot) {
  SD.chdir("/");  //The card's root
  SD.chdir(SN);  //Move into this logger's folder, named by its serial number
  File StatusFile = SD.open(FileNameStaC, FILE_WRITE);
  if (!StatusFile) return -1;
  StatusFile.print(LogTimeDate); StatusFile.print(',');
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
  for (int8_t i = -1; i < (int8_t)NumWatched; i++) {
    NW_Sensor& s = (i < 0) ? *this : *Watched[i]; //The logger itself first
    uint8_t bootKind = s.bootReportKind();
    if (!DeviceBootRows || (bootKind != 0 && bootKind != 6)) statusRow("boot", s, true);
    s.clearBootReport();
    if (s.reportKind() != 0) statusRow("report", s, false);
  }
  Pages.acknowledge(); //The logger's own report is written; the next one may latch
  DeviceBootRows = true;
}

void NW_Logger::note(const String& word) {
  if (Note.length() > 0) Note += ";";
  Note += word;
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
  detachInterrupt(digitalPinToInterrupt(ExtIntPin));
  // Set flag to just increment the counter and return to sleep
  ExtIntTripped = true;
}

void NW_Logger::writeDataToSD() {
  //Write global data to SD
  LogEvent = true; //Set flag for a log event
  AwakeCount = 0;
}

// ExtInt functions
void NW_Logger::setExtInt(uint8_t n, String header_entry) {
  ExtIntPin = n;
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
  uint16_t out = ExtInt_count;
  if (reset0) ExtInt_count = 0;
  sei();
  return out;
}

void NW_Logger::resetExtIntCount(uint16_t start) {
  // Protect the 16-bit write with cli/sei so isr2 cannot increment
  // ExtInt_count between the two store bytes. As with getExtIntCount,
  // any interrupt arriving during cli is deferred, not dropped.
  cli();
  ExtInt_count = start;
  sei();
}

void NW_Logger::dateTimeSD(uint16_t* date, uint16_t* time) {
  // return date using FAT_DATE macro to format fields
  *date = FAT_DATE(selfPointer->RTC.getValue(0) + 2000,
                   selfPointer->RTC.getValue(1),
                   selfPointer->RTC.getValue(2));

  // return time using FAT_TIME macro to format fields
  *time = FAT_TIME(selfPointer->RTC.getValue(3),
                   selfPointer->RTC.getValue(4),
                   selfPointer->RTC.getValue(5));
}

void NW_Logger::isr0() { selfPointer->buttonLog(); }
void NW_Logger::isr1() { selfPointer->writeDataToSD(); }
void NW_Logger::isr2() { selfPointer->extIntCounter(); }
