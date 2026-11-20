// Lumina Keypad: drive the ESP32 gamepad board from the keyboard while another app has focus.
//
// A floating, non-activating overlay shows the link and pad state. A session event tap
// captures the mapped keys system-wide (and swallows them, so the focused game doesn't also
// see them as keyboard input) and streams Lumina's wire frames to the board's serial port.
// ⌥⌘G toggles capturing. Needs Accessibility permission for the event tap.
//
// Wire protocol: see include/protocol.h. Build: tools/keypad/build.sh

import AppKit
import Carbon.HIToolbox
import IOKit
import IOKit.serial
import SwiftUI

// MARK: - Wire protocol

enum Wire {
  static let ping: UInt8 = 0x01
  static let report: UInt8 = 0x02
  static let setProfile: UInt8 = 0x03
  static let getProfile: UInt8 = 0x04
  static let pong: UInt8 = 0x81
  static let profile: UInt8 = 0x83
  static let protocolVersion: UInt8 = 1

  static func crc8<C: Collection>(_ data: C) -> UInt8 where C.Element == UInt8 {
    var crc: UInt8 = 0
    for byte in data {
      crc ^= byte
      for _ in 0..<8 {
        crc = (crc & 0x80) != 0 ? (crc << 1) ^ 0x07 : crc << 1
      }
    }
    return crc
  }

  static func frame(_ type: UInt8, _ payload: [UInt8] = []) -> [UInt8] {
    let body = [type, UInt8(payload.count)] + payload
    return [0xA5, 0x5A] + body + [crc8(body)]
  }
}

// Button bits of Lumina's wire report (mapButtons() in Lumina's hid_gamepad.m)
enum PadButton: UInt16, CaseIterable {
  case a = 0x0001, b = 0x0002, x = 0x0004, y = 0x0008
  case lb = 0x0010, rb = 0x0020, back = 0x0040, start = 0x0080
  case l3 = 0x0100, r3 = 0x0200, home = 0x0400

  var label: String {
    switch self {
    case .a: return "A"
    case .b: return "B"
    case .x: return "X"
    case .y: return "Y"
    case .lb: return "LB"
    case .rb: return "RB"
    case .back: return "Back"
    case .start: return "Start"
    case .l3: return "L3"
    case .r3: return "R3"
    case .home: return "Home"
    }
  }
}

enum Direction { case up, down, left, right }
enum Side { case left, right }

struct PadState: Equatable {
  var buttons: UInt16 = 0
  var dpad: Set<Direction> = []
  var lt: UInt8 = 0
  var rt: UInt8 = 0
  var lx: Int16 = 0
  var ly: Int16 = 0  // XInput style: positive = up
  var rx: Int16 = 0
  var ry: Int16 = 0

  // 0 = up, clockwise to 7 = up-left, 8 = centered
  var hat: UInt8 {
    let up = dpad.contains(.up), down = dpad.contains(.down)
    let left = dpad.contains(.left), right = dpad.contains(.right)
    switch (up, right, down, left) {
    case (true, true, _, _): return 1
    case (_, true, true, _): return 3
    case (_, _, true, true): return 5
    case (true, _, _, true): return 7
    case (true, _, _, _): return 0
    case (_, true, _, _): return 2
    case (_, _, true, _): return 4
    case (_, _, _, true): return 6
    default: return 8
    }
  }

  // Pad index + 13-byte body (HIDGamepadReport without its report ID), little-endian
  var payload: [UInt8] {
    func le(_ v: UInt16) -> [UInt8] { [UInt8(v & 0xFF), UInt8(v >> 8)] }
    func le(_ v: Int16) -> [UInt8] { le(UInt16(bitPattern: v)) }
    return [0] + le(buttons) + [hat, lt, rt] + le(lx) + le(ly) + le(rx) + le(ry)
  }
}

// Controllers the board can emulate; index = wire id (include/profiles.h)
let profileNames = ["Generic", "DualShock 4", "Switch Pro"]
let shortProfileNames = ["Generic", "DS4", "Switch Pro"]  // for the overlay's picker

// MARK: - Key bindings (physical keys; defaults match tools/tester.html)

enum Action {
  case button(PadButton)
  case dpad(Direction)
  case stick(Side, dx: Int, dy: Int)
  case trigger(Side)
}

