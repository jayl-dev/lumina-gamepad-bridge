#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "protocol.h"

namespace lumina {

// Button bits in Lumina's wire report (mapButtons() in Lumina's hid_gamepad.m).
// Face buttons are Xbox-positional: A = bottom, B = right, X = left, Y = top.
enum WireButton : uint16_t {
  BTN_A = 1u << 0,
  BTN_B = 1u << 1,
  BTN_X = 1u << 2,
  BTN_Y = 1u << 3,
  BTN_LB = 1u << 4,
  BTN_RB = 1u << 5,
  BTN_BACK = 1u << 6,
  BTN_START = 1u << 7,
  BTN_L3 = 1u << 8,
  BTN_R3 = 1u << 9,
  BTN_HOME = 1u << 10,
};

// Hat values: 0 = up, clockwise to 7 = up-left, 8 = centered
static constexpr uint8_t HAT_NONE = 8;

/**
 * Decoded gamepad state from Lumina. Sticks are XInput-style: Y positive = up.
 */
struct PadState {
  uint16_t buttons = 0;
  uint8_t hat = HAT_NONE;
  uint8_t lt = 0;
  uint8_t rt = 0;
  int16_t lx = 0;
  int16_t ly = 0;
  int16_t rx = 0;
  int16_t ry = 0;

  bool operator==(const PadState &o) const {
    return buttons == o.buttons && hat == o.hat && lt == o.lt && rt == o.rt &&
           lx == o.lx && ly == o.ly && rx == o.rx && ry == o.ry;
  }
  bool operator!=(const PadState &o) const { return !(*this == o); }
};

inline int16_t readI16(const uint8_t *p) {
  return static_cast<int16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

inline void writeI16(uint8_t *p, int16_t v) {
  p[0] = static_cast<uint8_t>(static_cast<uint16_t>(v) & 0xFF);
  p[1] = static_cast<uint8_t>(static_cast<uint16_t>(v) >> 8);
}

inline void writeU16(uint8_t *p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>(v >> 8);
}

// -32768 has no positive counterpart in int16, so it saturates to 32767.
inline int16_t invertAxis(int16_t v) {
  return v == INT16_MIN ? INT16_MAX : static_cast<int16_t>(-v);
}

// Maps a signed 16-bit axis onto an unsigned range of `bits` bits (center = half range).
inline uint16_t axisToUnsigned(int16_t v, unsigned bits) {
  return static_cast<uint16_t>((static_cast<int32_t>(v) + 32768) >> (16 - bits));
}

inline PadState decodeWire(const uint8_t *wire) {
  PadState s;
  s.buttons = static_cast<uint16_t>(wire[0]) | (static_cast<uint16_t>(wire[1]) << 8);
  s.hat = wire[2];
  s.lt = wire[3];
  s.rt = wire[4];
  s.lx = readI16(&wire[5]);
  s.ly = readI16(&wire[7]);
  s.rx = readI16(&wire[9]);
  s.ry = readI16(&wire[11]);
  return s;
}

/**
 * Rumble motor strengths as Moonlight models them: low = heavy low-frequency motor (left),
 * high = light high-frequency motor (right), each 0..65535.
 */
struct Rumble {
  uint16_t low = 0;
  uint16_t high = 0;

  bool operator==(const Rumble &o) const { return low == o.low && high == o.high; }
  bool operator!=(const Rumble &o) const { return !(*this == o); }
};

/**
 * Latest rumble a profile decoded from the host's output reports; reported to Lumina only
 * when it changes (games resend unchanged rumble constantly).
 */
class RumbleState {
public:
  void set(const Rumble &r) {
    if (r != current_) {
      current_ = r;
      dirty_ = true;
    }
  }

  bool take(Rumble &out) {
    if (!dirty_) {
      return false;
    }
    out = current_;
    dirty_ = false;
    return true;
  }

private:
  Rumble current_;
  bool dirty_ = false;
};

/**
 * An HID input report to send: report ID plus payload (without the ID byte).
 */
struct HidReport {
  uint8_t id = 0;
  uint8_t len = 0;
  uint8_t data[63] = {};
};

}  // namespace lumina
