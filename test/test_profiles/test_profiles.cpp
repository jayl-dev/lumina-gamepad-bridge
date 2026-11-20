#include <unity.h>

#include <cstring>
#include <map>
#include <utility>

#include "hid_output.h"
#include "pad_state.h"
#include "profile_ds4.h"
#include "profile_generic.h"
#include "profile_switch_pro.h"
#include "profiles.h"

using namespace lumina;

namespace {

const uint8_t kMac[6] = {0x7C, 0x4F, 0xAD, 0x1D, 0x0D, 0x60};

enum ReportKind { KIND_INPUT = 0x81, KIND_OUTPUT = 0x91, KIND_FEATURE = 0xB1 };

/**
 * Minimal HID report descriptor walker: total bits per (main item kind, report ID).
 * Enough to check hand-written descriptors against the real controllers' report sizes.
 */
std::map<std::pair<int, int>, int> reportBits(const uint8_t *d, uint16_t len) {
  std::map<std::pair<int, int>, int> bits;
  int size = 0, count = 0, id = 0;
  for (uint16_t i = 0; i < len;) {
    uint8_t prefix = d[i];
    uint8_t n = prefix & 0x03;
    if (n == 3) {
      n = 4;
    }
    uint32_t value = 0;
    for (uint8_t b = 0; b < n; b++) {
      value |= static_cast<uint32_t>(d[i + 1 + b]) << (8 * b);
    }
    switch (prefix & 0xFC) {
      case 0x74: size = static_cast<int>(value); break;   // Report Size
      case 0x94: count = static_cast<int>(value); break;  // Report Count
      case 0x84: id = static_cast<int>(value); break;     // Report ID
      case 0x80: bits[{KIND_INPUT, id}] += size * count; break;
      case 0x90: bits[{KIND_OUTPUT, id}] += size * count; break;
      case 0xB0: bits[{KIND_FEATURE, id}] += size * count; break;
      default: break;
    }
    i = static_cast<uint16_t>(i + 1 + n);
  }
  return bits;
}

int bytesOf(const std::map<std::pair<int, int>, int> &bits, int kind, int id) {
  auto it = bits.find({kind, id});
  TEST_ASSERT_TRUE_MESSAGE(it != bits.end(), "report missing from descriptor");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, it->second % 8, "report is not byte aligned");
  return it->second / 8;
}

// SDL's 12-bit stick unpacking (SDL_hidapi_switch.c)
void unpackStick(const uint8_t *p, uint16_t *x, uint16_t *y) {
  *x = static_cast<uint16_t>(p[0] | ((p[1] & 0x0F) << 8));
  *y = static_cast<uint16_t>(((p[1] & 0xF0) >> 4) | (p[2] << 4));
}

}  // namespace

void setUp() {}
void tearDown() {}

// ---- Descriptors ----

void test_ds4_descriptor_report_sizes() {
  uint16_t len = 0;
  const uint8_t *d = Ds4Profile::descriptor(&len);
  auto bits = reportBits(d, len);
  TEST_ASSERT_EQUAL_INT(Ds4Profile::INPUT_REPORT_LEN, bytesOf(bits, KIND_INPUT, 0x01));
  TEST_ASSERT_EQUAL_INT(31, bytesOf(bits, KIND_OUTPUT, 0x05));
  TEST_ASSERT_EQUAL_INT(36, bytesOf(bits, KIND_FEATURE, 0x02));
  TEST_ASSERT_EQUAL_INT(15, bytesOf(bits, KIND_FEATURE, 0x12));
  TEST_ASSERT_EQUAL_INT(48, bytesOf(bits, KIND_FEATURE, 0xA3));
}

void test_switch_descriptor_report_sizes() {
  uint16_t len = 0;
  const uint8_t *d = SwitchProProfile::descriptor(&len);
  auto bits = reportBits(d, len);
  for (int id : {0x30, 0x21, 0x81}) {
    TEST_ASSERT_EQUAL_INT(SwitchProProfile::REPORT_LEN, bytesOf(bits, KIND_INPUT, id));
  }
  for (int id : {0x01, 0x10, 0x80, 0x82}) {
    TEST_ASSERT_EQUAL_INT(63, bytesOf(bits, KIND_OUTPUT, id));
  }
}

