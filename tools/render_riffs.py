#!/usr/bin/env python3
"""Render docs/riffs.html's pieces with the toy's own sound, for the page's Listen buttons.

Same FluidSynth and SoundFont as tools/pc_synth.sh (gain 0.3, not its 1.0: at 1.0 #1 peaks ~9 dB over full scale), the song's beat read from
main/rhythm.c, the keys' program and velocity and the drum kit (tools/gen_edm_kit_sf2.py) from
main/keymap.{c,h}: what the toy
plays in EDM mode. Each piece gives <slug>-all.mp3 and one file per layer, two
passes of the piece each, in docs/riffs-audio/ (gitignored). Run after changing a
piece, a song's beat or the EDM velocities. Needs node, fluidsynth, ffmpeg.

A layer with `fx` (the toy's pot levels it was recorded with, 0-127: keys, filter, reverb,
pluck, glide, release, tremolo; `echo: true` for the Echo chick) plays on its own channel
with what main/mixer.c's mixer_send_layer_fx sends for those levels, and Echo's repeats as
main/fx.c makes them; FluidSynth gets pc_synth.sh's reverb room and tools/fluidsynth.cmd
(glide on every note), as live. A layer without `fx` plays as before: no effects.
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
GROWL = ROOT / "build-host/growl.sf2"  # ROCK's growl bass (tools/gen_growl_sf2.py)
PAD_VELOCITY = 100  # main/keymap.h VELOCITY: the pads aren't softened in EDM mode
RATE = 44100
PASSES = 2


def slug(s):
    return re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-")


def pieces():
    page = (ROOT / "docs/riffs.html").read_text()
    src = page[page.index("const PIECES = ") :]
    src = src[: src.index("};", src.index("const PADS = ")) + 3]  # the pieces and the pads
    js = src + "\nprocess.stdout.write(JSON.stringify({pieces: PIECES, pads: PADS}));"
    data = json.loads(subprocess.run(["node", "-e", js], capture_output=True, text=True, check=True).stdout)
    return data["pieces"], data["pads"]


def beat_named(name):
    """A beat of main/rhythm.c by its name (a piece's `beat`)."""
    src = (ROOT / "main/rhythm.c").read_text()
    drums = {m[1]: int(m[2]) for m in re.finditer(r"#define (\w+)\s+(\d+)", src)}
    block = src[src.index('{"' + name + '"') :]
    block = block[: block.index("},\n    {") + 2]
    lanes = [(drums[m[1]], m[2]) for m in re.finditer(r'\{(\w+),\s*"([xX.]+)"\}', block)]
    vel, acc = map(int, re.search(r"(\d+), (\d+)\},", block).groups())
    bpm = int(re.search(r'\{"' + name + r'", (\d+),', block)[1])
    return bpm, lanes, vel, acc


def keys_setup(button):
    """The keys' velocity, scale root, program (the piece's style button) and kit."""
    h = (ROOT / "main/keymap.h").read_text()
    c = (ROOT / "main/keymap.c").read_text()
    vel = int(re.search(r"#define EDM_VELOCITY\s+(\d+)", h)[1])
    root = int(re.search(r"#define SCALE_ROOT\s+(\d+)", h)[1])
    program = int(re.search(r"(\d+), /\* " + re.escape(button) + ":", c)[1])
    kit = int(re.search(r"#define EDM_KIT\s+(\d+)", h)[1])
    return vel, root, program, kit


MINOR = [0, 2, 3, 5, 7, 8, 10]


def cdiv(a, b):
    """C's integer division (truncates toward zero), as the firmware computes."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def consts(path, names):
    src = (ROOT / path).read_text()
    return {n: int(re.search(r"#define " + n + r"\s*\\?\s*\(?(-?\d+)", src)[1]) for n in names}


MIX = consts("main/mixer.h", ["MIXER_FILTER_CENTS", "MIXER_REVERB_SEND", "MIXER_PLUCK_CENTS", "MIXER_RELEASE_TC",
                              "MIXER_TREMOLO_STEPS", "MIXER_GLIDE_MS"])
FX = consts("main/fx.h", ["FX_ECHO_MAX", "FX_ECHO_LEVEL", "FX_ECHO_STEPS"])
PLUCK_SHAPE = [-6000, -6000, -6000, 4607, 1000]  # main/mixer.c send_shape: generators 25-29


def nrpn(ch, gen, offset):
    v = 8192 + offset
    return [bytes([0xB0 | ch, 99, 120]), bytes([0xB0 | ch, 98, gen]), bytes([0xB0 | ch, 38, v & 0x7F]),
            bytes([0xB0 | ch, 6, v >> 7])]


def layer_fx(ch, fx):
    """What main/mixer.c's mixer_send_layer_fx sends for a layer's levels."""
    lv = lambda k, d: fx.get(k, d)
    filt, pluck = lv("filter", 127), lv("pluck", 0)
    pluck_steps = cdiv(cdiv(pluck * MIX["MIXER_PLUCK_CENTS"], 127), 2)
    cutoff = cdiv(cdiv((127 - filt) * MIX["MIXER_FILTER_CENTS"], 127), 2) - pluck_steps
    glide_ms = cdiv(lv("glide", 0) * MIX["MIXER_GLIDE_MS"], 127)
    out = [bytes([0xB0 | ch, 11, lv("keys", 127)])]
    for i, off in enumerate(PLUCK_SHAPE):
        out += nrpn(ch, 25 + i, off if pluck else 0)
    out += nrpn(ch, 8, cutoff) + nrpn(ch, 11, pluck_steps)
    out += nrpn(ch, 38, cdiv((2 * lv("release", 64) - 127) * MIX["MIXER_RELEASE_TC"], 127))
    out += nrpn(ch, 13, cdiv(lv("tremolo", 0) * MIX["MIXER_TREMOLO_STEPS"], 127))
    out += [bytes([0xB0 | ch, 5, glide_ms >> 7]), bytes([0xB0 | ch, 37, glide_ms & 0x7F]),
            bytes([0xB0 | ch, 65, 127 if lv("glide", 0) else 0])]
    out += nrpn(ch, 16, cdiv((2 * lv("reverb", 0) - 127) * MIX["MIXER_REVERB_SEND"], 127))
    return out


def echo_repeats(vel):
    """Echo's repeats of a note at vel (main/fx.c, the Echo level fixed at FX_ECHO_LEVEL): (k, velocity)."""
    level = FX["FX_ECHO_LEVEL"]
    n = 1 + cdiv(level * (FX["FX_ECHO_MAX"] - 1), 127)
    decay = 40 + cdiv(level * 50, 127)
    out = []
    for k in range(1, n + 1):
        vel = vel * decay // 100
        out.append((k, vel or 1))
    return out


def synth_options():
    """pc_synth.sh's reverb room (-o synth.reverb.*) and its -f command file, so the clips sound as live."""
    src = (ROOT / "tools/pc_synth.sh").read_text()
    args = []
    for opt in re.findall(r"-o (synth\.reverb\.[\w-]+=[\d.]+)", src):
        args += ["-o", opt]
    return args + ["-f", str(ROOT / "tools/fluidsynth.cmd")]


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
    for i, layer in enumerate(layers):
        if "fx" in layer:  # its own channel (not 9, the drums), with what the toy sends it
            ch = i + 1 if i + 1 < 9 else i + 2
            layer["_ch"] = ch
            ev.append((0, 0, bytes([0xC0 | ch, program])))
            ev += [(0, 0, m) for m in layer_fx(ch, layer["fx"])]
    for p in range(PASSES):
        for layer in layers:
            ch = layer.get("_ch", 0)
            fx = layer.get("fx", {})
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
                    note = root + 12 * ((k - 1) // 7 + layer.get("octave", 0)) + MINOR[(k - 1) % 7]
                    on = int(step) + p * bar_steps
                    ev.append((on, 2, bytes([0x90 | ch, note, kvel])))
                    ev.append((on + int(length), 1, bytes([0x80 | ch, note, 0])))
                    if fx.get("echo"):
                        for k, v in echo_repeats(kvel):
                            at = on + k * FX["FX_ECHO_STEPS"]
                            ev.append((at, 2, bytes([0x90 | ch, note, v])))
                            ev.append((at + int(length), 1, bytes([0x80 | ch, note, 0])))
    ev = [e for e in ev if e[0] <= total]
    with tempfile.TemporaryDirectory() as d:
        mid, raw = pathlib.Path(d, "x.mid"), pathlib.Path(d, "x.wav")
        mid.write_bytes(midi_file(bpm, total, ev))
        subprocess.run(["fluidsynth", "-ni", "-g", "0.3", "-r", str(RATE), *synth_options(), "-F", str(raw), SF2, str(KIT), str(GROWL), str(mid)],
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
    subprocess.run([sys.executable, str(ROOT / "tools/gen_growl_sf2.py"), str(GROWL)], check=True)
    all_pieces, pads = pieces()
    for piece in all_pieces:
        base = slug(piece["name"])
        beat = beat_named(piece["beat"])
        if piece["bpm"] != beat[0]:
            sys.exit(f"{piece['name']}: {piece['bpm']} bpm, its beat {piece['beat']} is {beat[0]}")
        keys = keys_setup(piece["button"])
        render(piece, piece["layers"], beat, keys, pads, OUT / f"{base}-all.mp3")
        for layer in piece["layers"]:
            render(piece, [layer], beat, keys, pads, OUT / f"{base}-{slug(layer['name'])}.mp3")
        print(f"{piece['name']}: {len(piece['layers']) + 1} files")


if __name__ == "__main__":
    main()