/// Every controller input a key can drive.
enum Control: String, CaseIterable {
  case a, b, x, y, lb, rb, lt, rt, back, start, home, l3, r3
  case dpadUp, dpadDown, dpadLeft, dpadRight
  case leftUp, leftDown, leftLeft, leftRight
  case rightUp, rightDown, rightLeft, rightRight

  var label: String {
    switch self {
    case .a: return "A"
    case .b: return "B"
    case .x: return "X"
    case .y: return "Y"
    case .lb: return "LB"
    case .rb: return "RB"
    case .lt: return "LT"
    case .rt: return "RT"
    case .back: return "Back"
    case .start: return "Start"
    case .home: return "Home"
    case .l3: return "L3 (left click)"
    case .r3: return "R3 (right click)"
    case .dpadUp: return "D-pad up"
    case .dpadDown: return "D-pad down"
    case .dpadLeft: return "D-pad left"
    case .dpadRight: return "D-pad right"
    case .leftUp: return "Left stick up"
    case .leftDown: return "Left stick down"
    case .leftLeft: return "Left stick left"
    case .leftRight: return "Left stick right"
    case .rightUp: return "Right stick up"
    case .rightDown: return "Right stick down"
    case .rightLeft: return "Right stick left"
    case .rightRight: return "Right stick right"
    }
  }

  var action: Action {
    switch self {
    case .a: return .button(.a)
    case .b: return .button(.b)
    case .x: return .button(.x)
    case .y: return .button(.y)
    case .lb: return .button(.lb)
    case .rb: return .button(.rb)
    case .lt: return .trigger(.left)
    case .rt: return .trigger(.right)
    case .back: return .button(.back)
    case .start: return .button(.start)
    case .home: return .button(.home)
    case .l3: return .button(.l3)
    case .r3: return .button(.r3)
    case .dpadUp: return .dpad(.up)
    case .dpadDown: return .dpad(.down)
    case .dpadLeft: return .dpad(.left)
    case .dpadRight: return .dpad(.right)
    case .leftUp: return .stick(.left, dx: 0, dy: 1)
    case .leftDown: return .stick(.left, dx: 0, dy: -1)
    case .leftLeft: return .stick(.left, dx: -1, dy: 0)
    case .leftRight: return .stick(.left, dx: 1, dy: 0)
    case .rightUp: return .stick(.right, dx: 0, dy: 1)
    case .rightDown: return .stick(.right, dx: 0, dy: -1)
    case .rightLeft: return .stick(.right, dx: -1, dy: 0)
    case .rightRight: return .stick(.right, dx: 1, dy: 0)
    }
  }
}

let defaultBindings: [Control: Int] = [
  .a: kVK_ANSI_Z, .b: kVK_ANSI_X, .x: kVK_ANSI_C, .y: kVK_ANSI_V,
  .lb: kVK_ANSI_Q, .rb: kVK_ANSI_E, .lt: kVK_ANSI_1, .rt: kVK_ANSI_3,
  .back: kVK_Tab, .start: kVK_Return, .home: kVK_ANSI_G, .l3: kVK_ANSI_F, .r3: kVK_ANSI_H,
  .dpadUp: kVK_UpArrow, .dpadDown: kVK_DownArrow, .dpadLeft: kVK_LeftArrow, .dpadRight: kVK_RightArrow,
  .leftUp: kVK_ANSI_W, .leftDown: kVK_ANSI_S, .leftLeft: kVK_ANSI_A, .leftRight: kVK_ANSI_D,
  .rightUp: kVK_ANSI_I, .rightDown: kVK_ANSI_K, .rightLeft: kVK_ANSI_J, .rightRight: kVK_ANSI_L,
]

let toggleKey = kVK_ANSI_G  // with ⌥⌘

