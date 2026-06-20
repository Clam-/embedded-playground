// #################################
// COMPILER OPTIONS, debug etc.
// #################################
//#define DEBUG
//#define SERIALOUT
//#define MONITOR
//#define FIRSTTIMESETUP
//#define FAST
//#define SLOW
#define NORM

// #################################
// INCLUDES, REQUIREMENTS
// #################################
#include <Arduino.h>
#include <Wire.h>

// #################################
// DEFINITIONS
// #################################

class StaticMCP4725 {
  public:
    bool begin(byte address) {
      _address = address;
      Wire.begin();
      Wire.beginTransmission(_address);
      return Wire.endTransmission() == 0;
    }

    bool setVoltage(uint16_t output, bool writeEEPROM) {
      byte command = writeEEPROM ? B01100000 : B01000000;

      Wire.beginTransmission(_address);
      Wire.write(command);
      Wire.write(output >> 4);
      Wire.write((output & 0x0F) << 4);
      return Wire.endTransmission() == 0;
    }

  private:
    byte _address = 0;
};

class StaticModbusSerial {
  public:
    StaticModbusSerial(Stream &port, byte slaveId) : _stream(&port), _slaveId(slaveId) {}

    void config(unsigned long baud) {
      if (baud > 19200) {
        _t15 = 750;
        _t35 = 1750;
      } else {
        _t15 = 16500000UL / baud;
        _t35 = 38500000UL / baud;
      }
    }

    bool addIsts(word offset, bool value = false) {
      return addReg(offset + ISTS_OFFSET, value ? 0xFF00 : 0x0000);
    }

    bool addHreg(word offset, word value = 0) {
      return addReg(offset + HREG_OFFSET, value);
    }

    bool addIreg(word offset, word value = 0) {
      return addReg(offset + IREG_OFFSET, value);
    }

    bool setIsts(word offset, bool value) {
      return setReg(offset + ISTS_OFFSET, value ? 0xFF00 : 0x0000);
    }

    bool setHreg(word offset, word value) {
      return setReg(offset + HREG_OFFSET, value);
    }

    bool setIreg(word offset, word value) {
      return setReg(offset + IREG_OFFSET, value);
    }

    word hreg(word offset) {
      return reg(offset + HREG_OFFSET);
    }

    void task() {
      byte frameLength = readFrame();
      if (frameLength == 0) { return; }
      if (_frame[0] != _slaveId && _frame[0] != 0) { return; }
      if (!validCrc(frameLength)) { return; }

      bool broadcast = _frame[0] == 0;
      byte functionCode = _frame[1];
      byte response[MAX_FRAME_LENGTH];
      byte responseLength = 0;

      switch (functionCode) {
        case 0x02:
          responseLength = readBits(functionCode, ISTS_OFFSET, response);
          break;
        case 0x03:
          responseLength = readRegisters(functionCode, HREG_OFFSET, response);
          break;
        case 0x04:
          responseLength = readRegisters(functionCode, IREG_OFFSET, response);
          break;
        case 0x06:
          responseLength = writeSingleRegister(response);
          break;
        case 0x10:
          responseLength = writeMultipleRegisters(response);
          break;
        default:
          responseLength = exceptionResponse(functionCode, 0x01, response);
          break;
      }

      if (!broadcast && responseLength > 0) {
        sendResponse(response, responseLength);
      }
    }

  private:
    struct Register {
      uint32_t address;
      word value;
    };

    static const byte MAX_REGISTERS = 16;
    static const byte MAX_FRAME_LENGTH = 128;
    static const uint32_t ISTS_OFFSET = 10001;
    static const uint32_t IREG_OFFSET = 30001;
    static const uint32_t HREG_OFFSET = 40001;

    Stream *_stream;
    byte _slaveId;
    unsigned long _t15 = 750;
    unsigned long _t35 = 1750;
    Register _registers[MAX_REGISTERS];
    byte _registerCount = 0;
    byte _frame[MAX_FRAME_LENGTH];

    bool addReg(uint32_t address, word value) {
      Register *existing = findReg(address);
      if (existing) {
        existing->value = value;
        return true;
      }

      if (_registerCount >= MAX_REGISTERS) { return false; }

      _registers[_registerCount].address = address;
      _registers[_registerCount].value = value;
      _registerCount++;
      return true;
    }

