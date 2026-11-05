#pragma once

constexpr int SDA_PIN = 21, SCL_PIN = 22;
// This connected module reports 0x72. Set to 0 for strict MPU6050 ID (0x68).
// Compatibility must be checked using register readback and real measurements.
constexpr uint8_t MPU_COMPAT_WHO_AM_I = 0x72;
constexpr float DIRECTION = 1.0f;
constexpr float REST_Y = 11.0f;
constexpr float SLOPE_GAIN = 0.55f;
constexpr bool ENABLE_WAVE = false;
constexpr int TILT_AXIS = 0; // 0: roll quanh X; 1: pitch quanh Y.
constexpr uint32_t SAMPLE_MS = 10, FRAME_MS = 65;
// Assumed backpack wiring (not fixed by PCF8574): P0 RS, P1 RW,
// P2 E, P3 active-high BL, P4..7 D4..7. RW is always driven low.
constexpr uint8_t LCD_RS = 0x01, LCD_EN = 0x04, LCD_BL = 0x08;
#ifndef APP_MODE
#define APP_MODE 0
#endif
// 0: binh thuong; 1: scan; 2: LCD; 3: MPU; 4: LCD demo khong can MPU.
static_assert(TILT_AXIS == 0 || TILT_AXIS == 1, "TILT_AXIS must be 0 or 1");
static_assert(REST_Y >= 0 && REST_Y <= 15, "REST_Y outside display");
