/**
 * Lumina ESP32-S3 USB gamepad bridge.
 *
 * Enumerates as a composite USB device:
 *   - HID gamepad, emulating a generic joystick, a DualShock 4 or a Switch Pro Controller
 *   - CDC serial port that receives gamepad state from Lumina
 *
 * The emulated controller is stored in NVS and selected at boot. MSG_SET_PROFILE or the BOOT
 * button changes it: the board saves the choice and restarts as the new controller.
 *
 * Wire protocol: see include/protocol.h.
 * Must stay in sync with Lumina/src/platform/macos/esp_gamepad.{h,m}.
 */
#include <Arduino.h>
#include <Preferences.h>

#include "USB.h"
#include "esp32-hal-tinyusb.h"
#include "device/usbd_pvt.h"
#include "hid_output.h"
#include "pad_state.h"
#include "profiles.h"
#include "protocol.h"

#if ARDUINO_USB_MODE
  #error "Build with ARDUINO_USB_MODE=0 (USB-OTG/TinyUSB mode); see platformio.ini"
#endif

// Profile used until one is chosen (wire id, see profiles.h): Switch Pro
#ifndef LUMINA_DEFAULT_PROFILE
  #define LUMINA_DEFAULT_PROFILE 2
#endif

// BOOT button on DevKitC boards; pressing it cycles the emulated controller
#ifndef PROFILE_BUTTON_PIN
  #define PROFILE_BUTTON_PIN 0
#endif

using namespace lumina;

// Threading: every TinyUSB call and all profile state live on the TinyUSB task, pinned to
// the USB interrupt's core (see pinUsbTaskToInterruptCore); loop() only schedules
// usbService() onto it. TinyUSB serialises endpoints but not the controller registers they
// share, so this keeps the driver single-threaded. (The HID IN corruption seen on the
// Arduino 2.0.17 core was TinyUSB 0.16's own DWC2 bug, fixed by the 3.x core; see platformio.ini.)
static AnyProfile profile;
static Preferences settings;
static volatile int requestedProfile = -1;  // set on the usbd task, applied on the loop task

static HidReport pendingReport;
static bool havePendingReport = false;

// DevKitC-1 v1.0 puts the RGB LED on RGB_BUILTIN (GPIO 48); v1.1 uses GPIO 38.
// Override with -DSTATUS_LED_PIN=38 in platformio.ini if your board stays dark.
#if !defined(STATUS_LED_PIN) && defined(RGB_BUILTIN)
  #define STATUS_LED_PIN RGB_BUILTIN
#endif

// Pulses in the profile's colour while a host holds the serial link open, amber while idle.
// The LED pulses as long as the main loop runs, so a frozen LED means the firmware has stopped.
static void setLed(uint8_t r, uint8_t g, uint8_t b) {
#ifdef STATUS_LED_PIN
  rgbLedWrite(STATUS_LED_PIN, r, g, b);
#else
  (void) r;
  (void) g;
  (void) b;
#endif
}

struct Rgb {
  uint8_t r, g, b;
};

// Green = generic, blue = DualShock 4, red = Switch Pro
static Rgb profileColor(ProfileKind kind) {
  switch (kind) {
    case ProfileKind::Generic: return {0, 12, 0};
    case ProfileKind::Ds4: return {0, 0, 12};
    case ProfileKind::SwitchPro: return {12, 0, 0};
  }
  return {12, 12, 12};
}

static constexpr Rgb kIdleColor = {12, 6, 0};  // amber: no host holds the serial link

static void showProfileColor(ProfileKind kind) {
  Rgb c = profileColor(kind);
  setLed(c.r, c.g, c.b);
}

static void updateStatusLed(bool connected) {
#ifdef STATUS_LED_PIN
  static uint32_t lastToggleMs = 0;
  static bool bright = false;
  static int lastState = -1;
  uint32_t now = millis();
  int state = connected ? 1 : 0;
  if (state == lastState && now - lastToggleMs < 500) {
    return;
  }
  if (state == lastState) {
    bright = !bright;
  }
  lastToggleMs = now;
  lastState = state;
  Rgb c = connected ? profileColor(profile.kind()) : kIdleColor;
  uint8_t div = bright ? 1 : 3;  // dim half of the pulse
  setLed(c.r / div, c.g / div, c.b / div);
#else
  (void) connected;
#endif
}

// ---- HID interface, driven directly through TinyUSB ----
// Arduino's USBHID only routes output/feature reports whose IDs its descriptor parser
// recognises, which excludes the vendor-defined reports these controllers rely on.