private let keyNames: [Int: String] = {
  var names: [Int: String] = [
    kVK_Tab: "⇥", kVK_Return: "⏎", kVK_Space: "Space", kVK_Delete: "⌫", kVK_ForwardDelete: "⌦",
    kVK_UpArrow: "↑", kVK_DownArrow: "↓", kVK_LeftArrow: "←", kVK_RightArrow: "→",
    kVK_Home: "Home", kVK_End: "End", kVK_PageUp: "PgUp", kVK_PageDown: "PgDn",
    kVK_ANSI_Semicolon: ";", kVK_ANSI_Quote: "'", kVK_ANSI_Comma: ",", kVK_ANSI_Period: ".",
    kVK_ANSI_Slash: "/", kVK_ANSI_Backslash: "\\", kVK_ANSI_LeftBracket: "[", kVK_ANSI_RightBracket: "]",
    kVK_ANSI_Minus: "-", kVK_ANSI_Equal: "=", kVK_ANSI_Grave: "`", kVK_ANSI_KeypadEnter: "Num⏎",
  ]
  let letters: [(Int, String)] = [
    (kVK_ANSI_A, "A"), (kVK_ANSI_B, "B"), (kVK_ANSI_C, "C"), (kVK_ANSI_D, "D"), (kVK_ANSI_E, "E"),
    (kVK_ANSI_F, "F"), (kVK_ANSI_G, "G"), (kVK_ANSI_H, "H"), (kVK_ANSI_I, "I"), (kVK_ANSI_J, "J"),
    (kVK_ANSI_K, "K"), (kVK_ANSI_L, "L"), (kVK_ANSI_M, "M"), (kVK_ANSI_N, "N"), (kVK_ANSI_O, "O"),
    (kVK_ANSI_P, "P"), (kVK_ANSI_Q, "Q"), (kVK_ANSI_R, "R"), (kVK_ANSI_S, "S"), (kVK_ANSI_T, "T"),
    (kVK_ANSI_U, "U"), (kVK_ANSI_V, "V"), (kVK_ANSI_W, "W"), (kVK_ANSI_X, "X"), (kVK_ANSI_Y, "Y"),
    (kVK_ANSI_Z, "Z"), (kVK_ANSI_0, "0"), (kVK_ANSI_1, "1"), (kVK_ANSI_2, "2"), (kVK_ANSI_3, "3"),
    (kVK_ANSI_4, "4"), (kVK_ANSI_5, "5"), (kVK_ANSI_6, "6"), (kVK_ANSI_7, "7"), (kVK_ANSI_8, "8"),
    (kVK_ANSI_9, "9"), (kVK_ANSI_Keypad0, "Num0"), (kVK_ANSI_Keypad1, "Num1"), (kVK_ANSI_Keypad2, "Num2"),
    (kVK_ANSI_Keypad3, "Num3"), (kVK_ANSI_Keypad4, "Num4"), (kVK_ANSI_Keypad5, "Num5"),
    (kVK_ANSI_Keypad6, "Num6"), (kVK_ANSI_Keypad7, "Num7"), (kVK_ANSI_Keypad8, "Num8"), (kVK_ANSI_Keypad9, "Num9"),
    (kVK_F1, "F1"), (kVK_F2, "F2"), (kVK_F3, "F3"), (kVK_F4, "F4"), (kVK_F5, "F5"), (kVK_F6, "F6"),
    (kVK_F7, "F7"), (kVK_F8, "F8"), (kVK_F9, "F9"), (kVK_F10, "F10"), (kVK_F11, "F11"), (kVK_F12, "F12"),
  ]
  for (code, name) in letters { names[code] = name }
  return names
}()

func keyLabel(_ code: Int?) -> String {
  guard let code else { return "—" }
  return keyNames[code] ?? "Key \(code)"
}

// MARK: - Serial link to the board

final class BoardLink {
  private var fd: Int32 = -1
  private(set) var portPath: String?
  private(set) var profile: UInt8?  // nil: firmware without profile switching

  var isOpen: Bool { fd >= 0 }

  /// Finds the board's serial port by its USB serial number prefix.
  static func findPort() -> String? {
    guard let matching = IOServiceMatching(kIOSerialBSDServiceValue) as NSMutableDictionary? else { return nil }
    matching[kIOSerialBSDTypeKey] = kIOSerialBSDAllTypes
    var iterator: io_iterator_t = 0
    guard IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator) == KERN_SUCCESS else { return nil }
    defer { IOObjectRelease(iterator) }

