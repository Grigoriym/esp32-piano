#!/usr/bin/env python3
"""Watches the toy's log for pots jittering on their own, with timestamps.

The toy's log (build-host/toy-serial.log, written by the bridge) has no times.
This follows it (and starts over when the service truncates it), stamps each
line, and writes to build-host/pot-watch.log:
  - every POT line and every non-note line (beat, program, RECORD, stalls...),
  - "JITTER START <pot>" when a pot hops back and forth (>= 5 direction changes
    within 3 s, all within 10 levels: a hand turning it moves one way, further), with the 40 lines before it,
  - "JITTER END <pot>" after 5 s without a change, and a count once a minute
    while it lasts.
Bridge lines from journalctl (cable gone/open, which path plays) go in too.
Usage: tools/pot_watch.py (runs until killed).
"""
import collections
import os
import re
import subprocess
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG = os.path.join(ROOT, "build-host/toy-serial.log")
OUT = os.path.join(ROOT, "build-host/pot-watch.log")
WINDOW_S, REVERSALS, SPAN, QUIET_S = 3.0, 5, 10, 5.0

out = open(OUT, "a", buffering=1)
lock = threading.Lock()
recent = collections.deque(maxlen=40)


def write(line, keep=True):
    stamped = time.strftime("%H:%M:%S") + f".{int(time.time() * 1000) % 1000:03d} {line}"
    with lock:
        out.write(stamped + "\n")
        if keep:
            recent.append(stamped)


def bridge():
    p = subprocess.Popen(["journalctl", "--user", "-u", "chicco-piano", "-f", "-n", "0", "-o", "cat"],
                         stdout=subprocess.PIPE, text=True)
    for line in p.stdout:
        if re.search(r"cable|playing what|late|busy|woke|HELLO|up,", line):
            write("BRIDGE " + line.rstrip())


class Pot:
    def __init__(self):
        self.changes = collections.deque()  # (time, level)
        self.jitter = False
        self.count = 0
        self.last = 0.0
        self.reported = 0.0


pots = collections.defaultdict(Pot)


def pot_line(name, level, now):
    p = pots[name]
    p.changes.append((now, level))
    p.last = now
    while p.changes and now - p.changes[0][0] > WINDOW_S:
        p.changes.popleft()
    if p.jitter:
        p.count += 1
        if now - p.reported > 60:
            write(f"JITTER still {name}: {p.count} changes so far")
            p.reported = now
        return
    levels = [lv for _, lv in p.changes]
    steps = [b - a for a, b in zip(levels, levels[1:]) if b != a]
    reversals = sum(1 for a, b in zip(steps, steps[1:]) if (a > 0) != (b > 0))
    if reversals >= REVERSALS and max(levels) - min(levels) <= SPAN:
        p.jitter, p.count, p.reported = True, len(levels), now
        with lock:
            before = list(recent)
        write(f"JITTER START {name} (levels {min(levels)}-{max(levels)}); the lines before it:")
        for b in before:
            out.write("    | " + b + "\n")


def check_quiet():
    while True:
        time.sleep(1)
        now = time.time()
        for name, p in list(pots.items()):
            if p.jitter and now - p.last > QUIET_S:
                p.jitter = False
                write(f"JITTER END {name} after {p.count} changes")


write("watch started")
threading.Thread(target=bridge, daemon=True).start()
threading.Thread(target=check_quiet, daemon=True).start()
f0 = open(LOG, "rb")
f0.seek(0, 2)  # start at the end: only what happens from now on
follow_pos = f0.tell()
f0.close()


def follow_from_end():
    f = open(LOG, "rb")
    f.seek(follow_pos)
    pos, buf = follow_pos, b""
    while True:
        size = os.path.getsize(LOG)
        if size < pos:
            write("(log started over: service restart)")
            f.close()
            f = open(LOG, "rb")
            pos, buf = 0, b""
        chunk = f.read()
        if not chunk:
            time.sleep(0.05)
            continue
        pos += len(chunk)
        buf += chunk
        *lines, buf = buf.split(b"\n")
        now = time.time()
        for raw in lines:
            line = raw.decode("ascii", "replace").strip()
            if not line or line.startswith(("MIDI  note", "DOWN", "UP ")):
                continue
            write(line)
            m = re.match(r"POT +(\w+) (\d+)$", line)
            if m:
                pot_line(m[1], int(m[2]), now)


follow_from_end()
