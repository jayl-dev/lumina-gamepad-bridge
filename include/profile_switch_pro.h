#pragma once

#include "pad_state.h"

namespace lumina {

/**
 * Wired Nintendo Switch Pro Controller (057E:2009).
 *
 * SDL's HIDAPI Switch driver talks to it with Nintendo's USB protocol:
 *   0x80 <cmd>          USB commands, answered with input report 0x81 <cmd> ...
 *   0x01 <cnt> <rumble> <subcmd> <args>
 *                       subcommands, answered with input report 0x21 (ack + data)
 * After 0x80 0x04 ("force USB") or subcommand 0x03 the controller streams full
 * input reports 0x30. SPI flash reads (subcommand 0x10) are served from a small
 * table of factory calibration; everything else reads as erased flash (0xFF).
 * References: SDL src/joystick/hidapi/SDL_hidapi_switch.c,
 * dekuNukem/Nintendo_Switch_Reverse_Engineering.
 */
class SwitchProProfile {
public:
  static constexpr uint16_t VID = 0x057E;
  static constexpr uint16_t PID = 0x2009;
  static constexpr uint16_t BCD = 0x0200;
  static constexpr const char *MANUFACTURER = "Nintendo Co., Ltd.";
  static constexpr const char *PRODUCT = "Pro Controller";

  static constexpr uint8_t REPORT_LEN = 63;
  static constexpr uint32_t REPORT_INTERVAL_US = 8000;

  // Host -> controller output report IDs
  static constexpr uint8_t OUT_SUBCOMMAND = 0x01;
  static constexpr uint8_t OUT_RUMBLE = 0x10;
  static constexpr uint8_t OUT_USB = 0x80;

  // Controller -> host input report IDs
  static constexpr uint8_t IN_SUBCOMMAND_REPLY = 0x21;
  static constexpr uint8_t IN_FULL = 0x30;
  static constexpr uint8_t IN_USB_REPLY = 0x81;

  // 0x80 USB commands
  static constexpr uint8_t USB_STATUS = 0x01;
  static constexpr uint8_t USB_HANDSHAKE = 0x02;
  static constexpr uint8_t USB_HIGH_SPEED = 0x03;
  static constexpr uint8_t USB_FORCE_USB = 0x04;
  static constexpr uint8_t USB_ALLOW_TIMEOUT = 0x05;

  // Subcommands
  static constexpr uint8_t SUB_DEVICE_INFO = 0x02;
  static constexpr uint8_t SUB_INPUT_MODE = 0x03;
  static constexpr uint8_t SUB_TRIGGER_ELAPSED = 0x04;
  static constexpr uint8_t SUB_SPI_READ = 0x10;
  static constexpr uint8_t SUB_VOLTAGE = 0x50;

  static constexpr uint8_t CONTROLLER_TYPE_PRO = 0x03;
  static constexpr uint8_t TRIGGER_THRESHOLD = 30;  // ZL/ZR are digital
  static constexpr uint8_t BATTERY_FULL_CHARGING = 0x91;