    var service = IOIteratorNext(iterator)
    while service != 0 {
      defer {
        IOObjectRelease(service)
        service = IOIteratorNext(iterator)
      }
      let serial = IORegistryEntrySearchCFProperty(
        service, kIOServicePlane, "USB Serial Number" as CFString, kCFAllocatorDefault,
        IOOptionBits(kIORegistryIterateRecursively | kIORegistryIterateParents)) as? String
      guard let serial, serial.hasPrefix("LUMINA-") else { continue }
      if let path = IORegistryEntryCreateCFProperty(service, kIOCalloutDeviceKey as CFString, kCFAllocatorDefault, 0)?
        .takeRetainedValue() as? String {
        return path
      }
    }
    return nil
  }

  /// Opens the port and checks the board answers a ping. Returns an error message on failure.
  func open() -> String? {
    guard let path = BoardLink.findPort() else { return "Board not found" }
    let fd = Darwin.open(path, O_RDWR | O_NOCTTY | O_NONBLOCK)
    guard fd >= 0 else { return "Can't open \(path) (in use by Lumina or the tester?)" }

    var tio = termios()
    tcgetattr(fd, &tio)
    cfmakeraw(&tio)
    cfsetspeed(&tio, speed_t(B115200))  // never 1200: that reboots the board into its bootloader
    tio.c_cflag |= tcflag_t(CLOCAL | CREAD)
    tcsetattr(fd, TCSANOW, &tio)
    tcflush(fd, TCIOFLUSH)

    self.fd = fd
    guard write(Wire.frame(Wire.ping)), let pong = awaitFrame(Wire.pong, length: 2, timeout: 0.5) else {
      close()
      return "\(path) did not answer"
    }
    guard pong[0] == Wire.protocolVersion else {
      close()
      return "Firmware speaks protocol v\(pong[0]), expected v\(Wire.protocolVersion)"
    }
    profile = write(Wire.frame(Wire.getProfile)) ? awaitFrame(Wire.profile, length: 1, timeout: 0.3)?.first : nil
    _ = fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK)  // never drop a frame (e.g. a release)
    portPath = path
    return nil
  }

  /// Waits (port still non-blocking) for a frame of the given type and payload length.
  private func awaitFrame(_ type: UInt8, length: Int, timeout: TimeInterval) -> [UInt8]? {
    var buffer: [UInt8] = []
    let deadline = Date().addingTimeInterval(timeout)
    while Date() < deadline {
      var chunk = [UInt8](repeating: 0, count: 64)
      let n = read(fd, &chunk, chunk.count)
      if n > 0 {
        buffer += chunk[0..<n]
        if buffer.count >= 5 + length {
          for i in 0...(buffer.count - 5 - length)
          where buffer[i] == 0xA5 && buffer[i + 1] == 0x5A && buffer[i + 2] == type && Int(buffer[i + 3]) == length
            && Wire.crc8(buffer[(i + 2)..<(i + 4 + length)]) == buffer[i + 4 + length] {
            return Array(buffer[(i + 4)..<(i + 4 + length)])
          }
        }
      } else {
        usleep(5_000)
      }
    }
    return nil
  }

  @discardableResult
  func write(_ bytes: [UInt8]) -> Bool {
    guard fd >= 0 else { return false }
    var offset = 0
    while offset < bytes.count {
      let n = bytes[offset...].withUnsafeBytes { Darwin.write(fd, $0.baseAddress, $0.count) }
      if n < 0 {
        if errno == EINTR || errno == EAGAIN { continue }
        close()
        return false
      }
      offset += n
    }
    return true
  }

  func send(_ state: PadState) -> Bool { write(Wire.frame(Wire.report, state.payload)) }

  /// Asks the board to restart as another controller, then lets go of the port.
  func requestProfile(_ id: UInt8) -> Bool {
    let sent = write(Wire.frame(Wire.setProfile, [id]))
    close()
    return sent
  }

  func close() {
    if fd >= 0 {
      Darwin.close(fd)  // DTR drops, so the board releases every input
      fd = -1
    }
    portPath = nil
    profile = nil
  }
}

// MARK: - Keypad model

final class Keypad: ObservableObject {
  @Published private(set) var state = PadState()
  @Published private(set) var capturing = true
  @Published private(set) var linkStatus = "Connecting…"
  @Published private(set) var connected = false
  @Published private(set) var accessibilityGranted = AXIsProcessTrusted()
  @Published private(set) var profile: UInt8?
  @Published private(set) var bindings: [Control: Int]
  @Published private(set) var remapping: Control?  // next key press is bound to this input
  private var keyMap: [Int: Action] = [:]
  private var reconnectAfter = Date.distantPast
  private static let bindingsKey = "keyBindings"

  init() {
    let saved = UserDefaults.standard.dictionary(forKey: Keypad.bindingsKey) as? [String: Int]
    if let saved {
      bindings = Dictionary(uniqueKeysWithValues: saved.compactMap { k, v in Control(rawValue: k).map { ($0, v) } })
    } else {
      bindings = defaultBindings
    }
    rebuildKeyMap()
  }

  private let link = BoardLink()
  private var held = Set<Int>()
  private var timer: Timer?

