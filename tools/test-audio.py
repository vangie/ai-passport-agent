#!/usr/bin/env python3
"""Exercise bounded microphone capture over USB; no flash, reset or audio file."""

import argparse
import time

import serial


def receive(port, prefixes, timeout=10):
    deadline = time.monotonic() + timeout
    buffer = bytearray()
    while time.monotonic() < deadline:
        buffer.extend(port.read(port.in_waiting or 1))
        while b"\n" in buffer:
            line, _, remainder = buffer.partition(b"\n")
            buffer = bytearray(remainder)
            text = line.decode("utf-8", errors="replace").strip()
            if "Guru Meditation" in text or "Rebooting..." in text:
                raise RuntimeError("device restarted during audio test")
            if text.startswith(prefixes):
                return text
        if len(buffer) > 8192:
            raise RuntimeError("device returned an oversized diagnostic line")
    raise TimeoutError("device did not return an audio diagnostic before timeout")


def fields(line):
    return dict(item.split("=", 1) for item in line.split() if "=" in item)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="AI Passport USB serial port")
    parser.add_argument("--cycles", type=int, default=5)
    args = parser.parse_args()
    if not 1 <= args.cycles <= 20:
        parser.error("--cycles must be between 1 and 20")
    with serial.Serial(args.port, 115200, timeout=0.1, write_timeout=2) as port:
        port.write(b"HELLO\n")
        print(receive(port, ("HELLO ai-passport-openclaw ",)), flush=True)
        port.write(b"DIAG\n")
        state = receive(port, ("DIAG ",))
        print(state, flush=True)
        before = fields(state)
        if before.get("screen") != "0" or before.get("recording") != "0":
            raise RuntimeError("return to the home page and finish the current recording first")
        for index in range(args.cycles):
            port.write(b"AUDIO_TEST\n")
            result = receive(port, ("AUDIO_TEST done ", "AUDIO_TEST failed ",
                                    "AUDIO_TEST busy", "AUDIO_TEST read_failed", "ERR "))
            print(f"{index + 1}/{args.cycles}: {result}", flush=True)
            data = fields(result)
            if (not result.startswith("AUDIO_TEST done ") or
                    data.get("error") != "ESP_OK" or data.get("phase") != "stopped" or
                    data.get("worker") != "0" or data.get("recording") != "0" or
                    int(data.get("chunks", "0")) < 40):
                raise RuntimeError("capture or worker cleanup failed; query DIAG for the retained error")
        print("Device audio capture/stop/retry: PASS (speech recognition/quality not tested)")


if __name__ == "__main__":
    main()
