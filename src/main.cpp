/**
 * Lumina ESP32-S3 USB gamepad bridge.
 *
 * Enumerates as a composite USB device:
 *   - HID gamepad (same report descriptor as Lumina's hid_gamepad.m)
 *   - CDC serial port that receives gamepad state from Lumina
 *
 * Wire protocol (both directions):
 *   0xA5 0x5A <type> <len> <payload[len]> <crc8>
 *   crc8 is CRC-8/SMBUS (poly 0x07, init 0x00) over <type> <len> <payload>.
 *
 * Host -> board:
 *   0x01 PING    len 0
 *   0x02 REPORT  len 14: <pad> <13-byte HID report body, little-endian>
 * Board -> host:
 *   0x81 PONG    len 2: <protocol version> <pad count>
 *
 * Must stay in sync with Lumina/src/platform/macos/esp_gamepad.{h,m}.
 */
#include <Arduino.h>

#include "USB.h"
#include "USBHID.h"

#if ARDUINO_USB_MODE
  #error "Build with ARDUINO_USB_MODE=0 (USB-OTG/TinyUSB mode); see platformio.ini"
#endif

static constexpr uint16_t LUMINA_USB_VID = 0x1209;
static constexpr uint16_t LUMINA_USB_PID = 0x5854;

static constexpr uint8_t PROTOCOL_VERSION = 1;
static constexpr uint8_t PAD_COUNT = 1;

static constexpr uint8_t FRAME_SYNC0 = 0xA5;
static constexpr uint8_t FRAME_SYNC1 = 0x5A;
static constexpr uint8_t MSG_PING = 0x01;
static constexpr uint8_t MSG_REPORT = 0x02;
static constexpr uint8_t MSG_PONG = 0x81;

static constexpr uint8_t REPORT_ID = 0x01;
static constexpr size_t REPORT_BODY_LEN = 13;  // buttons(2) hat(1) triggers(2) sticks(8)

// Identical to kHIDReportDescriptor in Lumina/src/platform/macos/hid_gamepad.m
static const uint8_t kReportDescriptor[] = {
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

  // --- Triggers (2x 8-bit) on Z / Rz ---
  0x35, 0x00,        //   Physical Minimum (0)
  0x45, 0x00,        //   Physical Maximum (0 = use logical maximum)
  0x65, 0x00,        //   Unit (None)
  0x05, 0x01,        //   Usage Page (Generic Desktop)
  0x09, 0x32,        //   Usage (Z) - Left Trigger
  0x09, 0x35,        //   Usage (Rz) - Right Trigger
  0x15, 0x00,        //   Logical Minimum (0)
  0x26, 0xFF, 0x00,  //   Logical Maximum (255)
  0x75, 0x08,        //   Report Size (8)
  0x95, 0x02,        //   Report Count (2)
  0x81, 0x02,        //   Input (Data, Variable, Absolute)

  // --- Stick Axes (4x 16-bit signed) ---
  0x05, 0x01,        //   Usage Page (Generic Desktop)
  0x09, 0x30,        //   Usage (X) - Left Stick X
  0x09, 0x31,        //   Usage (Y) - Left Stick Y
  0x09, 0x33,        //   Usage (Rx) - Right Stick X
  0x09, 0x34,        //   Usage (Ry) - Right Stick Y
  0x16, 0x00, 0x80,  //   Logical Minimum (-32768)
  0x26, 0xFF, 0x7F,  //   Logical Maximum (32767)
  0x36, 0x00, 0x80,  //   Physical Minimum (-32768)
  0x46, 0xFF, 0x7F,  //   Physical Maximum (32767)
  0x75, 0x10,        //   Report Size (16)
  0x95, 0x04,        //   Report Count (4)
  0x81, 0x02,        //   Input (Data, Variable, Absolute)

  0xC0               // End Collection
};

// Neutral state: no buttons, hat = 8 (null), triggers and sticks at 0
static const uint8_t kNeutralBody[REPORT_BODY_LEN] = {0x00, 0x00, 0x08};

