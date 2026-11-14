#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
app=${2:-"$root/build/agent-talk.bin"}
expected_hash=${3:-}
port=${1:-}
if [[ -z "$port" || ! -f "$app" || -z "$expected_hash" ]]; then
  printf 'usage: flash-app.sh PORT APPLICATION SHA256\n' >&2
  exit 2
fi
export PATH="${HOME}/.local/bin:/opt/homebrew/bin:/usr/local/bin:${PATH}"
python_bin=${PASSPORT_PYTHON:-python3}
if [[ -n "${IDF_PYTHON_ENV_PATH:-}" && -x "$IDF_PYTHON_ENV_PATH/bin/python" ]]; then
  python_bin="$IDF_PYTHON_ENV_PATH/bin/python"
fi
"$python_bin" - "$app" "$expected_hash" <<'PY'
import hashlib
from pathlib import Path
import sys
data = Path(sys.argv[1]).read_bytes()
if len(data) < 288 or data[0] != 0xe9 or data[12:14] != b'\x05\x00' or data[32:36] != b'\x32\x54\xcd\xab':
    sys.exit('Not an ESP32-C3 application; refusing to flash')
if 0x10000 + len(data) > 0x350000:
    sys.exit('Application overlaps device data; refusing to flash')
if hashlib.sha256(data).hexdigest() != sys.argv[2]:
    sys.exit('Application checksum mismatch; refusing to flash')
PY
if command -v esptool >/dev/null 2>&1; then
  esptool=(esptool)
elif "$python_bin" -c 'import esptool' >/dev/null 2>&1; then
  esptool=("$python_bin" -m esptool)
else
  printf 'Install Python 3 and esptool first: pipx install esptool==4.11.0\n' >&2
  exit 4
fi
if "${esptool[@]}" --help 2>&1 | grep -q 'write-flash'; then
  write_command=write-flash
else
  write_command=write_flash
fi
exec "${esptool[@]}" --chip esp32c3 -p "$port" -b 460800 \
  --before default_reset --after hard_reset \
  "$write_command" --flash_mode dio --flash_freq 80m --flash_size 8MB \
  0x10000 "$app"