    bool setReg(uint32_t address, word value) {
      Register *target = findReg(address);
      if (!target) { return false; }

      target->value = value;
      return true;
    }

    word reg(uint32_t address) {
      Register *target = findReg(address);
      if (!target) { return 0; }

      return target->value;
    }

    Register *findReg(uint32_t address) {
      for (byte i = 0; i < _registerCount; i++) {
        if (_registers[i].address == address) {
          return &_registers[i];
        }
      }

      return nullptr;
    }

    byte readFrame() {
      if (_stream->available() <= 0) { return 0; }

      byte length = 0;
      do {
        while (_stream->available() > 0) {
          int data = _stream->read();
          if (data < 0) { break; }
          if (length < MAX_FRAME_LENGTH) {
            _frame[length++] = data;
          }
        }
        delayMicroseconds(_t15);
      } while (_stream->available() > 0);

      if (length < 4 || length >= MAX_FRAME_LENGTH) { return 0; }
      return length;
    }

    bool validCrc(byte frameLength) {
      uint16_t expected = crc16(_frame, frameLength - 2);
      uint16_t wireStandard = _frame[frameLength - 2] | (_frame[frameLength - 1] << 8);
      uint16_t wireReversed = (_frame[frameLength - 2] << 8) | _frame[frameLength - 1];

      return expected == wireStandard || expected == wireReversed;
    }

    byte readRegisters(byte functionCode, uint32_t offset, byte *response) {
      word start = (_frame[2] << 8) | _frame[3];
      word count = (_frame[4] << 8) | _frame[5];
      if (count < 1 || count > 0x7D) {
        return exceptionResponse(functionCode, 0x03, response);
      }

      byte responseLength = 2 + count * 2;
      if (responseLength > MAX_FRAME_LENGTH - 3) {
        return exceptionResponse(functionCode, 0x03, response);
      }

      response[0] = functionCode;
      response[1] = count * 2;
      for (word i = 0; i < count; i++) {
        Register *source = findReg(offset + start + i);
        if (!source) {
          return exceptionResponse(functionCode, 0x02, response);
        }

        response[2 + i * 2] = source->value >> 8;
        response[3 + i * 2] = source->value & 0xFF;
      }

      return responseLength;
    }

    byte readBits(byte functionCode, uint32_t offset, byte *response) {
      word start = (_frame[2] << 8) | _frame[3];
      word count = (_frame[4] << 8) | _frame[5];
      if (count < 1 || count > 0x07D0) {
        return exceptionResponse(functionCode, 0x03, response);
      }

      byte byteCount = count / 8;
      if (count % 8) { byteCount++; }
      if (byteCount > MAX_FRAME_LENGTH - 5) {
        return exceptionResponse(functionCode, 0x03, response);
      }

      response[0] = functionCode;
      response[1] = byteCount;
      for (byte i = 0; i < byteCount; i++) {
        response[2 + i] = 0;
      }

      for (word i = 0; i < count; i++) {
        Register *source = findReg(offset + start + i);
        if (!source) {
          return exceptionResponse(functionCode, 0x02, response);
        }

        if (source->value == 0xFF00) {
          bitSet(response[2 + (i / 8)], i % 8);
        }
      }

      return 2 + byteCount;
    }

    byte writeSingleRegister(byte *response) {
      word address = (_frame[2] << 8) | _frame[3];
      word value = (_frame[4] << 8) | _frame[5];
      if (!setReg(HREG_OFFSET + address, value)) {
        return exceptionResponse(0x06, 0x02, response);
      }

      response[0] = 0x06;
      response[1] = _frame[2];
      response[2] = _frame[3];
      response[3] = _frame[4];
      response[4] = _frame[5];
      return 5;
    }