  static const uint8_t *descriptor(uint16_t *len) {
    // Genuine Pro Controller USB report descriptor layout
    static const uint8_t kDescriptor[] = {
      0x05, 0x01,              // Usage Page (Generic Desktop)
      0x15, 0x00,              // Logical Minimum (0)
      0x09, 0x04,              // Usage (Joystick)
      0xA1, 0x01,              // Collection (Application)
      0x85, 0x30,              //   Report ID (0x30) - full input
      0x05, 0x01,              //   Usage Page (Generic Desktop)
      0x05, 0x09,              //   Usage Page (Button)
      0x19, 0x01,              //   Usage Minimum (1)
      0x29, 0x0A,              //   Usage Maximum (10)
      0x15, 0x00,              //   Logical Minimum (0)
      0x25, 0x01,              //   Logical Maximum (1)
      0x75, 0x01,              //   Report Size (1)
      0x95, 0x0A,              //   Report Count (10)
      0x55, 0x00,              //   Unit Exponent (0)
      0x65, 0x00,              //   Unit (None)
      0x81, 0x02,              //   Input (Data, Variable, Absolute)
      0x05, 0x09,              //   Usage Page (Button)
      0x19, 0x0B,              //   Usage Minimum (11)
      0x29, 0x0E,              //   Usage Maximum (14)
      0x15, 0x00,              //   Logical Minimum (0)
      0x25, 0x01,              //   Logical Maximum (1)
      0x75, 0x01,              //   Report Size (1)
      0x95, 0x04,              //   Report Count (4)
      0x81, 0x02,              //   Input (Data, Variable, Absolute)
      0x75, 0x01,              //   Report Size (1)
      0x95, 0x02,              //   Report Count (2)
      0x81, 0x03,              //   Input (Constant)
      0x0B, 0x01, 0x00, 0x01, 0x00,  // Usage (Generic Desktop: Pointer)
      0xA1, 0x00,              //   Collection (Physical)
      0x0B, 0x30, 0x00, 0x01, 0x00,  //   Usage (X)
      0x0B, 0x31, 0x00, 0x01, 0x00,  //   Usage (Y)
      0x0B, 0x32, 0x00, 0x01, 0x00,  //   Usage (Z)
      0x0B, 0x35, 0x00, 0x01, 0x00,  //   Usage (Rz)
      0x15, 0x00,              //     Logical Minimum (0)
      0x27, 0xFF, 0xFF, 0x00, 0x00,  // Logical Maximum (65535)
      0x75, 0x10,              //     Report Size (16)
      0x95, 0x04,              //     Report Count (4)
      0x81, 0x02,              //     Input (Data, Variable, Absolute)
      0xC0,                    //   End Collection
      0x0B, 0x39, 0x00, 0x01, 0x00,  // Usage (Hat Switch)
      0x15, 0x00,              //   Logical Minimum (0)
      0x25, 0x07,              //   Logical Maximum (7)
      0x35, 0x00,              //   Physical Minimum (0)
      0x46, 0x3B, 0x01,        //   Physical Maximum (315)
      0x65, 0x14,              //   Unit (Degrees)
      0x75, 0x04,              //   Report Size (4)
      0x95, 0x01,              //   Report Count (1)
      0x81, 0x02,              //   Input (Data, Variable, Absolute)
      0x05, 0x09,              //   Usage Page (Button)
      0x19, 0x0F,              //   Usage Minimum (15)
      0x29, 0x12,              //   Usage Maximum (18)
      0x15, 0x00,              //   Logical Minimum (0)
      0x25, 0x01,              //   Logical Maximum (1)
      0x75, 0x01,              //   Report Size (1)
      0x95, 0x04,              //   Report Count (4)
      0x81, 0x02,              //   Input (Data, Variable, Absolute)
      0x75, 0x08,              //   Report Size (8)
      0x95, 0x34,              //   Report Count (52)
      0x81, 0x03,              //   Input (Constant)
      0x06, 0x00, 0xFF,        //   Usage Page (Vendor 0xFF00)
      0x85, 0x21,              //   Report ID (0x21) - subcommand reply
      0x09, 0x01,              //   Usage (0x01)
      0x75, 0x08,              //   Report Size (8)
      0x95, 0x3F,              //   Report Count (63)
      0x81, 0x03,              //   Input (Constant)
      0x85, 0x81,              //   Report ID (0x81) - USB command reply
      0x09, 0x02,              //   Usage (0x02)
      0x75, 0x08,              //   Report Size (8)
      0x95, 0x3F,              //   Report Count (63)
      0x81, 0x03,              //   Input (Constant)
      0x85, 0x01,              //   Report ID (0x01) - rumble + subcommand
      0x09, 0x03,              //   Usage (0x03)
      0x75, 0x08,              //   Report Size (8)
      0x95, 0x3F,              //   Report Count (63)
      0x91, 0x83,              //   Output (Constant, Variable, Absolute, Volatile)
      0x85, 0x10,              //   Report ID (0x10) - rumble only
      0x09, 0x04,              //   Usage (0x04)
      0x75, 0x08,              //   Report Size (8)
      0x95, 0x3F,              //   Report Count (63)
      0x91, 0x83,              //   Output (Constant, Variable, Absolute, Volatile)
      0x85, 0x80,              //   Report ID (0x80) - USB command
      0x09, 0x05,              //   Usage (0x05)
      0x75, 0x08,              //   Report Size (8)
      0x95, 0x3F,              //   Report Count (63)
      0x91, 0x83,              //   Output (Constant, Variable, Absolute, Volatile)
      0x85, 0x82,              //   Report ID (0x82)
      0x09, 0x06,              //   Usage (0x06)
      0x75, 0x08,              //   Report Size (8)
      0x95, 0x3F,              //   Report Count (63)
      0x91, 0x83,              //   Output (Constant, Variable, Absolute, Volatile)
      0xC0                     // End Collection
    };
    *len = sizeof(kDescriptor);
    return kDescriptor;
  }

