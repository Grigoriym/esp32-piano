# Songs made on the toy

Each one: a `.mid` (what the toy sent, captured on the PC) and an `.mp3` rendered
from it with the toy's sounds (FluidSynth, FluidR3 + `build-host/cat.sf2` +
`build-host/edm_kit.sf2`).

How a song is captured, while its loop plays (service running):

    timeout -s INT 40 arecordmidi -p 128:0 -b 60 -t 1000 out.mid

The capture doesn't hold the sounds: the toy sends them when a loop starts or a
style button is pressed, before the capture. A track with program changes is put
in front, so they're what the song was played with, not read from it.

- `2026-11-03-first-song`: the first song, made in EDM mode: piece #1 of
  `docs/riffs.html` stacked in the looper, 4 bars at 124 bpm. Sounds assumed: saw
  lead (81) on every key and loop channel, the EDM kit (drum program 30).
- `2026-11-06-cosmic-bass`: the user's bass line for #6 Cosmic, one loop layer (channel 3)
  under the metronome at 130 bpm (channel 9), keys at octave −1 (notes 21/24/28 = keys 1/3/5
  an octave down). 2 bars, 3693 ms a pass. Written out as the Cosmic `Bass` layer in
  `docs/riffs.html`. No mp3: the sound wasn't captured.
