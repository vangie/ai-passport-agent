#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
if ! command -v passportsim >/dev/null 2>&1; then
  printf '先安装 PassportSim：https://github.com/BlackHole1/passportsim\n' >&2
  exit 1
fi
python3 "$root/tools/pebundle.py" >/dev/null
# Refresh the actual local server directory even when it is already running.
python3 - "$root" <<'PYTHON'
import importlib.util
import sys
from pathlib import Path
spec = importlib.util.spec_from_file_location("passport_page", Path(sys.argv[1]) / "tools/passport_page.py")
page = importlib.util.module_from_spec(spec)
spec.loader.exec_module(page)
page.prepare()
PYTHON
mkdir -p "$root/sim/shots"
if ! curl -sf -o /dev/null "http://127.0.0.1:8770/?lang=zh-CN&fw=badge"; then
  python3 "$root/tools/passport_page.py" >"$root/sim/shots/passport-page.log" 2>&1 &
  echo $! >"$root/sim/shots/passport-page.pid"
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    if curl -sf -o /dev/null "http://127.0.0.1:8770/?lang=zh-CN&fw=badge"; then
      break
    fi
    sleep 0.2
  done
fi
open "http://127.0.0.1:8770/?lang=zh-CN&fw=badge"
printf '页面已打开，载入 build/badge.pebundle\n'
