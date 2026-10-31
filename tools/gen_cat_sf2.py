#!/usr/bin/env python3
"""Build the cat instrument: a SoundFont with one preset, bank 0 program 19.

Usage: gen_cat_sf2.py <meow.wav> <out.sf2>

FluidSynth takes a program from the last SoundFont loaded that has it, so with
this one loaded after FluidR3 (tools/pc_synth.sh), program 19 (SLOW ROCK,
main/keymap.c) is the cat and every other program stays as it was. Without it,
program 19 is FluidR3's church organ.
"""
import array
import math
import struct
import sys
import wave

PROGRAM = 19
# The meow's pitch, measured: 579 Hz energy-weighted median of its fundamental,
# MIDI 73.74. It glides between 544 and 602 Hz (10th-90th percentile) while it
# sounds, the way a meow does, so this is its centre, not a steady note.
ROOT_KEY = 74
ROOT_CENTS = 26  # the sample sits 26 cents under its root key: correct it up
GAIN = 0.9       # peak after normalising
FADE_IN_S = 0.004
FADE_OUT_S = 0.08
RELEASE_S = 0.2  # after the key comes up


def chunk(tag, data):
    pad = b"\0" if len(data) % 2 else b""
    return tag + struct.pack("<I", len(data)) + data + pad


def riff_list(tag, *parts):
    return chunk(b"LIST", tag + b"".join(parts))


def name20(s):
    return s.encode("ascii")[:20].ljust(20, b"\0")


def read_samples(path):
    with wave.open(path) as w:
        if w.getnchannels() != 1 or w.getsampwidth() != 2:
            sys.exit(f"{path}: needs mono 16 bit")
        rate = w.getframerate()
        data = array.array("h", w.readframes(w.getnframes()))
    if sys.byteorder != "little":
        data.byteswap()
    peak = max(abs(s) for s in data) or 1
    scale = GAIN * 32767 / peak
    n = len(data)
    fade_in, fade_out = int(FADE_IN_S * rate), int(FADE_OUT_S * rate)
    out = array.array("h", bytes(2 * n))
    for i, s in enumerate(data):
        env = min(1.0, (i + 1) / fade_in, (n - i) / fade_out)
        out[i] = int(s * scale * env)
    return out, rate


def build(samples, rate):
    # 46 zero samples after each sample: the format's rule.
    smpl = samples.tobytes() + bytes(2 * 46)
    n = len(samples)

    info = riff_list(
        b"INFO",
        chunk(b"ifil", struct.pack("<HH", 2, 1)),
        chunk(b"isng", b"EMU8000\0"),
        chunk(b"INAM", b"Chicco Piano Cat\0\0"),
    )
    sdta = riff_list(b"sdta", chunk(b"smpl", smpl))

    GEN_INSTRUMENT, GEN_RELEASE, GEN_SAMPLE_MODES, GEN_SAMPLE = 41, 38, 54, 53
    GEN_ROOT_OVERRIDE = 58
    release_tc = round(1200 * math.log2(RELEASE_S))
    igens = [
        (GEN_RELEASE, release_tc & 0xFFFF),
        (GEN_ROOT_OVERRIDE, ROOT_KEY),
        (GEN_SAMPLE_MODES, 0),  # one shot: the meow plays once, no loop
        (GEN_SAMPLE, 0),        # must be last
    ]
    pdta = riff_list(
        b"pdta",
        chunk(b"phdr", struct.pack("<20sHHHIII", name20("Cat"), PROGRAM, 0, 0, 0, 0, 0)
              + struct.pack("<20sHHHIII", name20("EOP"), 0, 0, 1, 0, 0, 0)),
        chunk(b"pbag", struct.pack("<HH", 0, 0) + struct.pack("<HH", 1, 0)),
        chunk(b"pmod", bytes(10)),
        chunk(b"pgen", struct.pack("<HH", GEN_INSTRUMENT, 0) + struct.pack("<HH", 0, 0)),
        chunk(b"inst", struct.pack("<20sH", name20("Cat"), 0) + struct.pack("<20sH", name20("EOI"), 1)),
        chunk(b"ibag", struct.pack("<HH", 0, 0) + struct.pack("<HH", len(igens), 0)),
        chunk(b"imod", bytes(10)),
        chunk(b"igen", b"".join(struct.pack("<HH", g, v) for g, v in igens) + struct.pack("<HH", 0, 0)),
        chunk(b"shdr",
              struct.pack("<20sIIIIIBbHH", name20("meow"), 0, n, 0, n, rate, ROOT_KEY,
                          ROOT_CENTS, 0, 1)
              + struct.pack("<20sIIIIIBbHH", name20("EOS"), 0, 0, 0, 0, 0, 0, 0, 0, 0)),
    )
    return chunk(b"RIFF", b"sfbk" + info + sdta + pdta)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    samples, rate = read_samples(sys.argv[1])
    with open(sys.argv[2], "wb") as f:
        f.write(build(samples, rate))


if __name__ == "__main__":
    main()
