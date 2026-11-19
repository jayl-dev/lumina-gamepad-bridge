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
#include "protocol.h"

#if ARDUINO_USB_MODE
  #error "Build with ARDUINO_USB_MODE=0 (USB-OTG/TinyUSB mode); see platformio.ini"
#endif

using namespace lumina;

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
static uint8_t lastSentBody[REPORT_BODY_LEN];
static bool hasSentReport = false;
static bool reportDirty = false;

// DevKitC-1 v1.0 puts the RGB LED on RGB_BUILTIN (GPIO 48); v1.1 uses GPIO 38.
// Override with -DSTATUS_LED_PIN=38 in platformio.ini if your board stays dark.
#if !defined(STATUS_LED_PIN) && defined(RGB_BUILTIN)
  #define STATUS_LED_PIN RGB_BUILTIN
#endif

static void setStatusLed(bool connected) {
#ifdef STATUS_LED_PIN
  // Dim green when host holds the serial link open, dim amber while idle/waiting.
  neopixelWrite(STATUS_LED_PIN, connected ? 0 : 12, connected ? 12 : 6, 0);
#else
  (void) connected;
#endif
}

static void setPending(const uint8_t *body) {
  memcpy(pendingBody, body, REPORT_BODY_LEN);
  reportDirty = !hasSentReport || memcmp(pendingBody, lastSentBody, REPORT_BODY_LEN) != 0;
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

static FrameParser parser(handleFrame);
static bool hostConnected = false;

void setup() {
  static char serialNum[24];
  snprintf(serialNum, sizeof(serialNum), "LUMINA-%012llX", (unsigned long long) ESP.getEfuseMac());

  USB.VID(LUMINA_USB_VID);
  USB.PID(LUMINA_USB_PID);
  USB.manufacturerName("Lumina");
  USB.productName("Lumina ESP Gamepad");
  USB.serialNumber(serialNum);

  HID.addDevice(&Gamepad, sizeof(kReportDescriptor));
  HID.begin();
  LinkSerial.begin(115200);
  USB.begin();

  setStatusLed(false);
  setPending(kNeutralBody);
}

void loop() {
  // DTR drops when Lumina closes the port or exits; never leave inputs held
  bool connected = (bool) LinkSerial;
  if (connected != hostConnected) {
    hostConnected = connected;
    setStatusLed(connected);
    if (!connected) {
      parser.reset();
      setPending(kNeutralBody);
    }
  }

  int budget = 512;
  while (budget-- > 0 && LinkSerial.available() > 0) {
    parser.feed((uint8_t) LinkSerial.read());
  }

  // Only the latest state matters, so bursts from the host collapse into one report
  if (reportDirty && HID.ready()) {
    if (HID.SendReport(REPORT_ID, pendingBody, REPORT_BODY_LEN, 5)) {
      memcpy(lastSentBody, pendingBody, REPORT_BODY_LEN);
      hasSentReport = true;
      reportDirty = false;
    }
  }
}