void test_generic_descriptor_report_size() {
  uint16_t len = 0;
  const uint8_t *d = GenericProfile::descriptor(&len);
  auto bits = reportBits(d, len);
  TEST_ASSERT_EQUAL_INT(GenericProfile::REPORT_LEN, bytesOf(bits, KIND_INPUT, GenericProfile::REPORT_ID));
}

// ---- Generic ----

void test_generic_sends_neutral_once_then_only_changes() {
  GenericProfile p;
  HidReport r;
  TEST_ASSERT_TRUE(p.nextReport(0, r));  // initial neutral state
  TEST_ASSERT_FALSE(p.nextReport(1000, r));

  PadState s;
  p.setState(s);  // unchanged
  TEST_ASSERT_FALSE(p.nextReport(2000, r));

  s.buttons = BTN_A;
  p.setState(s);
  TEST_ASSERT_TRUE(p.nextReport(3000, r));
  TEST_ASSERT_EQUAL_HEX8(0x01, r.data[0]);
}

// ---- DualShock 4 ----

void test_ds4_neutral_report() {
  uint8_t d[Ds4Profile::INPUT_REPORT_LEN];
  Ds4Profile::encode(PadState(), 0, 0, d);
  for (int i = 0; i < 4; i++) {
    TEST_ASSERT_EQUAL_HEX8(0x80, d[i]);  // sticks centered
  }
  TEST_ASSERT_EQUAL_HEX8(0x08, d[4]);    // hat centered, no face buttons
  TEST_ASSERT_EQUAL_HEX8(0x00, d[5]);
  TEST_ASSERT_EQUAL_HEX8(0x00, d[30] & 0x04);  // report byte 31: "no controller" bit clear
  TEST_ASSERT_EQUAL_HEX8(0x80, d[34]);   // touch points inactive
  TEST_ASSERT_EQUAL_HEX8(0x80, d[38]);
}

