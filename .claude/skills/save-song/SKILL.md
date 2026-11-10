---
name: save-song
description: Save the loop playing on the Chicco piano as the next riffs-page song ("save my song", "save it as Song #n+1", "save this loop/layer"), with every layer's exact settings (sound, octave, chicks, knob levels) on the page and clips that sound like the toy. Use whenever the user wants a loop on the toy kept or written down.
---

# Save a song from the toy

"Save my song" means a piece on `docs/riffs.html` the user can rebuild from later, not only a
`.mid` (the user, 2026-11-06). The user forgets how a sound was made: the page must show each
layer's **exact** settings, and its "as the toy plays it" clips must have the layers' effects
(2026-11-10: a clip without layer 4's tremolo was the first complaint).

The loop lives only in the toy's RAM, and the facts about how it was made live only in
`build-host/toy-serial.log`, which **a service restart or a flash truncates**. So the order matters.

## 1. Keep the log, then capture (nothing restarts in between)

```bash
cp build-host/toy-serial.log songs/<date>-song-<m>.log.tmp
timeout -s INT 40 arecordmidi -p 128:0 -b 60 -t 1000 songs/<date>-song-<m>.mid
```

- The service must be running and the loop playing (ask the user to let it play; muted layers
  play nothing, ask for them unmuted).
- `<m>`: the next "Song #m" (the tool prints it). `<date>`: today, YYYY-MM-DD.
- Don't flash, don't restart the service, before step 2 is done.

## 2. Draft the piece from the two

```bash
python3 tools/save_song.py songs/<date>-song-<m>.mid songs/<date>-song-<m>.log.tmp
```

It prints the piece (JavaScript) and on stderr the layer count, bars, bpm and passes. Check:
- **Layer count** = what the user hears (BLUE BIRD undos and RESET are replayed from the log).
- **Notes are the keys pressed**: Echo's repeats and Octave's copies are taken out (the clips
  add them back from `chicks`). A `?N` in `notes` is a note no logged key press named: look at
  the take in the log (`DOWN  key k` then `MIDI  note on n`).
- Each layer's `program`, `octave`, `chicks` and `fx` (keys, filter, reverb, pluck, glide,
  release, tremolo, 0-127) come from the log: the `LAYER N: program P, octave O, chicks ...`
  line (firmware since 2026-11-10) and `layer N keeps ...`. Never guess one; if the log lacks it,
  say so in the piece's `about`.
- Echo's level (its pot became Detune, then Glide) is fixed at `FX_ECHO_LEVEL`; Hold, Arp and
  Stutter are already in the notes; Wide, Rise and Tape aren't recorded by the looper at all.

Then `--insert` adds it to the end of `PIECES`, and fill in every `TODO` by hand:
- `name`: keep `#n Song #m` (the user's naming); layer `name`s by role (Bass, Pad, Hook...).
- `hand`, `tip`: where to play, in the page's words ("key 12 on the "+" of beat 2"), knob
  positions in words and the chicks. Read the steps off `notes` (16 steps a bar, step 0 = the
  red click).
- `about`: what the loop is, which `.mid` it came from, which chicks are in the clip.
- `plan`: Start (button, FROG, SELECT), then one RECORD per layer with what to set first.
- `chords`: Am for A-minor EDM songs unless the bass says otherwise.

## 3. Clips, check, keep

```bash
python3 tools/render_riffs.py          # docs/riffs-audio/ (gitignored)
```

- Script check: extract the page's `<script>` and `node --check` it.
- Screenshot the song's view (`#<n>-song-<m>`): serve `docs/` on 127.0.0.1 and use
  `firefox --headless --profile <scratchpad>/ffprof --no-remote --screenshot` (CLAUDE.md: testing
  the pages). Each layer must show its settings tiles.
- Optional ear check of one layer against the capture: render the capture's own notes for that
  channel with the layer's `fx` (`render_riffs.layer_fx`) and compare.
- Add a line to `songs/README.md` (layers, sounds, settings, passes captured), delete the
  `.log.tmp` (or keep it as `songs/<date>-song-<m>.log` if the user wants the raw log), add the
  piece to CLAUDE.md's riffs list, commit and push.

Tell the user the song number, the layers in one line each, and that the settings are on the
page. Don't paste the piece.
