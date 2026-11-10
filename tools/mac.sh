#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
make -C "$root/sim" config-sock >/dev/null
mkdir -p "$root/sim/shots"
# 上次留下的 socket 文件不代表替身还活着，按进程判断。
if ! pgrep -f "$root/sim/config-sock" >/dev/null; then
  rm -f /tmp/passport-config.sock
  "$root/sim/config-sock" >"$root/sim/shots/config-sock.log" 2>&1 &
  echo $! >"$root/sim/shots/config-sock.pid"
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    [ -S /tmp/passport-config.sock ] && break
    sleep 0.1
  done
fi
cd "$root/mac"
# Release the running executable and USB connection before replacing its files.
pkill -x "AI Passport OpenClaw" 2>/dev/null || true
xcodegen generate >/dev/null
xcodebuild -project PassportMac.xcodeproj -scheme PassportMac -destination 'platform=macOS' \
  -derivedDataPath "$root/mac/build" DEVELOPMENT_TEAM="${DEVELOPMENT_TEAM:-}" \
  CODE_SIGNING_ALLOWED="${CODE_SIGNING_ALLOWED:-NO}" \
  build >/tmp/passport-mac-build.log
app=$(find "$root/mac/build/Build/Products" -name 'AI Passport OpenClaw.app' -type d | head -1)
# Unsigned builds still contain linker signatures. Seal the whole local bundle
# so Launch Services can verify resources after an incremental build.
if [ "${CODE_SIGNING_ALLOWED:-NO}" = NO ]; then
  codesign --force --deep --sign - "$app"
fi
codesign --verify --deep --strict "$app"
open "$app"
printf 'AI Passport OpenClaw 已打开，优先连接 USB 设备\n'
