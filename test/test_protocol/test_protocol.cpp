#include <unity.h>

#include <cstring>
#include <vector>

#include "profile_generic.h"
#include "protocol.h"

using namespace lumina;

namespace {

struct CapturedFrame {
  uint8_t type;
  std::vector<uint8_t> payload;
};

std::vector<CapturedFrame> g_frames;

void onFrame(uint8_t type, const uint8_t *payload, uint8_t len) {
  g_frames.push_back({type, std::vector<uint8_t>(payload, payload + len)});
}

std::vector<uint8_t> buildFrame(uint8_t type, const std::vector<uint8_t> &payload) {
  std::vector<uint8_t> body;
  body.push_back(type);
  body.push_back(static_cast<uint8_t>(payload.size()));
  body.insert(body.end(), payload.begin(), payload.end());

  std::vector<uint8_t> out = {FRAME_SYNC0, FRAME_SYNC1};
  out.insert(out.end(), body.begin(), body.end());
  out.push_back(crc8(body.data(), body.size()));
  return out;
}

void feedAll(FrameParser &parser, const std::vector<uint8_t> &bytes) {
  for (uint8_t b : bytes) {
    parser.feed(b);
  }
}

}  // namespace

void setUp() { g_frames.clear(); }
void tearDown() {}

void test_crc8_known_vectors() {
  const uint8_t single[] = {0x01};
  TEST_ASSERT_EQUAL_HEX8(0x07, crc8(single, sizeof(single)));

  const uint8_t pingBody[] = {MSG_PING, 0x00};
  TEST_ASSERT_EQUAL_HEX8(0x15, crc8(pingBody, sizeof(pingBody)));
}

void test_parse_valid_ping() {
  FrameParser parser(onFrame);
  feedAll(parser, buildFrame(MSG_PING, {}));

  TEST_ASSERT_EQUAL_UINT32(1, g_frames.size());
  TEST_ASSERT_EQUAL_HEX8(MSG_PING, g_frames[0].type);
  TEST_ASSERT_EQUAL_UINT32(0, g_frames[0].payload.size());
}

void test_parse_valid_report() {
  FrameParser parser(onFrame);
  std::vector<uint8_t> payload = {
      0x00,        // pad 0
      0x01, 0x00,  // buttons (A)
      0x08,        // hat neutral
      0x80, 0xFF,  // LT, RT
      0x00, 0x40,  // LX
      0x00, 0xC0,  // LY
      0x12, 0x34,  // RX
      0x56, 0x78,  // RY
  };
  feedAll(parser, buildFrame(MSG_REPORT, payload));

  TEST_ASSERT_EQUAL_UINT32(1, g_frames.size());
  TEST_ASSERT_EQUAL_HEX8(MSG_REPORT, g_frames[0].type);
  TEST_ASSERT_EQUAL_UINT32(payload.size(), g_frames[0].payload.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload.data(), g_frames[0].payload.data(), payload.size());
}

void test_rejects_bad_crc_and_resyncs_on_sync0_crc_byte() {
  FrameParser parser(onFrame);

  // Build a valid PING, corrupt its CRC to FRAME_SYNC0 (0xA5), then immediately
  // follow with the rest of a valid PING starting at FRAME_SYNC1 (0x5A).
  auto ping = buildFrame(MSG_PING, {});
  std::vector<uint8_t> stream = ping;
  stream.back() = FRAME_SYNC0;  // bad CRC that also starts the next frame
  stream.insert(stream.end(), ping.begin() + 1, ping.end());

  feedAll(parser, stream);

  TEST_ASSERT_EQUAL_UINT32(1, g_frames.size());
  TEST_ASSERT_EQUAL_HEX8(MSG_PING, g_frames[0].type);
}

