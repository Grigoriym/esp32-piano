#!/usr/bin/env python3
"""Build the EDM drum kit: a SoundFont with one drum preset, bank 128 program 30.

Usage: gen_edm_kit_sf2.py <out.sf2>

The drums are synthesized here, the same recipes as docs/riffs.html's page synth
(kick, clap, hat, open hat: the user liked that beat better than FluidR3's,
2026-11-03), plus sounds in the same style for the other notes the chick pads and
animals play (main/keymap.c DRUM_NOTE). Loaded after FluidR3 (tools/pc_synth.sh);
FluidR3 has no drum program 30, so every other kit stays as it was. EDM mode
(main/keymap.c) switches the drum channel to it. A note this kit lacks is silent.
"""
import array
import math
import random
import struct
import sys

PROGRAM = 30
RATE = 44100
GAIN = 0.9  # the loudest sound's peak; the others keep their level against it

GEN_KEY_RANGE, GEN_RELEASE, GEN_INSTRUMENT, GEN_SAMPLE, GEN_SAMPLE_MODES, GEN_ROOT_OVERRIDE = 43, 38, 41, 53, 54, 58

rand = random.Random(1)  # the same noise every build
NOISE = [rand.uniform(-1, 1) for _ in range(RATE * 2)]


def biquad(x, kind, f0, q=0.7071):
    """RBJ biquad, as WebAudio's BiquadFilterNode (its default Q for both kinds)."""
    w = 2 * math.pi * f0 / RATE
    alpha = math.sin(w) / (2 * q)
    c = math.cos(w)
    if kind == "highpass":
        b = [(1 + c) / 2, -(1 + c), (1 + c) / 2]
    else:  # bandpass, constant 0 dB peak; WebAudio's bandpass default Q is 1
        b = [alpha, 0, -alpha]
    a = [1 + alpha, -2 * c, 1 - alpha]
    y, x1, x2, y1, y2 = [], 0.0, 0.0, 0.0, 0.0
    for s in x:
        o = (b[0] * s + b[1] * x1 + b[2] * x2 - a[1] * y1 - a[2] * y2) / a[0]
        x2, x1, y2, y1 = x1, s, y1, o
        y.append(o)
    return y


def exp_env(n, start, dur):
    """WebAudio's exponentialRampToValueAtTime from start to 0.001 over dur."""
    return [start * (0.001 / start) ** min(1.0, i / (dur * RATE)) for i in range(n)]


def noise(dur, kind, f0, vol, q=None):
    n = int(dur * RATE)
    f = biquad(NOISE[:n], kind, f0, q if q else (1.0 if kind == "bandpass" else 0.7071))
    return [s * e for s, e in zip(f, exp_env(n, vol, dur))]


def sweep(f_start, f_end, sweep_s, dur, vol, wave=math.sin):
    n = int(dur * RATE)
    out, phase = [], 0.0
    env = exp_env(n, vol, dur)
    for i in range(n):
        t = min(i / RATE, sweep_s)
        f = f_start * (f_end / f_start) ** (t / sweep_s) if sweep_s else f_start
        phase += 2 * math.pi * f / RATE
        out.append(wave(phase) * env[i])
    return out


def mix(*parts):
    n = max(len(p) for p in parts)
    return [sum(p[i] for p in parts if i < len(p)) for i in range(n)]


def square(p):
    return 1.0 if math.sin(p) >= 0 else -1.0


# General MIDI drum note: (name, samples). The first four are the page's drums().
SOUNDS = {
    36: ("Kick", sweep(150, 45, 0.12, 0.3, 0.9)),
    39: ("Clap", noise(0.15, "bandpass", 1500, 0.5)),
    42: ("Hat", noise(0.04, "highpass", 8000, 0.15)),
    46: ("Open hat", noise(0.2, "highpass", 7000, 0.18)),
    38: ("Snare", mix(noise(0.18, "bandpass", 1800, 0.4), sweep(190, 160, 0.05, 0.12, 0.3))),
    45: ("Low tom", sweep(130, 80, 0.2, 0.35, 0.7)),
    48: ("High tom", sweep(210, 130, 0.2, 0.3, 0.6)),
    49: ("Crash", noise(1.2, "highpass", 5000, 0.3)),
    51: ("Ride", mix(noise(0.6, "highpass", 6000, 0.12), sweep(3200, 3200, 0, 0.6, 0.04))),
    54: ("Tambourine", noise(0.12, "highpass", 9000, 0.25)),
    56: ("Cowbell", biquad(mix(sweep(540, 540, 0, 0.3, 0.25, square), sweep(800, 800, 0, 0.3, 0.25, square)),
                         "bandpass", 700, 2)),
    75: ("Claves", sweep(2500, 2500, 0, 0.06, 0.5)),
}


