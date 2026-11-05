#!/usr/bin/env python3
"""Build the growl: a SoundFont with one bass preset for dubstep mode.

Usage: gen_growl_sf2.py <out.sf2>

ROCK is dubstep mode (main/keymap.c, 2026-11-05): its keys play PROGRAM, which
this file takes over from FluidR3 (loaded after it, tools/pc_synth.sh; the last
SoundFont with a program wins).

The wobble is rendered into the sound: one looped sample per note of the keys
(A natural minor from A1, main/keymap.c), each exactly one bar of the dubstep
beat (main/rhythm.c DUBSTEP, BPM below) long, so it wobbles at the beat's tempo
on every key, starting at the note on. A sample per note, not one transposed,
because a transposed sample wobbles faster on higher keys.

Each note: a saw and an FM voice (the FM index follows the wobble: the growl),
through two band-pass formant filters that move from an "oo" to an "ah" and back
on each 8th note (the vowel, "wow-wow"), then driven and high-passed (the
grit, without mud), plus a clean sine sub for the weight. 2026-11-05: the first
version (one wave behind SoundFont's low-pass) sounded toy-like, then muddy.
"""
import array
import math
import struct
import sys

PROGRAM = 38  # GM Synth Bass 1: nothing else on the toy uses it
RATE = 22050  # plenty for a bass, and the build stays quick in plain Python
BPM = 140
WOBBLES_PER_BAR = 8  # 8th notes
ROOT = 33  # A1: key 1 in EDM mode
MINOR = [0, 2, 3, 5, 7, 8, 10]
KEYS = 21  # keys 1-21 (A1-A4) get their own sample; higher keys transpose the top one
PRE_S = 0.2  # rendered before the loop and dropped, so the filters have settled
GAIN = 0.9

VOWEL_CLOSED = (350, 700)  # "oo": formants F1, F2 in Hz
VOWEL_OPEN = (800, 1300)  # "ah"
FORMANT_Q = 5.0
DRIVE = 3.0
HIGHPASS_HZ = 150  # the growl part above this; the sub below
SUB = 0.8  # the sine's share against the growl's
FM_INDEX = (0.5, 3.0)  # closed, open

GEN = {
    "keyRange": 43,
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


class Biquad:
    """RBJ biquad; set() can be called while running (the state is kept)."""

    def __init__(self):
        self.x1 = self.x2 = self.y1 = self.y2 = 0.0

    def set(self, kind, f0, q):
        w = 2 * math.pi * f0 / RATE
        alpha, c = math.sin(w) / (2 * q), math.cos(w)
        if kind == "bandpass":  # 0 dB peak
            b = (alpha, 0.0, -alpha)
        else:  # highpass
            b = ((1 + c) / 2, -(1 + c), (1 + c) / 2)
        a0 = 1 + alpha
        self.b0, self.b1, self.b2 = (v / a0 for v in b)
        self.a1, self.a2 = -2 * c / a0, (1 - alpha) / a0

    def run(self, x):
        y = self.b0 * x + self.b1 * self.x1 + self.b2 * self.x2 - self.a1 * self.y1 - self.a2 * self.y2
        self.x2, self.x1, self.y2, self.y1 = self.x1, x, self.y1, y
        return y


def note_hz(n):
    return 440.0 * 2 ** ((n - 69) / 12)


def render(note):
    """One bar of the growl at note, looped seamlessly: whole cycles, filters settled."""
    f = note_hz(note)
    bar_s = 4 * 60 / BPM
    cycles = round(bar_s * f)
    length = round(cycles * RATE / f)
    pre = int(PRE_S * RATE)
    bp1, bp2, hp = Biquad(), Biquad(), Biquad()
    hp.set("highpass", HIGHPASS_HZ, 0.7071)
    out = []
    index = 0.0
    for i in range(-pre, length):
        j = i % length  # the loop's own position, so the pre-roll is the loop's end
        if j % 32 == 0 or i == -pre:
            # The wobble: 0 closed .. 1 open, a raised cosine per 8th, closed at the note on.
            lfo = 0.5 - 0.5 * math.cos(2 * math.pi * WOBBLES_PER_BAR * j / length)
            f1 = VOWEL_CLOSED[0] + lfo * (VOWEL_OPEN[0] - VOWEL_CLOSED[0])
            f2 = VOWEL_CLOSED[1] + lfo * (VOWEL_OPEN[1] - VOWEL_CLOSED[1])
            bp1.set("bandpass", f1, FORMANT_Q)
            bp2.set("bandpass", f2, FORMANT_Q)
            index = FM_INDEX[0] + lfo * (FM_INDEX[1] - FM_INDEX[0])
        ph = (j * cycles / length) % 1.0
        saw = 2 * ph - 1
        fm = math.sin(2 * math.pi * ph + index * math.sin(2 * math.pi * ph))
        src = 0.5 * saw + 0.5 * fm
        voice = 2.0 * bp1.run(src) + 1.5 * bp2.run(src)
        grit = hp.run(math.tanh(DRIVE * voice))
        sub = math.sin(2 * math.pi * ph) if f < 230 else 0.0
        if i >= 0:
            out.append(grit + SUB * sub)
    peak = max(abs(v) for v in out)
    return array.array("h", (int(GAIN * 32767 * v / peak) for v in out))


def gens(pairs):
    return b"".join(struct.pack("<Hh" if v < 0 else "<HH", g, v) for g, v in pairs)


def key_range(lo, hi):
    return lo | (hi << 8)


def build():
    notes = [ROOT + 12 * (k // 7) + MINOR[k % 7] for k in range(KEYS)]
    smpl, shdr = b"", b""
    igen, imod, ibag = b"", b"", b""
    n_gen = n_mod = 0
    for z, note in enumerate(notes):
        data = render(note)
        start = len(smpl) // 2
        end = start + len(data)
        smpl += data.tobytes() + bytes(2 * 46)  # 46 zero samples after each: the format's rule
        shdr += struct.pack("<20sIIIIIBbHH", name20(f"Growl {note}"), start, end, start, end, RATE, note, 0, 0, 1)
        lo = 0 if z == 0 else note
        hi = 127 if z == len(notes) - 1 else notes[z + 1] - 1
        ibag += struct.pack("<HH", n_gen, n_mod)
        zone = [
            (GEN["keyRange"], key_range(lo, hi)),  # must be first
            (GEN["attackVolEnv"], -7973),  # ~10 ms: no click
            (GEN["releaseVolEnv"], round(1200 * math.log2(0.08))),
            (GEN["overridingRootKey"], note),
            (GEN["sampleModes"], 1),  # loop: a held note wobbles on
            (GEN["sampleID"], z),  # must be last
        ]
        igen += gens(zone)
        n_gen += len(zone)
        # Velocity -> volume and -> filter cutoff at 0, replacing the defaults:
        # the soft EDM-mode keys (velocity 55) play the growl at full loudness.
        imod += struct.pack("<HHhHH", 0x0502, 48, 0, 0, 0)
        imod += struct.pack("<HHhHH", 0x0102, 8, 0, 0, 0)
        n_mod += 2
    shdr += struct.pack("<20sIIIIIBbHH", name20("EOS"), 0, 0, 0, 0, 0, 0, 0, 0, 0)
    ibag += struct.pack("<HH", n_gen, n_mod)
    igen += struct.pack("<HH", 0, 0)
    imod += bytes(10)
    inst = struct.pack("<20sH", name20("Growl"), 0) + struct.pack("<20sH", name20("EOI"), len(notes))

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