    byte writeMultipleRegisters(byte *response) {
      word start = (_frame[2] << 8) | _frame[3];
      word count = (_frame[4] << 8) | _frame[5];
      byte byteCount = _frame[6];
      if (count < 1 || count > 123 || byteCount != count * 2) {
        return exceptionResponse(0x10, 0x03, response);
      }

      for (word i = 0; i < count; i++) {
        if (!findReg(HREG_OFFSET + start + i)) {
          return exceptionResponse(0x10, 0x02, response);
        }
      }

      for (word i = 0; i < count; i++) {
        word value = (_frame[7 + i * 2] << 8) | _frame[8 + i * 2];
        setReg(HREG_OFFSET + start + i, value);
      }

      response[0] = 0x10;
      response[1] = _frame[2];
      response[2] = _frame[3];
      response[3] = _frame[4];
      response[4] = _frame[5];
      return 5;
    }

    byte exceptionResponse(byte functionCode, byte exceptionCode, byte *response) {
      response[0] = functionCode | 0x80;
      response[1] = exceptionCode;
      return 2;
    }

    void sendResponse(byte *response, byte responseLength) {
      byte out[MAX_FRAME_LENGTH];
      byte outLength = responseLength + 3;
      if (outLength > MAX_FRAME_LENGTH) { return; }

      out[0] = _slaveId;
      for (byte i = 0; i < responseLength; i++) {
        out[1 + i] = response[i];
      }

      uint16_t crc = crc16(out, responseLength + 1);
      out[responseLength + 1] = crc & 0xFF;
      out[responseLength + 2] = crc >> 8;
      _stream->write(out, outLength);
      _stream->flush();
      delayMicroseconds(_t35);
    }

    uint16_t crc16(byte *buffer, byte length) {
      uint16_t crc = 0xFFFF;

      for (byte i = 0; i < length; i++) {
        crc ^= buffer[i];
        for (byte j = 0; j < 8; j++) {
          if (crc & 0x0001) {
            crc = (crc >> 1) ^ 0xA001;
          } else {
            crc = crc >> 1;
          }
        }
      }

      return crc;
    }
};

StaticMCP4725 exDAC1;
StaticMCP4725 exDAC2;

// Modbus config
#define SLAVE_ID   1
#define MODBUS_BAUD 38400
#define MODBUS_SERIAL Serial
#define DEBUG_SERIAL Serial
#define DEBUG_BAUD 115200
// Offsets are actually -1 from CAS Modbus Scanner
const int SPIN_STATUS = 2128; // 12129
const int RAM1_REG    = 641; // 40641
const int RAM2_REG    = 640; // 40642
const int MAINSH_REG  = 642; // 40643
const int TILL_REG    = 643; // 40644
const int HEEL_REG    = 644; // 40645
StaticModbusSerial mb(MODBUS_SERIAL, SLAVE_ID);

const unsigned int ENCEXT = B100000;
unsigned int OPS = 0;
unsigned int PREVOPS = 0;
unsigned long PREVTIME = 0;
unsigned long PREVTIMEOPS = 0;
unsigned long MONTIME = 50;
unsigned long OPSTIME = 1000;
unsigned long RAMCOOLTIME = 0;
bool RAMON = true;
unsigned long RAMCOOLDURATION = 1500;
unsigned long DEBUG_LOOP_COUNT = 0;
unsigned long DEBUG_LAST_STATUS_TIME = 0;
const unsigned long DEBUG_STATUS_INTERVAL_MS = 1000;
const char *DEBUG_LOOP_STAGE = "boot";
bool DEBUG_TRACE_FIRST_LOOP = true;
unsigned int ENC1EXT = ENCEXT;
unsigned int ENC2EXT = ENCEXT;
unsigned int ENC3EXT = ENCEXT;

// #################################
// PIN CONFIGURATION
// #################################
const int SPIN_BUTTON_PIN = 12; // Spinnaker
const int TEST_LED_PIN = LED_BUILTIN;
const unsigned long TEST_LED_TOGGLE_INTERVAL_MS = 1000;
bool TEST_LED_STATE = false;
unsigned long TEST_LED_LAST_TOGGLE_TIME = 0;

// Encoder 1  MainSheet
const int SENC1_CS = 4;      // Blue
const int SENC1_DATA = 3;    // Orange
const int SENC1_CLOCK = 2;   // Yellow
// Encoder 2  Rudder
const int SENC2_CS = 7;     // Blue
const int SENC2_DATA = 6;    // Orange
const int SENC2_CLOCK = 5;   // Yellow
// Encoder 3  Heel
const int SENC3_CS = 10;     // Blue
const int SENC3_DATA = 9;   // Orange
const int SENC3_CLOCK = 8;  // Yellow

