#!/bin/bash
# Builds "Lumina Keypad.app" into tools/keypad/build/.
# macOS ties the Accessibility permission to the app's signature; after a rebuild you may
# need to remove and re-add the app in System Settings → Privacy & Security → Accessibility.
set -euo pipefail

cd "$(dirname "$0")"
APP="build/Lumina Keypad.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"

xcrun swiftc -O -swift-version 5 -parse-as-library \
  -framework AppKit -framework SwiftUI -framework IOKit -framework Carbon \
  LuminaKeypad.swift -o "$APP/Contents/MacOS/LuminaKeypad"

cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleIdentifier</key><string>com.lumina.keypad</string>
  <key>CFBundleName</key><string>Lumina Keypad</string>
  <key>CFBundleExecutable</key><string>LuminaKeypad</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>LSUIElement</key><true/>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST

codesign --force --sign - "$APP"
echo "Built $APP"
