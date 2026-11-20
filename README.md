# Lumina ESP32-S3 Gamepad Bridge

Firmware that turns an inexpensive ESP32-S3 board into a real USB game controller for
[**Lumina**](https://github.com/jayl-dev/Lumina), a macOS-only fork of Sunshine (the game-streaming
host for Moonlight clients).

## Why

When a Moonlight client connects a controller, the streaming host has to make it appear on the Mac.
Lumina's built-in option creates a virtual HID device (`IOHIDUserDevice`), which macOS only allows
with the restricted `com.apple.developer.hid.virtual.device` entitlement, which in practice means
disabling SIP/AMFI and self-signing Lumina. Without that, Lumina can only turn the controller into
keyboard and mouse events.

This bridge avoids all of that. The ESP32-S3 plugs into the Mac and **is** a USB controller, so
macOS, games, emulators and CrossOver see ordinary wired hardware. No entitlements, no SIP or AMFI
changes, no signing.

## How it works

```
Moonlight client ──network──▶ Lumina ──USB serial (CDC)──▶ ESP32-S3 ──USB HID──▶ macOS / games
 (phone, tablet, PC)          (Mac)                         (this firmware)
```

1. A Moonlight client streams from Lumina and sends its controller input over the network.
2. Lumina looks for a serial port on a USB device whose serial number starts with `LUMINA-`,
   confirms it with a ping, and forwards every controller update as a small frame (see
   [Protocol](#protocol)). It tries the board first, then the virtual HID device, then keyboard
   emulation. The Lumina side is `src/platform/macos/esp_gamepad.{h,m}` in the Lumina repo.
3. This firmware translates each frame into the reports of the controller it emulates (DualShock 4,
   Switch Pro or a generic joystick) and sends them to the Mac as a real USB gamepad.

The board appears as one composite USB device:

- **HID gamepad**: emulates the controller chosen at runtime (see below). Lumina doesn't need to
  know which one; it only talks to the serial port.
- **CDC serial port**: Lumina streams gamepad state frames to it.

Its USB serial number is always `LUMINA-<chip id>`, which is how Lumina and the test tools find it
whatever controller it emulates. A real controller has no serial port, so it can't be mistaken for
the board.

## What is an ESP32-S3?

The ESP32 family are cheap microcontrollers made by Espressif, best known for Wi-Fi and Bluetooth.
The **S3** variant also has a native USB OTG controller, so it can act as a USB device. Here it acts
as a gamepad and a serial port at the same time over one cable.

Only the S3 (and the older S2) have that. The original ESP32, the C3 and the C6 can only expose a
serial port over USB and **won't work**.

## What to buy

- **An ESP32-S3 development board with 8 MB of flash or more**, with the chip's native USB port on
  a connector. The firmware targets Espressif's
  [ESP32-S3-DevKitC-1](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/)
  (the N8, N8R8 and N16R8 variants all qualify), which is what it was tested on. Compact ESP32-S3
  boards (Super Mini, Seeed XIAO ESP32S3, Waveshare ESP32-S3-Zero and similar) use the same chip and
  should work but are untested. Their RGB LED and BOOT button may sit on other pins (set
  `STATUS_LED_PIN` / `PROFILE_BUTTON_PIN`), and 4 MB boards need a smaller partition table.
- **A USB data cable** (USB-C or micro-USB, whichever the board has). Charge-only cables power the
  board but it never shows up on the Mac.

The DevKitC-1 has two USB connectors. Always use the one labelled **USB** (the native port), not
**UART**/**COM**.

Getting started: build and flash as in [Build and flash](#build-and-flash), plug the board in,
install a Lumina build with ESP support, and connect from Moonlight. The board's LED pulses amber
while it waits and switches to the profile's colour once Lumina is driving it.

## Controller profiles

One firmware emulates any of these; the board remembers the chosen one across power cycles.

| Id | Profile | Emulates | USB ID | LED | Works with |
|---|---|---|---|---|---|
| 1 | `ds4` | Wired DualShock 4 | `054c:09cc` | blue | macOS GameController (native games, System Settings), browsers, SDL apps, Steam |
| 2 | `switchpro` | Wired Switch Pro Controller | `057e:2009` | red | SDL and GameController apps such as Ryujinx and CrossOver games (native Switch layout). **Default.** |
| 0 | `generic` | Generic joystick, 8BitDo Ultimate Wired layout | `2dc8:3011` | green | Raw HID readers; SDL apps only with `SDL_JOYSTICK_MFI=0` |

**Switching** restarts the board as the new controller (about 2 s, no reflashing or replugging):

- **BOOT button**: press it to cycle generic → DualShock 4 → Switch Pro. The LED flashes the new
  profile's colour three times before the restart.
- **Tester or Keypad**: the *Controller mode* picker.
- **Command line**: `.venv/bin/python tools/set_profile.py switchpro` (no argument prints the
  active profile).

Every boot shows the active profile's colour on the LED for a moment. Lumina finds the board by
its `LUMINA-` serial number, so it works with any profile.

Why the generic profile is invisible to SDL by default: macOS's GameController framework claims
the 8BitDo ID, so SDL leaves the device to it, but the framework never creates a controller.
The DS4 and Switch Pro profiles avoid this because SDL's own HIDAPI drivers handle them first.

**Switch Pro and other controller drivers:** macOS's own Pro Controller driver sets the
controller up when it is plugged in, and fails (sticks then read as dead in apps that use it,
such as Ryujinx) if another program talks to the controller at the same moment. Chromium
browsers (Chrome, Edge) run their own Switch driver in the background, so plug the board in
while they are closed; starting them afterwards is fine. A real Pro Controller behaves the same.
The DS4 profile has no such setup and isn't affected.

Face buttons map **by position**: Lumina's A (bottom) becomes Cross on the DS4 and B on the
Switch Pro. Switch Pro ZL/ZR are digital (pressed above ~12% trigger travel). Neither profile
reports motion or touch yet; the controllers report them as idle.

These profiles reuse Sony's and Nintendo's USB IDs. Fine for your own board; don't distribute
hardware or firmware with them.

## Build and flash

```sh
git clone https://github.com/jayl-dev/lumina-gamepad-bridge.git
cd lumina-gamepad-bridge
```

The firmware needs Arduino-ESP32 3.x (pioarduino platform, see `platformio.ini`): the 2.0.17
core's TinyUSB 0.16 corrupted HID reports while the serial link received data, and macOS then
reset and disabled the board. pioarduino requires Python 3.10–3.14, so use a project-local
PlatformIO:

```sh
uv venv --python 3.13 .venv && uv pip install --python .venv/bin/python platformio   # once

.venv/bin/pio run                    # build
.venv/bin/pio run -t upload          # flash
.venv/bin/pio test -e native         # protocol and profile unit tests on the host
```

Uploads use the 1200 bps touch: opening the board's serial port at 1200 baud reboots it into the
ROM bootloader. The firmware ignores the DTR/RTS reset pattern, so ordinary serial apps can't
send it into flashing mode by accident.

> Pass `-e native` when running tests; a bare `pio test` also runs against the `esp32-s3`
> environment and attempts to upload the test runner to the board.

Plug the board in through its **native USB** port. On DevKitC boards that's the port labelled
`USB`, not `UART`/`COM`. **First flash of a new board:** it doesn't run this firmware yet, so the
1200 bps touch can't reach it. Hold **BOOT**, tap **RST**, release **BOOT**, then upload. Later
uploads find it on their own. After every upload the board stays in the bootloader (it shows
up as "USB JTAG/serial debug unit"); unplug it and plug it back in to start the firmware.

### Status LED

The onboard RGB LED **pulses amber** while idle/waiting for a host and **pulses in the active
profile's colour** (green generic, blue DualShock 4, red Switch Pro) while Lumina, the Keypad or
the tester holds the serial link open. At boot it briefly shows the profile's colour too. A LED that stops pulsing means the firmware has
stopped. If the USB side ever stalls for 3 s, the firmware saves a core dump to the `coredump`
partition and restarts. DevKitC-1 v1.0 wires the LED to GPIO 48
(`RGB_BUILTIN`); on v1.1 boards it is on GPIO 38, so uncomment `-DSTATUS_LED_PIN=38` in
`platformio.ini` if the LED stays dark.

## Test without Lumina

Quit Lumina first: it holds the board's serial port exclusively.

### Interactive tester (Chrome or Edge)

```sh
open -a "Google Chrome" tools/tester.html
```

If **Connect board** reports that Web Serial is unavailable, serve the page from localhost instead:
`python3 -m http.server -d tools 8000`, then open <http://localhost:8000/tester.html>.

- **Send to board**: drag the sticks, hold the buttons, or use the keyboard. Every change goes out
  as the same frame Lumina sends.
- **What macOS reports**: the board as the browser Gamepad API sees it, live. Chrome only lists a
  gamepad after one of its buttons is pressed, so press A once after connecting.
- **Controller mode**: switches the board's profile and reconnects after the restart. The new
  profile has a different USB ID, so Chrome asks to pick the port once per profile.
- **Map inputs**: presses every control in turn and lists the Gamepad API button or axis each one
  produced. The list matches what you'll see on hardwaretester.com and in SDL-based emulators.

### Keyboard as a controller, over any app (Lumina Keypad)

A floating overlay that turns your keyboard into the board's controller while a game or
emulator has focus, with the same key map as the web tester.

```sh
tools/keypad/build.sh && open "tools/keypad/build/Lumina Keypad.app"
```

- First launch asks for **Accessibility** permission (System Settings → Privacy & Security →
  Accessibility), which it needs to intercept keys system-wide. The permission is tied to the
  build's signature, so after a rebuild the old entry stops applying; the app then clears its own
  stale entries (`tccutil reset … com.lumina.keypad`) and asks again, so you only switch it on.
- While **capturing**, the mapped keys go only to the controller; the focused app doesn't see
  them. **⌥⌘G** (or the overlay's button) toggles capturing. ⌘-shortcuts always pass through.
- It reconnects by itself when the board is replugged and releases everything when it quits.
- The **Controller** picker switches the board's profile and reconnects after the restart.
- **Mapping** remaps any controller input to another key: click the input, press the key (Esc
  cancels). Mappings are saved and the overlay's labels follow them; *Reset to Defaults* restores
  the layout below.

### Scripted smoke test

```sh
.venv/bin/python tools/test_link.py
```

The script pings the board, presses A, then sweeps the left stick. Watch the result in a gamepad
tester while it runs.

## Using it with Lumina

Needs a [Lumina](https://github.com/jayl-dev/Lumina) build with ESP32 board support. Quit the
Keypad and the web tester first, since Lumina needs the board's serial port to itself. Then just
plug the board in. When a Moonlight client connects a controller, Lumina picks a gamepad mode
in this order:

1. **ESP32 board** (found by its `LUMINA-` USB serial number and confirmed with a ping)
2. IOHIDUserDevice virtual gamepad (only with SIP disabled)
3. Keyboard/mouse emulation

The board currently exposes **one** gamepad. Extra controllers fall back to modes 2 and 3.
If Lumina exits or the stream ends, the board releases every input.

## Protocol

Every frame is `A5 5A <type> <len> <payload> <crc8>`, where crc8 is CRC-8/SMBUS (poly `0x07`)
over `<type> <len> <payload>`.

| Type   | Direction    | Payload |
|--------|--------------|---------|
| `0x01` PING   | host → board | none |
| `0x81` PONG   | board → host | protocol version (1), pad count |
| `0x02` REPORT | host → board | pad index + 13-byte HID report body (buttons u16, hat u8, LT u8, RT u8, LX/LY/RX/RY i16, little-endian) |
| `0x03` SET_PROFILE | host → board | profile id (0 generic, 1 DualShock 4, 2 Switch Pro); the board replies, then restarts as that controller |
| `0x04` GET_PROFILE | host → board | none |
| `0x83` PROFILE | board → host | active (or newly selected) profile id |

The wire protocol lives in `include/protocol.h`, the controller emulations in
`include/profile_*.h` (runtime selection in `include/profiles.h`). The Lumina side lives in `Lumina/src/platform/macos/esp_gamepad.{h,m}`. Keep
both sides in sync when changing the protocol or the serial number prefix.
