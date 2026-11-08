#!/usr/bin/env python3
"""向工牌的 USB 口发 HELLO。回答了就退出 0。"""
import os
import sys
import time

open("/tmp/usb-hello-ran", "w").write("start\n")
path = sys.argv[1]
fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
os.write(fd, b"HELLO\n")
buf = b""
end = time.time() + 4
while time.time() < end:
    try:
        chunk = os.read(fd, 1000)
    except BlockingIOError:
        chunk = b""
    if chunk:
        buf += chunk
        if b"HELLO ai-passport-openclaw" in buf:
            open("/tmp/usb-hello-ran", "a").write("ok\n")
            raise SystemExit(0)
    else:
        time.sleep(0.05)
raise SystemExit(1)