void test_rejects_invalid_length_without_swallowing_next_frame() {
  FrameParser parser(onFrame);

  // Corrupt frame claiming len=200 for MSG_REPORT, immediately followed by valid PING.
  std::vector<uint8_t> stream = {FRAME_SYNC0, FRAME_SYNC1, MSG_REPORT, 200};
  auto ping = buildFrame(MSG_PING, {});
  stream.insert(stream.end(), ping.begin(), ping.end());

  feedAll(parser, stream);

  TEST_ASSERT_EQUAL_UINT32(1, g_frames.size());
  TEST_ASSERT_EQUAL_HEX8(MSG_PING, g_frames[0].type);
}

void test_rejects_unknown_type_without_swallowing_next_frame() {
  FrameParser parser(onFrame);

  std::vector<uint8_t> stream = {FRAME_SYNC0, FRAME_SYNC1, 0x7F};
  auto ping = buildFrame(MSG_PING, {});
  stream.insert(stream.end(), ping.begin(), ping.end());

  feedAll(parser, stream);

  TEST_ASSERT_EQUAL_UINT32(1, g_frames.size());
  TEST_ASSERT_EQUAL_HEX8(MSG_PING, g_frames[0].type);
}

void test_sync_bytes_inside_payload_and_back_to_back_frames() {
  FrameParser parser(onFrame);

  std::vector<uint8_t> payload(1 + REPORT_BODY_LEN, 0x00);
  payload[3] = FRAME_SYNC0;
  payload[4] = FRAME_SYNC1;
  payload[5] = MSG_PING;

  std::vector<uint8_t> stream = {0x00, 0xFF, FRAME_SYNC0, FRAME_SYNC0};
  auto report = buildFrame(MSG_REPORT, payload);
  auto ping = buildFrame(MSG_PING, {});
  stream.insert(stream.end(), report.begin(), report.end());
  stream.insert(stream.end(), ping.begin(), ping.end());

  feedAll(parser, stream);

  TEST_ASSERT_EQUAL_UINT32(2, g_frames.size());
  TEST_ASSERT_EQUAL_HEX8(MSG_REPORT, g_frames[0].type);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload.data(), g_frames[0].payload.data(), payload.size());
  TEST_ASSERT_EQUAL_HEX8(MSG_PING, g_frames[1].type);
}

void test_recovers_valid_frame_starting_inside_corrupt_frame() {
  FrameParser parser(onFrame);

  // Truncated REPORT (only 4 payload bytes) immediately followed by a valid PING.
  // The parser consumes the PING bytes while finishing the corrupt REPORT, then
  // replays them on CRC failure and recovers the PING.
  std::vector<uint8_t> stream = {
      FRAME_SYNC0, FRAME_SYNC1, MSG_REPORT, 1 + REPORT_BODY_LEN,
      0x00, 0x01, 0x00, 0x08,
  };
  auto ping = buildFrame(MSG_PING, {});
  stream.insert(stream.end(), ping.begin(), ping.end());
  // Pad enough trailing bytes to trigger the corrupt REPORT's CRC check.
  stream.insert(stream.end(), 10, 0x00);

  feedAll(parser, stream);

  TEST_ASSERT_EQUAL_UINT32(1, g_frames.size());
  TEST_ASSERT_EQUAL_HEX8(MSG_PING, g_frames[0].type);
}

