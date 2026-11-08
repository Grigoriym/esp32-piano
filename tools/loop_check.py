#!/usr/bin/env python3
"""Checks a loop against what was played into it (the user, 2026-11-08: recordings
sometimes play back wrong and a guess finds nothing).

    tools/loop_check.py record [seconds]   capture what the PC plays (default 180 s,
                                           Ctrl+C ends early), then check it
    tools/loop_check.py <capture.mid>      check a capture (arecordmidi -t 1000)

It needs the toy's marks on channel 16 (main.c, 2026-11-08): CC 118 = what the looper
did (* 8 + layers), CC 119 = a pass's start. For each take (the first layer or one
added) it takes the notes played live between its marks: channel 1 (keys, Octave, Arp,
Stutter; a note held with Hold ends at the pedal's release) and channel 11 (Echo's
repeats, except those over their own held key: take.c leaves them out). Then, in each
full pass after the take, it looks for every one of them on the layer's channel at the
same place in the loop, and lists what is missing, moved, cut short, longer, or extra.

Places are given in seconds from the pass start and as bar.beat when the metronome or
the beat played (bars from the metronome's beat-1 clicks, else the pass start).
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from midi_dump import events  # noqa: E402

KEY_CH, ECHO_CH, LOOP_CH, METRO_CH, MARK_CH = 1, 11, 3, 9, 16  # 1-based, as aseqdump
TAKE_CC, PASS_CC = 118, 119
ARMED, CLOSED, DUB, DUB_CLOSED, STOPPED, DROPPED = 1, 2, 3, 4, 5, 6
TIME_MS = 35    # a scan or two, and the cable/WiFi jitter
LENGTH_MS = 60  # the same, at both ends
NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']


def name(n):
    return f'{NAMES[n % 12]}{n // 12 - 1}'


def notes_and_marks(path):
    """Notes per channel as [start, end, pitch, vel], the marks, CC64 per channel."""
    on, notes, marks, pedal = {}, {}, [], {}
    last = 0
    for t, st, a in events(path):
        last = max(last, t)
        ch, kind = (st & 0x0f) + 1, st & 0xf0
        if kind == 0x90 and a[1] > 0:
            if (ch, a[0]) in on:  # retriggered: the old one ends here
                s, v = on.pop((ch, a[0]))
                notes.setdefault(ch, []).append([s, t, a[0], v])
            on[(ch, a[0])] = (t, a[1])
        elif kind in (0x80, 0x90):
            if (ch, a[0]) in on:
                s, v = on.pop((ch, a[0]))
                notes.setdefault(ch, []).append([s, t, a[0], v])
        elif kind == 0xb0 and ch == MARK_CH and a[0] in (TAKE_CC, PASS_CC):
            marks.append((t, a[0], a[1]))
        elif kind == 0xb0 and a[0] == 64:
            pedal.setdefault(ch, []).append((t, a[1] >= 64))
        elif kind == 0xb0 and a[0] == 123:  # all notes off
            for (c, n) in [k for k in on if k[0] == ch]:
                s, v = on.pop((c, n))
                notes.setdefault(ch, []).append([s, t, n, v])
    for (ch, n), (s, v) in on.items():
        notes.setdefault(ch, []).append([s, last, n, v])
    for ch in notes:
        notes[ch].sort()
    return notes, marks, pedal, last


def held_by_pedal(notes, pedal):
    """Live notes end where the sound ends: a note let go with Hold down rings to its release."""
    out = []
    for s, e, n, v in notes:
        down, end = False, e
        for t, d in pedal:
            if t > e:
                if down and not d:
                    end = t
                    break
                if not down:
                    break
            down = d
        out.append([s, end, n, v])
    return out


def takes_of(marks):
    """[(start, end, layer)], each take the loop still has when its passes are checked."""
    takes, open_take = [], None
    for t, cc, v in marks:
        if cc != TAKE_CC:
            continue
        what, layers = v // 8, v % 8
        if what == DROPPED:
            takes = [k for k in takes if k[2] < layers]
            open_take = None
        elif what == ARMED:
            takes, open_take = [], (t, 0)
        elif what == DUB:
            open_take = (t, layers)
        elif what in (CLOSED, DUB_CLOSED) and open_take:
            takes.append((open_take[0], t, open_take[1]))
            open_take = None
    return takes


def passes_after(marks, t0, last):
    """Pass starts after t0, until the loop stops, is dropped or rearmed: [(start, end)]."""
    starts, end = [], last
    for t, cc, v in marks:
        if t <= t0:
            continue
        if cc == PASS_CC:
            starts.append(t)
        elif cc == TAKE_CC and v // 8 in (ARMED, STOPPED, DROPPED, DUB):
            end = t
            break
    starts = [s for s in starts if s <= end]
    out = []
    for a, b in zip(starts, starts[1:] + [None]):
        if b is None:  # the last one counts if the capture saw it end
            if len(starts) > 1 and a + (a - starts[-2]) <= end:
                out.append((a, a + (a - starts[-2])))
        else:
            out.append((a, b))
    return out


def bar_beat(t, clicks, beat_ms):
    """Where t is, as bar.beat from the last beat-1 click (None: no metronome)."""
    before = [c for c in clicks if c <= t]
    if not before or not beat_ms:
        return ''
    b = (t - before[-1]) / beat_ms
    return f' (beat {1 + b:.2f} of its bar)'


def check(path):
    notes, marks, pedal, last = notes_and_marks(path)
    if not marks:
        print('no loop marks in the capture: the toy needs firmware from 2026-11-08 on')
        return 2
    clicks = sorted(s for s, e, n, v in notes.get(METRO_CH, []) if n == 84)
    beats = sorted(s for s, e, n, v in notes.get(METRO_CH, []))
    beat_ms = (beats[-1] - beats[0]) / (len(beats) - 1) if len(beats) > 4 else 0
    keys = held_by_pedal(notes.get(KEY_CH, []), pedal.get(KEY_CH, []))
    echoes = notes.get(ECHO_CH, [])
    takes = takes_of(marks)
    if not takes:
        print('no take closed in the capture (RECORD, play, RECORD)')
        return 2
    problems = unchecked = 0
    for (t0, t1, layer) in takes:
        played = [k for k in keys if t0 <= k[0] <= t1]
        held = [(s, e, n) for s, e, n, v in played]
        left_out = 0
        for s, e, n, v in echoes:
            if not t0 <= s <= t1:
                continue
            if any(hs < s < he and hn == n for hs, he, hn in held):
                left_out += 1
                continue
            played.append([s, e, n, v, 'echo'])
        played = sorted(k[:4] + ['key'] if len(k) == 4 else k for k in played)
        ch = LOOP_CH + layer
        passes = passes_after(marks, t1, last)
        print(f'take {layer + 1}: {len(played)} notes played ({left_out} echoes over their held key left out'
              f' by design), layer channel {ch}, {len(passes)} full passes after it')
        if len(passes) < 1:
            print('  no full pass to check: let it play 3 passes after RECORD')
            unchecked += 1
            continue
        length = passes[0][1] - passes[0][0]
        loop_notes = [x for x in notes.get(ch, []) if passes[0][0] <= x[0] < passes[-1][1]]
        used = set()
        for s, e, n, v, src in played:
            p = (s - passes[0][0]) % length
            want = min(e - s, length - p)  # held past the loop's end: stops there
            issues = []
            for i, (a, b) in enumerate(passes):
                period = b - a
                at = a + p
                hit, cap = None, want
                for at, c in ((a + p, want), (a + p - period, min(e - s, length))):
                    # played just before the loop's start: it plays at the start and rings on
                    for j, x in enumerate(loop_notes):
                        if j not in used and x[2] == n and abs(x[0] - at) <= TIME_MS:
                            hit, cap = j, c
                            break
                    if hit is not None:
                        break
                if hit is None:
                    issues.append(f'pass {i + 1}: missing')
                    continue
                used.add(hit)
                x = loop_notes[hit]
                got = x[1] - x[0]
                if src == 'echo':  # repeats of one note overlap and retrigger: where, not how long
                    pass
                elif got < cap - LENGTH_MS:
                    issues.append(f'pass {i + 1}: cut to {got} ms')
                elif got > (e - s) + LENGTH_MS and got > cap + LENGTH_MS:
                    issues.append(f'pass {i + 1}: {got} ms, longer')
                off = min((x[0] - at, x[0] - at + period), key=abs)
                if abs(off) > 15:
                    issues.append(f'pass {i + 1}: {off:+d} ms off')
                if abs(x[3] - v) > 2:
                    issues.append(f'pass {i + 1}: velocity {x[3]}')
            if issues:
                problems += 1
                where = bar_beat(s, clicks, beat_ms)
                what = 'echo' if src == 'echo' else 'note'
                print(f'  {what} {name(n):4} at {p / 1000:5.2f} s{where}, played {e - s} ms vel {v}: ' + '; '.join(issues))
        extra = [x for j, x in enumerate(loop_notes) if j not in used]
        for x in extra:
            p = (x[0] - passes[0][0]) % length
            problems += 1
            print(f'  {name(x[2]):4} at {p / 1000:5.2f} s{bar_beat(x[0], clicks, beat_ms)}: extra in the loop'
                  f' ({x[1] - x[0]} ms vel {x[3]}), never played in the take')
    if problems:
        print(f'{problems} differences')
    elif unchecked:
        print('nothing checked' if unchecked == len(takes) else 'the checked takes are OK')
    else:
        print('OK: the loop plays what was played')
    return 1 if problems else 2 if unchecked == len(takes) else 0


def record(seconds):
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build-host', 'loop-check.mid')
    print(f'recording what the PC plays for {seconds} s into {os.path.normpath(out)} (Ctrl+C ends it)')
    p = subprocess.Popen(['arecordmidi', '-p', 'Chicco Piano', '-b', '60', '-t', '1000', out])
    try:
        p.wait(timeout=seconds)
    except (subprocess.TimeoutExpired, KeyboardInterrupt):
        p.send_signal(2)
        p.wait()
    return check(out)


if __name__ == '__main__':
    if len(sys.argv) >= 2 and sys.argv[1] == 'record':
        sys.exit(record(int(sys.argv[2]) if len(sys.argv) > 2 else 180))
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(check(sys.argv[1]))