void test_ds4_buttons_sticks_triggers() {
  PadState s;
  s.buttons = BTN_A | BTN_Y | BTN_LB | BTN_START | BTN_R3 | BTN_HOME;
  s.hat = 2;  // right
  s.lt = 200;
  s.rt = 0;
  s.lx = 32767;
  s.ly = 32767;   // full up
  s.rx = -32768;
  s.ry = -32768;  // full down

  uint8_t d[Ds4Profile::INPUT_REPORT_LEN];
  Ds4Profile::encode(s, 5, 0x1234, d);

  TEST_ASSERT_EQUAL_HEX8(0xFF, d[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00, d[1]);  // DS4 Y is down = 255, so up = 0
  TEST_ASSERT_EQUAL_HEX8(0x00, d[2]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, d[3]);
  TEST_ASSERT_EQUAL_HEX8(0x20 | 0x80 | 0x02, d[4]);  // cross + triangle, hat right
  TEST_ASSERT_EQUAL_HEX8(0x01 | 0x04 | 0x20 | 0x80, d[5]);  // L1, L2 digital, options, R3
  TEST_ASSERT_EQUAL_HEX8((5 << 2) | 0x01, d[6]);  // counter + PS
  TEST_ASSERT_EQUAL_UINT8(200, d[7]);
  TEST_ASSERT_EQUAL_UINT8(0, d[8]);
  TEST_ASSERT_EQUAL_HEX8(0x34, d[9]);
  TEST_ASSERT_EQUAL_HEX8(0x12, d[10]);
}

void test_ds4_streams_every_4ms_with_counter() {
  Ds4Profile p;
  p.begin(kMac);
  HidReport r;
  TEST_ASSERT_TRUE(p.nextReport(1000, r));
  TEST_ASSERT_EQUAL_HEX8(0x01, r.id);
  TEST_ASSERT_EQUAL_UINT8(63, r.len);
  uint8_t c0 = r.data[6] >> 2;
  TEST_ASSERT_FALSE(p.nextReport(4999, r));
  TEST_ASSERT_TRUE(p.nextReport(5000, r));
  TEST_ASSERT_EQUAL_UINT8((c0 + 1) & 0x3F, r.data[6] >> 2);
}

void test_ds4_feature_reports() {
  Ds4Profile p;
  p.begin(kMac);
  uint8_t buf[64];

  TEST_ASSERT_EQUAL_UINT16(15, p.getFeature(0x12, buf, sizeof(buf)));
  for (int i = 0; i < 6; i++) {
    TEST_ASSERT_EQUAL_HEX8(kMac[5 - i], buf[i]);  // least significant byte first
  }

  TEST_ASSERT_EQUAL_UINT16(36, p.getFeature(0x02, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_INT16(8774, readI16(&buf[6]));  // gyro pitch plus at report bytes 7-8

  TEST_ASSERT_EQUAL_UINT16(48, p.getFeature(0xA3, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_UINT16(0, p.getFeature(0x99, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_UINT16(10, p.getFeature(0x02, buf, 10));  // truncated to request
}

// ---- Switch Pro ----

void test_switch_header_buttons_and_dpad() {
  PadState s;
  s.buttons = BTN_A | BTN_B | BTN_X | BTN_Y | BTN_RB | BTN_LB | BTN_BACK | BTN_START | BTN_HOME | BTN_L3;
  s.hat = 7;  // up-left
  s.lt = 255;
  s.rt = 10;  // below the ZR threshold

  uint8_t d[SwitchProProfile::REPORT_LEN] = {};
  SwitchProProfile::encodeHeader(s, 0x42, d);
  TEST_ASSERT_EQUAL_HEX8(0x42, d[0]);
  TEST_ASSERT_EQUAL_HEX8(0x01 | 0x02 | 0x04 | 0x08 | 0x40, d[2]);   // Y X B A R, no ZR
  TEST_ASSERT_EQUAL_HEX8(0x01 | 0x02 | 0x08 | 0x10 | 0x80, d[3]);   // - + LS home, grip bit
  TEST_ASSERT_EQUAL_HEX8(0x02 | 0x08 | 0x40 | 0x80, d[4]);          // up, left, L, ZL
}

void test_switch_positional_face_buttons() {
  PadState s;
  s.buttons = BTN_A;  // bottom face button
  uint8_t d[SwitchProProfile::REPORT_LEN] = {};
  SwitchProProfile::encodeHeader(s, 0, d);
  TEST_ASSERT_EQUAL_HEX8(0x04, d[2]);  // Nintendo B = SDL SOUTH
}

void test_switch_sticks_12bit() {
  PadState s;
  s.lx = 32767;
  s.ly = 32767;  // full up
  s.rx = 0;
  s.ry = -32768;
  uint8_t d[SwitchProProfile::REPORT_LEN] = {};
  SwitchProProfile::encodeHeader(s, 0, d);

  uint16_t x, y;
  unpackStick(&d[5], &x, &y);
  TEST_ASSERT_EQUAL_UINT16(4095, x);
  TEST_ASSERT_EQUAL_UINT16(4095, y);  // Switch Y is positive = up
  unpackStick(&d[8], &x, &y);
  TEST_ASSERT_EQUAL_UINT16(2048, x);
  TEST_ASSERT_EQUAL_UINT16(0, y);
}

void test_switch_sdl_usb_handshake() {
  SwitchProProfile p;
  p.begin(kMac);
  HidReport r;
  uint8_t cmd[63] = {};

  // Nothing streams before the host sets things up
  TEST_ASSERT_FALSE(p.nextReport(0, r));

  cmd[0] = SwitchProProfile::USB_STATUS;
  p.onOutput(SwitchProProfile::OUT_USB, cmd, sizeof(cmd));
  TEST_ASSERT_TRUE(p.nextReport(100, r));
  TEST_ASSERT_EQUAL_HEX8(0x81, r.id);
  TEST_ASSERT_EQUAL_HEX8(0x01, r.data[0]);
  TEST_ASSERT_EQUAL_HEX8(0x03, r.data[2]);  // report byte 3: Pro Controller
  for (int i = 0; i < 6; i++) {
    TEST_ASSERT_EQUAL_HEX8(kMac[5 - i], r.data[3 + i]);
  }

  for (uint8_t c : {SwitchProProfile::USB_HANDSHAKE, SwitchProProfile::USB_HIGH_SPEED, SwitchProProfile::USB_HANDSHAKE}) {
    cmd[0] = c;
    p.onOutput(SwitchProProfile::OUT_USB, cmd, sizeof(cmd));
    TEST_ASSERT_TRUE(p.nextReport(200, r));
    TEST_ASSERT_EQUAL_HEX8(0x81, r.id);
    TEST_ASSERT_EQUAL_HEX8(c, r.data[0]);
  }

  // Force USB: no reply, starts the 0x30 stream
  cmd[0] = SwitchProProfile::USB_FORCE_USB;
  p.onOutput(SwitchProProfile::OUT_USB, cmd, sizeof(cmd));
  TEST_ASSERT_TRUE(p.streaming());
  TEST_ASSERT_TRUE(p.nextReport(1000, r));
  TEST_ASSERT_EQUAL_HEX8(0x30, r.id);
  TEST_ASSERT_EQUAL_UINT8(63, r.len);
  uint8_t t0 = r.data[0];
  TEST_ASSERT_FALSE(p.nextReport(8999, r));
  TEST_ASSERT_TRUE(p.nextReport(9000, r));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(t0 + 1), r.data[0]);
}

static void sendSubcommand(SwitchProProfile &p, uint8_t sub, const uint8_t *args, uint8_t argLen) {
  uint8_t out[63] = {};
  out[0] = 0x05;  // packet counter
  out[9] = sub;
  std::memcpy(&out[10], args, argLen);
  p.onOutput(SwitchProProfile::OUT_SUBCOMMAND, out, sizeof(out));
}

void test_switch_spi_read_stick_calibration() {
  SwitchProProfile p;
  p.begin(kMac);
  const uint8_t args[5] = {0x3D, 0x60, 0x00, 0x00, 0x12};
  sendSubcommand(p, SwitchProProfile::SUB_SPI_READ, args, sizeof(args));

  HidReport r;
  TEST_ASSERT_TRUE(p.nextReport(0, r));
  TEST_ASSERT_EQUAL_HEX8(0x21, r.id);
  TEST_ASSERT_EQUAL_HEX8(0x90, r.data[12]);  // report byte 13: ack
  TEST_ASSERT_EQUAL_HEX8(0x10, r.data[13]);  // report byte 14: echoed subcommand
  TEST_ASSERT_EQUAL_UINT8_ARRAY(args, &r.data[14], 5);  // address + length echoed

  // Left stick: X/Y max-above, X/Y center, X/Y min-below (each 12-bit packed pair)
  const uint8_t *cal = &r.data[19];
  uint16_t a, b;
  unpackStick(&cal[0], &a, &b);
  TEST_ASSERT_EQUAL_UINT16(0x7FF, a);
  TEST_ASSERT_EQUAL_UINT16(0x7FF, b);
  unpackStick(&cal[3], &a, &b);
  TEST_ASSERT_EQUAL_UINT16(0x800, a);
  TEST_ASSERT_EQUAL_UINT16(0x800, b);
  // Right stick starts with its center
  unpackStick(&cal[9], &a, &b);
  TEST_ASSERT_EQUAL_UINT16(0x800, a);
  TEST_ASSERT_EQUAL_UINT16(0x800, b);
}

void test_switch_spi_read_unpopulated_is_erased() {
  SwitchProProfile p;
  p.begin(kMac);
  const uint8_t args[5] = {0x10, 0x80, 0x00, 0x00, 0x16};  // user stick calibration
  sendSubcommand(p, SwitchProProfile::SUB_SPI_READ, args, sizeof(args));
  HidReport r;
  TEST_ASSERT_TRUE(p.nextReport(0, r));
  for (int i = 0; i < 0x16; i++) {
    TEST_ASSERT_EQUAL_HEX8(0xFF, r.data[19 + i]);
  }
}

void test_switch_device_info_and_plain_acks() {
  SwitchProProfile p;
  p.begin(kMac);
  sendSubcommand(p, SwitchProProfile::SUB_DEVICE_INFO, nullptr, 0);
  const uint8_t lights[1] = {0x01};
  sendSubcommand(p, 0x30, lights, 1);

  HidReport r;
  TEST_ASSERT_TRUE(p.nextReport(0, r));
  TEST_ASSERT_EQUAL_HEX8(0x82, r.data[12]);
  TEST_ASSERT_EQUAL_HEX8(0x02, r.data[13]);
  TEST_ASSERT_EQUAL_HEX8(0x03, r.data[16]);  // controller type: Pro
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kMac, &r.data[18], 6);

  TEST_ASSERT_TRUE(p.nextReport(0, r));
  TEST_ASSERT_EQUAL_HEX8(0x80, r.data[12]);
  TEST_ASSERT_EQUAL_HEX8(0x30, r.data[13]);
}

void test_switch_replies_take_priority_over_stream() {
  SwitchProProfile p;
  p.begin(kMac);
  uint8_t cmd[63] = {SwitchProProfile::USB_FORCE_USB};
  p.onOutput(SwitchProProfile::OUT_USB, cmd, sizeof(cmd));
  HidReport r;
  TEST_ASSERT_TRUE(p.nextReport(0, r));
  TEST_ASSERT_EQUAL_HEX8(0x30, r.id);

  const uint8_t mode[1] = {0x30};
  sendSubcommand(p, SwitchProProfile::SUB_INPUT_MODE, mode, 1);
  TEST_ASSERT_TRUE(p.nextReport(100, r));  // reply goes out without waiting for the interval
  TEST_ASSERT_EQUAL_HEX8(0x21, r.id);
}

// ---- Output report parsing (both TinyUSB conventions) ----

void test_output_report_from_out_endpoint() {
  const uint8_t data[] = {0x80, 0x02, 0x00};
  OutputReport r;
  // TinyUSB 0.16 passes INVALID, 0.17+ passes OUTPUT; the ID is the first byte either way
  for (uint8_t type : {HID_TYPE_INVALID, HID_TYPE_OUTPUT}) {
    TEST_ASSERT_TRUE(parseOutputReport(0, type, data, sizeof(data), r));
    TEST_ASSERT_EQUAL_HEX8(0x80, r.id);
    TEST_ASSERT_EQUAL_UINT16(2, r.len);
    TEST_ASSERT_EQUAL_HEX8(0x02, r.data[0]);
  }
}

void test_output_report_from_control_pipe() {
  const uint8_t withId[] = {0x05, 0xFF, 0x00};
  const uint8_t withoutId[] = {0xFF, 0x00};
  OutputReport r;
  TEST_ASSERT_TRUE(parseOutputReport(0x05, HID_TYPE_OUTPUT, withId, sizeof(withId), r));
  TEST_ASSERT_EQUAL_HEX8(0x05, r.id);
  TEST_ASSERT_EQUAL_UINT16(2, r.len);
  TEST_ASSERT_TRUE(parseOutputReport(0x05, HID_TYPE_OUTPUT, withoutId, sizeof(withoutId), r));
  TEST_ASSERT_EQUAL_UINT16(2, r.len);
  TEST_ASSERT_EQUAL_HEX8(0xFF, r.data[0]);
}

void test_output_report_ignores_features_and_empty() {
  const uint8_t data[] = {0x13, 0x01};
  OutputReport r;
  TEST_ASSERT_FALSE(parseOutputReport(0x13, HID_TYPE_FEATURE, data, sizeof(data), r));
  TEST_ASSERT_FALSE(parseOutputReport(0, HID_TYPE_OUTPUT, data, 0, r));
}

// ---- Runtime profile selection ----

void test_any_profile_identity_and_descriptor_follow_selection() {
  AnyProfile p;
  struct Expect {
    ProfileKind kind;
    uint16_t vid, pid;
    const uint8_t *descriptor;
  };
  uint16_t len = 0;
  const Expect cases[] = {
    {ProfileKind::Generic, 0x2DC8, 0x3011, GenericProfile::descriptor(&len)},
    {ProfileKind::Ds4, 0x054C, 0x09CC, Ds4Profile::descriptor(&len)},
    {ProfileKind::SwitchPro, 0x057E, 0x2009, SwitchProProfile::descriptor(&len)},
  };
  for (const Expect &e : cases) {
    p.select(e.kind);
    UsbIdentity id = p.identity();
    TEST_ASSERT_EQUAL_HEX16(e.vid, id.vid);
    TEST_ASSERT_EQUAL_HEX16(e.pid, id.pid);
    TEST_ASSERT_EQUAL_PTR(e.descriptor, p.descriptor(&len));
  }
}

void test_any_profile_dispatches_reports_to_selected_profile() {
  HidReport r;
  AnyProfile ds4(ProfileKind::Ds4);
  ds4.begin(kMac);
  TEST_ASSERT_TRUE(ds4.nextReport(0, r));
  TEST_ASSERT_EQUAL_HEX8(0x01, r.id);

  AnyProfile sw(ProfileKind::SwitchPro);
  sw.begin(kMac);
  TEST_ASSERT_FALSE(sw.nextReport(0, r));  // silent until the host's handshake
  uint8_t cmd[63] = {SwitchProProfile::USB_STATUS};
  sw.onOutput(SwitchProProfile::OUT_USB, cmd, sizeof(cmd));
  TEST_ASSERT_TRUE(sw.nextReport(0, r));
  TEST_ASSERT_EQUAL_HEX8(0x81, r.id);

  uint8_t buf[64];
  TEST_ASSERT_EQUAL_UINT16(15, ds4.getFeature(0x12, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_UINT16(0, sw.getFeature(0x12, buf, sizeof(buf)));
}

void test_profile_cycle_and_validation() {
  TEST_ASSERT_TRUE(nextProfile(ProfileKind::Generic) == ProfileKind::Ds4);
  TEST_ASSERT_TRUE(nextProfile(ProfileKind::Ds4) == ProfileKind::SwitchPro);
  TEST_ASSERT_TRUE(nextProfile(ProfileKind::SwitchPro) == ProfileKind::Generic);
  TEST_ASSERT_TRUE(isValidProfile(2));
  TEST_ASSERT_FALSE(isValidProfile(3));
}

// ---- Rumble ----

void test_ds4_rumble_from_effects_report() {
  Ds4Profile p;
  Rumble r;
  TEST_ASSERT_FALSE(p.takeRumble(r));

  // Report 0x05 without its ID: flags, 2 reserved, right (light), left (heavy)
  uint8_t out[31] = {Ds4Profile::EFFECT_RUMBLE, 0, 0, 0x80, 0xFF};
  p.onOutput(Ds4Profile::OUTPUT_EFFECTS, out, sizeof(out));
  TEST_ASSERT_TRUE(p.takeRumble(r));
  TEST_ASSERT_EQUAL_UINT16(65535, r.low);
  TEST_ASSERT_EQUAL_UINT16(0x80 * 257, r.high);
  TEST_ASSERT_FALSE(p.takeRumble(r));  // reported once

  p.onOutput(Ds4Profile::OUTPUT_EFFECTS, out, sizeof(out));  // unchanged: nothing new
  TEST_ASSERT_FALSE(p.takeRumble(r));

  uint8_t ledOnly[31] = {0x02, 0, 0, 0, 0};  // lightbar update must not stop the rumble
  p.onOutput(Ds4Profile::OUTPUT_EFFECTS, ledOnly, sizeof(ledOnly));
  TEST_ASSERT_FALSE(p.takeRumble(r));
}

// SDL's EncodeRumble: index i (0..100) per band, high band 2*i, low band 0x40 + i/2 with bit 0 in byte 2
static void encodeSdlRumble(uint8_t highIndex, uint8_t lowIndex, uint8_t *side) {
  if (highIndex == 0 && lowIndex == 0) {
    const uint8_t neutral[4] = {0x00, 0x01, 0x40, 0x40};
    std::memcpy(side, neutral, 4);
    return;
  }
  const uint16_t highFreq = 0x0074;
  const uint8_t lowFreq = 0x3D;
  side[0] = highFreq & 0xFF;
  side[1] = static_cast<uint8_t>((highIndex * 2) | ((highFreq >> 8) & 0x01));
  side[2] = static_cast<uint8_t>(lowFreq | ((lowIndex & 1) << 7));
  side[3] = static_cast<uint8_t>(0x40 + (lowIndex >> 1));
}

void test_switch_rumble_decodes_hd_rumble() {
  SwitchProProfile p;
  p.begin(kMac);
  Rumble r;
  uint8_t out[63] = {};

  // Rumble-only report 0x10: [0] counter, [1..4] left, [5..8] right
  encodeSdlRumble(50, 100, &out[1]);
  encodeSdlRumble(50, 100, &out[5]);
  p.onOutput(SwitchProProfile::OUT_RUMBLE, out, sizeof(out));
  TEST_ASSERT_TRUE(p.takeRumble(r));
  TEST_ASSERT_EQUAL_UINT16(65535, r.low);
  TEST_ASSERT_EQUAL_UINT16(50 * 65535u / 100, r.high);

  // Subcommand reports carry rumble too; neutral stops it
  encodeSdlRumble(0, 0, &out[1]);
  encodeSdlRumble(0, 0, &out[5]);
  out[9] = 0x30;  // player lights subcommand
  p.onOutput(SwitchProProfile::OUT_SUBCOMMAND, out, sizeof(out));
  TEST_ASSERT_TRUE(p.takeRumble(r));
  TEST_ASSERT_EQUAL_UINT16(0, r.low);
  TEST_ASSERT_EQUAL_UINT16(0, r.high);
  HidReport reply;
  TEST_ASSERT_TRUE(p.nextReport(0, reply));  // the subcommand is still answered
  TEST_ASSERT_EQUAL_HEX8(0x21, reply.id);
}

void test_switch_rumble_takes_stronger_side() {
  SwitchProProfile p;
  Rumble r;
  uint8_t out[63] = {};
  encodeSdlRumble(0, 0, &out[1]);     // left idle
  encodeSdlRumble(25, 75, &out[5]);   // right active
  p.onOutput(SwitchProProfile::OUT_RUMBLE, out, sizeof(out));
  TEST_ASSERT_TRUE(p.takeRumble(r));
  TEST_ASSERT_EQUAL_UINT16(75 * 65535u / 100, r.low);
  TEST_ASSERT_EQUAL_UINT16(25 * 65535u / 100, r.high);
}

void test_any_profile_rumble_dispatch() {
  Rumble r;
  AnyProfile generic(ProfileKind::Generic);
  uint8_t out[31] = {Ds4Profile::EFFECT_RUMBLE, 0, 0, 0xFF, 0xFF};
  generic.onOutput(Ds4Profile::OUTPUT_EFFECTS, out, sizeof(out));
  TEST_ASSERT_FALSE(generic.takeRumble(r));

  AnyProfile ds4(ProfileKind::Ds4);
  ds4.onOutput(Ds4Profile::OUTPUT_EFFECTS, out, sizeof(out));
  TEST_ASSERT_TRUE(ds4.takeRumble(r));
  TEST_ASSERT_EQUAL_UINT16(65535, r.low);
}

int main(int argc, char **argv) {
  (void) argc;
  (void) argv;
  UNITY_BEGIN();
  RUN_TEST(test_ds4_descriptor_report_sizes);
  RUN_TEST(test_switch_descriptor_report_sizes);
  RUN_TEST(test_generic_descriptor_report_size);
  RUN_TEST(test_generic_sends_neutral_once_then_only_changes);
  RUN_TEST(test_ds4_neutral_report);
  RUN_TEST(test_ds4_buttons_sticks_triggers);
  RUN_TEST(test_ds4_streams_every_4ms_with_counter);
  RUN_TEST(test_ds4_feature_reports);
  RUN_TEST(test_switch_header_buttons_and_dpad);
  RUN_TEST(test_switch_positional_face_buttons);
  RUN_TEST(test_switch_sticks_12bit);
  RUN_TEST(test_switch_sdl_usb_handshake);
  RUN_TEST(test_switch_spi_read_stick_calibration);
  RUN_TEST(test_switch_spi_read_unpopulated_is_erased);
  RUN_TEST(test_switch_device_info_and_plain_acks);
  RUN_TEST(test_switch_replies_take_priority_over_stream);
  RUN_TEST(test_output_report_from_out_endpoint);
  RUN_TEST(test_output_report_from_control_pipe);
  RUN_TEST(test_output_report_ignores_features_and_empty);
  RUN_TEST(test_any_profile_identity_and_descriptor_follow_selection);
  RUN_TEST(test_any_profile_dispatches_reports_to_selected_profile);
  RUN_TEST(test_profile_cycle_and_validation);
  RUN_TEST(test_ds4_rumble_from_effects_report);
  RUN_TEST(test_switch_rumble_decodes_hd_rumble);
  RUN_TEST(test_switch_rumble_takes_stronger_side);
  RUN_TEST(test_any_profile_rumble_dispatch);
  return UNITY_END();
}
