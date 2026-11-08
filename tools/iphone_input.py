#!/usr/bin/env python3
"""把本机窗口里的点击送进没有界面的 iPhone 模拟器。"""

import json
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHOT = ROOT / "sim" / "shots" / "iphone.jpg"
EVENTS = ROOT / "sim" / "shots" / "iphone-events.jsonl"


def run(args):
    return subprocess.run(["xcrun", "simctl", *args], check=False, capture_output=True)


def refresh():
    SHOT.parent.mkdir(parents=True, exist_ok=True)
    run(["io", "booted", "screenshot", str(SHOT)])


def main():
    EVENTS.write_text("")
    offset = 0
    while True:
        time.sleep(0.2)
        if not EVENTS.exists():
            continue
        data = EVENTS.read_bytes()
        if len(data) <= offset:
            continue
        chunk = data[offset:]
        offset = len(data)
        for line in chunk.splitlines():
            if not line.strip():
                continue
            event = json.loads(line)
            if event.get("type") != "tap":
                continue
            # 模拟器屏幕是 1206 x 2622。窗口坐标是 0..1。
            x = int(float(event["x"]) * 1206)
            y = int(float(event["y"]) * 2622)
            run(["io", "booted", "enumerate"])
            subprocess.run(
                ["xcrun", "simctl", "spawn", "booted", "notifyutil", "-p", "badge.tap"],
                check=False,
                capture_output=True,
            )
            print(f"tap {x} {y}", flush=True)
        refresh()


if __name__ == "__main__":
    main()