def chunk(tag, data):
    pad = b"\0" if len(data) % 2 else b""
    return tag + struct.pack("<I", len(data)) + data + pad


def riff_list(tag, *parts):
    return chunk(b"LIST", tag + b"".join(parts))


def name20(s):
    return s.encode("ascii")[:20].ljust(20, b"\0")


def build():
    peak = max(abs(s) for _, data in SOUNDS.values() for s in data)
    scale = GAIN * 32767 / peak
    smpl, shdr, ibag, igen = b"", b"", b"", b""
    release_tc = round(1200 * math.log2(2.0))  # longer than any sound: they play out after note off
    igen_count = 0
    for i, (note, (name, data)) in enumerate(sorted(SOUNDS.items())):
        pcm = array.array("h", (max(-32767, min(32767, int(s * scale))) for s in data))
        if sys.byteorder != "little":
            pcm.byteswap()
        start = len(smpl) // 2
        end = start + len(pcm)
        smpl += pcm.tobytes() + bytes(2 * 46)  # 46 zero samples after each: the format's rule
        shdr += struct.pack("<20sIIIIIBbHH", name20(name), start, end, start, end, RATE, note, 0, 0, 1)
        ibag += struct.pack("<HH", igen_count, 0)
        gens = [
            (GEN_KEY_RANGE, note | note << 8),  # must be first
            (GEN_RELEASE, release_tc & 0xFFFF),
            (GEN_ROOT_OVERRIDE, note),  # played at its own pitch
            (GEN_SAMPLE_MODES, 0),  # one shot
            (GEN_SAMPLE, i),  # must be last
        ]
        igen += b"".join(struct.pack("<HH", g, v) for g, v in gens)
        igen_count += len(gens)
    n = len(SOUNDS)
    phdr = struct.pack("<20sHHHIII", name20("EDM kit"), PROGRAM, 128, 0, 0, 0, 0)
    phdr += struct.pack("<20sHHHIII", name20("EOP"), 0, 0, 1, 0, 0, 0)
    pbag = struct.pack("<HH", 0, 0) + struct.pack("<HH", 1, 0)
    pgen = struct.pack("<HH", GEN_INSTRUMENT, 0) + struct.pack("<HH", 0, 0)
    inst = struct.pack("<20sH", name20("EDM kit"), 0) + struct.pack("<20sH", name20("EOI"), n)
    ibag += struct.pack("<HH", igen_count, 0)
    igen += struct.pack("<HH", 0, 0)
    shdr += struct.pack("<20sIIIIIBbHH", name20("EOS"), 0, 0, 0, 0, 0, 0, 0, 0, 0)

    info = riff_list(
        b"INFO",
        chunk(b"ifil", struct.pack("<HH", 2, 1)),
        chunk(b"isng", b"EMU8000\0"),
        chunk(b"INAM", b"Chicco EDM kit\0\0"),  # even length: FluidSynth refuses odd ones
    )
    sdta = riff_list(b"sdta", chunk(b"smpl", smpl))
    pdta = riff_list(
        b"pdta",
        chunk(b"phdr", phdr),
        chunk(b"pbag", pbag),
        chunk(b"pmod", bytes(10)),
        chunk(b"pgen", pgen),
        chunk(b"inst", inst),
        chunk(b"ibag", ibag),
        chunk(b"imod", bytes(10)),
        chunk(b"igen", igen),
        chunk(b"shdr", shdr),
    )
    return chunk(b"RIFF", b"sfbk" + info + sdta + pdta)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    with open(sys.argv[1], "wb") as f:
        f.write(build())


if __name__ == "__main__":
    main()
