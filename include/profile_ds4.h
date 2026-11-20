#pragma once

#include "pad_state.h"

namespace lumina {

/**
 * Wired DualShock 4 (v2, 054C:09CC).
 *
 * macOS's GameController framework and SDL's HIDAPI PS4 driver both support this
 * controller natively, so it shows up in native games, browsers and SDL apps such
 * as Ryujinx with a standard mapping. SDL accepts any device with Sony's VID and
 * tolerates missing calibration, so only input report 0x01 must be exact.
 * Reference: SDL src/joystick/hidapi/SDL_hidapi_ps4.c.
 */
class Ds4Profile {
public:
  static constexpr uint16_t VID = 0x054C;
  static constexpr uint16_t PID = 0x09CC;
  static constexpr uint16_t BCD = 0x0100;
  static constexpr const char *MANUFACTURER = "Sony Interactive Entertainment";
  static constexpr const char *PRODUCT = "Wireless Controller";

  static constexpr uint8_t INPUT_REPORT_ID = 0x01;
  static constexpr uint8_t INPUT_REPORT_LEN = 63;
  static constexpr uint32_t REPORT_INTERVAL_US = 4000;  // 250 Hz, like a wired DS4

  static constexpr uint8_t FEATURE_CALIBRATION = 0x02;
  static constexpr uint8_t FEATURE_PAIRING_INFO = 0x12;
  static constexpr uint8_t FEATURE_FIRMWARE_INFO = 0xA3;

  // Analog trigger value above which L2/R2 also report as pressed digitally
  static constexpr uint8_t TRIGGER_DIGITAL_THRESHOLD = 0;