void test_wire_to_hid_report_translation() {
  // Wire: A(0) + X(2) + LB(4) + Back(6) + L3(8) + Home(10), hat=2(E),
  // LT=0xAA, RT=0x55, LX=0x1122, LY=0x3344, RX=0x5566, RY=0x7788
  const uint16_t wireButtons = (1u << 0) | (1u << 2) | (1u << 4) | (1u << 6) | (1u << 8) | (1u << 10);
  const uint8_t wire[REPORT_BODY_LEN] = {
      static_cast<uint8_t>(wireButtons & 0xFF),
      static_cast<uint8_t>(wireButtons >> 8),
      0x02,
      0xAA, 0x55,
      0x22, 0x11, 0x44, 0x33, 0x66, 0x55, 0x88, 0x77,
  };

  uint8_t hid[REPORT_BODY_LEN] = {};
  wireToHidReport(wire, hid);

  // Expected HID buttons: A->b0, X->b3, LB->b6, Back->b10, Home->b12, L3->b13
  const uint16_t expectedButtons = (1u << 0) | (1u << 3) | (1u << 6) | (1u << 10) | (1u << 12) | (1u << 13);
  const uint16_t gotButtons = static_cast<uint16_t>(hid[0]) | (static_cast<uint16_t>(hid[1]) << 8);
  TEST_ASSERT_EQUAL_HEX16(expectedButtons, gotButtons);
  TEST_ASSERT_EQUAL_HEX8(0x02, hid[2]);

  // X axes pass through; Y axes are inverted (XInput up-positive -> HID down-positive)
  TEST_ASSERT_EQUAL_INT16(0x1122, readI16(&hid[3]));   // LX
  TEST_ASSERT_EQUAL_INT16(-0x3344, readI16(&hid[5]));  // LY
  TEST_ASSERT_EQUAL_INT16(0x5566, readI16(&hid[7]));   // RX
  TEST_ASSERT_EQUAL_INT16(-0x7788, readI16(&hid[9]));  // RY

  // Triggers: Ry(a4)=RT=0x55, Rz(a5)=LT=0xAA
  TEST_ASSERT_EQUAL_HEX8(0x55, hid[11]);
  TEST_ASSERT_EQUAL_HEX8(0xAA, hid[12]);
}

void test_wire_to_hid_y_axis_extremes() {
  uint8_t wire[REPORT_BODY_LEN] = {0x00, 0x00, 0x08, 0x00, 0x00};
  writeI16(&wire[7], INT16_MIN);   // LY full down
  writeI16(&wire[11], INT16_MAX);  // RY full up

  uint8_t hid[REPORT_BODY_LEN] = {};
  wireToHidReport(wire, hid);

  TEST_ASSERT_EQUAL_INT16(INT16_MAX, readI16(&hid[5]));  // saturates instead of overflowing
  TEST_ASSERT_EQUAL_INT16(-INT16_MAX, readI16(&hid[9]));
}

void test_profile_messages() {
  FrameParser parser(onFrame);
  feedAll(parser, buildFrame(MSG_GET_PROFILE, {}));
  feedAll(parser, buildFrame(MSG_SET_PROFILE, {0x02}));
  feedAll(parser, buildFrame(MSG_SET_PROFILE, {}));  // wrong length: rejected

  TEST_ASSERT_EQUAL_UINT32(2, g_frames.size());
  TEST_ASSERT_EQUAL_HEX8(MSG_GET_PROFILE, g_frames[0].type);
  TEST_ASSERT_EQUAL_HEX8(MSG_SET_PROFILE, g_frames[1].type);
  TEST_ASSERT_EQUAL_UINT32(1, g_frames[1].payload.size());
  TEST_ASSERT_EQUAL_HEX8(0x02, g_frames[1].payload[0]);
}

int main(int argc, char **argv) {
  (void) argc;
  (void) argv;
  UNITY_BEGIN();
  RUN_TEST(test_crc8_known_vectors);
  RUN_TEST(test_parse_valid_ping);
  RUN_TEST(test_parse_valid_report);
  RUN_TEST(test_rejects_bad_crc_and_resyncs_on_sync0_crc_byte);
  RUN_TEST(test_rejects_invalid_length_without_swallowing_next_frame);
  RUN_TEST(test_rejects_unknown_type_without_swallowing_next_frame);
  RUN_TEST(test_sync_bytes_inside_payload_and_back_to_back_frames);
  RUN_TEST(test_recovers_valid_frame_starting_inside_corrupt_frame);
  RUN_TEST(test_wire_to_hid_report_translation);
  RUN_TEST(test_profile_messages);
  RUN_TEST(test_wire_to_hid_y_axis_extremes);
  return UNITY_END();
}