  // mac: 6 bytes, most significant first
  void begin(const uint8_t *mac) { std::memcpy(mac_, mac, 6); }

  void setState(const PadState &s) { state_ = s; }

  bool streaming() const { return streaming_; }

  /**
   * Output report from the host (without the report ID). Replies are queued and
   * sent ahead of the 0x30 stream by nextReport().
   */
  void onOutput(uint8_t id, const uint8_t *data, uint16_t len) {
    if (id == OUT_USB && len >= 1) {
      handleUsbCommand(data[0]);
    } else if (id == OUT_SUBCOMMAND && len >= 10) {
      handleRumble(&data[1]);
      handleSubcommand(data[9], &data[10], static_cast<uint16_t>(len - 10));
    } else if (id == OUT_RUMBLE && len >= 9) {
      handleRumble(&data[1]);
    }
  }

  bool takeRumble(Rumble &out) { return rumble_.take(out); }

  /**
   * HD rumble for one side (4 bytes) -> amplitude index 0..100 of each band:
   *   [1] high-band amplitude << 1 (bit 0 = frequency bit 8)
   *   [2] bit 7 = low-band amplitude bit 0, [3] low-band amplitude >> 1, offset by 0x40
   * Neutral is 00 01 40 40. SDL puts the game's high/low-frequency strengths in these bands.
   */
  static void decodeRumbleSide(const uint8_t *b, uint8_t *highBand, uint8_t *lowBand) {
    uint8_t high = static_cast<uint8_t>(b[1] >> 1);
    uint8_t low = static_cast<uint8_t>((((b[3] - 0x40) & 0x7F) << 1) | (b[2] >> 7));
    *highBand = high > 100 ? 100 : high;
    *lowBand = low > 100 ? 100 : low;
  }

  uint16_t getFeature(uint8_t id, uint8_t *buf, uint16_t len) const {
    (void) id;
    (void) buf;
    (void) len;
    return 0;
  }

  bool nextReport(uint32_t nowUs, HidReport &out) {
    if (queued_ > 0) {
      out = queue_[0];
      for (uint8_t i = 1; i < queued_; i++) {
        queue_[i - 1] = queue_[i];
      }
      queued_--;
      return true;
    }
    if (!streaming_) {
      return false;
    }
    if (sentFull_ && static_cast<uint32_t>(nowUs - lastFullUs_) < REPORT_INTERVAL_US) {
      return false;
    }
    sentFull_ = true;
    lastFullUs_ = nowUs;
    out.id = IN_FULL;
    out.len = REPORT_LEN;
    std::memset(out.data, 0, sizeof(out.data));
    writeHeader(out.data);
    return true;
  }

