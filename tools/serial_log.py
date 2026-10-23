#!/usr/bin/env python3
"""Reset the board and capture a short boot log (idf.py monitor needs a real
TTY, which the Claude Code harness doesn't have).

Run with the IDF python env (has pyserial):
  ~/.espressif/python_env/idf6.2_py3.14_env/bin/python tools/serial_log.py \
      [seconds] [grep-regex]

Port /dev/ttyUSB0 (the DevKit, CP2102); PORT=... in the environment picks another one.

Retries once if nothing matching comes back -- the first read after a flash
or USB re-enumeration is sometimes empty. SIGTERM/SIGINT end the capture
early and still print what arrived (matches are only printed at the end).
"""
import os
import re
import signal
import sys
import time

import serial

PORT = os.environ.get("PORT", "/dev/ttyUSB0")  # the DevKit (CP2102)
MAX_BYTES = 200_000  # 115200 baud can't legitimately produce more in ~20s
stopped = False


def stop(*_):
    global stopped
    stopped = True


def capture(seconds):
    buf = b""
    try:
        with serial.Serial(PORT, 115200, timeout=0.2) as s:
            s.dtr = False
            s.rts = True  # hold EN low -> reset
            time.sleep(0.1)
            s.rts = False
            start = time.time()
            while not stopped and time.time() - start < seconds and len(buf) < MAX_BYTES:
                buf += s.read(4096)
    except serial.SerialException as e:
        # unplugged (or never there): print what arrived, and fail
        print(buf.decode(errors="replace"), end="")
        sys.exit(f"{PORT}: {e}")
    return buf.decode(errors="replace").splitlines()


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 10
    pattern = re.compile(sys.argv[2]) if len(sys.argv) > 2 else None
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    for _ in range(2):
        lines = [l for l in capture(seconds) if not pattern or pattern.search(l)]
        if lines or stopped:
            print("\n".join(lines))
            return
    print("(nothing captured -- ask what the screen shows)", file=sys.stderr)
    sys.exit(1)


if __name__ == "__main__":
    main()
