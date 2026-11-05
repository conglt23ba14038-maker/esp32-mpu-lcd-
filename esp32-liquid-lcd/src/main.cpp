#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "Config.h"
#include "LiquidMath.h"
constexpr float PI_F = Liquid::PI_F;
uint8_t lcdAddress = 0, mpuAddress = 0;
bool lcdReady = false;
float roll = 0, zeroRoll = 0, gyroBias = 0;
float tilt = 0, previousTilt = 0, wave = 0, waveSpeed = 0;
uint8_t previousCells[2][16];
uint32_t lastSample = 0, lastFrame = 0, lastLog = 0;

float limit(float v, float lo, float hi) { return fmaxf(lo, fminf(hi, v)); }
float wrap(float a) { return atan2f(sinf(a), cosf(a)); }

bool responds(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(mpuAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readRegs(uint8_t reg, uint8_t *data, uint8_t size) {
  Wire.beginTransmission(mpuAddress);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(mpuAddress, size) != size) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (uint8_t i = 0; i < size; ++i) data[i] = Wire.read();
  return true;
}

// RW always low: use command delays instead of reading the LCD busy flag.
bool lcdNibble(uint8_t nibble, bool data) {
  const uint8_t value = (nibble << 4) | LCD_BL | (data ? LCD_RS : 0);
  Wire.beginTransmission(lcdAddress);
  Wire.write(value);
  Wire.write(value | LCD_EN);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool lcdByte(uint8_t value, bool data = false) {
  if (!lcdNibble(value >> 4, data) || !lcdNibble(value & 15, data)) return false;
  if (!data && (value == 0x01 || value == 0x02)) delay(2);
  else delayMicroseconds(50);
  return true;
}

bool lcdPosition(uint8_t x, uint8_t row) {
  return lcdByte(0x80 | (row == 0 ? 0x00 : 0x40) | x);
}

bool lcdText(const char *text, uint8_t row = 0) {
  if (!lcdPosition(0, row)) return false;
  for (int i = 0; text[i] && i < 16; ++i)
    if (!lcdByte((uint8_t)text[i], true)) return false;
  return true;
}

void halt(const char *message) {
  Serial.println(message);
  if (lcdReady) { lcdByte(0x01); lcdText(message); }
  for (;;) delay(100);
}

bool startLcd() {
  // Only one PCF8574 backpack is expected. ACK cannot verify its pin mapping.
  for (uint8_t a = 0x20; a <= 0x27 && !lcdAddress; ++a)
    if (responds(a)) lcdAddress = a;
  for (uint8_t a = 0x38; a <= 0x3F && !lcdAddress; ++a)
    if (responds(a)) lcdAddress = a;
  if (!lcdAddress) return false;
  delay(50);
  // Datasheet initialization: wake in 8-bit mode, then switch to 4-bit.
  if (!lcdNibble(3, false)) return false;
  delay(5);
  if (!lcdNibble(3, false)) return false;
  delayMicroseconds(150);
  if (!lcdNibble(3, false)) return false;
  delayMicroseconds(150);
  if (!lcdNibble(2, false)) return false;
  delayMicroseconds(150);
  if (!lcdByte(0x28) || !lcdByte(0x08) || !lcdByte(0x01) ||
      !lcdByte(0x06) || !lcdByte(0x0C)) return false;
  // Eight glyphs put a three-pixel-wide dot at vertical offsets 0..7.
  // Offset 7 is one pixel high; other glyphs are two pixels high.
  for (uint8_t y = 0; y < 8; ++y) {
    if (!lcdByte(0x40 | (y << 3))) return false;
    for (uint8_t line = 0; line < 8; ++line)
      if (!lcdByte((line == y) ? 0b11111 : 0, true)) return false;
  }
  return true;
}

bool startMpu() {
  for (uint8_t address = 0x68; address <= 0x69; ++address) {
    if (!responds(address)) continue;
    mpuAddress = address;
    uint8_t id = 0;
    // WHO_AM_I stays 0x68 when AD0 selects bus address 0x69.
    if (readRegs(0x75, &id, 1)) {
      Serial.printf("MPU at 0x%02X: WHO_AM_I=0x%02X (expected 0x68)\n", address, id);
      if (id == 0x68) break;
      if (MPU_COMPAT_WHO_AM_I != 0 && id == MPU_COMPAT_WHO_AM_I) {
        Serial.println("Nonstandard MPU ID: configured compatibility mode; verify measurements.");
        break;
      }
    } else {
      Serial.printf("MPU at 0x%02X: WHO_AM_I read failed\n", address);
    }
    mpuAddress = 0;
  }
  if (!mpuAddress || !writeReg(0x6B, 0x80)) return false;
  delay(100);
  if (!writeReg(0x6B, 0x01) || // Wake, PLL with X gyro reference.
      !writeReg(0x6C, 0x00) || // Enable every axis.
      !writeReg(0x1A, 0x04) || // DLPF accel 21 Hz / gyro 20 Hz.
      !writeReg(0x19, 0x09) || // 1 kHz / (1+9) = 100 Hz.
      !writeReg(0x1B, 0x08) || // +/-500 deg/s; 65.5 LSB/(deg/s).
      !writeReg(0x1C, 0x08))   // +/-4g; 8192 LSB/g.
    return false;
  delay(100);
  const uint8_t registers[] = {0x6B, 0x6C, 0x1A, 0x19, 0x1B, 0x1C};
  const uint8_t expected[] = {0x01, 0x00, 0x04, 0x09, 0x08, 0x08};
  for (uint8_t i = 0; i < sizeof(registers); ++i) {
    uint8_t actual = 0;
    if (!readRegs(registers[i], &actual, 1) || actual != expected[i]) {
      Serial.printf("MPU config readback failed: reg=0x%02X got=0x%02X expected=0x%02X\n",
                    registers[i], actual, expected[i]);
      return false;
    }
  }
  Serial.println("MPU config readback OK");
  return true;
}

struct Motion { float ax, ay, az, gx, gy; };
bool readMotion(Motion &m) {
  uint8_t raw[14]; // Accel XYZ, temperature, gyro XYZ.
  if (!readRegs(0x3B, raw, sizeof(raw))) return false;
  m.ax = Liquid::signed16(raw) / 8192.0f;
  m.ay = Liquid::signed16(raw + 2) / 8192.0f;
  m.az = Liquid::signed16(raw + 4) / 8192.0f;
  m.gx = Liquid::signed16(raw + 8) / 65.5f * PI_F / 180.0f;
  m.gy = Liquid::signed16(raw + 10) / 65.5f * PI_F / 180.0f;
  return true;
}

float accelAngle(const Motion &m) {
  return TILT_AXIS == 0 ? atan2f(m.ay,m.az) : atan2f(-m.ax,m.az);
}
float angularRate(const Motion &m) { return TILT_AXIS == 0 ? m.gx : m.gy; }

void calibrate() {
  if (!lcdByte(0x01) || !lcdText("Keep flat/still") ||
      !lcdText("Calibrating...", 1)) halt("LCD write failed");
  Serial.println("Keep the MPU flat and still for 2 seconds.");
  float gy = 0, ay = 0, az = 0;
  for (int i = 0; i < 200; ++i) {
    Motion m;
    if (!readMotion(m)) halt("MPU read failed");
    gy += angularRate(m); ay += TILT_AXIS == 0 ? m.ay : -m.ax; az += m.az;
    delay(10);
  }
  gyroBias = gy / 200;
  zeroRoll = atan2f(ay, az);
  roll = zeroRoll;
  tilt = previousTilt = wave = waveSpeed = 0;
  memset(previousCells, 0xFF, sizeof(previousCells));
  if (!lcdByte(0x01)) halt("LCD write failed");
  lastSample = millis();
}

void sample(float dt) {
  Motion m;
  if (!readMotion(m)) halt("MPU read failed");
  const float prediction = roll + (angularRate(m) - gyroBias) * dt;
  const float norm = sqrtf(m.ax * m.ax + m.ay * m.ay + m.az * m.az);
  // Complementary filter; suppress accelerometer correction during shocks.
  const float weight = fabsf(norm - 1) < 0.15f ? dt / (0.45f + dt) : 0;
  roll = wrap(prediction + weight * wrap(accelAngle(m) - prediction));
  tilt = DIRECTION * limit(wrap(roll - zeroRoll), -PI_F / 3, PI_F / 3);
  if (ENABLE_WAVE) {
    waveSpeed = limit(waveSpeed - 12 * (tilt - previousTilt), -25, 25);
    waveSpeed += (-49 * wave - 3.2f * waveSpeed) * dt;
    wave = limit(wave + waveSpeed * dt, -3, 3);
  }
  previousTilt = tilt;
}

void draw() {
  uint8_t cells[2][16];
  Liquid::render(tilt, wave, REST_Y, SLOPE_GAIN, cells);
  for (int row = 0; row < 2; ++row) {
    for (int x = 0; x < 16; ++x) {
      if (previousCells[row][x] == cells[row][x]) continue;
      if (!lcdPosition(x, row) || !lcdByte(cells[row][x], true)) halt("LCD write failed");
      previousCells[row][x] = cells[row][x];
    }
  }
}

void scanBus() {
  Serial.printf("I2C scan: SDA=%d SCL=%d, 7-bit addresses in hex\n", SDA_PIN, SCL_PIN);
  int found=0;
  for (uint8_t a=1;a<127;++a) if(responds(a)) { Serial.printf("  0x%02X\n",a); ++found; }
  Serial.printf("Found %d devices. MPU: 68/69. PCF8574: 20-27; PCF8574A: 38-3F.\n",found);
}
void invalidateFrame() { memset(previousCells,0xFF,sizeof(previousCells)); }
void lcdTest() {
  if (!lcdByte(0x01) || !lcdText("LCD1602 TEST OK") || !lcdText("0123456789ABCDEF",1)) halt("LCD write failed");
  Serial.println("Check LCD text: LCD1602 TEST OK / 0123456789ABCDEF");
  delay(2000);
  if (!lcdByte(0x01)) halt("LCD write failed");
  invalidateFrame();
}
void help() { Serial.println("Commands: ? help | i scan | c calibrate | t LCD text test"); }

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\nLiquid Dots - APP_MODE=%d\n", APP_MODE);
  Wire.begin(SDA_PIN,SCL_PIN);
  Wire.setClock(100000);
  Wire.setTimeOut(50);
  scanBus();
  if (APP_MODE == 1) return;
  if (APP_MODE == 3) {
    if (!startMpu()) halt("MPU not found");
    Serial.println("MPU only: ax,ay,az in g; gx,gy in rad/s");
    return;
  }
  if (!startLcd()) halt("LCD not found");
  lcdReady=true;
  Serial.printf("LCD:0x%02X\n",lcdAddress);
  if (APP_MODE == 2) { lcdTest(); return; }
  if (APP_MODE == 4) {
    Serial.println("DEMO: generated angle, no MPU measurements");
    invalidateFrame();
    return;
  }
  if (!startMpu()) halt("MPU not found");
  Serial.printf("MPU:0x%02X\n",mpuAddress);
  calibrate();
  help();
}

void loop() {
  if (Serial.available()) {
    const char command=Serial.read();
    if (command=='?') help();
    if (command=='i') { scanBus(); lastSample=millis(); }
    if (command=='c' && APP_MODE==0) calibrate();
    if (command=='t' && lcdReady) { lcdTest(); lastSample=millis(); }
  }
  const uint32_t now=millis();
  if (APP_MODE==1 || APP_MODE==2) { delay(10); return; }
  if (APP_MODE==3) {
    if (now-lastLog>=200) {
      lastLog=now;
      Motion m;
      if (!readMotion(m)) halt("MPU read failed");
      Serial.printf("ax:%.3f,ay:%.3f,az:%.3f,gx:%.3f,gy:%.3f\n",m.ax,m.ay,m.az,m.gx,m.gy);
    }
    delay(1); return;
  }
  if (APP_MODE==4) tilt=0.7f*sinf(now/1800.0f);
  else if (now-lastSample>=SAMPLE_MS) {
    const float dt=limit((now-lastSample)/1000.0f,0.001f,0.04f);
    lastSample=now;
    sample(dt);
  }
  if (now-lastFrame>=FRAME_MS) { lastFrame=now; draw(); }
  if (now-lastLog>=200) {
    lastLog=now;
    Serial.printf("tilt_deg:%.2f,wave:%.2f\n",tilt*180/PI_F,wave);
  }
  delay(1);
}