  /**
   * Fills the common header of 0x30/0x21 reports (report byte N is data[N - 1]):
   *   1 timer, 2 battery/connection, 3-5 buttons (right, shared, left), 6-8 left stick,
   *   9-11 right stick (12-bit packed), 12 vibrator report.
   */
  static void encodeHeader(const PadState &s, uint8_t timer, uint8_t *d) {
    d[0] = timer;
    d[1] = BATTERY_FULL_CHARGING;

    // Positional mapping: Xbox A (bottom) -> Nintendo B (bottom), and so on.
    uint8_t right = 0, shared = 0, left = 0;
    if (s.buttons & BTN_X) right |= 0x01;  // Y (left)
    if (s.buttons & BTN_Y) right |= 0x02;  // X (top)
    if (s.buttons & BTN_A) right |= 0x04;  // B (bottom)
    if (s.buttons & BTN_B) right |= 0x08;  // A (right)
    if (s.buttons & BTN_RB) right |= 0x40;
    if (s.rt > TRIGGER_THRESHOLD) right |= 0x80;  // ZR

    if (s.buttons & BTN_BACK) shared |= 0x01;   // minus
    if (s.buttons & BTN_START) shared |= 0x02;  // plus
    if (s.buttons & BTN_R3) shared |= 0x04;
    if (s.buttons & BTN_L3) shared |= 0x08;
    if (s.buttons & BTN_HOME) shared |= 0x10;
    shared |= 0x80;  // charging grip / USB powered

    switch (s.hat) {
      case 0: left |= 0x02; break;          // up
      case 1: left |= 0x02 | 0x04; break;   // up-right
      case 2: left |= 0x04; break;          // right
      case 3: left |= 0x01 | 0x04; break;   // down-right
      case 4: left |= 0x01; break;          // down
      case 5: left |= 0x01 | 0x08; break;   // down-left
      case 6: left |= 0x08; break;          // left
      case 7: left |= 0x02 | 0x08; break;   // up-left
      default: break;
    }
    if (s.buttons & BTN_LB) left |= 0x40;
    if (s.lt > TRIGGER_THRESHOLD) left |= 0x80;  // ZL

    d[2] = right;
    d[3] = shared;
    d[4] = left;
    // Switch stick Y is positive = up, same as Lumina's
    packStick(axisToUnsigned(s.lx, 12), axisToUnsigned(s.ly, 12), &d[5]);
    packStick(axisToUnsigned(s.rx, 12), axisToUnsigned(s.ry, 12), &d[8]);
    d[11] = 0x0C;  // vibrator input report
  }

  static void packStick(uint16_t x, uint16_t y, uint8_t *out) {
    out[0] = static_cast<uint8_t>(x & 0xFF);
    out[1] = static_cast<uint8_t>(((x >> 8) & 0x0F) | ((y & 0x0F) << 4));
    out[2] = static_cast<uint8_t>(y >> 4);
  }

  /**
   * Reads from the emulated SPI flash. Only the regions hosts read are populated.
   */
  static void readSpi(uint32_t addr, uint8_t *out, uint8_t len) {
    std::memset(out, 0xFF, len);
    size_t count = 0;
    const SpiRegion *regions = spiRegions(&count);
    for (size_t r = 0; r < count; r++) {
      for (uint8_t i = 0; i < len; i++) {
        uint32_t a = addr + i;
        if (a >= regions[r].addr && a < regions[r].addr + regions[r].len) {
          out[i] = regions[r].data[a - regions[r].addr];
        }
      }
    }
  }

private:
  // Bytes 0-3 drive the left motor, 4-7 the right; each Moonlight motor takes the stronger side.
  void handleRumble(const uint8_t *hd) {
    uint8_t leftHigh, leftLow, rightHigh, rightLow;
    decodeRumbleSide(&hd[0], &leftHigh, &leftLow);
    decodeRumbleSide(&hd[4], &rightHigh, &rightLow);
    uint8_t low = leftLow > rightLow ? leftLow : rightLow;
    uint8_t high = leftHigh > rightHigh ? leftHigh : rightHigh;
    rumble_.set({static_cast<uint16_t>(low * 65535u / 100), static_cast<uint16_t>(high * 65535u / 100)});
  }

  struct SpiRegion {
    uint32_t addr;
    uint8_t len;
    const uint8_t *data;
  };

  static const SpiRegion *spiRegions(size_t *count) {
    // Factory stick calibration: center 0x800, 0x7FF travel each way (1:1 with Lumina's range).
    // Left: X/Y max-above-center, X/Y center, X/Y min-below-center.
    // Right: X/Y center, X/Y min-below-center, X/Y max-above-center.
    static const uint8_t kStickCal[18] = {
      0xFF, 0xF7, 0x7F, 0x00, 0x08, 0x80, 0xFF, 0xF7, 0x7F,
      0x00, 0x08, 0x80, 0xFF, 0xF7, 0x7F, 0xFF, 0xF7, 0x7F,
    };
    // Factory IMU calibration: zero offsets, nominal accel (0x4000) and gyro (0x343B) scales.
    static const uint8_t kImuCal[24] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x40, 0x00, 0x40,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3B, 0x34, 0x3B, 0x34, 0x3B, 0x34,
    };
    // Body, buttons, left grip, right grip colours
    static const uint8_t kColors[12] = {
      0x32, 0x32, 0x32, 0xFF, 0xFF, 0xFF, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32,
    };
    // Stick parameters (dead zone, range ratio) as found on genuine controllers
    static const uint8_t kStickParams[18] = {
      0x0F, 0x30, 0x61, 0x96, 0x30, 0xF3, 0xD4, 0x14, 0x54,
      0x41, 0x15, 0x54, 0xC7, 0x79, 0x9C, 0x33, 0x36, 0x63,
    };
    static const SpiRegion kRegions[] = {
      {0x6020, sizeof(kImuCal), kImuCal},
      {0x603D, sizeof(kStickCal), kStickCal},
      {0x6050, sizeof(kColors), kColors},
      {0x6086, sizeof(kStickParams), kStickParams},
      {0x6098, sizeof(kStickParams), kStickParams},
    };
    *count = sizeof(kRegions) / sizeof(kRegions[0]);
    return kRegions;
  }