static uint16_t loadHidDescriptor(uint8_t *dst, uint8_t *itf) {
  static bool loaded = false;
  if (loaded) {
    return 0;
  }
  loaded = true;

  uint16_t reportLen = 0;
  profile.descriptor(&reportLen);
  uint8_t epIn = tinyusb_get_free_in_endpoint();
  uint8_t epOut = tinyusb_get_free_out_endpoint();
  if (!epIn || !epOut) {
    return 0;
  }
  uint8_t descriptor[TUD_HID_INOUT_DESC_LEN] = {
    TUD_HID_INOUT_DESCRIPTOR(*itf, 0, HID_ITF_PROTOCOL_NONE, reportLen, epOut, (uint8_t) (0x80 | epIn), 64, 1)
  };
  *itf += 1;
  memcpy(dst, descriptor, sizeof(descriptor));
  return sizeof(descriptor);
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
  (void) instance;
  uint16_t len = 0;
  return profile.descriptor(&len);
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) {
  (void) instance;
  if (report_type != HID_REPORT_TYPE_FEATURE) {
    return 0;
  }
  return profile.getFeature(report_id, buffer, reqlen);
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {
  (void) instance;
  OutputReport report;
  if (parseOutputReport(report_id, (uint8_t) report_type, buffer, bufsize, report)) {
    profile.onOutput(report.id, report.data, report.len);  // already on the usbd task
  }
}

// ---- CDC serial interface, driven directly through TinyUSB ----
// Arduino's USBCDC posts events with portMAX_DELAY from the USB task (deadlocking it on
// some host resets) and reboots into the bootloader on esptool's DTR/RTS pattern, which
// any serial app can trigger. Flashing uses the 1200 bps touch instead (see platformio.ini).

static constexpr uint32_t BOOTLOADER_TOUCH_BAUD = 1200;
static volatile bool bootloaderRequested = false;

static uint16_t loadCdcDescriptor(uint8_t *dst, uint8_t *itf) {
  // Endpoints 0x85 (notify), 0x03 (OUT) and 0x84 (IN) are reserved for CDC by
  // tinyusb_enable_interface(USB_INTERFACE_CDC, ...).
  uint8_t strIndex = tinyusb_add_string_descriptor("Lumina Link");
  uint8_t descriptor[TUD_CDC_DESC_LEN] = {
    TUD_CDC_DESCRIPTOR(*itf, strIndex, 0x85, 64, 0x03, 0x84, 64)
  };
  *itf += 2;
  memcpy(dst, descriptor, sizeof(descriptor));
  return sizeof(descriptor);
}

void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *coding) {
  (void) itf;
  if (coding->bit_rate == BOOTLOADER_TOUCH_BAUD) {
    bootloaderRequested = true;
  }
}

// ---- Serial link from Lumina ----

static void sendFrame(uint8_t type, const uint8_t *payload, uint8_t len) {
  uint8_t frame[4 + 2 + 1] = {FRAME_SYNC0, FRAME_SYNC1, type, len};
  memcpy(&frame[4], payload, len);
  frame[4 + len] = crc8(&frame[2], 2 + len);
  // Never wait for space: if the host isn't reading, it can ask again
  if (tud_cdc_write_available() >= 5u + len) {
    tud_cdc_write(frame, 5 + len);
    tud_cdc_write_flush();
  }
}

static void sendPong() {
  const uint8_t payload[2] = {PROTOCOL_VERSION, PAD_COUNT};
  sendFrame(MSG_PONG, payload, sizeof(payload));
}

static void sendProfile(uint8_t id) { sendFrame(MSG_PROFILE, &id, 1); }

static void handleFrame(uint8_t type, const uint8_t *payload, uint8_t len) {
  switch (type) {
    case MSG_PING:
      sendPong();
      break;
    case MSG_REPORT:
      if (len == 1 + REPORT_BODY_LEN && payload[0] < PAD_COUNT) {
        profile.setState(decodeWire(&payload[1]));
      }
      break;
    case MSG_GET_PROFILE:
      sendProfile(static_cast<uint8_t>(profile.kind()));
      break;
    case MSG_SET_PROFILE:
      if (len == 1 && isValidProfile(payload[0])) {
        sendProfile(payload[0]);  // confirm before the board restarts
        requestedProfile = payload[0];
      } else {
        sendProfile(static_cast<uint8_t>(profile.kind()));
      }
      break;
    default:
      break;
  }
}

static FrameParser parser(handleFrame);
static bool hostConnected = false;
static volatile bool linkConnected = false;   // for the LED on the loop task
static volatile bool servicePending = false;

// Runs on the usbd task: serial link in, HID reports out.
static void usbService(void *) {
  // DTR drops when Lumina closes the port or exits; never leave inputs held
  bool connected = tud_cdc_connected();
  if (connected != hostConnected) {
    hostConnected = connected;
    linkConnected = connected;
    if (!connected) {
      parser.reset();
      profile.setState(PadState());
    }
  }

  uint8_t rx[64];
  for (int budget = 8; budget > 0 && tud_cdc_available() > 0; budget--) {
    uint32_t n = tud_cdc_read(rx, sizeof(rx));
    for (uint32_t i = 0; i < n; i++) {
      parser.feed(rx[i]);
    }
  }

  if (!havePendingReport && tud_hid_ready()) {
    havePendingReport = profile.nextReport(micros(), pendingReport);
  }
  if (havePendingReport && tud_hid_ready()) {
    if (tud_hid_report(pendingReport.id, pendingReport.data, pendingReport.len)) {
      havePendingReport = false;
    }
  }

  servicePending = false;
}

