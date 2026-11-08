# Songs made on the toy

Each one: a `.mid` (what the toy sent, captured on the PC) and an `.mp3` rendered
from it with the toy's sounds (FluidSynth, FluidR3 + `build-host/cat.sf2` +
`build-host/edm_kit.sf2`).

How a song is captured, while its loop plays (service running):

    timeout -s INT 40 arecordmidi -p 128:0 -b 60 -t 1000 out.mid

The capture doesn't hold the sounds: the toy sends them when a loop starts or a
style button is pressed, before the capture. They are in the toy's log
(`build-host/toy-serial.log`: `MIDI  program N`, `layer N keeps keys K, filter F, reverb R`)
since 2026-11-06; before that they were assumed. A track with program changes is put
in front, so they're what the song was played with, not read from it.

- `2026-11-03-first-song`: the first song, made in EDM mode: piece #1 of
  `docs/riffs.html` stacked in the looper, 4 bars at 124 bpm. Sounds assumed: saw
  lead (81) on every key and loop channel, the EDM kit (drum program 30).
- `2026-11-06-cosmic-bass`: the user's bass line for #6 Cosmic, one loop layer (channel 3)
  under the metronome at 130 bpm (channel 9), keys at octave −1 (notes 21/24/28 = keys 1/3/5
  an octave down). 2 bars, 3693 ms a pass. Written out as the Cosmic `Bass` layer in
  `docs/riffs.html`. No mp3: the sound wasn't captured.
- `2026-11-06-song-1`: "Song #1", three loop layers (channels 3-5) over SLOW ROCK's TECHNO beat at
  130 bpm with the metronome clicking (channel 9). 2 bars, 3693 ms a pass, ~10 passes captured
  while it played. Sounds from the toy's log, not assumed: saw lead (81) on all three layers
  (layer 3 kept the keys fader at 89), filter open, reverb send +100% (the pots at 127), EDM kit
  (30); the metronome's woodblock (115), dry. Rendered with `pc_synth.sh`'s reverb room, gain 0.3.
- `2026-11-06-song-1-layer-4`: Song #1 again after a fourth layer (channel 6: keys 19/20/22, keys fader
  105, filter 69). Captured raw (no sounds in front). The beat and metronome here sit 86 ms after the
  loop's start (a DISCO/SLOW ROCK switch moved them); layer 4 was played to the beat.
- `2026-11-08-song-2`: "Song #2", three loop layers (channels 3-5) over SLOW ROCK's TECHNO beat at 130 bpm with the
  metronome (channel 9), ~5 passes, 3692 ms a pass. Layers 1-2 were recorded with Echo + Octave on (their repeats
  at velocity 38/26/18 and +12 copies are in the capture); layer 1 at octave −1 (notes 21/24/28 = keys 1/3/5).
  From the log: layer 1 keys 114, filter 53, reverb 127; layers 2-3 keys/filter/reverb 127; the style button and
  layer programs weren't in the log (truncated at a service restart): saw lead and SLOW ROCK assumed from the
  130 bpm beat. Written out as `#8 Song #2` in `docs/riffs.html`. No mp3.