  func start() {
    // Reconnects after unplugging; also resends the state so a lost frame can't leave a key stuck
    timer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in self?.tick() }
    tick()
  }

  /// Publishes only real changes: every published write re-renders the overlay, and the
  /// segmented picker re-measures itself on each render, so unchanged writes made it jitter.
  private func setLink(connected: Bool, status: String) {
    if self.connected != connected { self.connected = connected }
    if linkStatus != status { linkStatus = status }
  }

  private func tick() {
    let trusted = AXIsProcessTrusted()
    if accessibilityGranted != trusted { accessibilityGranted = trusted }
    if !link.isOpen {
      guard Date() >= reconnectAfter else { return }  // board is restarting as another controller
      if let error = link.open() {
        setLink(connected: false, status: error)
        return
      }
      if profile != link.profile { profile = link.profile }
    }
    if link.send(state) {
      setLink(connected: true, status: link.portPath ?? "Connected")
    } else {
      setLink(connected: false, status: "Board disconnected")
    }
  }

  func switchProfile(to id: UInt8) {
    guard link.isOpen, id != profile else { return }
    held.removeAll()
    state = PadState()
    if link.requestProfile(id) {
      profile = id
      reconnectAfter = Date().addingTimeInterval(2)  // reconnect once it re-enumerates
      setLink(connected: false, status: "Switching to \(profileNames[Int(id)])…")
    } else {
      setLink(connected: false, status: "Board disconnected")
    }
  }

  // MARK: Remapping

  func beginRemap(_ control: Control) {
    held.removeAll()
    update()
    remapping = control
  }

  func cancelRemap() { remapping = nil }

  func resetBindings() {
    remapping = nil
    bindings = defaultBindings
    saveBindings()
  }

  func keyName(for control: Control) -> String { keyLabel(bindings[control]) }

  /// A key drives one input only: binding it here unbinds it elsewhere.
  private func bind(_ control: Control, to keyCode: Int) {
    for (other, code) in bindings where code == keyCode && other != control {
      bindings[other] = nil
    }
    bindings[control] = keyCode
    saveBindings()
  }

  private func saveBindings() {
    UserDefaults.standard.set(
      Dictionary(uniqueKeysWithValues: bindings.map { ($0.key.rawValue, $0.value) }), forKey: Keypad.bindingsKey)
    rebuildKeyMap()
  }

  private func rebuildKeyMap() {
    keyMap = Dictionary(bindings.map { ($0.value, $0.key.action) }, uniquingKeysWith: { first, _ in first })
  }

  func setCapturing(_ on: Bool) {
    capturing = on
    if !on {
      held.removeAll()
      update()
    }
  }

  /// Returns true when the key was consumed.
  func handle(keyCode: Int, down: Bool, isRepeat: Bool, flags: CGEventFlags) -> Bool {
    if let target = remapping, !flags.contains(.maskCommand) {
      if down && !isRepeat {
        remapping = nil
        if keyCode != kVK_Escape { bind(target, to: keyCode) }
      }
      return true  // the key belongs to the remap, not to the focused app
    }
    if keyCode == toggleKey && flags.contains(.maskCommand) && flags.contains(.maskAlternate) {
      if down && !isRepeat {
        setCapturing(!capturing)
      }
      return true
    }
    guard capturing, keyMap[keyCode] != nil else { return false }
    // Let shortcuts through (⌘Q, ⌘Tab, ...)
    if flags.contains(.maskCommand) && !held.contains(keyCode) { return false }
    if !isRepeat {
      if down { held.insert(keyCode) } else { held.remove(keyCode) }
      update()
    }
    return true
  }

  private func update() {
    var next = PadState()
    var sticks: [Side: (x: Int, y: Int)] = [.left: (0, 0), .right: (0, 0)]
    for key in held {
      switch keyMap[key] {
      case .button(let button): next.buttons |= button.rawValue
      case .dpad(let direction): next.dpad.insert(direction)
      case .trigger(.left): next.lt = 255
      case .trigger(.right): next.rt = 255
      case .stick(let side, let dx, let dy):
        sticks[side]!.x += dx
        sticks[side]!.y += dy
      case nil: break
      }
    }
    func axis(_ v: Int, _ other: Int) -> Int16 {
      let scale = other != 0 ? 0.7071 : 1.0  // keep diagonals on the unit circle
      return Int16(Double(max(-1, min(1, v))) * scale * 32767)
    }
    next.lx = axis(sticks[.left]!.x, sticks[.left]!.y)
    next.ly = axis(sticks[.left]!.y, sticks[.left]!.x)
    next.rx = axis(sticks[.right]!.x, sticks[.right]!.y)
    next.ry = axis(sticks[.right]!.y, sticks[.right]!.x)

    guard next != state else { return }
    state = next
    if link.isOpen && !link.send(state) {
      setLink(connected: false, status: "Board disconnected")
    }
  }

  func shutdown() {
    timer?.invalidate()
    held.removeAll()
    state = PadState()
    _ = link.send(state)
    link.close()
  }
}

