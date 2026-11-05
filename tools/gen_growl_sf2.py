#!/usr/bin/env python3
"""Build the growl: a SoundFont with one bass preset for dubstep mode.

Usage: gen_growl_sf2.py <out.sf2>

ROCK is dubstep mode (main/keymap.c, 2026-11-05): its keys play PROGRAM, which
this file takes over from FluidR3 (loaded after it, tools/pc_synth.sh; the last
SoundFont with a program wins). The sound: a looped wave synthesized here (a saw
and a square an octave down, driven hard through tanh and partly wavefolded) behind SoundFont's resonant
low-pass filter, which the modulation LFO opens and shuts in 8th notes at the
dubstep beat's tempo: the wobble. The LFO restarts at each note on, so a note
played on the beat wobbles in time with it.
"""
import array
import math
import struct
import sys

PROGRAM = 38  # GM Synth Bass 1: nothing else on the toy uses it
RATE = 44100
ROOT = 33  # A1, the lowest key in EDM mode
CYCLES = 22  # whole cycles in the loop: 22 * 44100 / 55 = 17640 samples, the sub's 11 too
DRIVE = 6.0
FOLD = 0.45  # share of the wavefolded signal: metallic, snarling overtones (2026-11-05: "toy-like" at 0)
SUB = 1.0
GAIN = 0.9
BPM = 140  # main/rhythm.c's DUBSTEP
WOBBLES_PER_BEAT = 2  # 8th notes
CUTOFF_HZ = 800  # the filter's centre
SWEEP_CENTS = 3000  # the LFO moves it this far up and down: 2.5 octaves each way (200 Hz-4.5 kHz)
RESONANCE_CB = 160  # 16 dB peak at the cutoff: the vowel-like "yoy"
RELEASE_S = 0.08

GEN = {
    "modLfoToFilterFc": 10,
    "initialFilterFc": 8,
    "initialFilterQ": 9,
    "delayModLFO": 21,
    "freqModLFO": 22,
    "attackVolEnv": 34,
    "releaseVolEnv": 38,
    "instrument": 41,
    "sampleID": 53,
    "sampleModes": 54,
    "overridingRootKey": 58,
}


def chunk(tag, data):
    pad = b"\0" if len(data) % 2 else b""
    return tag + struct.pack("<I", len(data)) + data + pad


def riff_list(tag, *parts):
    return chunk(b"LIST", tag + b"".join(parts))


def name20(s):
    return s.encode("ascii")[:20].ljust(20, b"\0")


def cents(hz):
    """SoundFont absolute cents: 8.176 Hz * 2^(c/1200)."""
    return round(1200 * math.log2(hz / 8.176))


def wave_loop():
    period = RATE * CYCLES / 55.0
    n = round(period)
    ys = []
    for i in range(n):
        ph = (i * CYCLES / n) % 1.0  # the saw's phase, 0..1
        sub = 1.0 if (i * CYCLES / 2 / n) % 1.0 < 0.5 else -1.0  # square an octave down
        x = ((2 * ph - 1) + SUB * sub) / (1 + SUB)
        y = (1 - FOLD) * math.tanh(DRIVE * x) + FOLD * math.sin(2.5 * math.pi * x)
        ys.append(y)
    peak = max(abs(y) for y in ys)
    return array.array("h", (int(GAIN * 32767 * y / peak) for y in ys))


def gens_bytes(pairs):
    return b"".join(struct.pack("<Hh" if v < 0 else "<HH", g, v) for g, v in pairs)


def build():
    samples = wave_loop()
    n = len(samples)
    smpl = samples.tobytes() + bytes(2 * 46)
    shdr = struct.pack("<20sIIIIIBbHH", name20("Growl"), 0, n, 0, n, RATE, ROOT, 0, 0, 1)
    shdr += struct.pack("<20sIIIIIBbHH", name20("EOS"), 0, 0, 0, 0, 0, 0, 0, 0, 0)

    lfo_hz = BPM / 60 * WOBBLES_PER_BEAT
    igens = [
        (GEN["initialFilterFc"], cents(CUTOFF_HZ)),
        (GEN["initialFilterQ"], RESONANCE_CB),
        (GEN["freqModLFO"], cents(lfo_hz)),
        (GEN["delayModLFO"], -12000),  # no delay: 2^(-12000/1200) s, about 1 ms
        (GEN["modLfoToFilterFc"], SWEEP_CENTS),
        (GEN["attackVolEnv"], -7973),  # ~10 ms: no click
        (GEN["releaseVolEnv"], round(1200 * math.log2(RELEASE_S))),
        (GEN["overridingRootKey"], ROOT),
        (GEN["sampleModes"], 1),  # loop all the time
        (GEN["sampleID"], 0),  # must be last
    ]
    igen = gens_bytes(igens) + struct.pack("<HH", 0, 0)
    # Instrument modulators: velocity -> filter cutoff at 0, replacing the
    # default one (-2400 cents at velocity 0) that would shut the filter on the
    # soft EDM-mode keys (velocity 55).
    # And velocity -> volume at 0 too: the growl is as loud on the soft keys as at
    # full velocity (the default takes ~10 dB off at 55).
    imod = struct.pack("<HHhHH", 0x0102, GEN["initialFilterFc"], 0, 0, 0)
    imod += struct.pack("<HHhHH", 0x0502, 48, 0, 0, 0) + bytes(10)
    inst = struct.pack("<20sH", name20("Growl"), 0) + struct.pack("<20sH", name20("EOI"), 1)
    ibag = struct.pack("<HH", 0, 0) + struct.pack("<HH", len(igens), 2)

    phdr = struct.pack("<20sHHHIII", name20("Growl"), PROGRAM, 0, 0, 0, 0, 0)
    phdr += struct.pack("<20sHHHIII", name20("EOP"), 0, 0, 1, 0, 0, 0)
    pbag = struct.pack("<HH", 0, 0) + struct.pack("<HH", 1, 0)
    pgen = struct.pack("<HH", GEN["instrument"], 0) + struct.pack("<HH", 0, 0)

    info = riff_list(
        b"INFO",
        chunk(b"ifil", struct.pack("<HH", 2, 1)),
        chunk(b"isng", b"EMU8000\0"),
        chunk(b"INAM", b"Chicco Piano Growl\0\0"),  # even length: FluidSynth refuses odd ones
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
        chunk(b"imod", imod),
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