int FI = 0;
bool FLASH() {
  if (FI > 10) {return true;}
  return false;
}
void FLASH_ITER() {
  if (FI > 20) { FI = 0; }
  else { FI++; }
}

void initEncSlow(int csPin, int clkPin, int dPin);

void debugBegin() {
#ifdef DEBUG
  DEBUG_SERIAL.begin(DEBUG_BAUD);
#endif
}

void debugWaitForSerial() {
#if defined(DEBUG) && defined(DEBUG_WAIT_FOR_SERIAL)
  while (!DEBUG_SERIAL) {
    delay(10);
  }
  delay(500);
#endif
}

void debugLog(const char *message) {
#ifdef DEBUG
  DEBUG_SERIAL.print(millis());
  DEBUG_SERIAL.print("ms ");
  DEBUG_SERIAL.println(message);
  DEBUG_SERIAL.flush();
#endif
}

void debugLoopStage(const char *stage) {
#ifdef DEBUG
  DEBUG_LOOP_STAGE = stage;
  if (DEBUG_TRACE_FIRST_LOOP) {
    DEBUG_SERIAL.print(millis());
    DEBUG_SERIAL.print("ms loop-stage: ");
    DEBUG_SERIAL.println(stage);
  }
#endif
}

void debugLoopStatus(unsigned int enciter, unsigned int ram1, unsigned int ram2) {
#ifdef DEBUG
  unsigned long currentTime = millis();
  if (currentTime - DEBUG_LAST_STATUS_TIME < DEBUG_STATUS_INTERVAL_MS) { return; }

  DEBUG_LAST_STATUS_TIME = currentTime;
  DEBUG_SERIAL.print(currentTime);
  DEBUG_SERIAL.print("ms heartbeat loop=");
  DEBUG_SERIAL.print(DEBUG_LOOP_COUNT);
  DEBUG_SERIAL.print(" stage=");
  DEBUG_SERIAL.print(DEBUG_LOOP_STAGE);
  DEBUG_SERIAL.print(" led=");
  DEBUG_SERIAL.print(TEST_LED_STATE ? "on" : "off");
  DEBUG_SERIAL.print(" enciter=");
  DEBUG_SERIAL.print(enciter);
  DEBUG_SERIAL.print(" ram1=");
  DEBUG_SERIAL.print(ram1);
  DEBUG_SERIAL.print(" ram2=");
  DEBUG_SERIAL.print(ram2);
  DEBUG_SERIAL.print(" ramon=");
  DEBUG_SERIAL.print(RAMON ? "true" : "false");
  DEBUG_SERIAL.print(" ops=");
  DEBUG_SERIAL.println(OPS);
#endif
}

void debugFinishFirstLoopTrace() {
#ifdef DEBUG
  if (!DEBUG_TRACE_FIRST_LOOP) { return; }

  DEBUG_TRACE_FIRST_LOOP = false;
  debugLog("loop: first pass complete");
#endif
}

void setTestLed(bool state) {
  TEST_LED_STATE = state;
  digitalWrite(TEST_LED_PIN, TEST_LED_STATE ? HIGH : LOW);
}

void initTestLed() {
  pinMode(TEST_LED_PIN, OUTPUT);
  TEST_LED_LAST_TOGGLE_TIME = millis();
  setTestLed(true);
}

void updateTestLed() {
  unsigned long currentTime = millis();
  if (currentTime - TEST_LED_LAST_TOGGLE_TIME < TEST_LED_TOGGLE_INTERVAL_MS) { return; }

  TEST_LED_LAST_TOGGLE_TIME = currentTime;
  setTestLed(!TEST_LED_STATE);
  debugLog(TEST_LED_STATE ? "led: on" : "led: off");
}

void initEncFast() {
  initEncSlow(SENC1_CS, SENC1_CLOCK, SENC1_DATA);
  initEncSlow(SENC2_CS, SENC2_CLOCK, SENC2_DATA);
  initEncSlow(SENC3_CS, SENC3_CLOCK, SENC3_DATA);
}

