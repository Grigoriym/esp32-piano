#!/usr/bin/env python3
"""Build the growl: a SoundFont with one bass preset for dubstep mode.

Usage: gen_growl_sf2.py <out.sf2> [voice]   (voice: one of VOICES, default VOICE)

ROCK is dubstep mode (main/keymap.c, 2026-11-05): its keys play PROGRAM, which
this file takes over from FluidR3 (loaded after it, tools/pc_synth.sh; the last
SoundFont with a program wins).

One looped sample per note of the keys (A natural minor from A1, main/keymap.c),
each one bar of the dubstep beat (main/rhythm.c DUBSTEP, BPM) long with a whole
number of every part's cycles, so it loops without a seam and its slow movement
(the detuned voices drifting) keeps the beat's tempo on every key.

VOICE is the user's pick of 2026-11-05 ("E" of an audition, A-G, rendered with
#5's Hits): three detuned squares plus one an octave up, crushed to coarse steps
and driven, low-passed, the lows cut, a clean sine sub under it. Before it: a
filter-sweep wobble ("toy-like", then "a brown sound"), then a vowel wobble ("I
don't like it"); in the audition reese and riddim were liked, dive not, metal
mixed. The other voices stay here to try again: gen_growl_sf2.py out.sf2 <voice>.
"""
import array
import math
import struct
import sys

PROGRAM = 38  # GM Synth Bass 1: nothing else on the toy uses it
RATE = 22050  # plenty for a bass, and the build stays quick in plain Python
BPM = 140
ROOT = 33  # A1: key 1 in EDM mode
MINOR = [0, 2, 3, 5, 7, 8, 10]
KEYS = 21  # keys 1-21 (A1-A4) get their own sample; higher keys transpose the top one
PRE_S = 0.2  # rendered before the loop and dropped, so the filters have settled
GAIN = 0.9

HIGHPASS_HZ = 150  # the growl part above this; the sub below

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
        assert kind == "highpass"
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


def lowpass(f0, q=0.7071):
    w = 2 * math.pi * f0 / RATE
    alpha, c = math.sin(w) / (2 * q), math.cos(w)
    bq = Biquad()
    a0 = 1 + alpha
    bq.b0, bq.b1, bq.b2 = ((1 - c) / 2 / a0, (1 - c) / a0, (1 - c) / 2 / a0)
    bq.a1, bq.a2 = -2 * c / a0, (1 - alpha) / a0
    return bq


def saw(ph):
    return 2 * (ph % 1.0) - 1


def square(ph):
    return 1.0 if ph % 1.0 < 0.5 else -1.0


def tau(ph):
    return 2 * math.pi * ph


# Each voice: (ph, cycles) -> the growl part at one sample; ph counts cycles of
# the note from the loop's start. A whole number of cycles of every part fits the
# loop, so it loops without a seam.
def reese(ph, cycles):
    k = 3 / cycles  # +-3 cycles a bar: the saws drift against each other ~1.75 times a second
    return (saw(ph) + saw(ph * (1 + k)) + saw(ph * (1 - k))) / 3


def riddim(ph, cycles):
    x = square(ph) + 0.4 * square(2 * ph)
    return round(4 * x) / 4  # coarse steps: grit


def dive(ph, cycles):
    return 0.6 * saw(ph) + 0.4 * square(ph)


def metal(ph, cycles):
    return math.sin(tau(ph) + 4 * math.sin(tau(2 * ph)) + 2 * math.sin(tau(3 * ph)))


def reese_square(ph, cycles):
    k = 3 / cycles
    x = (square(ph) + square(ph * (1 + k)) + square(ph * (1 - k))) / 3 + 0.3 * square(2 * ph)
    return round(4 * x) / 4


def reese_wide(ph, cycles):
    k = 5 / cycles
    return (saw(ph) + saw(ph * (1 + k)) + saw(ph * (1 - k))) / 3


VOICES = {
    # name: (wave, drive, low-pass Hz, sub)
    "reese": (reese, 2.5, 1400, 0.8),
    "riddim": (riddim, 8.0, 2600, 0.7),
    "dive": (dive, 4.0, 2200, 0.8),
    "metal": (metal, 3.0, 3500, 0.7),
    "reese_square": (reese_square, 6.0, 2000, 0.8),
    "reese_heavy": (reese_wide, 5.0, 2000, 0.8),
    "riddim_dark": (riddim, 8.0, 1300, 0.9),
}
VOICE = "reese_square"
DIVE_S = 0.16  # dive: the drop from two octaves up onto the note
DIVE_FROM = 4.0


def render(note):
    """One looped sample of the voice at note: (samples, loop start).

    Whole cycles in the loop (one bar long, filters settled by a pre-roll); the
    dive's drop is a one-off before the loop."""
    wave, drive, lp_hz, sub_amt = VOICES[VOICE]
    f = note_hz(note)
    bar_s = 4 * 60 / BPM
    cycles = round(bar_s * f)
    length = round(cycles * RATE / f)
    head = int(DIVE_S * RATE) if VOICE == "dive" else 0
    pre = int(PRE_S * RATE)
    lp, hp = lowpass(lp_hz, 0.9), Biquad()
    hp.set("highpass", HIGHPASS_HZ, 0.7071)
    out = []
    head_ph = 0.0
    for i in range(-pre, head + length):
        if i < head and VOICE == "dive" and i >= 0:
            # Exponential glide from DIVE_FROM x f down to f; the phase is integrated.
            r = DIVE_FROM ** (1 - i / head)
            head_ph += r * f / RATE
            ph = head_ph - (head_ph // 1)
            x = saw(ph) * (1 + 2 * (1 - i / head))
        else:
            j = (i - head) % length
            ph = j * cycles / length
            x = wave(ph, cycles)
        grit = hp.run(lp.run(math.tanh(drive * x)))
        sub = math.sin(tau(ph)) if f < 230 else 0.0
        if i >= 0:
            out.append(grit + sub_amt * sub)
    peak = max(abs(v) for v in out)
    return array.array("h", (int(GAIN * 32767 * v / peak) for v in out)), head


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
        data, head = render(note)
        start = len(smpl) // 2
        end = start + len(data)
        smpl += data.tobytes() + bytes(2 * 46)  # 46 zero samples after each: the format's rule
        shdr += struct.pack("<20sIIIIIBbHH", name20(f"Growl {note}"), start, end, start + head, end, RATE, note, 0, 0, 1)
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
    global VOICE
    if len(sys.argv) not in (2, 3) or (len(sys.argv) == 3 and sys.argv[2] not in VOICES):
        sys.exit(__doc__)
    if len(sys.argv) == 3:
        VOICE = sys.argv[2]
    with open(sys.argv[1], "wb") as f:
        f.write(build())


if __name__ == "__main__":
    main()
