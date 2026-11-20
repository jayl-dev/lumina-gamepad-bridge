#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace lumina {

static constexpr uint8_t PROTOCOL_VERSION = 1;
static constexpr uint8_t PAD_COUNT = 1;

static constexpr uint8_t FRAME_SYNC0 = 0xA5;
static constexpr uint8_t FRAME_SYNC1 = 0x5A;
// Host -> board
static constexpr uint8_t MSG_PING = 0x01;
static constexpr uint8_t MSG_REPORT = 0x02;
static constexpr uint8_t MSG_SET_PROFILE = 0x03;  // len 1: profile id; the board restarts as that controller
static constexpr uint8_t MSG_GET_PROFILE = 0x04;  // len 0
// Board -> host
static constexpr uint8_t MSG_PONG = 0x81;
static constexpr uint8_t MSG_PROFILE = 0x83;      // len 1: active (or newly selected) profile id
static constexpr uint8_t MSG_RUMBLE = 0x84;       // len 5: pad, low-frequency u16 LE, high-frequency u16 LE

// Lumina's wire report body: hid_gamepad.m's HIDGamepadReport without the report ID
static constexpr size_t REPORT_BODY_LEN = 13;  // buttons(2) hat(1) triggers(2) sticks(8)
static constexpr size_t MAX_PAYLOAD_LEN = 1 + REPORT_BODY_LEN;

inline uint8_t crc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x80) ? (uint8_t) ((crc << 1) ^ 0x07) : (uint8_t) (crc << 1);
    }
  }
  return crc;
}

inline int expectedPayloadLen(uint8_t type) {
  switch (type) {
    case MSG_PING:
    case MSG_GET_PROFILE:
      return 0;
    case MSG_REPORT:
      return 1 + REPORT_BODY_LEN;
    case MSG_SET_PROFILE:
      return 1;
    default:
      return -1;
  }
}

/**
 * Byte-at-a-time frame parser.
 * Rejects unknown message types or unexpected lengths immediately so corrupt
 * bytes cannot swallow subsequent valid frames, and resyncs on 0xA5 0x5A.
 */
class FrameParser {
public:
  using FrameCallback = void (*)(uint8_t type, const uint8_t *payload, uint8_t len);

  explicit FrameParser(FrameCallback cb = nullptr) : onFrame(cb) {}

  void setCallback(FrameCallback cb) { onFrame = cb; }

  void reset() {
    state = State::Sync0;
    received = 0;
  }

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
        if (expectedPayloadLen(byte) >= 0) {
          buf[0] = byte;
          state = State::Len;
        } else {
          state = (byte == FRAME_SYNC0) ? State::Sync1 : State::Sync0;
        }
        break;
      case State::Len:
        if (byte == expectedPayloadLen(buf[0])) {
          buf[1] = byte;
          received = 0;
          state = byte ? State::Payload : State::Crc;
        } else {
          state = (byte == FRAME_SYNC0) ? State::Sync1 : State::Sync0;
        }
        break;
      case State::Payload:
        buf[2 + received++] = byte;
        if (received == buf[1]) {
          state = State::Crc;
        }
        break;
      case State::Crc:
        if (crc8(buf, 2 + buf[1]) == byte) {
          if (onFrame) {
            onFrame(buf[0], &buf[2], buf[1]);
          }
          state = State::Sync0;
        } else {
          // Replay bytes consumed after Sync0/Sync1 (plus this byte) so a valid
          // frame that began inside the corrupt frame is not lost.
          uint8_t replay[2 + MAX_PAYLOAD_LEN + 1];
          size_t count = 2 + buf[1];
          std::memcpy(replay, buf, count);
          replay[count++] = byte;
          reset();
          for (size_t i = 0; i < count; i++) {
            feed(replay[i]);
          }
        }
        break;
    }
  }

private:
  enum class State { Sync0, Sync1, Type, Len, Payload, Crc };
  FrameCallback onFrame;
  State state = State::Sync0;
  uint8_t buf[2 + MAX_PAYLOAD_LEN] = {};  // type, len, payload
  uint8_t received = 0;
};

}  // namespace lumina