  void writeHeader(uint8_t *d) { encodeHeader(state_, timer_++, d); }

  void push(const HidReport &r) {
    if (queued_ < kQueueSize) {
      queue_[queued_++] = r;
    }
  }

  void handleUsbCommand(uint8_t cmd) {
    HidReport r;
    r.id = IN_USB_REPLY;
    r.len = REPORT_LEN;
    r.data[0] = cmd;
    switch (cmd) {
      case USB_STATUS:
        r.data[1] = 0x00;
        r.data[2] = CONTROLLER_TYPE_PRO;
        for (int i = 0; i < 6; i++) {
          r.data[3 + i] = mac_[5 - i];  // little-endian over USB
        }
        push(r);
        break;
      case USB_HANDSHAKE:
      case USB_HIGH_SPEED:
        push(r);
        break;
      case USB_FORCE_USB:
        streaming_ = true;
        break;
      case USB_ALLOW_TIMEOUT:
        streaming_ = false;
        break;
      default:
        break;
    }
  }

  void handleSubcommand(uint8_t sub, const uint8_t *args, uint16_t argLen) {
    HidReport r;
    r.id = IN_SUBCOMMAND_REPLY;
    r.len = REPORT_LEN;
    writeHeader(r.data);
    uint8_t &ack = r.data[12];
    r.data[13] = sub;
    uint8_t *reply = &r.data[14];
    ack = 0x80;

    switch (sub) {
      case SUB_DEVICE_INFO:
        ack = 0x82;
        reply[0] = 0x04;  // firmware 4.33
        reply[1] = 0x33;
        reply[2] = CONTROLLER_TYPE_PRO;
        reply[3] = 0x02;
        std::memcpy(&reply[4], mac_, 6);
        reply[10] = 0x01;
        reply[11] = 0x02;  // colours come from SPI
        break;
      case SUB_INPUT_MODE:
        streaming_ = true;
        break;
      case SUB_TRIGGER_ELAPSED:
        ack = 0x83;
        break;
      case SUB_SPI_READ: {
        if (argLen < 5) {
          return;
        }
        uint32_t addr = static_cast<uint32_t>(args[0]) | (static_cast<uint32_t>(args[1]) << 8) |
                        (static_cast<uint32_t>(args[2]) << 16) | (static_cast<uint32_t>(args[3]) << 24);
        uint8_t len = args[4];
        if (len > 0x1D) {
          len = 0x1D;  // max payload that fits in a 64-byte reply
        }
        ack = 0x90;
        std::memcpy(reply, args, 4);
        reply[4] = len;
        readSpi(addr, &reply[5], len);
        break;
      }
      case SUB_VOLTAGE:
        ack = 0xD0;
        writeU16(reply, 0x0618);  // ~1.56 V, full battery
        break;
      default:
        break;  // lights, IMU, vibration, ...: plain ack
    }
    push(r);
  }

  static constexpr uint8_t kQueueSize = 8;

  PadState state_;
  RumbleState rumble_;
  uint8_t mac_[6] = {};
  uint8_t timer_ = 0;
  bool streaming_ = false;
  bool sentFull_ = false;
  uint32_t lastFullUs_ = 0;
  HidReport queue_[kQueueSize];
  uint8_t queued_ = 0;
};

}  // namespace lumina
