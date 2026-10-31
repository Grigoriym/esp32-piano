#!/usr/bin/env python3
"""Build the cats: a SoundFont with one preset per sound in CATS.

Usage: gen_cat_sf2.py <out.sf2>

FluidSynth takes a program from the last SoundFont loaded that has it, so with
this one loaded after FluidR3 (tools/pc_synth.sh), these programs (the style
buttons, main/keymap.c) are cats and every other program stays as it was.

To change a cat: put a mono 16-bit wav of one meow in tools/sounds/, starting
where the sound starts, measure its pitch, and edit its line here.
"""
import array
import math
import pathlib
import struct
import sys
import wave

SOUNDS = pathlib.Path(__file__).resolve().parent / "sounds"

# A meow glides, so the pitch is its centre: the energy-weighted median of its
# fundamental, as MIDI note root + (-cents / 100). cents corrects it up to root.
CATS = [
    # file, program, name, root, cents
    ("meow.wav", 19, "Cat", 74, 26),         # ROCK: 579 Hz, 544-602 Hz
    ("siamese.wav", 88, "Siamese", 59, 33),  # NEW AGE: 242 Hz, 214-255 Hz
]
GAIN = 0.9  # peak after normalising
FADE_IN_S = 0.004
FADE_OUT_S = 0.08
RELEASE_S = 0.2  # after the key comes up

GEN_RELEASE, GEN_INSTRUMENT, GEN_SAMPLE, GEN_SAMPLE_MODES, GEN_ROOT_OVERRIDE = 38, 41, 53, 54, 58


def chunk(tag, data):
    pad = b"\0" if len(data) % 2 else b""
    return tag + struct.pack("<I", len(data)) + data + pad


def riff_list(tag, *parts):
    return chunk(b"LIST", tag + b"".join(parts))


def name20(s):
    return s.encode("ascii")[:20].ljust(20, b"\0")


def read_samples(path):
    with wave.open(str(path)) as w:
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


def build():
    smpl, shdr, phdr, inst, pbag, ibag, pgen, igen = b"", b"", b"", b"", b"", b"", b"", b""
    release_tc = round(1200 * math.log2(RELEASE_S))
    igen_count = 0
    # Presets in program order, as SoundFont files usually keep them.
    for i, (file, program, name, root, cents) in enumerate(sorted(CATS, key=lambda c: c[1])):
        samples, rate = read_samples(SOUNDS / file)
        start = len(smpl) // 2
        end = start + len(samples)
        smpl += samples.tobytes() + bytes(2 * 46)  # 46 zero samples after each: the format's rule
        shdr += struct.pack("<20sIIIIIBbHH", name20(name), start, end, start, end, rate, root, cents, 0, 1)
        phdr += struct.pack("<20sHHHIII", name20(name), program, 0, i, 0, 0, 0)
        pbag += struct.pack("<HH", i, 0)
        pgen += struct.pack("<HH", GEN_INSTRUMENT, i)
        inst += struct.pack("<20sH", name20(name), i)
        ibag += struct.pack("<HH", igen_count, 0)
        gens = [
            (GEN_RELEASE, release_tc & 0xFFFF),
            (GEN_ROOT_OVERRIDE, root),
            (GEN_SAMPLE_MODES, 0),  # one shot: the meow plays once, no loop
            (GEN_SAMPLE, i),        # must be last
        ]
        igen += b"".join(struct.pack("<HH", g, v) for g, v in gens)
        igen_count += len(gens)
    n = len(CATS)
    phdr += struct.pack("<20sHHHIII", name20("EOP"), 0, 0, n, 0, 0, 0)
    pbag += struct.pack("<HH", n, 0)
    pgen += struct.pack("<HH", 0, 0)
    inst += struct.pack("<20sH", name20("EOI"), n)
    ibag += struct.pack("<HH", igen_count, 0)
    igen += struct.pack("<HH", 0, 0)
    shdr += struct.pack("<20sIIIIIBbHH", name20("EOS"), 0, 0, 0, 0, 0, 0, 0, 0, 0)

    info = riff_list(
        b"INFO",
        chunk(b"ifil", struct.pack("<HH", 2, 1)),
        chunk(b"isng", b"EMU8000\0"),
        chunk(b"INAM", b"Chicco Piano Cat\0\0"),
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
