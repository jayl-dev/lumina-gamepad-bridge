# Lumina ESP32-S3 Gamepad Bridge

Firmware that turns an ESP32-S3 into a real USB game controller driven by Lumina.
macOS sees an ordinary USB HID gamepad, so it needs no `com.apple.developer.hid.virtual.device`
entitlement, no SIP or AMFI changes, and no self-signing.

```
Moonlight client ──network──▶ Lumina ──USB serial (CDC)──▶ ESP32-S3 ──USB HID──▶ macOS / games
```

The board appears as one composite USB device (`1209:5854`, "Lumina ESP Gamepad"):

- **HID gamepad**: the same report descriptor as Lumina's IOHIDUserDevice gamepad
  (`src/platform/macos/hid_gamepad.m`), so games see the same controller either way.
- **CDC serial port**: Lumina streams gamepad state frames to it.

## Build and flash

```sh
pio run                 # build
pio run -t upload       # flash
```

Plug the board in through its **native USB** port. On DevKitC boards that's the port labelled
`USB`, not `UART`/`COM`. If the first upload can't connect, hold **BOOT**, tap **RST**,
release **BOOT**, then upload again. Once this firmware is running, later uploads reset into
the bootloader automatically.

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
- **Map inputs**: presses every control in turn and lists the Gamepad API button or axis each one
  produced. The list matches what you'll see on hardwaretester.com and in SDL-based emulators.

### Scripted smoke test

```sh
"$(brew --prefix platformio)/libexec/bin/python" tools/test_link.py
```

The script pings the board, presses A, then sweeps the left stick. Watch the result in a gamepad
tester while it runs.

## Using it with Lumina

Just plug the board in. When a Moonlight client connects a controller, Lumina picks a gamepad mode
in this order:

1. **ESP32 board** (found by USB VID/PID and confirmed with a ping)
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

The Lumina side lives in `Lumina/src/platform/macos/esp_gamepad.{h,m}`. Keep both sides in sync
when changing the protocol, VID/PID or report descriptor.