void initEncSlow(int csPin, int clkPin, int dPin) {
  pinMode(csPin, OUTPUT);
  pinMode(clkPin, OUTPUT);
  pinMode(dPin, INPUT);
  digitalWrite(clkPin, HIGH);
  digitalWrite(csPin, LOW);
}

void setup() {
  initTestLed();

  debugBegin();
  debugWaitForSerial();
  debugLog("setup: start");
  debugLog("setup: led on");

#ifdef SERIALOUT
  DEBUG_SERIAL.println("Connected "); 
#endif
#ifdef MONITOR
  debugLog("setup: monitor output enabled");
#endif

  debugLog("setup: modbus serial begin");
  MODBUS_SERIAL.begin(MODBUS_BAUD, SERIAL_8E1);
  debugLog("setup: modbus config");
  mb.config(MODBUS_BAUD);
  debugLog("setup: add SPIN_STATUS");
  mb.addIsts(SPIN_STATUS, false);
  debugLog("setup: added SPIN_STATUS");
  debugLog("setup: add RAM1_REG");
  mb.addHreg(RAM1_REG);
  debugLog("setup: added RAM1_REG");
  debugLog("setup: add RAM2_REG");
  mb.addHreg(RAM2_REG);
  debugLog("setup: added RAM2_REG");
  debugLog("setup: add MAINSH_REG");
  mb.addIreg(MAINSH_REG);
  debugLog("setup: added MAINSH_REG");
  debugLog("setup: add TILL_REG");
  mb.addIreg(TILL_REG);
  debugLog("setup: added TILL_REG");
  debugLog("setup: add HEEL_REG");
  mb.addIreg(HEEL_REG);
  debugLog("setup: added HEEL_REG");
  debugLog("setup: add input register 0");
  mb.addIreg(0);
  debugLog("setup: added input register 0");
  debugLog("setup: add input register 1");
  mb.addIreg(1);
  debugLog("setup: added input register 1");
  // For Adafruit MCP4725A1 the address is 0x62 (default) or 0x63 (ADDR pin tied to VCC)
  // For MCP4725A0 the address is 0x60 or 0x61
  // For MCP4725A2 the address is 0x64 or 0x65
  debugLog("setup: dac1 begin");
  bool dac1Ready = exDAC1.begin(0x62);
  debugLog(dac1Ready ? "setup: dac1 ready" : "setup: dac1 not found");
  debugLog("setup: dac2 begin");
  bool dac2Ready = exDAC2.begin(0x63);
  debugLog(dac2Ready ? "setup: dac2 ready" : "setup: dac2 not found");
#ifdef FIRSTTIMESETUP
  debugLog("setup: dac voltage eeprom write");
  exDAC1.setVoltage(0, true);
  exDAC2.setVoltage(0, true);
#else
  debugLog("setup: dac voltage write");
  exDAC1.setVoltage(0, false);
  exDAC2.setVoltage(0, false);
#endif
  debugLog("setup: encoders init");
#ifdef FAST
  initEncFast(); // Init Encoders
#endif
#if defined(SLOW) | defined(NORM) 
  initEncSlow(SENC1_CS, SENC1_CLOCK, SENC1_DATA); // Init Encoder 1
  initEncSlow(SENC2_CS, SENC2_CLOCK, SENC2_DATA); // Init Encoder 2
  initEncSlow(SENC3_CS, SENC3_CLOCK, SENC3_DATA); // Init Encoder 3
#endif
  pinMode(SPIN_BUTTON_PIN, INPUT);
  PREVTIME = millis();
  debugLog("setup: complete");
}

int readEncoder(int csPin, int clkPin, int dPin) {
  digitalWrite(csPin, HIGH);
  digitalWrite(csPin, LOW);
  int pos = 0;
  for (int i=0; i<16; i++) { 
    digitalWrite(clkPin, LOW);
    digitalWrite(clkPin, HIGH);
   
    pos = pos | digitalRead(dPin);
    if (i<15) { pos = pos << 1; }
  }
  digitalWrite(clkPin, LOW);
  digitalWrite(clkPin, HIGH);
  return pos; // including extdata
}