  static const uint8_t *descriptor(uint16_t *len) {
    // Input/output layout of the DS4 v2 descriptor; vendor feature reports trimmed
    // to the ones hosts actually read.
    static const uint8_t kDescriptor[] = {
      0x05, 0x01,        // Usage Page (Generic Desktop)
      0x09, 0x05,        // Usage (Game Pad)
      0xA1, 0x01,        // Collection (Application)
      0x85, 0x01,        //   Report ID (1)
      0x09, 0x30,        //   Usage (X)
      0x09, 0x31,        //   Usage (Y)
      0x09, 0x32,        //   Usage (Z)
      0x09, 0x35,        //   Usage (Rz)
      0x15, 0x00,        //   Logical Minimum (0)
      0x26, 0xFF, 0x00,  //   Logical Maximum (255)
      0x75, 0x08,        //   Report Size (8)
      0x95, 0x04,        //   Report Count (4)
      0x81, 0x02,        //   Input (Data, Variable, Absolute)
      0x09, 0x39,        //   Usage (Hat Switch)
      0x15, 0x00,        //   Logical Minimum (0)
      0x25, 0x07,        //   Logical Maximum (7)
      0x35, 0x00,        //   Physical Minimum (0)
      0x46, 0x3B, 0x01,  //   Physical Maximum (315)
      0x65, 0x14,        //   Unit (Degrees)
      0x75, 0x04,        //   Report Size (4)
      0x95, 0x01,        //   Report Count (1)
      0x81, 0x42,        //   Input (Data, Variable, Absolute, Null State)
      0x65, 0x00,        //   Unit (None)
      0x05, 0x09,        //   Usage Page (Button)
      0x19, 0x01,        //   Usage Minimum (1)
      0x29, 0x0E,        //   Usage Maximum (14)
      0x15, 0x00,        //   Logical Minimum (0)
      0x25, 0x01,        //   Logical Maximum (1)
      0x75, 0x01,        //   Report Size (1)
      0x95, 0x0E,        //   Report Count (14)
      0x81, 0x02,        //   Input (Data, Variable, Absolute)
      0x06, 0x00, 0xFF,  //   Usage Page (Vendor 0xFF00)
      0x09, 0x20,        //   Usage (0x20) - report counter
      0x75, 0x06,        //   Report Size (6)
      0x95, 0x01,        //   Report Count (1)
      0x15, 0x00,        //   Logical Minimum (0)
      0x25, 0x7F,        //   Logical Maximum (127)
      0x81, 0x02,        //   Input (Data, Variable, Absolute)
      0x05, 0x01,        //   Usage Page (Generic Desktop)
      0x09, 0x33,        //   Usage (Rx) - L2
      0x09, 0x34,        //   Usage (Ry) - R2
      0x15, 0x00,        //   Logical Minimum (0)
      0x26, 0xFF, 0x00,  //   Logical Maximum (255)
      0x75, 0x08,        //   Report Size (8)
      0x95, 0x02,        //   Report Count (2)
      0x81, 0x02,        //   Input (Data, Variable, Absolute)
      0x06, 0x00, 0xFF,  //   Usage Page (Vendor 0xFF00)
      0x09, 0x21,        //   Usage (0x21) - timestamp, IMU, battery, touchpad
      0x95, 0x36,        //   Report Count (54)
      0x81, 0x02,        //   Input (Data, Variable, Absolute)

      0x85, 0x05,        //   Report ID (5) - rumble / lightbar
      0x09, 0x22,        //   Usage (0x22)
      0x95, 0x1F,        //   Report Count (31)
      0x91, 0x02,        //   Output (Data, Variable, Absolute)

      0x85, 0x04,        //   Report ID (4)
      0x09, 0x23,        //   Usage (0x23)
      0x95, 0x24,        //   Report Count (36)
      0xB1, 0x02,        //   Feature (Data, Variable, Absolute)
      0x85, 0x02,        //   Report ID (2) - IMU calibration
      0x09, 0x24,        //   Usage (0x24)
      0x95, 0x24,        //   Report Count (36)
      0xB1, 0x02,        //   Feature (Data, Variable, Absolute)
      0x85, 0x08,        //   Report ID (8)
      0x09, 0x25,        //   Usage (0x25)
      0x95, 0x03,        //   Report Count (3)
      0xB1, 0x02,        //   Feature (Data, Variable, Absolute)
      0x85, 0x10,        //   Report ID (16)
      0x09, 0x26,        //   Usage (0x26)
      0x95, 0x04,        //   Report Count (4)
      0xB1, 0x02,        //   Feature (Data, Variable, Absolute)
      0x85, 0x11,        //   Report ID (17)
      0x09, 0x27,        //   Usage (0x27)
      0x95, 0x02,        //   Report Count (2)
      0xB1, 0x02,        //   Feature (Data, Variable, Absolute)
      0x85, 0x12,        //   Report ID (18) - pairing info / MAC
      0x06, 0x02, 0xFF,  //   Usage Page (Vendor 0xFF02)
      0x09, 0x21,        //   Usage (0x21)
      0x95, 0x0F,        //   Report Count (15)
      0xB1, 0x02,        //   Feature (Data, Variable, Absolute)
      0x85, 0x13,        //   Report ID (19)
      0x09, 0x22,        //   Usage (0x22)
      0x95, 0x16,        //   Report Count (22)
      0xB1, 0x02,        //   Feature (Data, Variable, Absolute)
      0x06, 0x80, 0xFF,  //   Usage Page (Vendor 0xFF80)
      0x85, 0xA3,        //   Report ID (163) - firmware info
      0x09, 0x43,        //   Usage (0x43)
      0x95, 0x30,        //   Report Count (48)
      0xB1, 0x02,        //   Feature (Data, Variable, Absolute)
      0xC0               // End Collection
    };
    *len = sizeof(kDescriptor);
    return kDescriptor;
  }

  // mac: 6 bytes, most significant first
  void begin(const uint8_t *mac) { std::memcpy(mac_, mac, 6); }

  void setState(const PadState &s) { state_ = s; }

  // Output report 0x05 (rumble / lightbar) needs no reply.
  void onOutput(uint8_t id, const uint8_t *data, uint16_t len) {
    (void) id;
    (void) data;
    (void) len;
  }

  /**
   * GET_REPORT(Feature). Fills `buf` without the report ID (TinyUSB prepends it).
   * Runs on the USB task, so it only reads data fixed at begin().
   */
  uint16_t getFeature(uint8_t id, uint8_t *buf, uint16_t len) const {
    uint8_t tmp[48] = {};
    uint16_t n = 0;
    switch (id) {
      case FEATURE_CALIBRATION: {
        // Values from a genuine DS4; SDL falls back to defaults if they look wrong.
        static const int16_t kCal[17] = {
          0, 0, 0,             // gyro bias pitch, yaw, roll
          8774, -8802,         // gyro pitch plus/minus
          8826, -8790,         // gyro yaw plus/minus
          8818, -8806,         // gyro roll plus/minus
          540, 540,            // gyro speed plus/minus
          8196, -8164,         // accel X plus/minus
          8236, -8156,         // accel Y plus/minus
          8058, -8306,         // accel Z plus/minus
        };
        for (int i = 0; i < 17; i++) {
          writeI16(&tmp[i * 2], kCal[i]);
        }
        n = 36;
        break;
      }
      case FEATURE_PAIRING_INFO:
        // Bytes 0..5: controller MAC, least significant first
        for (int i = 0; i < 6; i++) {
          tmp[i] = mac_[5 - i];
        }
        tmp[6] = 0x08;
        tmp[7] = 0x25;
        tmp[8] = 0x00;
        n = 15;
        break;
      case FEATURE_FIRMWARE_INFO: {
        static const char kDate[] = "Sep 21 2018";
        static const char kTime[] = "04:50:51";
        std::memcpy(&tmp[0], kDate, sizeof(kDate) - 1);
        std::memcpy(&tmp[16], kTime, sizeof(kTime) - 1);
        writeU16(&tmp[34], 0x00B4);  // hardware version
        writeU16(&tmp[40], 0x0702);  // firmware version
        n = 48;
        break;
      }
      default:
        return 0;
    }
    if (n > len) {
      n = len;
    }
    std::memcpy(buf, tmp, n);
    return n;
  }

