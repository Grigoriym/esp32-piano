#!/usr/bin/env python3
"""Draft a docs/riffs.html piece from the loop playing on the toy: what it plays (a MIDI capture)
and how each layer was made (the toy's log). The save-song skill (.claude/skills/save-song) runs it.

    python3 tools/save_song.py <capture.mid> <toy-serial.log copy> [--insert]

Prints the piece as JavaScript; --insert also adds it at the end of PIECES in docs/riffs.html.
The names, tips, about and plan are left as TODO for a person (or Claude) to write.

From the log (main.c, mixer.c, looper.c, fx.c lines), replayed in order: which takes became the
loop's layers (RECORD, BLUE BIRD undo, RESET), the keys pressed in each take with the note each
played (so a note maps back to its key exactly, octave included), the chicks on during the take,
the sound (`MIDI  program N`, or the `LAYER N:` line since 2026-11-10), each layer's knob levels
(`layer N keeps ...`), the style button and the beat (`beat start NAME (BPM bpm)`).
From the capture: the pass starts (CC 119 on channel 16, main.c's loop marks) and each layer's
notes on its channel (3-6). Echo's repeats (they drift up to ~35 ms off the dotted 8th) and Octave's copies are left out of `notes` (the
page's clips add them back from `chicks`), so the notes are the keys pressed.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from midi_dump import events  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
STYLES = ["SLOW ROCK", "ROCK", "NEW AGE", "DISCO", "MARCH", "WALTZ", "SAMBA", "BLUES"]
LOOP_CH = 2  # main/keymap.h: layer 1's channel (0-based)
COLORS = ["bass", "chords", "hook", "arp"]
ECHO_STEPS = 3  # main/fx.h FX_ECHO_STEPS
ECHO_MAX_REPEATS = 8


def parse_log(path):
    st = {"program": 0, "octave": 0, "chicks": set(), "button": None, "beat": None, "bpm": None, "metro": False}
    layers, take, last_key, song = [], None, None, {}
    for line in Path(path).read_text(errors="replace").splitlines():
        m = re.match(r"DOWN  (.+?)\s+\(", line)
        if m:
            name = m[1].strip()
            if name in STYLES:
                st["button"] = name
            elif name == "RESET":
                layers, take = [], None
                st.update(program=0, octave=0, chicks=set())
            k = re.match(r"key (\d+)", name)
            last_key = int(k[1]) if k else None
            if k and take is not None:
                take.setdefault("octave", st["octave"])
            continue
        m = re.match(r"MIDI  (.*)", line)
        if not m:
            m = re.match(r"LAYER (\d+): program (\d+), octave (-?\d+), chicks (.*)", line)
            if m and int(m[1]) <= len(layers):
                lay = layers[int(m[1]) - 1]
                lay.update(program=int(m[2]), octave=int(m[3]))
                lay["chicks"] = sorted(set(lay["chicks"]) | (set(m[4].split()) - {"none"}))
            continue
        msg = m[1]
        if (n := re.match(r"note on (\d+)", msg)) and last_key is not None:
            if take is not None:
                take["keys"][int(n[1])] = last_key
            last_key = None
        elif n := re.match(r"program (\d+)$", msg):
            st["program"] = int(n[1])
        elif n := re.match(r"octave ([+-]?\d+)", msg):
            st["octave"] = int(n[1])
        elif n := re.match(r"chick: (\w+) (on|off)", msg):
            (st["chicks"].add if n[2] == "on" else st["chicks"].discard)(n[1])
            if n[2] == "on" and take is not None:
                take["chicks"].add(n[1])
        elif msg.startswith("chicks: all off"):
            st["chicks"] = set()
        elif n := re.match(r"beat start (\w+) \((\d+) bpm\)", msg):
            st["beat"], st["bpm"] = n[1], int(n[2])
        elif msg == "metronome on":
            st["metro"] = True
        elif msg == "metronome off":
            st["metro"] = False
        elif msg in ("loop: recording from the first note", "loop: adding a layer"):
            if msg.startswith("loop: recording"):
                layers = []
            take = {"keys": {}, "chicks": set(st["chicks"])}
        elif msg.startswith("loop plays (") or msg.startswith("loop: layer added"):
            if take is not None and (msg.startswith("loop: layer added") or not layers):
                if not layers:  # the song's setup: as the first layer was recorded, not at the log's end
                    song = {k: st[k] for k in ("button", "beat", "bpm", "metro")}
                layers.append({"keys": take["keys"], "chicks": sorted(take["chicks"]), "program": st["program"],
                               "octave": take.get("octave", st["octave"]), "fx": {}})
                take = None
        elif msg in ("loop: new layer dropped", "loop: nothing added", "loop: recording dropped",
                     "loop: nothing recorded"):
            take = None
        elif msg == "loop: last layer dropped":
            layers = layers[:-1]
        elif msg == "loop: erased":
            layers, take = [], None
        elif n := re.match(r"layer (\d+) keeps (.*)", msg):
            if int(n[1]) <= len(layers):
                layers[int(n[1]) - 1]["fx"] = {k: int(v) for k, v in re.findall(r"(\w+) (\d+)", n[2])}
    return {**st, **song}, layers


def note_key(note, octave, edm):
    """main/keymap.c keymap_key_note, backwards (key 1-32), for a note no logged press names."""
    for key in range(32):
        if edm:
            minor = [0, 2, 3, 5, 7, 8, 10]
            n = 33 + 12 * (octave + key // 7) + minor[key % 7]
        else:
            n = 53 + 12 * octave + key
        if n == note:
            return key + 1
    return None


def capture(path):
    on, notes, marks = {}, [], []
    for t, s, a in events(path):
        ch, ty = s & 0x0F, s & 0xF0
        if ty == 0xB0 and ch == 15 and a[0] == 119:
            marks.append(t)
        elif ty == 0x90 and a[1]:
            on[(ch, a[0])] = (t, a[1])
        elif ty in (0x80, 0x90) and (ch, a[0]) in on:
            t0, v = on.pop((ch, a[0]))
            notes.append((t0, ch, a[0], v, t - t0))
    return notes, marks


def pressed(notes, chicks, six):
    """The notes the keys played: without Echo's repeats (the same note a dotted 8th apart, quieter)
    and Octave's copies (an octave up at the same time and velocity)."""
    out = []
    for t, n, v, ln in notes:
        if "Octave" in chicks and any(abs(t2 - t) < 15 and n2 == n - 12 and v2 == v for t2, n2, v2, _ in notes):
            continue
        if "Echo" in chicks and any(
                n2 == n and v2 > v and any(abs(t - t2 - k * ECHO_STEPS * six) < six / 2 for k in range(1, ECHO_MAX_REPEATS + 1))
                for t2, n2, v2, _ in notes):
            continue
        out.append((t, n, v, ln))
    return out


