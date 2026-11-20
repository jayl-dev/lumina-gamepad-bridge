#pragma once

#include "pad_state.h"

namespace lumina {

/**
 * Generic HID joystick laid out like SDL2's macOS mapping for the 8BitDo Ultimate
 * Wired Controller (03000000c82d00001130000000020000).
 *
 * Note: macOS's GameController framework claims this VID/PID, so SDL with its MFi
 * backend enabled (the default) skips the device. Use the DS4 or Switch Pro profile
 * for SDL apps such as Ryujinx, or launch them with SDL_JOYSTICK_MFI=0.
 */
class GenericProfile {
public:
  static constexpr uint16_t VID = 0x2DC8;
  static constexpr uint16_t PID = 0x3011;
  static constexpr uint16_t BCD = 0x0200;
  static constexpr const char *MANUFACTURER = "Lumina";
  static constexpr const char *PRODUCT = "Lumina ESP Gamepad";

  static constexpr uint8_t REPORT_ID = 0x01;
  static constexpr uint8_t REPORT_LEN = 13;

  static const uint8_t *descriptor(uint16_t *len) {
    static const uint8_t kDescriptor[] = {
      0x05, 0x01,        // Usage Page (Generic Desktop)
      0x09, 0x04,        // Usage (Joystick)
      0xA1, 0x01,        // Collection (Application)
      0x85, REPORT_ID,   //   Report ID (1)

      // --- 16 Buttons ---
      0x05, 0x09,        //   Usage Page (Button)
      0x19, 0x01,        //   Usage Minimum (1)
      0x29, 0x10,        //   Usage Maximum (16)
      0x15, 0x00,        //   Logical Minimum (0)
      0x25, 0x01,        //   Logical Maximum (1)
      0x95, 0x10,        //   Report Count (16)
      0x75, 0x01,        //   Report Size (1)
      0x81, 0x02,        //   Input (Data, Variable, Absolute)

      // --- Hat Switch (D-pad) ---
      0x05, 0x01,        //   Usage Page (Generic Desktop)
      0x09, 0x39,        //   Usage (Hat Switch)
      0x15, 0x00,        //   Logical Minimum (0)
      0x25, 0x07,        //   Logical Maximum (7)
      0x35, 0x00,        //   Physical Minimum (0)
      0x46, 0x3B, 0x01,  //   Physical Maximum (315)
      0x65, 0x14,        //   Unit (Degrees)
      0x75, 0x04,        //   Report Size (4)
      0x95, 0x01,        //   Report Count (1)
      0x81, 0x42,        //   Input (Data, Variable, Absolute, Null State)

      // --- Hat Switch Padding (4 bits) ---
      0x75, 0x04,        //   Report Size (4)
      0x95, 0x01,        //   Report Count (1)
      0x81, 0x01,        //   Input (Constant)

      // --- Stick Axes (4x 16-bit signed) on X, Y, Z, Rx (macOS a0..a3) ---
      0x65, 0x00,        //   Unit (None)
      0x05, 0x01,        //   Usage Page (Generic Desktop)
      0x09, 0x30,        //   Usage (X)  - Left Stick X (a0)
      0x09, 0x31,        //   Usage (Y)  - Left Stick Y (a1)
      0x09, 0x32,        //   Usage (Z)  - Right Stick X (a2)
      0x09, 0x33,        //   Usage (Rx) - Right Stick Y (a3)
      0x16, 0x00, 0x80,  //   Logical Minimum (-32768)
      0x26, 0xFF, 0x7F,  //   Logical Maximum (32767)
      0x36, 0x00, 0x80,  //   Physical Minimum (-32768)
      0x46, 0xFF, 0x7F,  //   Physical Maximum (32767)
      0x75, 0x10,        //   Report Size (16)
      0x95, 0x04,        //   Report Count (4)
      0x81, 0x02,        //   Input (Data, Variable, Absolute)

      // --- Triggers (2x 8-bit unsigned) on Ry, Rz (macOS a4=RT, a5=LT) ---
      0x35, 0x00,        //   Physical Minimum (0)
      0x46, 0xFF, 0x00,  //   Physical Maximum (255)
      0x05, 0x01,        //   Usage Page (Generic Desktop)
      0x09, 0x34,        //   Usage (Ry) - Right Trigger (a4)
      0x09, 0x35,        //   Usage (Rz) - Left Trigger (a5)
      0x15, 0x00,        //   Logical Minimum (0)
      0x26, 0xFF, 0x00,  //   Logical Maximum (255)
      0x75, 0x08,        //   Report Size (8)
      0x95, 0x02,        //   Report Count (2)
      0x81, 0x02,        //   Input (Data, Variable, Absolute)

      0xC0               // End Collection
    };
    *len = sizeof(kDescriptor);
    return kDescriptor;
  }

