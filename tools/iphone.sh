#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
device="iPhone 17"
derived="$root/ios/build"
(cd "$root/ios" && xcodegen generate >/dev/null)
xcrun simctl boot "$device" >/dev/null 2>&1 || true
xcodebuild -project "$root/ios/Passport.xcodeproj" -scheme Passport \
  -destination "platform=iOS Simulator,name=$device" \
  -derivedDataPath "$derived" build >/tmp/passport-ios-build.log
app="$derived/Build/Products/Debug-iphonesimulator/Passport.app"
if [ ! -d "$app" ]; then
  printf '没有找到 Passport.app\n' >&2
  exit 1
fi
xcrun simctl install booted "$app"
xcrun simctl terminate booted ai.mirasim.passport >/dev/null 2>&1 || true
xcrun simctl launch booted ai.mirasim.passport >/tmp/passport-ios-launch.txt
shot="$root/sim/shots/iphone.png"
mkdir -p "$root/sim/shots"
if [ -d "/Applications/Xcode.app/Contents/Applications/Simulator.app" ]; then
  open -a "/Applications/Xcode.app/Contents/Applications/Simulator.app"
  printf '模拟器窗口已打开。\n'
else
  xcrun simctl io booted screenshot "$shot" >/dev/null
  swift "$root/tools/iphone_window.swift" "$root" >/tmp/passport-iphone-window.log 2>&1 &
  echo $! > /tmp/passport-iphone-window.pid
  (
    while true; do
      xcrun simctl io booted screenshot "$shot" >/dev/null 2>&1 || exit 0
      sleep 0.4
    done
  ) >/tmp/passport-iphone-refresh.log 2>&1 &
  echo $! > /tmp/passport-iphone-refresh.pid
  printf '这套 Xcode 没有 Simulator.app。本机窗口显示模拟器画面：%s\n' "$shot"
fi
printf 'iPhone 模拟器的蓝牙连不到 PassportSim 里的虚拟工牌。\n'