class LuminaGamepad : public USBHIDDevice {
public:
  uint16_t _onGetDescriptor(uint8_t *dst) override {
    memcpy(dst, kReportDescriptor, sizeof(kReportDescriptor));
    return sizeof(kReportDescriptor);
  }
};

USBCDC LinkSerial;
USBHID HID;
LuminaGamepad Gamepad;

static uint8_t pendingBody[REPORT_BODY_LEN];
static bool reportDirty = false;

static uint8_t crc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x80) ? (uint8_t) ((crc << 1) ^ 0x07) : (uint8_t) (crc << 1);
    }
  }
  return crc;
}

static void setPending(const uint8_t *body) {
  memcpy(pendingBody, body, REPORT_BODY_LEN);
  reportDirty = true;
}

static void sendPong() {
  uint8_t frame[7] = {FRAME_SYNC0, FRAME_SYNC1, MSG_PONG, 2, PROTOCOL_VERSION, PAD_COUNT, 0};
  frame[6] = crc8(&frame[2], 4);
  LinkSerial.write(frame, sizeof(frame));
  LinkSerial.flush();
}

static void handleFrame(uint8_t type, const uint8_t *payload, uint8_t len) {
  switch (type) {
    case MSG_PING:
      sendPong();
      break;
    case MSG_REPORT:
      if (len == 1 + REPORT_BODY_LEN && payload[0] < PAD_COUNT) {
        setPending(&payload[1]);
      }
      break;
    default:
      break;
  }
}

/**
 * Byte-at-a-time frame parser. Resyncs on the next 0xA5 0x5A after any error.
 */
class FrameParser {
public:
  void feed(uint8_t byte) {
    switch (state) {
      case State::Sync0:
        if (byte == FRAME_SYNC0) {
          state = State::Sync1;
        }
        break;
      case State::Sync1:
        state = (byte == FRAME_SYNC1) ? State::Type : (byte == FRAME_SYNC0 ? State::Sync1 : State::Sync0);
        break;
      case State::Type:
        buf[0] = byte;
        state = State::Len;
        break;
      case State::Len:
        buf[1] = byte;
        received = 0;
        state = byte ? State::Payload : State::Crc;
        break;
      case State::Payload:
        buf[2 + received++] = byte;
        if (received == buf[1]) {
          state = State::Crc;
        }
        break;
      case State::Crc:
        if (crc8(buf, 2 + buf[1]) == byte) {
          handleFrame(buf[0], &buf[2], buf[1]);
        }
        state = State::Sync0;
        break;
    }
  }

private:
  enum class State { Sync0, Sync1, Type, Len, Payload, Crc };
  State state = State::Sync0;
  uint8_t buf[2 + 255] = {};  // type, len, payload
  uint8_t received = 0;
};

static FrameParser parser;
static bool hostConnected = false;

void setup() {
  USB.VID(LUMINA_USB_VID);
  USB.PID(LUMINA_USB_PID);
  USB.manufacturerName("Lumina");
  USB.productName("Lumina ESP Gamepad");
  USB.serialNumber("LUMINA-ESP-0");

  HID.addDevice(&Gamepad, sizeof(kReportDescriptor));
  HID.begin();
  LinkSerial.begin(115200);
  USB.begin();

  setPending(kNeutralBody);
}

void loop() {
  // DTR drops when Lumina closes the port or exits; never leave inputs held
  bool connected = (bool) LinkSerial;
  if (hostConnected && !connected) {
    setPending(kNeutralBody);
  }
  hostConnected = connected;

  int budget = 512;
  while (budget-- > 0 && LinkSerial.available() > 0) {
    parser.feed((uint8_t) LinkSerial.read());
  }

  // Only the latest state matters, so bursts from the host collapse into one report
  if (reportDirty && HID.ready()) {
    if (HID.SendReport(REPORT_ID, pendingBody, REPORT_BODY_LEN, 5)) {
      reportDirty = false;
    }
  }
}
