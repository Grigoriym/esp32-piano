#!/usr/bin/env python3
"""Render docs/riffs.html's pieces with the toy's own sound, for the page's Listen buttons.

Same FluidSynth and SoundFont as tools/pc_synth.sh (gain 0.3, not its 1.0: at 1.0 #1 peaks ~9 dB over full scale), the EDM beat read from
main/rhythm.c, the keys' program and velocity and the drum kit (tools/gen_edm_kit_sf2.py) from
main/keymap.{c,h}: what the toy
plays in EDM mode. Each piece gives <slug>-all.mp3 and one file per layer, two
passes of the piece each, in docs/riffs-audio/ (gitignored). Run after changing a
piece, the EDM beat or the EDM velocities. Needs node, fluidsynth, ffmpeg.
"""
import json
import os
import pathlib
import re
import struct
import subprocess
import sys
import tempfile
import wave

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "docs/riffs-audio"
SF2 = os.environ.get("SF2", "/usr/share/sounds/sf2/FluidR3_GM.sf2")
KIT = ROOT / "build-host/edm_kit.sf2"
PAD_VELOCITY = 100  # main/keymap.h VELOCITY: the pads aren't softened in EDM mode
RATE = 44100
PASSES = 2


def slug(s):
    return re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-")


def pieces():
    page = (ROOT / "docs/riffs.html").read_text()
    src = page[page.index("const PIECES = ") :]
    src = src[: src.index("\n", src.index("const PADS = ")) + 1]  # the pieces and the pads
    js = src + "\nprocess.stdout.write(JSON.stringify({pieces: PIECES, pads: PADS}));"
    data = json.loads(subprocess.run(["node", "-e", js], capture_output=True, text=True, check=True).stdout)
    return data["pieces"], data["pads"]


def edm_beat():
    src = (ROOT / "main/rhythm.c").read_text()
    drums = {m[1]: int(m[2]) for m in re.finditer(r"#define (\w+)\s+(\d+)", src)}
    block = src[src.index('{"EDM"') :]
    block = block[: block.index("},\n    {") + 2]
    lanes = [(drums[m[1]], m[2]) for m in re.finditer(r'\{(\w+),\s*"([xX.]+)"\}', block)]
    vel, acc = map(int, re.search(r"(\d+), (\d+)\},", block).groups())
    bpm = int(re.search(r'\{"EDM", (\d+),', block)[1])
    return bpm, lanes, vel, acc


def keys_setup():
    h = (ROOT / "main/keymap.h").read_text()
    c = (ROOT / "main/keymap.c").read_text()
    vel = int(re.search(r"#define EDM_VELOCITY\s+(\d+)", h)[1])
    root = int(re.search(r"#define SCALE_ROOT\s+(\d+)", h)[1])
    program = int(re.search(r"(\d+), /\* DISCO", c)[1])
    kit = int(re.search(r"#define EDM_KIT\s+(\d+)", h)[1])
    return vel, root, program, kit


MINOR = [0, 2, 3, 5, 7, 8, 10]


def var_len(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.insert(0, (n & 0x7F) | 0x80)
        n >>= 7
    return bytes(out)


def midi_file(bpm, steps, events):
    """events: (step, order, bytes); one tick a 16th note."""
    track = bytearray()
    track += b"\x00\xff\x51\x03" + struct.pack(">I", 60_000_000 // bpm)[1:]
    last = 0
    for step, _, msg in sorted(events, key=lambda e: (e[0], e[1])):
        track += var_len(step - last) + msg
        last = step
    track += var_len(steps - last) + b"\xff\x2f\x00"
    return b"MThd" + struct.pack(">IHHH", 6, 0, 1, 4) + b"MTrk" + struct.pack(">I", len(track)) + track


def render(piece, layers, beat, keys, pads, path):
    bpm, lanes, dvel, dacc = beat
    kvel, root, program, kit = keys
    kvel_pad = PAD_VELOCITY
    bar_steps = piece["bars"] * 16
    total = bar_steps * PASSES
    ev = [(0, 0, bytes([0xC0, program])), (0, 0, bytes([0xC9, kit])), (0, 0, bytes([0xB0, 7, 127])),
          (0, 0, bytes([0xB9, 7, 127]))]
    for s in range(total):
        for note, hits in lanes:
            hit = hits[s % len(hits)]
            if hit != ".":
                ev.append((s, 2, bytes([0x99, note, dacc if hit == "X" else dvel])))
                ev.append((s + 1, 1, bytes([0x89, note, 0])))
    for p in range(PASSES):
        for layer in layers:
            for tok in layer.get("pads", "").split():  # chick pads: drums, as the toy sends them
                step, rest = tok.split(":")
                pad = pads[rest.split("/")[0]]["note"]
                on = int(step) + p * bar_steps
                ev.append((on, 2, bytes([0x99, pad, kvel_pad])))
                ev.append((on + 1, 1, bytes([0x89, pad, 0])))
            for tok in layer.get("notes", "").split():
                step, rest = tok.split(":")
                ks, length = rest.split("/")
                for k in map(int, ks.split("+")):
                    note = root + 12 * ((k - 1) // 7) + MINOR[(k - 1) % 7]
                    on = int(step) + p * bar_steps
                    ev.append((on, 2, bytes([0x90, note, kvel])))
                    ev.append((on + int(length), 1, bytes([0x80, note, 0])))
    ev = [e for e in ev if e[0] <= total]
    with tempfile.TemporaryDirectory() as d:
        mid, raw = pathlib.Path(d, "x.mid"), pathlib.Path(d, "x.wav")
        mid.write_bytes(midi_file(bpm, total, ev))
        subprocess.run(["fluidsynth", "-ni", "-g", "0.3", "-r", str(RATE), "-F", str(raw), SF2, str(KIT), str(mid)],
                       check=True, capture_output=True)
        # Exactly the piece's length, so the loop and the page's playhead line up.
        frames = round(total * 60 / bpm / 4 * RATE)
        with wave.open(str(raw)) as w:
            params, data = w.getparams(), w.readframes(frames)
        data += b"\0" * (frames * params.sampwidth * params.nchannels - len(data))
        cut = pathlib.Path(d, "cut.wav")
        with wave.open(str(cut), "wb") as w:
            w.setparams(params)
            w.writeframes(data)
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(cut), "-b:a", "160k", str(path)], check=True)


def main():
    OUT.mkdir(exist_ok=True)
    KIT.parent.mkdir(exist_ok=True)
    subprocess.run([sys.executable, str(ROOT / "tools/gen_edm_kit_sf2.py"), str(KIT)], check=True)
    beat, keys = edm_beat(), keys_setup()
    all_pieces, pads = pieces()
    for piece in all_pieces:
        base = slug(piece["name"])
        if piece["bpm"] != beat[0]:
            sys.exit(f"{piece['name']}: {piece['bpm']} bpm, the EDM beat is {beat[0]}")
        render(piece, piece["layers"], beat, keys, pads, OUT / f"{base}-all.mp3")
        for layer in piece["layers"]:
            render(piece, [layer], beat, keys, pads, OUT / f"{base}-{slug(layer['name'])}.mp3")
        print(f"{piece['name']}: {len(piece['layers']) + 1} files")


if __name__ == "__main__":
    main()