// MARK: - Event tap

final class KeyTap {
  private var tap: CFMachPort?
  private let keypad: Keypad

  init(keypad: Keypad) { self.keypad = keypad }

  /// Installs the tap; fails until Accessibility permission is granted.
  func install() -> Bool {
    if tap != nil { return true }
    let mask = (1 << CGEventType.keyDown.rawValue) | (1 << CGEventType.keyUp.rawValue)
    let callback: CGEventTapCallBack = { _, type, event, refcon in
      let me = Unmanaged<KeyTap>.fromOpaque(refcon!).takeUnretainedValue()
      if type == .tapDisabledByTimeout || type == .tapDisabledByUserInput {
        if let tap = me.tap { CGEvent.tapEnable(tap: tap, enable: true) }
        return Unmanaged.passUnretained(event)
      }
      let consumed = me.keypad.handle(
        keyCode: Int(event.getIntegerValueField(.keyboardEventKeycode)),
        down: type == .keyDown,
        isRepeat: event.getIntegerValueField(.keyboardEventAutorepeat) != 0,
        flags: event.flags)
      return consumed ? nil : Unmanaged.passUnretained(event)
    }
    guard let tap = CGEvent.tapCreate(
      tap: .cgSessionEventTap, place: .headInsertEventTap, options: .defaultTap,
      eventsOfInterest: CGEventMask(mask), callback: callback,
      userInfo: Unmanaged.passUnretained(self).toOpaque())
    else { return false }
    self.tap = tap
    let source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0)
    CFRunLoopAddSource(CFRunLoopGetMain(), source, .commonModes)
    CGEvent.tapEnable(tap: tap, enable: true)
    return true
  }
}

// MARK: - Overlay

struct Chip: View {
  let label: String
  let on: Bool
  var body: some View {
    Text(label)
      .font(.system(size: 11, weight: .semibold, design: .rounded))
      .frame(minWidth: 26, minHeight: 20)
      .padding(.horizontal, 4)
      .background(RoundedRectangle(cornerRadius: 5).fill(on ? Color.green : Color.white.opacity(0.12)))
      .foregroundStyle(on ? Color.black : Color.white)
  }
}

struct StickView: View {
  let x: Int16
  let y: Int16
  let label: String
  var body: some View {
    VStack(spacing: 2) {
      ZStack {
        Circle().stroke(Color.white.opacity(0.35), lineWidth: 1.5)
        Circle().fill(x != 0 || y != 0 ? Color.green : Color.white.opacity(0.6))
          .frame(width: 12, height: 12)
          .offset(x: CGFloat(x) / 32767 * 17, y: -CGFloat(y) / 32767 * 17)
      }
      .frame(width: 46, height: 46)
      Text(label).font(.system(size: 9)).foregroundStyle(.secondary)
    }
  }
}

struct OverlayView: View {
  @ObservedObject var keypad: Keypad
  let onToggle: () -> Void
  let onOpenKeys: () -> Void

  private func chip(_ name: String, _ control: Control, on: Bool) -> Chip {
    Chip(label: "\(name) \(keypad.keyName(for: control))", on: on)
  }

  private func stickLabel(_ up: Control, _ left: Control, _ down: Control, _ right: Control) -> String {
    [up, left, down, right].map { keypad.keyName(for: $0) }.joined(separator: " ")
  }