int readENC1Fast() {
  digitalWrite(SENC1_CS, HIGH);
  digitalWrite(SENC1_CS, LOW);
  int pos = 0;
  for (int i=0; i<10; i++) {
    digitalWrite(SENC1_CLOCK, LOW);
    digitalWrite(SENC1_CLOCK, HIGH);
    pos = pos | digitalRead(SENC1_DATA);
    if (i<9) { pos = pos << 1; }
  }
  for (int i=0; i<6; i++) {
    digitalWrite(SENC1_CLOCK, LOW);
    digitalWrite(SENC1_CLOCK, HIGH);
  }
  digitalWrite(SENC1_CLOCK, LOW);
  digitalWrite(SENC1_CLOCK, HIGH);
  return pos;
}
int readENC2Fast() {
  digitalWrite(SENC2_CS, HIGH);
  digitalWrite(SENC2_CS, LOW);
  int pos = 0;
  for (int i=0; i<10; i++) {
    digitalWrite(SENC2_CLOCK, LOW);
    digitalWrite(SENC2_CLOCK, HIGH);
    pos = pos | digitalRead(SENC2_DATA);
    if (i<9) { pos = pos << 1; }
  }
  for (int i=0; i<6; i++) {
    digitalWrite(SENC2_CLOCK, LOW);
    digitalWrite(SENC2_CLOCK, HIGH);
  }
  digitalWrite(SENC2_CLOCK, LOW);
  digitalWrite(SENC2_CLOCK, HIGH);
  return pos;
}
int readENC3Fast() {
  digitalWrite(SENC3_CS, HIGH);
  digitalWrite(SENC3_CS, LOW);
  int pos = 0;
  for (int i=0; i<10; i++) {
    digitalWrite(SENC3_CLOCK, LOW);
    digitalWrite(SENC3_CLOCK, HIGH);
    pos = pos | digitalRead(SENC3_DATA);
    if (i<9) { pos = pos << 1; }
  }
  for (int i=0; i<6; i++) {
    digitalWrite(SENC3_CLOCK, LOW);
    digitalWrite(SENC3_CLOCK, HIGH);
  }
  digitalWrite(SENC3_CLOCK, LOW);
  digitalWrite(SENC3_CLOCK, HIGH);
  return pos;
}

// If this returns anything other than 0, there's parity error.
unsigned int parity_check(unsigned int v){
  //http://graphics.stanford.edu/~seander/bithacks.html#ParityNaive
  v ^= v >> 16;
  v ^= v >> 8;
  v ^= v >> 4;
  v &= 0xf;
  return (0x6996 >> v) & 1;
}

