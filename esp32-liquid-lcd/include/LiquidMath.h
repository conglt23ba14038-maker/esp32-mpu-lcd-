#pragma once
#include <stdint.h>
#include <math.h>
namespace Liquid {
constexpr float PI_F = 3.14159265f;
inline float clamp(float v, float lo, float hi) { return fmaxf(lo, fminf(hi, v)); }
inline float wrap(float a) { return atan2f(sinf(a), cosf(a)); }
inline int16_t signed16(const uint8_t *p) {
  const int32_t value = ((uint16_t)p[0] << 8) | p[1];
  return static_cast<int16_t>(value >= 32768 ? value - 65536 : value);
}
inline void render(float tilt, float wave, float rest, float gain, uint8_t cells[2][16]) {
  for (int row = 0; row < 2; ++row)
    for (int x = 0; x < 16; ++x) cells[row][x] = ' ';
  tilt = clamp(tilt, -PI_F / 3, PI_F / 3);
  for (int x = 0; x < 16; ++x) {
    const float position = x - 7.5f;
    const float y = rest + tanf(tilt) * gain * position + wave * sinf(position / 7.5f * PI_F / 2);
    const int pixel = (int)lroundf(clamp(y, 0, 15));
    cells[pixel / 8][x] = pixel % 8;
  }
}
}