  var body: some View {
    let s = keypad.state
    VStack(alignment: .leading, spacing: 8) {
      HStack(spacing: 6) {
        Circle().fill(keypad.connected ? Color.green : Color.red).frame(width: 8, height: 8)
        Text(keypad.linkStatus).font(.system(size: 11)).lineLimit(1).truncationMode(.middle)
        Spacer()
        Button(keypad.capturing ? "Capturing" : "Paused", action: onToggle)
          .buttonStyle(.borderedProminent)
          .tint(keypad.capturing ? .green : .gray)
          .controlSize(.small)
      }
      if !keypad.accessibilityGranted {
        Text("Allow Lumina Keypad in System Settings → Privacy & Security → Accessibility, then relaunch it.")
          .font(.system(size: 11)).foregroundStyle(.orange).fixedSize(horizontal: false, vertical: true)
      }
      HStack(alignment: .center, spacing: 10) {
        StickView(x: s.lx, y: s.ly, label: stickLabel(.leftUp, .leftLeft, .leftDown, .leftRight))
        VStack(spacing: 2) {
          Chip(label: "↑", on: s.dpad.contains(.up))
          HStack(spacing: 2) {
            Chip(label: "←", on: s.dpad.contains(.left))
            Chip(label: "→", on: s.dpad.contains(.right))
          }
          Chip(label: "↓", on: s.dpad.contains(.down))
        }
        VStack(spacing: 3) {
          HStack(spacing: 3) {
            chip("LT", .lt, on: s.lt > 0)
            chip("LB", .lb, on: s.buttons & PadButton.lb.rawValue != 0)
            chip("RB", .rb, on: s.buttons & PadButton.rb.rawValue != 0)
            chip("RT", .rt, on: s.rt > 0)
          }
          HStack(spacing: 3) {
            chip("A", .a, on: s.buttons & PadButton.a.rawValue != 0)
            chip("B", .b, on: s.buttons & PadButton.b.rawValue != 0)
            chip("X", .x, on: s.buttons & PadButton.x.rawValue != 0)
            chip("Y", .y, on: s.buttons & PadButton.y.rawValue != 0)
          }
          HStack(spacing: 3) {
            chip("Back", .back, on: s.buttons & PadButton.back.rawValue != 0)
            chip("Start", .start, on: s.buttons & PadButton.start.rawValue != 0)
            chip("Home", .home, on: s.buttons & PadButton.home.rawValue != 0)
          }
          HStack(spacing: 3) {
            chip("L3", .l3, on: s.buttons & PadButton.l3.rawValue != 0)
            chip("R3", .r3, on: s.buttons & PadButton.r3.rawValue != 0)
          }
        }
        StickView(x: s.rx, y: s.ry, label: stickLabel(.rightUp, .rightLeft, .rightDown, .rightRight))
      }
      HStack(spacing: 8) {
        Text("Controller").font(.system(size: 11)).foregroundStyle(.secondary)
        Picker("Controller", selection: Binding(
          get: { Int(keypad.profile ?? 255) },
          set: { keypad.switchProfile(to: UInt8($0)) })
        ) {
          ForEach(shortProfileNames.indices, id: \.self) { Text(shortProfileNames[$0]).tag($0) }
        }
        .pickerStyle(.segmented)
        .labelsHidden()
        .controlSize(.small)
        .frame(width: 210)  // fixed, equal segments: its layout must not depend on state
        .disabled(!keypad.connected || keypad.profile == nil)
        Spacer()
        Button("Mapping", action: onOpenKeys).controlSize(.small)
      }
      Text("⌥⌘G toggles capture. While capturing, mapped keys go to the controller only.")
        .font(.system(size: 10)).foregroundStyle(.secondary)
    }
    .padding(12)
    .frame(width: 430)
  }
}

// MARK: - Key mapping window

struct KeyMapView: View {
  @ObservedObject var keypad: Keypad

  private let columns: [[Control]] = [
    [.a, .b, .x, .y, .lb, .rb, .lt, .rt, .back, .start, .home, .l3, .r3],
    [.dpadUp, .dpadDown, .dpadLeft, .dpadRight, .leftUp, .leftDown, .leftLeft, .leftRight,
     .rightUp, .rightDown, .rightLeft, .rightRight],
  ]

  var body: some View {
    VStack(alignment: .leading, spacing: 12) {
      Text("Click an input, then press the key for it (Esc cancels). A key can drive one input; "
        + "reusing it unbinds the old one.")
        .font(.system(size: 11)).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
      if !keypad.accessibilityGranted {
        Text("Remapping needs Accessibility permission, like capturing.")
          .font(.system(size: 11)).foregroundStyle(.orange)
      }
      HStack(alignment: .top, spacing: 20) {
        ForEach(columns.indices, id: \.self) { i in
          VStack(spacing: 4) {
            ForEach(columns[i], id: \.self) { control in
              HStack {
                Text(control.label).font(.system(size: 12)).frame(width: 120, alignment: .leading)
                Button(keypad.remapping == control ? "Press a key…" : keypad.keyName(for: control)) {
                  keypad.beginRemap(control)
                }
                .frame(width: 96)
              }
            }
          }
        }
      }
      HStack {
        Spacer()
        Button("Reset to Defaults") { keypad.resetBindings() }
      }
    }
    .padding(16)
    .frame(width: 520)
  }
}