unsigned int ENCITER = 0; // 0 = enc1, 2 = enc2, 4 = enc3
bool WARMUP = true;
unsigned int enc1 = 0, enc2 = 0, enc3 = 0;
void loop() {
  unsigned int ram1 = 0, ram2 = 0;
  DEBUG_LOOP_COUNT++;
  debugLoopStage("mb.task");
  debugLoopStatus(ENCITER, ram1, ram2);
  mb.task();

  debugLoopStage("encoder-read");
#if defined(FAST)
  if (ENCITER == 0) { enc1 = readENC1Fast(); }
  if (ENCITER == 2) { enc2 = readENC2Fast(); }
  if (ENCITER == 4) { enc3 = readENC3Fast(); }
#elif defined(NORM)
  if (ENCITER == 0) { enc1 = readEncoder(SENC1_CS, SENC1_CLOCK, SENC1_DATA); }
  if (ENCITER == 2) { enc2 = readEncoder(SENC2_CS, SENC2_CLOCK, SENC2_DATA); }
  if (ENCITER == 4) { enc3 = readEncoder(SENC3_CS, SENC3_CLOCK, SENC3_DATA); }
#endif

// RAM Control
  //normal code
  debugLoopStage("ram-read");
  ram1 = mb.hreg(RAM1_REG);
  ram2 = mb.hreg(RAM2_REG);
  if (ram1 == 22 && ram2 == 22) {
    // lockout RAM for 1s
    RAMON = false;
    debugLoopStage("ram-lockout");
    exDAC1.setVoltage(0, false); exDAC2.setVoltage(0, false);
    RAMCOOLTIME = millis();
  }
  if (RAMON) {
    debugLoopStage("dac-write");
    exDAC1.setVoltage(ram1, false);
    exDAC2.setVoltage(ram2, false);
  } else {
    if (millis() - RAMCOOLTIME > RAMCOOLDURATION){
      RAMON = true;
    }
    mb.setHreg(RAM1_REG, 0); mb.setHreg(RAM2_REG, 0);
  }

#ifdef SERIALOUT
  DEBUG_SERIAL.print("RAM1:"); DEBUG_SERIAL.print(mb.hreg(RAM1_REG)); DEBUG_SERIAL.print("  RAM2:"); DEBUG_SERIAL.println(mb.hreg(RAM2_REG)); 
#endif
#ifdef MONITOR
  debugLoopStage("monitor");
  if (!WARMUP) {
    // check encoder extra bits.
    unsigned int enc1e = enc1 & B011110;
    unsigned int enc2e = enc2 & B011110;
    unsigned int enc3e = enc3 & B011110;
    if (parity_check(enc1)) {enc1e |= 1;}
    if (parity_check(enc2)) {enc2e |= 1;}
    if (parity_check(enc3)) {enc3e |= 1;}
    // add bit to error persistence
    if (enc1e != ENCEXT) { ENC1EXT = enc1e | ENC1EXT; }
    if (enc2e != ENCEXT) { ENC2EXT = enc2e | ENC2EXT; }
    if (enc3e != ENCEXT) { ENC3EXT = enc3e | ENC3EXT; }
    
    // ENCITER 5 means all encoders have been read
    if ((ENCITER == 5) && millis() - PREVTIMEOPS > MONTIME) {
      // Debug format: enc1  enc2  enc3  ram1  ram2  ops ext1 ext2 ext3
      //               BBBB BBBB BBBB BBBB BBBB OOO EE EE EE
      DEBUG_SERIAL.print(enc1, HEX); DEBUG_SERIAL.print(" "); DEBUG_SERIAL.print(enc2, HEX); DEBUG_SERIAL.print(" "); DEBUG_SERIAL.print(enc3, HEX); DEBUG_SERIAL.print(" "); 
      DEBUG_SERIAL.print(0, HEX); DEBUG_SERIAL.print(" "); DEBUG_SERIAL.print(0, HEX); DEBUG_SERIAL.print(" "); 
      DEBUG_SERIAL.print(PREVOPS, HEX); DEBUG_SERIAL.print(" "); DEBUG_SERIAL.print(ENC1EXT, HEX); DEBUG_SERIAL.print(" "); DEBUG_SERIAL.print(ENC2EXT, HEX);
      DEBUG_SERIAL.print(" "); DEBUG_SERIAL.println(ENC3EXT, HEX);
      PREVTIMEOPS = millis();
    }
  }
#endif

  // LED update
  //doENC1LEDs(enc1); doENC2LEDs(enc2); doENC3LEDs(enc3);

// spinnaker
  debugLoopStage("led");
  updateTestLed();
  // set modbus data
  debugLoopStage("modbus-registers");
  if (ENCITER == 5) { mb.setIsts(SPIN_STATUS, !digitalRead(SPIN_BUTTON_PIN)); }
  // shift to omit extra encoder bits
  if (ENCITER == 0) { mb.setIreg(MAINSH_REG, enc1 >> 6); } 
  if (ENCITER == 2) { mb.setIreg(TILL_REG, enc2 >> 6); }
  if (ENCITER == 4) { mb.setIreg(HEEL_REG, enc3 >> 6); }
  FLASH_ITER();
  // encoder read cycle
  ENCITER++;
  if (ENCITER > 5) { ENCITER = 0; WARMUP = false; }
  //ops calc
  debugLoopStage("ops");
  if (millis() - PREVTIME > OPSTIME){
    PREVOPS = OPS; OPS = 0;
    PREVTIME = millis();
  } else { OPS++; }
  debugLoopStage("loop-end");
  debugLoopStatus(ENCITER, ram1, ram2);
  debugFinishFirstLoopTrace();
}
