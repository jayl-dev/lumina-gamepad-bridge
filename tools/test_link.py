#!/usr/bin/env python3
"""
Bring-up test for the Lumina ESP32-S3 gamepad bridge, without Lumina.

Pings the board, then presses A, sweeps the left stick and releases everything.
Watch the result in a gamepad tester (e.g. https://hardwaretester.com/gamepad).

Usage: python3 tools/test_link.py [/dev/cu.usbmodemXXXX]
Requires pyserial (bundled with Homebrew PlatformIO: $(brew --prefix platformio)/libexec/bin/python).
"""
import glob
import math
import struct
import sys
import time

import serial

SYNC = b"\xA5\x5A"
MSG_PING, MSG_REPORT, MSG_PONG = 0x01, 0x02, 0x81
BTN_A = 1 << 0
HAT_NONE = 8


def crc8(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def frame(msg_type: int, payload: bytes = b"") -> bytes:
    body = bytes([msg_type, len(payload)]) + payload
    return SYNC + body + bytes([crc8(body)])


def report(buttons=0, hat=HAT_NONE, lt=0, rt=0, lx=0, ly=0, rx=0, ry=0, pad=0) -> bytes:
    # Same layout as HIDGamepadReport in Lumina's hid_gamepad.h, minus the report ID
    return frame(MSG_REPORT, struct.pack("<BHBBBhhhh", pad, buttons, hat, lt, rt, lx, ly, rx, ry))


def main():
    ports = sys.argv[1:] or sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("No /dev/cu.usbmodem* port found; is the board plugged into its native USB port?")
    port = ports[0]

    with serial.Serial(port, 115200, timeout=0.5) as link:
        link.reset_input_buffer()
        link.write(frame(MSG_PING))
        reply = link.read(64)
        idx = reply.find(SYNC + bytes([MSG_PONG, 2]))
        if idx < 0 or crc8(reply[idx + 2:idx + 6]) != reply[idx + 6]:
            sys.exit(f"{port}: no valid PONG (got {reply.hex()})")
        print(f"{port}: protocol v{reply[idx + 4]}, {reply[idx + 5]} pad(s)")

        print("Pressing A for 1s")
        link.write(report(buttons=BTN_A))
        time.sleep(1)
        link.write(report())

        print("Sweeping left stick for 2s")
        start = time.monotonic()
        while (t := time.monotonic() - start) < 2:
            angle = t * math.pi
            link.write(report(lx=int(32767 * math.cos(angle)), ly=int(32767 * math.sin(angle))))
            time.sleep(0.008)

        link.write(report())
        print("Released all inputs")


if __name__ == "__main__":
    main()