  // Streams continuously like a real controller; timestamp and counter keep advancing.
  bool nextReport(uint32_t nowUs, HidReport &out) {
    if (started_ && static_cast<uint32_t>(nowUs - lastReportUs_) < REPORT_INTERVAL_US) {
      return false;
    }
    started_ = true;
    lastReportUs_ = nowUs;
    out.id = INPUT_REPORT_ID;
    out.len = INPUT_REPORT_LEN;
    encode(state_, counter_++, timestampTicks(nowUs), out.data);
    return true;
  }

  /**
   * Input report 0x01 body (report byte N is data[N - 1]):
   *   1-4 LX LY RX RY (0..255, Y down = 255), 5 hat + face buttons, 6 shoulders/sticks/share/options,
   *   7 PS + touchpad click + 6-bit counter, 8-9 L2 R2, 10-11 timestamp, 13-24 gyro/accel,
   *   30 battery, 35/39 touch points (bit 7 set = not touching).
   */
  static void encode(const PadState &s, uint8_t counter, uint16_t timestamp, uint8_t *d) {
    std::memset(d, 0, INPUT_REPORT_LEN);
    d[0] = static_cast<uint8_t>(axisToUnsigned(s.lx, 8));
    d[1] = static_cast<uint8_t>(axisToUnsigned(invertAxis(s.ly), 8));
    d[2] = static_cast<uint8_t>(axisToUnsigned(s.rx, 8));
    d[3] = static_cast<uint8_t>(axisToUnsigned(invertAxis(s.ry), 8));

    // Positional mapping: Xbox A/B/X/Y -> cross/circle/square/triangle
    uint8_t face = 0;
    if (s.buttons & BTN_X) face |= 0x10;  // square
    if (s.buttons & BTN_A) face |= 0x20;  // cross
    if (s.buttons & BTN_B) face |= 0x40;  // circle
    if (s.buttons & BTN_Y) face |= 0x80;  // triangle
    d[4] = static_cast<uint8_t>(face | (s.hat <= 7 ? s.hat : 0x08));

    uint8_t b = 0;
    if (s.buttons & BTN_LB) b |= 0x01;
    if (s.buttons & BTN_RB) b |= 0x02;
    if (s.lt > TRIGGER_DIGITAL_THRESHOLD) b |= 0x04;
    if (s.rt > TRIGGER_DIGITAL_THRESHOLD) b |= 0x08;
    if (s.buttons & BTN_BACK) b |= 0x10;   // share
    if (s.buttons & BTN_START) b |= 0x20;  // options
    if (s.buttons & BTN_L3) b |= 0x40;
    if (s.buttons & BTN_R3) b |= 0x80;
    d[5] = b;

    d[6] = static_cast<uint8_t>(((counter & 0x3F) << 2) | ((s.buttons & BTN_HOME) ? 0x01 : 0x00));
    d[7] = s.lt;
    d[8] = s.rt;
    writeU16(&d[9], timestamp);

    d[29] = 0x1B;  // cable connected, battery full
    d[32] = 0x01;  // one touch packet
    d[34] = 0x80;  // touch point 1 inactive
    d[38] = 0x80;  // touch point 2 inactive
  }

  // DS4 timestamps count in units of 16/3 us
  static uint16_t timestampTicks(uint32_t nowUs) {
    return static_cast<uint16_t>((static_cast<uint64_t>(nowUs) * 3) / 16);
  }

private:
  PadState state_;
  uint8_t mac_[6] = {};
  uint8_t counter_ = 0;
  uint32_t lastReportUs_ = 0;
  bool started_ = false;
};

}  // namespace lumina
