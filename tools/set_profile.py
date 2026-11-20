#!/usr/bin/env python3
"""
Shows or changes the controller the Lumina ESP32-S3 board emulates.

Usage: .venv/bin/python tools/set_profile.py [generic|ds4|switchpro]
Without an argument it prints the active profile. Changing it restarts the board, which then
re-enumerates as the new controller (no reflashing). Quit Lumina first: it holds the port.
"""
import sys
import time

import serial

from test_link import SYNC, crc8, find_port, frame

MSG_SET_PROFILE, MSG_GET_PROFILE, MSG_PROFILE = 0x03, 0x04, 0x83
PROFILES = ["generic", "ds4", "switchpro"]


def request_profile(port: str, message: bytes) -> int | None:
    """Sends a profile message and returns the profile id from the board's reply."""
    with serial.Serial(port, 115200, timeout=0.5) as link:
        link.reset_input_buffer()
        link.write(message)
        reply = link.read(64)
    idx = reply.find(SYNC + bytes([MSG_PROFILE, 1]))
    if idx < 0 or idx + 6 > len(reply) or crc8(reply[idx + 2:idx + 5]) != reply[idx + 5]:
        return None
    return reply[idx + 4]


def main():
    port = find_port()
    if len(sys.argv) < 2:
        current = request_profile(port, frame(MSG_GET_PROFILE))
        if current is None:
            sys.exit(f"{port}: no reply; the firmware may predate profile switching")
        print(PROFILES[current])
        return

    name = sys.argv[1].lower()
    if name not in PROFILES:
        sys.exit(f"Unknown profile {name!r}; choose one of {', '.join(PROFILES)}")
    target = PROFILES.index(name)
    if request_profile(port, frame(MSG_SET_PROFILE, bytes([target]))) != target:
        sys.exit(f"{port}: board did not accept the profile change")

    print(f"Switching to {name}; the board restarts…")
    time.sleep(2)  # let the old identity disappear before looking for the new one
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        try:
            if request_profile(find_port(), frame(MSG_GET_PROFILE)) == target:
                print(f"Board is now {name}")
                return
        except (SystemExit, serial.SerialException):
            pass
        time.sleep(0.5)
    sys.exit("Board did not come back; check it with a gamepad tester")


if __name__ == "__main__":
    main()