def next_numbers():
    page = (ROOT / "docs/riffs.html").read_text()
    piece = max(int(x) for x in re.findall(r"name: '#(\d+) ", page))
    song = max([int(x) for x in re.findall(r"name: '#\d+ Song #(\d+)'", page)] or [0])
    return piece + 1, song + 1


def js(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return str(v)
    if isinstance(v, list):
        return "[" + ", ".join(js(x) for x in v) + "]"
    if isinstance(v, dict):
        return "{ " + ", ".join(f"{k}: {js(x)}" for k, x in v.items()) + " }"
    return "'" + str(v).replace("\\", "\\\\").replace("'", "\\'") + "'"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 2:
        sys.exit(__doc__)
    st, layers = parse_log(args[1])
    notes, marks = capture(args[0])
    if not layers:
        sys.exit("no loop in the log: was it copied before a service restart truncated it?")
    if len(marks) < 2:
        sys.exit("fewer than two pass marks (CC 119, channel 16) in the capture: capture a few passes")
    passes = [b - a for a, b in zip(marks, marks[1:])]
    pass_ms = sorted(passes)[len(passes) // 2]
    bpm = st["bpm"]
    if not bpm:  # no beat: the metronome's beat-1 clicks (channel 9, note 84) are a bar apart
        ones = [t for t, ch, n, *_ in notes if ch == 8 and n == 84]
        bpm = round(4 * 60000 / (sorted(b - a for a, b in zip(ones, ones[1:]))[0])) if len(ones) > 1 else 120
    six = 60000 / bpm / 4
    bars = max(1, round(pass_ms / (16 * six)))
    steps = bars * 16
    start = marks[0]
    edm = st["button"] in ("SLOW ROCK", "ROCK", "DISCO", "MARCH")
    out_layers = []
    for i, lay in enumerate(layers):
        mine = [(t, n, v, ln) for t, ch, n, v, ln in notes if ch == LOOP_CH + i]
        toks = {}
        for t, n, v, ln in pressed(mine, lay["chicks"], six):
            if not start - 30 <= t < start + pass_ms - 30:
                continue
            step = max(0, round((t - start) / six)) % steps
            key = lay["keys"].get(n) or note_key(n, lay["octave"], edm)
            toks.setdefault((step, max(1, round(ln / six))), []).append(key if key else f"?{n}")
        notes_s = " ".join(f"{s}:{'+'.join(str(k) for k in sorted(ks, key=str))}/{ln}" for (s, ln), ks in sorted(toks.items()))
        reps = max(1, 4 // bars) if 4 % bars == 0 else 1  # the page shows 4 bars: a shorter loop repeated
        if reps > 1:
            notes_s = " ".join(
                f"{int(tok.split(':')[0]) + r * steps}:{tok.split(':')[1]}" for r in range(reps) for tok in notes_s.split())
        fx = dict(lay["fx"])
        out_layers.append({
            "name": f"Layer {i + 1}", "color": COLORS[i % len(COLORS)], "hand": "TODO",
            **({"octave": lay["octave"]} if lay["octave"] else {}),
            "program": lay["program"], "chicks": lay["chicks"], "fx": fx,
            "tip": "TODO", "notes": notes_s,
        })
    n_piece, n_song = next_numbers()
    piece = {
        "name": f"#{n_piece} Song #{n_song}", "compact": True, "button": st["button"] or "TODO", "beat": st["beat"] or "TODO",
        "metronome": st["metro"], "about": "TODO", "bpm": bpm, "bars": bars * (4 // bars if 4 % bars == 0 else 1),
        "chords": ["Am"] * 4, "layers": out_layers, "plan": [["TODO", "TODO"]],
    }
    lines = ["  {"]
    for k, v in piece.items():
        if k == "layers":
            lines.append("    layers: [")
            for lay in v:
                lines.append("      {")
                lines += [f"        {kk}: {js(vv)}," for kk, vv in lay.items()]
                lines.append("      },")
            lines.append("    ],")
        else:
            lines.append(f"    {k}: {js(v)},")
    lines.append("  },")
    text = "\n".join(lines)
    print(text)
    print(f"\n// {len(layers)} layers, {bars} bar(s) a pass ({pass_ms} ms at {bpm} bpm), {len(marks) - 1} passes captured",
          file=sys.stderr)
    if "--insert" in sys.argv:
        page_path = ROOT / "docs/riffs.html"
        page = page_path.read_text()
        end = page.index("\n];", page.index("const PIECES = "))
        page_path.write_text(page[:end] + "\n" + text + page[end:])
        print(f"// inserted into {page_path.relative_to(ROOT)}", file=sys.stderr)


if __name__ == "__main__":
    main()