// The Arduino core creates TinyUSB's task without core affinity, while the USB interrupt is
// allocated on the core that called USB.begin(). TinyUSB's DWC2 driver updates shared
// controller registers from both, which is only safe when they can't run in parallel, so
// replace the task with one pinned to the interrupt's core.
static void pinnedUsbTask(void *) {
  while (true) {
    tud_task();
  }
}

static void pinUsbTaskToInterruptCore() {
  TaskHandle_t arduinoUsbd = xTaskGetHandle("usbd");
  if (arduinoUsbd == nullptr) {
    return;
  }
  // Only take over while it is idle in its event queue, never mid-event
  while (eTaskGetState(arduinoUsbd) != eBlocked) {
    delay(1);
  }
  vTaskSuspend(arduinoUsbd);
  xTaskCreatePinnedToCore(pinnedUsbTask, "usbd_pinned", 4096, nullptr, configMAX_PRIORITIES - 1, nullptr, xPortGetCoreID());
}

static void loadProfile() {
  settings.begin("lumina", false);
  uint8_t id = settings.getUChar("profile", LUMINA_DEFAULT_PROFILE);
  if (!isValidProfile(id)) {
    id = LUMINA_DEFAULT_PROFILE;
  }
  profile.select(static_cast<ProfileKind>(id));
}

/**
 * Saves the profile and restarts as that controller. The host sees the board unplug and
 * re-enumerate with the new USB identity; a software restart doesn't enter the bootloader.
 */
static void switchProfile(ProfileKind kind) {
  if (kind == profile.kind()) {
    return;
  }
  settings.putUChar("profile", static_cast<uint8_t>(kind));
  for (int i = 0; i < 3; i++) {
    showProfileColor(kind);
    delay(150);
    setLed(0, 0, 0);
    delay(100);
  }
  usbd_defer_func([](void *) { tud_disconnect(); }, nullptr, false);
  delay(100);
  esp_restart();
}

static void checkProfileButton() {
  static bool wasDown = false;
  static uint32_t downSinceMs = 0;
  bool down = digitalRead(PROFILE_BUTTON_PIN) == LOW;
  uint32_t now = millis();
  if (down && !wasDown) {
    downSinceMs = now;
  } else if (!down && wasDown && now - downSinceMs >= 50) {
    requestedProfile = static_cast<int>(nextProfile(profile.kind()));
  }
  wasDown = down;
}

void setup() {
  loadProfile();
  pinMode(PROFILE_BUTTON_PIN, INPUT_PULLUP);
  showProfileColor(profile.kind());  // briefly shows which controller this boot emulates
  delay(400);

  uint64_t efuseMac = ESP.getEfuseMac();
  uint8_t mac[6];
  for (int i = 0; i < 6; i++) {
    mac[i] = (uint8_t) (efuseMac >> (8 * i));  // eFuse stores the first MAC byte lowest
  }
  profile.begin(mac);

  // Lumina finds the board by this serial number prefix, whatever controller it emulates
  static char serialNum[24];
  snprintf(serialNum, sizeof(serialNum), "LUMINA-%012llX", (unsigned long long) efuseMac);

  UsbIdentity id = profile.identity();
  USB.VID(id.vid);
  USB.PID(id.pid);
  USB.firmwareVersion(id.bcd);
  USB.manufacturerName(id.manufacturer);
  USB.productName(id.product);
  USB.serialNumber(serialNum);

  tinyusb_enable_interface(USB_INTERFACE_CDC, TUD_CDC_DESC_LEN, loadCdcDescriptor);
  tinyusb_enable_interface(USB_INTERFACE_HID, TUD_HID_INOUT_DESC_LEN, loadHidDescriptor);
  USB.begin();  // allocates the USB interrupt on this core
  pinUsbTaskToInterruptCore();

  updateStatusLed(false);
}

// USB stall watchdog. The host polls the HID IN endpoint every 1 ms while configured,
// so an endpoint that stays busy for seconds means the USB task has stopped. Aborting
// saves a core dump of every task's stack to the coredump partition (see README) and
// restarts the chip, which brings USB back without a replug.
static constexpr uint32_t USB_STALL_TIMEOUT_MS = 3000;

static void checkUsbStall() {
  static uint32_t lastProgressMs = 0;
  uint32_t now = millis();
  bool stuck = tud_mounted() && !tud_suspended() && !tud_hid_ready();
  if (!stuck) {
    lastProgressMs = now;
  } else if (now - lastProgressMs > USB_STALL_TIMEOUT_MS) {
    abort();
  }
}

void loop() {
  if (bootloaderRequested) {
    usb_persist_restart(RESTART_BOOTLOADER);
  }
  checkProfileButton();
  if (requestedProfile >= 0) {
    ProfileKind kind = static_cast<ProfileKind>(requestedProfile);
    requestedProfile = -1;
    delay(50);  // let the MSG_PROFILE confirmation reach the host
    switchProfile(kind);
  }

  // One service run per millisecond; the delay also keeps the usbd task (top priority)
  // from starving the idle task watched by the task watchdog.
  if (!servicePending) {
    servicePending = true;
    usbd_defer_func(usbService, nullptr, false);
  }

  updateStatusLed(linkConnected);
  checkUsbStall();
  delay(1);
}