  void begin(const uint8_t *mac) { (void) mac; }

  void setState(const PadState &s) {
    if (!sentOnce || s != lastSent) {
      pending = s;
      dirty = true;
    } else {
      dirty = false;
    }
  }

  void onOutput(uint8_t id, const uint8_t *data, uint16_t len) {
    (void) id;
    (void) data;
    (void) len;
  }

  // No output reports, so no rumble
  bool takeRumble(Rumble &out) {
    (void) out;
    return false;
  }

  uint16_t getFeature(uint8_t id, uint8_t *buf, uint16_t len) const {
    (void) id;
    (void) buf;
    (void) len;
    return 0;
  }

  // Sends only on change; the host keeps the last state.
  bool nextReport(uint32_t nowUs, HidReport &out) {
    (void) nowUs;
    if (!dirty) {
      return false;
    }
    out.id = REPORT_ID;
    out.len = REPORT_LEN;
    encode(pending, out.data);
    lastSent = pending;
    sentOnce = true;
    dirty = false;
    return true;
  }

  /**
   * HID report body for the 8BitDo Ultimate Wired layout:
   *   buttons: a:b0 b:b1 x:b3 y:b4 lb:b6 rb:b7 back:b10 start:b11 guide:b12 l3:b13 r3:b14
   *   hat:     h0
   *   axes:    X(a0)=LX, Y(a1)=LY, Z(a2)=RX, Rx(a3)=RY, Ry(a4)=RT, Rz(a5)=LT
   */
  static void encode(const PadState &s, uint8_t *hid) {
    uint16_t hb = 0;
    if (s.buttons & BTN_A) hb |= 1u << 0;
    if (s.buttons & BTN_B) hb |= 1u << 1;
    if (s.buttons & BTN_X) hb |= 1u << 3;
    if (s.buttons & BTN_Y) hb |= 1u << 4;
    if (s.buttons & BTN_LB) hb |= 1u << 6;
    if (s.buttons & BTN_RB) hb |= 1u << 7;
    if (s.buttons & BTN_BACK) hb |= 1u << 10;
    if (s.buttons & BTN_START) hb |= 1u << 11;
    if (s.buttons & BTN_HOME) hb |= 1u << 12;
    if (s.buttons & BTN_L3) hb |= 1u << 13;
    if (s.buttons & BTN_R3) hb |= 1u << 14;

    writeU16(&hid[0], hb);
    hid[2] = s.hat;
    // Lumina's Y axes are XInput-style (positive = up); HID/SDL Y is positive = down.
    writeI16(&hid[3], s.lx);
    writeI16(&hid[5], invertAxis(s.ly));
    writeI16(&hid[7], s.rx);
    writeI16(&hid[9], invertAxis(s.ry));
    hid[11] = s.rt;
    hid[12] = s.lt;
  }

private:
  PadState pending;
  PadState lastSent;
  bool sentOnce = false;
  bool dirty = true;
};

// Translates a raw wire body into the generic HID body (kept for tests and tools).
inline void wireToHidReport(const uint8_t *wire, uint8_t *hid) {
  GenericProfile::encode(decodeWire(wire), hid);
}

}  // namespace lumina