// MARK: - App

final class AppDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
  let keypad = Keypad()
  lazy var keyTap = KeyTap(keypad: keypad)
  var panel: NSPanel!
  var keysWindow: NSWindow?
  var tapRetry: Timer?

  func applicationDidFinishLaunching(_ notification: Notification) {
    // macOS ties the permission to the code signature, which changes with every (ad-hoc signed)
    // build, so an entry left by an earlier build stays switched on but no longer applies.
    // Clear this app's own stale entries so the prompt below creates one that works.
    if !AXIsProcessTrusted() {
      resetOwnPermissions()
    }
    let prompt = kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String
    _ = AXIsProcessTrustedWithOptions([prompt: true] as CFDictionary)
    if !keyTap.install() {
      tapRetry = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] timer in
        if self?.keyTap.install() == true { timer.invalidate() }
      }
    }
    keypad.start()

    let view = OverlayView(
      keypad: keypad,
      onToggle: { [weak self] in
        guard let self else { return }
        self.keypad.setCapturing(!self.keypad.capturing)
      },
      onOpenKeys: { [weak self] in self?.showKeysWindow() })
    let hosting = NSHostingView(rootView: view)
    panel = NSPanel(
      contentRect: NSRect(origin: .zero, size: hosting.fittingSize),
      styleMask: [.titled, .closable, .nonactivatingPanel, .utilityWindow],
      backing: .buffered, defer: false)
    panel.title = "Lumina Keypad"
    // See-through background; text and chips stay fully opaque
    panel.appearance = NSAppearance(named: .darkAqua)
    panel.isOpaque = false
    panel.backgroundColor = NSColor(white: 0.08, alpha: 0.7)
    panel.titlebarAppearsTransparent = true
    panel.contentView = hosting
    panel.level = .statusBar  // above normal and floating windows
    panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]
    panel.isFloatingPanel = true
    panel.hidesOnDeactivate = false
    panel.becomesKeyOnlyIfNeeded = true
    panel.isMovableByWindowBackground = true
    panel.delegate = self
    if let screen = NSScreen.main?.visibleFrame {
      panel.setFrameTopLeftPoint(NSPoint(x: screen.maxX - panel.frame.width - 20, y: screen.maxY - 20))
    }
    panel.orderFrontRegardless()
  }

  /// Removes Accessibility / input-monitoring entries for this bundle ID only.
  private func resetOwnPermissions() {
    guard let bundleID = Bundle.main.bundleIdentifier else { return }
    for service in ["Accessibility", "ListenEvent", "PostEvent"] {
      let tccutil = Process()
      tccutil.executableURL = URL(fileURLWithPath: "/usr/bin/tccutil")
      tccutil.arguments = ["reset", service, bundleID]
      tccutil.standardOutput = FileHandle.nullDevice
      tccutil.standardError = FileHandle.nullDevice
      try? tccutil.run()
      tccutil.waitUntilExit()
    }
  }

  func showKeysWindow() {
    if keysWindow == nil {
      let window = NSWindow(
        contentRect: .zero, styleMask: [.titled, .closable], backing: .buffered, defer: false)
      window.title = "Key Mapping"
      window.contentView = NSHostingView(rootView: KeyMapView(keypad: keypad))
      window.setContentSize(window.contentView!.fittingSize)
      window.level = .statusBar
      window.isReleasedWhenClosed = false
      window.delegate = self
      window.center()
      keysWindow = window
    }
    NSApp.activate(ignoringOtherApps: true)
    keysWindow?.makeKeyAndOrderFront(nil)
  }

  func windowWillClose(_ notification: Notification) {
    if (notification.object as? NSWindow) === keysWindow {
      keypad.cancelRemap()
    } else {
      NSApp.terminate(nil)  // closing the overlay quits
    }
  }

  func applicationWillTerminate(_ notification: Notification) { keypad.shutdown() }
}

@main
struct LuminaKeypadMain {
  static func main() {
    let app = NSApplication.shared
    let delegate = AppDelegate()
    app.delegate = delegate
    app.setActivationPolicy(.accessory)  // no Dock icon, never steals focus from the game
    app.run()
  }
}
