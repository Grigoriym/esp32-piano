# esp32-piano: new brains for a toy keyboard

## Concept
A Chicco toy keyboard (flea market, 2026-10-11) gets an ESP32 inside that
reads its keys and buttons. What it does with them is **not decided yet**,
the user picks once the toy is opened (milestone 1):
- **A. MIDI controller**: the ESP32 sends the keys as MIDI (over WiFi), and a synth
  on the PC or phone makes the sound.
- **B. Own synth**: the ESP32 makes the sound itself (I2S amp module into
  the toy's own speaker); the style buttons pick sounds or patterns.
- Both: same key scanning, the output is a setting.

The toy's own electronics are a sound chip (probably a chip-on-board blob)
that also scans the keys. It gets replaced or bypassed, not reused.

Sibling projects (`../`): `esp32-desk-display` and `esp32-mukklet` (the
reference for code to lift: build setup, CI, host tests, WiFi, secrets),
`esp32-hw-checks` (bring-up tests: a new module gets checked there first).
The planning home is `../grappim-watcher` (ESP32 sphere, `docs/esp32/`).

## Shared ESP32 docs
One source of truth per fact, never a copy (the user's rule, 2026-10-03):
- **Everything about a part** (pinout, voltage, quirks, measurements, test
  status, how many owned), **including the toy itself**: its
  entry in the **Homebox inventory** (`http://192.168.0.139:34899`).
  `python3 ../grappim-watcher/docs/esp32/inventory/parts.py show Chicco`
  prints the toy's entry; plain `parts.py` lists everything (`find
  <text>` searches). Read-only. Check it before suggesting a part or a
  purchase. It doesn't track what's free or which project uses a part: ask
  the user. Writes (new facts) go through the API one entry at a time with the user's OK, never
  silently; how: `../grappim-watcher/docs/esp32/inventory/README.md`.
  Notes there are capped at ~1000 characters: long material (the key
  matrix map, teardown photos) lives in this repo and the entry links here.
- The toy and other whole devices carry the **`Salvage`** tag; their
  description reads `<state>. Project: <name>. Useful inside: <...>`.
  Update it as the toy gets opened and modified.
- Rules for any ESP32 project: `../grappim-watcher/docs/esp32/`
  `WIRING_RULES.md` (usable GPIOs, power, cables),
  `ENCLOSURE_PLAYBOOK.md`, `FIRMWARE_PLAYBOOK.md` (flash and serial
  traps, code shape, the four checks). Flashing: the `esp32-flash-verify`
  skill.

**Rule: a new fact about a part goes in its Homebox entry, not here.** This
file keeps what is this project's own: the matrix map, pins, decisions,
milestones.

## Parts likely to be used
- An ESP32 DevKit 30-pin for the breadboard stage. The final board is
  decided by what fits inside the toy.
- `Audio converter & amplifier module (I2S)` for direction B.
- `LiPo charge/discharge module` / `USB-C breakout` only if the user wants
  it rechargeable instead of the toy's battery box.
- 1N4148 diodes if the key matrix has no diodes and chords ghost.

## Rules for this project
- **Nothing irreversible without the user's OK**: no cutting traces,
  desoldering or drilling the case until they agree to that specific step.
  First check the toy works as bought (batteries in, try every button) and
  write down what it does.
- **Measure, don't guess**: the matrix layout, which lines are rows and
  which are columns, and the battery voltage under load get measured with a
  multimeter, not inferred from photos.
- **Read the meter's display in beep mode, don't only listen**: some lines
  on the key board run through black printed bars and read ~280-300 Ω,
  above the beep threshold. A silent pair is not proof of no connection
  (4-wire wire 3 looked dead until read in Ω).
- **Photos are not readings**: pad type, pin labels and markings read off
  a phone photo were wrong twice (carbon vs metal pads, `EF`/`AB` label
  list). Ask for a sharp straight-on close-up before writing them anywhere.
  Trace routes too: on 2026-10-23 two of my photo tracings were wrong (a line
  that was key 1's, a route between parallel jogging lines). What worked: the
  user follows the line on the board and draws it on the photo.
- **The meter can't reach copper under the green coating**: probe bare pads,
  combs or ringed holes. A silent probe on a coated trace proves nothing.
- **ESP32 on the toy's lines: toy batteries out.** The chip ran straight from
  the batteries (up to ~6.4 V fresh); with it removed, the only voltage on
  the lines is the ESP32's 3.3 V. Firmware only pulls lines low, never
  drives them high.

## Build
ESP-IDF project at the repo root, `sdkconfig.defaults` from `../esp32-hw-checks`
(4 MB flash, large single app partition) plus a 1000 Hz tick (the key scan aims to run
every 2 ms). First build in a fresh checkout: `cp main/wifi_secrets.h.example
main/wifi_secrets.h` (fill in, gitignored; same network as mukklet), then
`. ~/esp/esp-idf/export.sh && idf.py set-target esp32 && idf.py build`. Flash
with the `esp32-flash-verify` skill. After changing `sdkconfig.defaults`:
`rm sdkconfig` and build again.
- `main/display.c`, `canvas.c`, `font.c`, `font_data.c` (+ `tools/gen_font.py`)
  are lifted from mukklet (pins changed); `ui.c` (pure, host-tested) draws
  the screen, `screen.c` runs it in its own task on core 1 and redraws when
  the state `main.c` hands it (`screen_set`, every scan) changes.
- `main/main.c`: key scan through the MCP23017 (I2C 0x20 on D21/D22; rows
  `AB0`–`AB7` on port A pulled low one at a time, columns `EF0`–`EF7` on port B
  read, the chip's pull-ups), serial log, glue. The logic is in pure files
  (no ESP-IDF includes, host-tested): `matrix.c` (debounce 3 scans, ghost
  rectangles; a press that may be a ghost is held back), `keymap.c` (names
  and what every key and button plays), `demo.c` (DEMO: Megalovania intro,
  channel 2, 100 bpm, velocity 70), `looper.c` (RECORD/REPLAY/BLUE BIRD: a looper
  that starts at the first note; with the beat on, or else the metronome clicking (2026-11-06: a loop to
  the metronome alone was 3709 ms against its 3692 ms 2 bars and drifted) (`looper_set_grid`, from main
  every scan) it starts on that note's bar line, its length is first note to
  RECORD rounded to whole bars (from the bar line, a bass begun on step 6 made a
  5-bar loop with an empty bar, 2026-11-03), notes past the end before the first
  note's place wrap to the start, a late RECORD's next-pass notes are dropped; RECORD while playing adds a layer, heard once closed; recorded over several passes (second RECORD rounded to
  whole passes from the pass start nearest the first) the loop grows to that many passes, older layers repeated, up to
  8 (the user, 2026-11-06: a part, then a pass of silence; before, every pass stacked into one) (up to 4, keys on
  channels 3-6 each with its instrument, drums on 10), BLUE BIRD undoes
  one (the knob's press until 2026-11-08; BLUE BIRD was an open hi-hat); `looper_mute` silences a layer and brings it
  back, on the next bar line with a beat/metronome, else at once (2026-11-08, the Layers screen, below); RESET erases it), `rhythm.c` (SELECT: a drum
  beat per style button, one bar of `x`/`X`/`.` steps per drum; while it plays a style button changes only the
  keys' sound, not the beat or its kit (the user, 2026-11-06: it used to switch at the beat's bar line and moved the
  beat off a playing loop); velocity 70, not recorded by the looper). They send through
  `midi_out.h`'s callback. The line-pair finder that mapped the grid is in git (`233c8c3`).
- `main/encoder.c` + `encoder_decode.c` (KY-040, from mukklet, without its press
  gestures): `main.c` turns knob events into the screen shown and logs `KNOB ...`.
  `groove.c` (pure, 2026-11-03) is the Groove screen's timing (a third knob screen:
  the beat's bar as steps with a running column, its first two drums, the player's
  presses green within 30 ms of a step or orange, an early/late meter); main feeds it
  the beat grid and key presses.
  `mixer.c` (pure, 2026-11-04): the 3 pots (ADC1, read every 10 ms, smoothed, hysteresis
  tested against +-20 counts; each read is the mean of 8; a pot left alone 0.5 s needs 2 levels to move again (`MIXER_WAKE`: the tempo flipped 130/131 by itself and when other pots turned, 2026-11-06); the LED bar only for a move of 3 levels
  or an end, `MIXER_TURN`: a key press tipped pot 3 a level and lit the bar white, 2026-11-05) scale note-on velocities: pot 1 the beat (level 0 drops the note on); pot 2 is the
  metronome's volume since 2026-11-06 (CC11 on its channel; before, the newest loop layer's level with a pick-up
  after an undo, the user: "too much, I can rerecord"; layers now play as recorded); pot 3 is the live keys' fader since 2026-11-06, CC11 on channel 1 (a held note follows it; velocity didn't move held notes and the growl ignores velocity). The
  looper records unscaled. VOLUME +/- stays the master (CC7). Serial: `POT   beat|layer|keys|filter|reverb N`.
  Pots 7-10 (2026-11-09) on the ADS1115: see Next step. Pots 4-5 (2026-11-06, wired and flashed): filter on D35, reverb on VP, on the live channels 1-2 (keys, DEMO); a loop
  layer keeps the filter and reverb it was recorded with (`mixer_freeze_layer` when `n_layers` grows, onto its own
  channel; the user: "we shouldn't touch the recordings"). Since 2026-11-07 a layer also records the moves of pots 3-5 (keys fader, filter, reverb) made during its take (`looper_pot`, stored as CC 102+pot, played as CC11/NRPN on its channel, the take's starting value first; the user: a reverb turned while recording was lost). Not the drums,
  sent at most every 30 ms and again when the PC link comes up. FluidSynth 2.3.4 ignores CC74 (rendered: no change),
  so the filter is the SF2 NRPN (99=120, 98=8 initialFilterFc, LSB 38 before MSB 6), 0 to -4800 cents; it
  survives a program change; past ~-5000 FluidR3's saw lead gets brighter again. Reverb: the same NRPN on gen 16
  (reverbEffectsSend), -100% to +100% (0 dry, middle = the preset's own, 127 full). CC91 was inaudible (the user: "no
  diff"): FluidSynth's default room left the tail ~57 dB under the note, CC91 adds only 20%, and the saw lead has its
  own send, so CC91 0 wasn't dry. `pc_synth.sh` sets the room: room-size 1.0, damp 0, level 1, width 1 (the user's
  pick C of three rendered rooms, 2026-11-06). LED bar per pot: 1 green, 2 pink (click
  volume), 3 white, 4 blue (filter), 5 violet (reverb), 6 warm white (click tempo). VN (ADC1 ch 3) is the last free ADC1 pin. An unwired ADC pin floats: wire before flashing.
  **Pot names (2026-11-06, the user grouped them on the box):** three sections of two, Volume (beat = pot 1, keys = pot 3),
  Effects (filter = 4, reverb = 5), Metronome (tempo = 6, volume = 2). Docs for the player use these names, not
  numbers; the numbers stay the code's and wiring's order (pin, `mixer_pot_t`).
  `metronome.c` (pure, 2026-11-06, the user's ask): FROG (was a low tom) toggles a woodblock click (program 115, beat 1
  note 84, others 77) on its own channel 9 (`METRO_CH` 8), sent 150 ms ahead like the beat, dry (CC91 0 + reverb-send
  NRPN -100%, no filter: the user); never recorded. Pot 6 on VN (the last ADC1 pin) is its tempo, 60-180 bpm, pot 2
  its volume (the user's picks); a new tempo carries on from the last click. While the
  beat plays it follows the beat's grid. FROG on during a playing loop starts it on the loop's bar line (`metro_align`);
  a tempo change keeps the bar count. The header of every screen shows `Click N bpm` (greyed while off) with a dot flashing on each
  click, red on beat 1 (`metro_flash`, the click's own time, not the send's); it takes DEMO's place there. More pots would need an analog chip (ADS1115 / a 4067 mux; none in Homebox): an MCP23017 is digital only.
  `fx.c` (pure, 2026-11-06, the user's ask): the 8 chicks are effects on the live keys (they were drums): chick 1 Echo
  (3 repeats, dotted 8th, 70% each, channel 11), 2 Hold (CC64), 3 Octave, 4 Arp (held keys up, 16ths), 5 Rise
  (1 bar: bend +1 octave, filter opens), 6 Tape (half a bar: keys and loop bend -1 octave, keys go dark, then stop),
  7 Stutter (held: retrigger on 16ths), 8 Wide (two copies +-12 cents on channels 12-13). Grid: the beat's, else the
  metronome's tempo. Bend range 12 (RPN, `fx_setup`, at boot and link-up). `send_midi` copies every CC and program on
  channel 1 to channels 11-13. Rendered first: bend RPN and CC64 work; CC93 chorus and CC10 pan did nothing on the saw
  lead. The looper records the notes the chicks make (`take.c`, pure, 2026-11-08, the user's ask: a take with Echo
  and Octave played back plain): Echo's repeats, Octave, Arp, Stutter, Hold as late note offs, all on the layer's one
  channel (an echo over its still-held key is left out: it cut held notes to 350 ms, 2026-11-08); not Wide, Rise, Tape (channel-wide bends/copies).
  **Chick numbers are the user's** (2026-11-06): 1-4 the top row from the left, 5-8 the bottom row from the left;
  chick 8 is next to the style buttons at the BLUES end (first written as "T1", backwards: Echo sat on chick 8).
  Every screen's bottom row shows them by initial, `E H O A  R T S W` = chicks 1-8,
  lit cyan while on (Rise/Tape while running, Stutter while held; `ui_state_t.fx`, 2026-11-06, the user's ask).
  `leds.c` (pure) + `strip.c` (RMT, WS2812 on TX2, channels capped at 24/255 on 3V3):
  LEDs 1-4 the layers in cyan/magenta/yellow/orange (the newest brighter, recording blinks
  red), 5-8 the beat's count (all turned 2 along the strip, `LEDS_TURN`: the strip lies in a circle, the user
  wanted the beat on strip LEDs 7, 8, 1, 2, 2026-11-05); a pot turned shows its level as a bar for 1.5 s. All 3 pots
  read 0-127 over their travel (2026-11-04); mid-travel they can jitter, see Open items.
  **Layers screen** (2026-11-08, the user's ask after "is it a groovebox?": performing a song from one loop by
  muting layers): the fourth knob screen, one row per layer (LED colour, sound, on / muted / mute at bar / rec). The
  knob walks its rows before the next screen (`ui_knob_turn`, host-tested); its press mutes the row's layer. A muted
  layer is dim on the strip. `tools/loop_check.py` doesn't know mutes yet: a muted pass reads as missing notes.
  Next planned: the ADS1115 pots as one fader per layer.
  `stats.c` (2026-11-06, the user felt the chip warm): the fifth knob screen, Debug (each core's load from the idle
  tasks' run time, `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS`; free and lowest RAM, WiFi dBm, the longest scan gap,
  a scan pass's work average / longest, uptime), and a `STATS ...` log line every 10 s. First reading: core 0 ~60%
  busy, a scan pass ~5.3 ms of work (worst 7.7), so the scan runs every ~7 ms, not the 2 ms the notes assume (debounce
  3 scans = ~21 ms, not 6). Not looked into yet; core 1 (screen) 2-5%, heap 160 KB.
  `chord.c` (pure) names the held keys for `ui.c`'s Play screen; `FONT_BIG`
  (`tools/gen_font.py`, ASCII only) is its big chord name.
- `main/wifi.c` (from mukklet, power save off: it holds packets up to a
  beacon interval) and `main/midi_link.c`: MIDI over UDP, protocol in
  `main/midi_link.h`. The toy is `chicco-piano.local` (mDNS, port 21928); the
  PC sends `HELLO` once a second, the toy sends each MIDI message twice with a
  sequence number back to where the HELLO came from. Since 2026-11-03 each packet also
  carries the toy's send time and a delay: the beat, DEMO and loop run
  `MIDI_LINK_LEAD_US` (60 ms) ahead and the bridge plays them at their time (toy
  clock mapped from the fastest packets of the last 5 s); live keys play at once.
  Measured over WiFi with 4-35 ms ping: beat steps within 0.8 ms of the grid, was up
  to 24 ms off (audible hiccups). The bridge logs each timed message that came after
  its time (`late: X ms ...`) and each live note slower than 30 ms over the fastest
  recent packets (`slow key: X ms ...`). **Since 2026-11-03 the same packets also go over
  the USB cable** (console UART at 921600, frames `0xF5 len packet sum` between the
  log lines, sent while the bridge says `HELLO` on the cable): the bridge plays
  whichever copy comes first (shared sequence number) and logs `playing what comes
  over the cable|WiFi` when that changes. Either works alone. The toy's log lines
  then go to `build-host/toy-serial.log` (truncated at each service start). Lead raised to 150 ms on 2026-11-03: at
  60 ms, 4-26 of ~2000 timed messages a minute came late.
  2026-11-04: the WiFi send has its own task and queue (`midi_link.c`): a blocking
  `sendto` used to hold up the scan loop, the beat and the cable copy. Stall logs: the toy
  prints `SCAN  stalled X ms` / `WiFi send blocked X ms`, the bridge `bridge busy`,
  `bridge woke ... late`, `late: ... over the cable|WiFi` and `its other copy came X ms
  after`. **Root cause of the last hiccups (2026-11-04):** the bridge looked up
  `chicco-piano.local` once a second inside its main loop; every ~2 min the mDNS cache
  entry runs out and the lookup blocks 220-290 ms, so nothing was read or played (both
  paths late together, a burst every ~122 s). The lookup now runs in its own thread;
  after that the lookups were still slow at the 2-min marks and nothing came late, and
  the user heard no hiccups. Never block in the bridge's main loop.
- `tools/piano_bridge.c` (PC, links `-lasound`): owns the ALSA port
  "Chicco Piano" (stays while it runs, toy on or off), says HELLO, drops the
  copies. `tools/pc_synth.sh` builds it into `build-host/`, starts it with
  FluidSynth and keeps the route between them.
- `tools/chicco-piano.service`: systemd user service running `pc_synth.sh`.
  **On demand only, never enabled** (no `[Install]` section since 2026-10-24,
  the user's call: running all day it made the desktop's audio crackle, see
  Traps). Linked, not enabled, in `~/.config/systemd/user/`. Log:
  `journalctl --user -u chicco-piano`. The path in it is hardcoded to
  `~/proj/grappim/esp32-piano`.
- `tools/chicco-piano.desktop`: "Chicco Piano" in the app menu (symlink in
  `~/.local/share/applications/`, 2026-10-22), runs
  `tools/chicco-piano-launch.sh`: links the unit if it isn't (a `disable`
  removes the link) and (re)starts it; right-click **Stop** stops it. Path
  hardcoded too.
- `docs/cheatsheet.html`: what every button and knob does (only that since 2026-11-07, the user's ask; the keyboard,
  the Megalovania opening, the metronome and the Sinan Sakić chords are in git at `81a7e2d`). Local only: the user
  doesn't use artifacts (2026-11-05: "all docs we have offline"); an old published
  copy exists, don't republish it.
- EDM mode (2026-11-03, the user wants to make simple electronic music, not learn
  piano): DISCO or MARCH (`keymap_is_edm`; MARCH's trumpet and march beat gone, its
  beat is `BREAKS`, a 124 bpm breakbeat, the user's ask; SLOW ROCK since 2026-11-05, beat `TECHNO`, 130 bpm, 16th hats, its 12/8 slow rock and piano gone, RESET still gives piano;
  ROCK since 2026-11-05 too, dubstep: beat `DUBSTEP` 140 bpm half-time (kick 1, snare+clap on 3), keys play the growl bass, program 38
  from `tools/gen_growl_sf2.py` (voice `reese_square`, the user's pick "E" of an audition of 7 rendered drops: three detuned
  squares + one an octave up, crushed and driven, one looped one-bar sample per key 1-21 at 22 kHz, a clean sine sub; velocity
  modulators zeroed so the soft EDM keys are full loudness. Disliked before it: a filter-sweep wobble ("toy-like", then "a brown
  sound"), a vowel wobble ("I don't like it"); in the audition reese and riddim liked, dive not, metal mixed; the voices stay in the file), loaded last by `pc_synth.sh`; its cat is off the
  button (the user: keep the cat files). `DEFAULT_STYLE` (SELECT before any style button, and after RESET) is BLUES now) makes the keys play only A natural minor (`keymap_key_note`: key 1 =
  A1, 7 keys an octave, key 8 = A2), keys at velocity 55, beat `EDM` 124 bpm four on the floor at
  110/127 (the saw lead drowned the beat twice). Its drums are kit 30 (`EDM_KIT`), built by
  `tools/gen_edm_kit_sf2.py` (synthesized: the page synth's kick/clap/hats, which the
  user liked better than FluidR3's, plus the pads' other notes), loaded after FluidR3 by
  `pc_synth.sh` (FluidR3 has no drum program 30). Every other style sends kit 0.
  Its sounds go through tanh drive 2.5 (`DRIVE`, the user asked for a louder beat):
  rendered, the kit averages ~4 dB over FluidR3's standard kit, ~6 dB over undriven (2026-11-03). Another style button or RESET ends it.
- `docs/riffs.html` (2026-11-03, replaced `docs/learn-piano.html`, which is in git at
  `076f9c7`): pieces to play over the EDM beat, layers (bass, chords, hook) shown bar by
  bar as key numbers, each playable in the browser (WebAudio saw + the beat). The user
  tries one and proposes changes. Pieces are the `PIECES` data at the top of its
  script (layers of keys as `notes`, `octave: -1` for a layer played an octave down, of drum pads as `pads` with
  `PADS` below (no piece uses pads since the chicks became effects); a
  piece's `plan` is its song plan). The page opens on a list of the songs; each song
  is its own view (`#<slug>`, e.g. `#2-night-drive`). 2026-11-03: #1 First Drop (bass,
  chords, arpeggio, hook; the clap roll was dropped at the user's ask, pad layers still
  work) #5 Riddim Drop (2026-11-05, rewritten for the growl "E": ROCK/DUBSTEP, a Riddim stutter on key 1 with a hole on the snare, an Answer in that hole, a high Screech, a build-up with Rise (chick 5; was a snare-pad Build); the user likes "heaviest dubstep drops" and Skrillex; page synth has a `growl` voice for pieces with `sound: 'growl'`; `render_riffs.py` takes each piece's program from its button) and #4 Bunker (2026-11-05, replaced #4 Sunset, which is in git at `e514df5`; SLOW ROCK/TECHNO, the user wanted something heavier, Berlin style, "let's experiment": key 1 rumbling after every kick, then "aggressive": a 16th-note sequence around key 8, a stab on every offbeat, built by layers and pots) and #3 Warehouse (2026-11-05, SLOW ROCK/TECHNO 130 bpm, Am Am Dm Em: a bass rolling on the 16ths after each kick, offbeat stabs, a one-bar hook moved up on bars 3-4) and #6 Cosmic (2026-11-06, SLOW ROCK/TECHNO: the user's keys 5 3 1 found with the filter pot; layers Melody (the user's rhythm: 5 a bar, 3 and 1 half a bar), Echo and Taps (picked from an audition, `build-host/audition/cosmic/`), Pulse and Bass (the user's own, read off MIDI captures; the bass one octave down, `songs/2026-11-06-cosmic-bass.mid`)) and #7 Song #1 (2026-11-06, the user's own four-layer loop (layer 4 Riff on 19/20/22, fader 105, filter 69, added later), read off `songs/2026-11-06-song-1.mid`: Cosmic's bass, an offbeat pulse on 12/10/8, a high A on 22/24 at the keys fader ~89) and #8 Song #2 (2026-11-08, the user's three layers read off `songs/2026-11-08-song-2.mid`: bass 1/5/3 an octave down, stabs on 8/10, a melody on 10/12/13; layers 1-2 were played with Echo + Octave on, the notes are only the pressed keys) and #2 Night Drive (F G Am Am: a bass on the breakbeat's kicks plus an octave pickup, held chords, hook; its first bass was four-on-the-floor and fought the breakbeat). Each song names
  its `button` and `beat` (#1 DISCO/EDM, #2 MARCH/BREAKS); the page synth's `BEATS`
  copies main/rhythm.c's patterns (change both), the clips read rhythm.c. After changing a chord or `matrix_diodes`: `tools/check_riff_chords.py`
  (some key sets are held back and would never sound). Its Listen buttons play
  "as the toy plays it" (clips from `tools/render_riffs.py`: the same FluidSynth and
  FluidR3, the EDM beat and velocities read from the firmware sources, into gitignored
  `docs/riffs-audio/`; rerun after changing a piece or those) or "page synth" (WebAudio
  saw + a made-up kit; the user liked that beat better than the toy's, 2026-11-03).
  Rendering #1 at `pc_synth.sh`'s gain 1.0 peaks ~9 dB over full scale (the script uses
  0.3); whether the live toy clips too isn't checked.
- `tools/gen_cat_sf2.py` + `tools/sounds/` (free recordings, see its README; the
  table `CATS` lists them): ROCK's program 19 was a cat (off the button since 2026-11-05, ROCK is dubstep; the file and entry stay), NEW AGE's 88 a deeper
  Siamese (2026-10-31; ~0.9 s at F3, a ~0.15 s blip at C6). `pc_synth.sh` builds `build-host/cat.sf2` and
  loads it after FluidR3; the last SoundFont loaded with a program wins, so only 19
  changes. Tuning: rendered through FluidSynth, within 1 cent at F3 to C6 of the
  meow's centre pitch (it glides, as a meow does). A low note plays slower and longer
  (F3 ~3 s, C6 ~0.5 s). The beat on SELECT is unchanged. FluidSynth refuses an SF2
  whose INFO strings have an odd length.
- `docs/live.html` + `tools/live_server.py` (2026-11-06, the user's ask): what the PC plays, live, at
  http://127.0.0.1:21929/ (started by `pc_synth.sh`, so it's up while the service runs). The server runs `aseqdump`
  on the "Chicco Piano" port and streams each message to the page (Server-Sent Events); the page draws a scrolling
  piano roll: keys white, loop layers in the LED strip's colours, chick copies faint, drum lanes, the metronome's bar
  lines, tempo and bar:beat from its clicks (else from the kicks). Sounds sent before the server started are unknown
  (no name on the chip). Tested from a recorded stream in headless Firefox (call `resize()` and `draw()` before the
  screenshot, or it catches an empty frame); not yet seen by the user.
- **32x8 matrix, a random visualizer** (2026-11-09, the user's idea, "like the old Windows Player"): the MAX7219
  board on D25/D26/D27 (VCC on VIN), mounted standing up: 8 across, 32 tall (the user; which end is up and where
  the IN header sits: not told yet). `docs/matrix-random.html` previews, upright, 8 music-reacting scenes (A Spectrum, B Ripples,
  C Starfield, D Fireworks, E Life, F Wave, G Plasma, H Rain), each with random settings re-rolled each time it comes
  up, and the rotation: a random scene every 4 bars on the bar line. The user picked D Fireworks alone (no rotation),
  2026-11-09: `fireworks.c` (pure, host-tested: a note on channels 1-6 launches a rocket in its pitch's column, higher
  notes fly higher, the next beat of the drum beat or metronome bursts it, else it bursts 0.6 s after topping out;
  gravity and burst size re-rolled every 4 bars; the bottom row counts the beat, all 8 dots on beat 1, two sparks up
  at each bar line, dark 2 s after the beat stops, the user's pick) shown by `dots.c` (own task on core 1, bit-banged like check 4,
  brightness 1, a frame sent only when it changed). `DOTS_IN_AT_TOP` / `DOTS_MIRROR` are guesses: the user hasn't
  said which end is up. In `docs/wiring.html`. Built, not flashed.
- **8x8 LED matrix: gone** (2026-11-09, the user took it off the piano for good: too many wires). `ledmx.c` (two
  74HC595s on D25/D26/D27, 1 kHz timer), `viz.c` (the "Mix" picture: falling notes + beat count) and its test are in
  git at `007f8a5`; D25/D26/D27 are free again. `docs/matrix-ideas.html` (9 picture ideas) stays. Likely successor:
  the 32x8 MAX7219 board (Homebox, both passed `esp32-hw-checks` check 4 on 25/26/27, 2026-11-09), not decided.
- `docs/how-scan-works.html`: animated explainer of the row-by-row scan and the ghost (local page).
- `docs/wiring.html`: **the wiring doc**, every DevKit pin and what goes on it
  (local page, open in a browser; not published). Update it with every
  wiring change, in the same step as the wiring (2026-11-07 the matrix went onto the piano without it until the
  user asked "did you add the matrix in our wiring?"). 2026-10-22: the 16 toy lines on the MCP23017 (`EFn` → `Bn`,
  `ABn` → `An`), the 10 kΩ pull-ups gone, all keys checked by the user. Screen (TFT 1.69",
  HSPI: SCL D14, SDA D13, CS D15, RES D5, DC D4, BLK open) works, shows
  `ui.c` (sound, volume, octave, beat, loop, DEMO, PC link); the user checked
  it. Knob: CLK D18, DT D19, SW D23, + on 3V3. Pots: wipers on D32 (beat), D33 (metronome volume; the newest loop layer until 2026-11-06), D34 (live keys), D35 (filter), VP (reverb), VN (metronome tempo), all ADC1 (2026-11-06); LED strip data on TX2 (GPIO17), V on 3V3. The amp is off the wiring doc (2026-11-04, not used; it was planned on D26/D25/D27).
- The BLE-MIDI version (NimBLE, `ble_midi.c`) is in git at `923500c`.
- **The four checks** (from mukklet, `FIRMWARE_PLAYBOOK.md`): `tools/test.sh`
  (host tests in `test/`, gcc + ASan/UBSan, Unity from `$IDF_PATH`),
  `tools/format.sh --check`, `tools/lint.sh` (clang-tidy, its own
  `build/clang` tree), `tools/size_check.sh` (33% flash free, 2026-11-04). Format and lint only see git-tracked files: `git add` new
  ones first. **Source `~/esp/esp-idf/export.sh` in the same shell first**: without `$IDF_PATH` test.sh and lint.sh
  exit 1 with no failing test named (2026-11-08 a commit went out on "test 1 / lint 1" read too late; rerun in an
  exported shell, both 0). `tools/test.sh` runs one binary per test file and exits 1 on any
  failure: judge it by its exit code, not `| tail` (2026-10-25/26 a `tail` showed
  the last file's OK and hid two `test_matrix` failures until CI caught them;
  the matrix tests use the real `matrix_diodes`, so changing it can break them).
  `.github/workflows/ci.yml` runs them on every push to
  https://github.com/Grigoriym/esp32-piano (public since 2026-10-22, like the
  siblings). First run green 2026-10-13. `tools/piano_bridge.c` is formatted, not
  linted or tested. Right after a push, `gh run list -L1` still shows the
  previous run for a few seconds: wait until the new commit's run is listed
  before `gh run watch`.
- Firmware: ESP-IDF in C, like the siblings. No Arduino.

### Traps found here
- **An effect pot turned fast froze the song** (2026-11-09, wobble to max): each step of an NRPN pot is 20 messages
  (keys + DEMO channels, copied to the 3 chick channels), every 30 ms while turning, each twice over WiFi; the WiFi
  queue fell over 1 s behind, and the bridge took each late WiFi packet for a toy restart (offset > 1 s) and dropped
  its clock sync, 806 times in 13 s, so the beat and loop froze. The bridge now resets only when the toy's clock goes
  back over 10 s (a real restart). The flood itself is still there (core 0 up to 91%, `SCAN stalled` ~1300 times);
  the dot matrix task added ~35-45% on core 1 (was 2-5%).
- **Settings sent before FluidSynth was routed were lost** (2026-11-08, the user: layers sounded wetter than played):
  the bridge said HELLO ~1 s before `pc_synth.sh` connected it to the synth, the toy's link-up burst (reverb, filter,
  bend range, programs) went nowhere, so live keys played with the preset's reverb and each new layer with the pot's
  +100%. The bridge now holds HELLO until a `FLUID` client reads its port and for its first 3.5 s (the toy resends only
  after 3 s without HELLO). Check after a service start: the journal's `synth connected: saying HELLO` comes after
  `toy ... -> synth`, and the toy log shows `reverb N` after it.
- **A burst of MIDI over WiFi lost its tail** (2026-11-08, the cable garbled so WiFi carried all): RESET's ~40
  messages x 2 copies filled lwIP's buffers, `sendto` failed (ENOMEM, unchecked) and the last ones never arrived, Hold's
  pedal-up among them (notes rang on after RESET; seen in `aseqdump`). The WiFi sender now retries 1 ms apart, up to 20
  times, and logs `WiFi send failed`. Checked with the cable up only; over WiFi alone not yet.
- **The cable can come up garbled**: 2026-11-04 the bridge read only junk (mostly 0xF8)
  for a whole session, so everything went over WiFi (`playing what comes over WiFi`,
  never `the cable`) and WiFi stalls made the hiccups. A reset of the board (a pyserial
  open) and a service restart fixed it; cause unknown. Again 2026-11-04 ~21:00: the bridge
  logged `cable: gone`, ~3 min later the cable reopened while the board restarted, and it
  read junk until a `systemctl --user restart chicco-piano` (no board reset needed). Again 2026-11-08 (junk from the
  service start, WiFi carried everything and lost RESET's tail, see the burst trap); a flash + service start fixed it. Check
  `build-host/toy-serial.log` is readable text.
- **The service holds the toy's serial port (exclusive) since 2026-11-03: stop it to
  flash or capture** (`systemctl --user stop chicco-piano`, then start it again).
  esptool then fails with a busy port instead of garbage. The bridge opens the port
  without resetting the board (RTS dropped before DTR; pyserial resets it). The
  console is at 921600: `tools/serial_log.py` reads at that rate, the ROM's `rst:`
  lines (115200) come out garbled, so the app prints `reset reason: N` itself.
- **This PC runs ufw, which drops UDP that isn't a reply.** The toy must
  answer from the port the HELLO went to (21928); sending from a second socket
  (another source port) made every packet vanish with no error on either side.
- **A recording plays back wrong: `tools/loop_check.py record`** (2026-11-08, the user: "sometimes I have problems
  with recordings and you cannot find a problem"). Start it, the user records and lets it play 3 passes, Ctrl+C or
  the timeout; it lists each note of the take that is missing, moved, cut short, longer or extra in each pass
  (`tools/loop_check.py <capture.mid>` checks a saved capture). It reads the toy's marks on channel 16 (`main.c`:
  CC 118 = what the looper did * 8 + layers, CC 119 = a pass start at its own time; FluidSynth ignores them), so a
  capture from before 2026-11-08 has none. A take is checked in the passes before the next take starts. The same
  comparison runs in the host tests: `test_take.c` plays a real take (Echo + Octave, held notes, 130 bpm) through
  fx -> take -> looper and checks 3 passes; it failed on the 350 ms bug. Add a test case there for each new
  recording bug, from its capture.
- **What the user played: capture it, don't guess it.** 2026-11-06 a riff given as key names got a guessed
  rhythm, wrong ("mine is different"); `arecordmidi` while the user played, then `tools/midi_dump.py`, read it
  right first time (notes against the metronome's beat-1 clicks, 16ths from its tempo). Same for saving a loop:
  capture a few passes of it playing. **"Save my song" means a riffs-page piece** (the user, 2026-11-06: a `.mid` in
  `songs/` alone isn't something to reproduce from). What the capture lacks is in `build-host/toy-serial.log`: each
  layer's sound (`MIDI  program N`), its fader/filter/reverb (`layer N keeps keys K, filter F, reverb R`), and which key
  made each note (`DOWN  key k` followed by `MIDI  note on n`; in EDM mode the note alone doesn't give the key or the
  octave). Line a layer up against the pass start (layer 1's first note) and check it against the beat-1 clicks too:
  2026-11-06 they differed by 86 ms (a style switch had moved the beat). **A service restart (and so every flash)
  truncates `toy-serial.log`**: 2026-11-08 Song #2's style button and layer programs were gone that way and had to
  be assumed; copy the log before restarting if a loop may be saved. **Flashing erases the loop on the toy**
  (RAM only): capture it first if the user may want it.
- **Key tests: serial for what the grid reads, the PC side for what plays.**
  The user checks by ear: start the service before asking for presses
  (2026-10-25: a serial capture ran while the user waited for sound, and caught
  only stray buttons).
  Every serial capture resets the board (CP2102 auto-reset) and may start with
  stale `rst:` lines. `~/.espressif/python_env/idf*_env/bin/python
  tools/serial_log.py <s> "DOWN|UP |GHOST"` (from mukklet, port `/dev/ttyUSB0`,
  run in the background, the user presses after ~5 s) prints every debounced
  change with its crossing (`key 5 (EF4+AB0)`), **before** the ghost hold-back:
  a key missing there was never read (contact), a key there but silent was held
  back (ghost). It prints only at the end; SIGTERM ends it early and keeps the
  output (2026-10-22). What was sent: `aseqdump` subscribed to "Chicco Piano",
  through `stdbuf -oL` when written to a file (block-buffered otherwise, lost
  on kill).
- **Which pad is which key: bridge its two combs with a metal tip** during a
  serial capture (USB on, toy batteries out, 3.3 V only): the log names the
  key. Found key 5 that way (2026-10-22); key 1 is at the end away from
  `9105B` (`docs/MATRIX.md`).
  aseqdump shows order, not timing. Timing as the PC gets it: `timeout -s INT 40
  arecordmidi -p 128:0 -b 60 -t 1000 out.mid` (1 tick = 1 ms), then the drum
  note-ons' distance from the step grid (2026-11-03; a style switch mid-capture
  changes the tempo, cut it off before fitting).
- **A whole row or column of keys dead = check that line's jumper first.**
  2026-10-13: keys 25–32 silent, D25 (`AB3`) had come off the breadboard.
  Check it by continuity from the DevKit pin to the slot pad while wiggling
  the wire.
- **Chord tests: `aseqdump` alone, no serial capture.** It doesn't reset the
  board, and on 2026-10-25 the serial capture returned stale presses nobody
  made while aseqdump was right. Notes → key numbers at the default octave
  (key = note − 52): `timeout 60 stdbuf -oL aseqdump -p "Chicco Piano" |
  awk '/Note/{for(i=1;i<=NF;i++)if($i=="note")n=$(i+1);gsub(",","",n);printf
  "%s%d ",($3=="on"?"+":"-"),n-52}'` (background, the user presses at once).
  A fake key shows as a `+k -k` pair inside a chord.
- **Open back shell = flaky chords**: 2026-10-25, 7+8+16 dropped keys in and
  out with the key board hanging free; with the back panel closed it played
  clean 14 times out of 14. Test chords with the back closed.
- **After handling the key board, recheck every key**: 2026-10-25 the 4-wire
  ribbon's wire 4 (`AB3`, keys 25–32) came off its key-board solder point,
  then key 5 went silent: its diode had come loose (the user resoldered both).
- **Keys that need a hard press (14, 21, 23, 30 on 2026-10-13) are the key
  board's seating, not the contacts or the firmware.** Pressed, they read
  510–570 Ω across their DevKit pins (key 13, which works, the same), open at
  rest; a hard press always registers. The user moved the board and it
  changed. After the user reseated it, all 32 played with a normal press
  (2026-10-13); key 20 still felt a bit off but registered. 2026-10-22: key
  14 plays alone, with 5 or with 8, but with 5+8 held only on a very hard
  press (serial log: no DOWN at all). Nothing electrical joins them, so it's
  the seating again; reseat when the toy is open next.
- **Three keys on corners of a grid rectangle never play together** (no
  diodes: 5+8+13 lights 16 too, `matrix.c` holds both back). Key 5 has a
  diode since 2026-10-23: 5+8+13 no longer lights 16 (serial log). Other rectangles still ghost. By design;
  the cheat sheet's chord voicings avoid it.
- **A whole column reads "always pressed" (every key on it DOWN at boot) =
  its pull-up goes to GND, not 3V3.** (The setup before the MCP23017.) 2026-10-21: `EF0`–`EF3` on D34-VN, the
  breadboard's + rail was tied to GND; D34 read 0 V. Measure the pin against
  the DevKit's own GND pin, not a rail (a rail can be split or miswired).
- **Screen with CS on GND and RES on 3V3: backlight only, no picture**
  (2026-10-21, SPI mode 0, wiring checked by continuity). With CS on D15 and
  RES on D5, as on mukklet, it works. Untried: CS on GND with SPI mode 3
  (what CS-less ST7789 boards usually need). Not needed since the MCP23017
  freed the pins (2026-10-22).
- **Diode mode reads the black bars too**: at the meter's test current a
  ~300 Ω bar adds ~0.3 V, so a path reading "two diodes" may be one diode plus
  bars. The chip's real current is tiny, so the decisive check is a chord by
  ear (a fake key that should be blocked). 2026-10-26: key 24 → key 8's 1.209 V
  was one diode (0.614 V across its legs) plus 590 Ω of bars, not two diodes as
  noted the day before. **Bars or diode: read Ω both ways** (same value both ways
  = only bars; a diode reads OL one way). Same few hundred mV both ways in diode
  mode means the same.
- **The small holes in the key board are cut factory links**, not vias: a short
  line between two lines with a hole drilled through it (likely the plating bus,
  inference). Dead spots: don't probe or solder there.
- **A new `ui_state_t` field also goes in `screen.c`'s `same()`**, or the screen redraws it only
  when something else changes (2026-11-06: the metronome header waited for key presses; the user caught it,
  no host test can: `screen.c` isn't host-built).
- **Check a MIDI message on FluidSynth by rendering before building on it** (`fluidsynth -ni -F out.wav ...`
  + a level/centroid measurement): 2026-11-06 that showed CC74 does nothing and CC91 is inaudible here before any
  firmware used them.
- **Every button is live**: a press of VOLUME− (or a style button) during a
  test can make the toy go quiet. RESET restores the defaults.
- **A bad `cat.sf2` fails silently**: `pc_synth.sh` throws FluidSynth's output away and
  the program falls back to FluidR3's (19 = organ). 2026-10-31 that happened with an odd
  INFO string; what caught it was comparing renders of the program with and without the
  file. To see the error: `fluidsynth -ni -F out.wav <FluidR3> build-host/cat.sf2 x.mid`.
  Measuring a meow's pitch: scale the window and the search range with the note; a
  fixed window read a correctly tuned note 58 cents off, and F3 as its third harmonic.
- **Testing the pages from here:** the Chrome extension needs Brave open and refuses
  `file://` (serve `docs/` on 127.0.0.1); `brave-browser --headless` hangs here even on
  about:blank; `firefox --headless --screenshot` worked until 2026-11-05, when it hung twice
  (also with its own `--profile`; the user's Firefox may have been open); on 2026-11-06 it worked again with
  `--profile <scratchpad dir> --no-remote` (~10 s a shot). It shoots right after load: for a canvas page, feed it data
  and call its resize and draw synchronously first, or it catches an empty frame. `docs/live.html` was checked that
  way with a recorded event stream replayed into a copy of the page. A copy of the page with a
  test script appended can POST its results to a small local server. Cheap check that always
  works: extract the `<script>` and `node --check` it.
- **Flashing failed once on each of the last 3 flashes, 2026-10-31** (`No serial data
  received`, `Serial data stream stopped`); each retry ended in `Done`. Cause unknown,
  the cable or USB connection not checked. Read the `Done`, it isn't a given here.
  2026-11-06 it failed twice in a row (`Serial data stream stopped`, then `The chip stopped responding`);
  `idf.py -p /dev/ttyUSB0 -b 115200 flash` worked first time. Later that day 2 of 6 flashes at 115200 failed too
  (`FAILED: CMakeFiles/flash.util`, once mid-write with the board still booting the new hash); a second
  `flash` ended in `Done` each time.
- esptool prints `Detected crystal freq 15.44 MHz ... Unsupported crystal`
  on this DevKit; flashing and boot work regardless.
- FluidSynth fed `noteon` commands on stdin stayed silent; a small `.mid`
  file played. Audio output: the G733 headset was the default; on 2026-10-13 PipeWire
  played through the onboard `analog-stereo` sink.
- **FluidSynth's `audio.period-size` sets the whole desktop's PipeWire
  quantum** (the graph runs at the smallest any client asks). At 64 the sink
  ran at 64 samples and all desktop audio (the music player too) crackled under
  CPU load, 174 xruns 5 min after boot (2026-10-24, found by the system
  session). Now 256 (`pw-top` shows the sink at 256 while it runs). Don't go
  back below 256; if latency matters, try 128 and check `pw-top`'s ERR column.

## Milestones
0. **Teardown and map** (no changes to the toy). Does it work as bought?
   Open it; photos of both sides of every board into `docs/teardown/`.
   Find: the sound chip, the key contacts (rubber domes on PCB pads? a
   membrane?), how keys and buttons reach the chip (connector, ribbon, how
   many lines), diodes or not, the battery wiring and on/off switch, the
   speaker (impedance printed on it?). Write the matrix map to
   `docs/MATRIX.md` (line → keys), add the physical facts to the toy's
   Homebox entry.
1. **Decide with the user**: direction A, B or both; keep the sound chip as
   a fallback or remove it; how the ESP32 reaches the matrix (cut the
   lines from the chip, or lift the connector); power (toy's batteries
   through a regulator, or USB).
2. **Key scan on the breadboard**: the ESP32 scans the matrix and prints
   key down/up events over serial. Every key and button verified, chords
   included (ghosting check).
3. **Sound / MIDI**: whichever direction was chosen. Direction A done
   2026-10-13 (MVP: every key and button over WiFi to FluidSynth).
4. **Inside the toy**: power, mounting, the case closed, the battery life
   checked.

## Decisions
- 2026-10-11: the sound chip goes out of the loop. Its upright daughterboard
  on the main board gets desoldered and kept (bagged, can go back in); the
  ESP32 drives the key and button lines itself at 3.3 V. Reason: the chip
  runs straight from the batteries (VDD = battery voltage) and stops working
  at 3 V, so listening in on it would need dividers on every line.
  Direction A / B (MIDI or own synth) is still open.
- 2026-10-12: direction A first (BLE-MIDI to a synth on the PC), B (own
  synth) after A works. FluidSynth + `FluidR3_GM.sf2` installed.
- 2026-10-13: WiFi instead of Bluetooth (the user's call). BLE dropped the
  link now and then and every reset popped up connect/disconnect
  notifications (blueman's ConnectionNotifier); the siblings use WiFi and
  never had that. Custom UDP + a PC bridge rather than RTP-MIDI: no extra
  package, and the bridge gives a fixed ALSA port.
- 2026-10-17: milestone 4 started. Direction B too: the toy's own speaker
  (8 Ω 1 W) on the free I2S amp module. Every comfortable GPIO is taken by
  the scan, so `EF` columns move to input-only pins (external 10 kΩ
  pull-ups to 3V3), freeing outputs for I2S. Done 2026-10-21 with 4 columns
  (D34/D35/VP/VN); the freed pins went to the screen, see 2026-10-21.
  Power: NiMH AAs in the toy's own battery box (cleaned, no corrosion),
  charged outside; LiPo + charger module kept as a later option.
  Supply: one fixed-5 V buck-boost (4 NiMH run from ~5.8 V fresh to ~4.0 V,
  across 5 V; the amp takes 3.3-5 V) feeding the ESP32's VIN/5V and the
  amp's VCC. Board in the toy: the DevKit already in use is allowed, the
  ESP32 mini isn't required (the user, 2026-10-17).
  Printing, if needed: mukklet's `enclosure/` and the
  `enclosure-preprint-review` skill (`../agentic-grappim/skills/`).
- 2026-10-21: a screen (the user's idea): the free TFT 1.69" (the bigger one,
  as on mukklet, not the 0.96" OLED), showing sound, volume, octave, beat,
  loop, DEMO and the PC link. It took 5 pins (SCL/SDA/DC/CS/RES), so the amp
  has only D2 left and needs a pin decision first: CS on GND with SPI mode 3
  (untried, frees D15/D5) or an MCP23017 (the 16 key lines over I2C: 4 wires
  to the slot instead of 17, frees 16 pins; ~3-4 ms scan instead of 2; not
  in Homebox, a purchase). Battery and audio postponed (the user).
- 2026-10-22: the MCP23017 it is (the user ordered one). SPI mode 3 with CS
  on GND not tried. When it arrives: Homebox entry and an `esp32-hw-checks`
  test first (new module), then the key lines move to it. Done 2026-10-22:
  the check found it at 0x20 with all 16 inputs high (on the DevKit, not the
  WROVER), then every key played through it.
- 2026-10-22: WALTZ is the Balkan button (the user's pick): accordion, beat
  `BALKAN` (čoček 2/4, 126 bpm, tempo by guess, not yet judged by ear). RESET
  and boot give full volume (127), the user's ask.
- 2026-11-03: the PC stays the sound source (the user, asked about an on-chip synth to
  kill WiFi hiccups). Fix the link instead: the code keeps both WiFi and the USB cable
  (the user: "we should be able to use both"), the cable is the one in use.
- 2026-11-03: the user can't play piano and wants to make simple electronic music
  (loops, layers, a song), not learn piano: `docs/learn-piano.html` went, the riffs
  page took its place. Making parts easier (a quantizing looper, an on-the-beat bass)
  was declined: "no need to make it easier"; timing feedback on the screen was wanted.
- 2026-10-30: the ESP32 and screen go in a separate printed box, not inside the
  toy (replaces that part of the 2026-10-17 plan). The wires leave through a hole
  that's already in the toy's back, can be widened (the user). They're soldered,
  ~13 cm, to a temporary custom board with female pins; length can change. Base: mukklet's case v2
  (`../esp32-mukklet/enclosure/`, same TFT; it took two prints, its lessons are
  in the model). A KY-040 knob changes screens, as on the siblings (the user):
  wired 2026-10-30 on CLK D18, DT D19, SW D23 (mukklet's D25/D27 are the amp's
  here). Turning switches Status / Play (`ui.c`; `chord.c` names the held keys,
  host-tested); a press is only logged. The rest is a draft the user wants kept
  loose ("we can always edit"): `enclosure/README.md` and `enclosure/enclosure.scad`
  (MCP23017 at the toy, 4 wires to the box, the free ESP32 mini, amp left out).

## Next step
**2026-11-09:** the 8x8 matrix is off the piano and out of the code (built and checked, not yet flashed: the toy
still runs the matrix firmware until the next flash). ADS1115: one board passed `esp32-hw-checks` check 5 (0x48,
A0-A3 each 0-3242 mV); the other 4 not checked one by one (the user). In `docs/wiring.html` since 2026-11-09: on
D21/D22 beside the MCP23017 (ADDR open = 0x48), VDD 3V3, A0-A3 = 4 effect pots (B10K, the user's pick over layer
faders: "just add what you suggested, we will improve/change afterwards"): resonance, echo, release, wobble
(`mixer.h`, `fx.h`; `main.c` reads one input per pot read, single-shot). Built, checks pass, not flashed, not
heard. Render-checked first: release 60 ms-1.9 s on the saw lead, Q peaks at the cutoff but drops the level (filter
-2400 cents + Q 480 cB: rms 5183 -> 703), vibrato swing 47 -> 285 cents. Their moves during a take aren't recorded.
A second ADS1115 (#2, ADDR on VDD = 0x49, one of the 4 boards not checked one by one) takes beat, keys, filter and
reverb (A0-A3, the user's ask, 2026-11-09); only the metronome's two pots stay on ADC1 (D33, VN); D32, D34, D35, VP
are free. `main.c` steps both boards every 2 ms (`ADS_PERIOD_US`), one input each: a pot every ~8 ms at best, slower
when a scan pass is long. Built, checks pass, not flashed. If #2 misreads: run `esp32-hw-checks` check 5 on it alone on
the bus (the check reads the first board it finds at 0x48-0x4B).

**State 2026-11-08 (late, end of session):** the user asked what the toy is: a loop-based groovebox; a full song is
performed from one loop by muting and fading layers. Built and flashed: the Layers screen (knob walks the layers, press
mutes at the next bar line; the user used it), BLUE BIRD = undo. Fixed: the bridge said HELLO before FluidSynth was
routed, so the toy's settings were lost at each service start (Traps). Saved the user's loop as riffs #8 Song #2.
Next: the ADS1115 pots as one fader per layer (when they arrive: Homebox entry, `esp32-hw-checks` test first). Later
idea told the user: several loops (scenes, verse/chorus) switched on a bar line. Not done: `loop_check.py` knowing
mutes; the knob's turn direction still unchecked.

**State 2026-11-08 (end of session):** RESET's Hold-off was lost over WiFi (burst trap, fixed). A loop now records
the chicks' notes (Echo, Hold, Octave, Arp, Stutter; `take.c`); the first try cut held notes to 350 ms, fixed and
caught by the new end-to-end test. `tools/loop_check.py` + the toy's channel-16 marks: first run on the toy, a 40-note
take over 38 passes, OK. The user went through chicks 1-3 (Echo, Hold, Octave explained); 4-8 still to go. Hold on
piano works but only lengthens the decay (~0.5 s -> ~2 s, rendered): the user first heard no difference. Ideas
offered: Echo's fade on a pot (ADS1115) or the knob; Wide, Rise, Tape in recordings (they bend or copy the whole
channel, a layer has one).

**State 2026-11-07 (end of session):** the matrix is on the piano and shows "Mix" (falling notes + beat count),
flashed; the user played keys ("seems to work"), the beat rows not confirmed. Looper fixes flashed, not yet judged:
a note pressed just before the loop's start rings on (it was cut at the wrap: a held 2.7 s note played 9 ms,
found by capturing the loop and lining it up with the beat), and a layer records pot 3-5 moves made during its take.
Cheat sheet trimmed to buttons and knobs. Unresolved: the user hears a recorded bass "play 3 times normally and cut
the beginning the 4th time" (Open items); the live page "does not work" for the user (Open items). Offered, not
answered: the "Chicco Piano" menu entry also opening the live page.

**State 2026-11-06 (night, end of session):** flashed, not yet tried by the user: the looper's multiply (a layer
recorded over several passes grows the loop; the user's ask, to have a part and then a pass of silence), a style
button while the beat plays changes only the keys (the user noticed the loop break when DISCO/SLOW ROCK moved the
beat), the Debug screen, the chicks' row on every screen and the chicks numbered the user's way (1-4 top row from the
left; Echo was on chick 8). New: `docs/live.html` (live piano roll, started with the service; not yet seen by the user
in a browser), riffs #7 Song #1 (four layers, read off captures and the log). Ideas offered, not taken up: WiFi off
while the cable works (the chip runs warm, mostly the radio with power save off), SELECT starting the beat on a
playing loop's bar line (Open items), finding the slow key scan (Open items).

**State 2026-11-06 (late, end of session):** new and flashed, not yet tried by the user: the chicks are 8 effects
(`fx.c`; the user checks them next), a loop layer keeps pot 3's fader, a pot left alone needs 2 levels to move (the
tempo flipped 130/131), the header shows the tempo while the metronome is off. Tried and fine: a loop recorded to the
metronome alone stays on it (captured: 3693 ms a pass at 130). The pots are named by the user's sections now (Volume:
beat, keys; Effects: filter, reverb; Metronome: tempo, volume). **Next: the 8x8 LED matrix** (Homebox: single colour,
common anode, Freenove; the kit drives it with 2x 74HC595 + 8x 220R, one column at a time, its tutorial chapter 16).
Plan told the user: the two 74HC595s on D25/D26/D27 (free since the amp went), so it stays off the keys' I2C bus; a
visualizer of what's played (ideas: falling notes by pitch, beat pulse, layers as bars). First an `esp32-hw-checks`
test; the matrix's pinout and where the resistors sit come from the kit's chapter 16, not from a photo. A spare
MCP23017 (I2C, would share the scan's bus) and a MAX7219 (not owned) were the alternatives.

**State 2026-11-06 (end of session):** 6 pots, all flashed and wired: 1 beat, 2 metronome volume, 3 live keys
(CC11, held notes follow), 4 filter, 5 reverb (huge room, the user's pick C), 6 metronome tempo. Filter and reverb
touch only live playing; a loop layer keeps what it was recorded with (the user). FROG = metronome, shown in every
screen's header with a flashing dot (fixed after the user saw it update only on key presses: `same()` in screen.c).
Not yet confirmed by the user: the header dot after that fix. The user
ordered 5 ADS1115 boards (16-bit, 4 channels, I2C; listing photos match the usual breakout, chip marking unread) for
more pots: when they arrive, a Homebox entry (user's OK) and an `esp32-hw-checks` test first, then on the piano's
I2C (D21/D22, beside the MCP23017 at 0x20). VDD on 3V3, not 5 V (its 10k SCL/SDA pull-ups go to VDD). Up to 4
boards by ADDR (0x48-0x4B). #6 Cosmic's rhythm: fixed later that day (the user's own).

**State 2026-11-05 (end of session):** two new EDM buttons: SLOW ROCK = techno (130), ROCK =
dubstep (140 half-time, the growl bass "E" picked by ear from 7 rendered variants). Riffs page:
#3 Warehouse, #4 Bunker (Berlin, aggressive), #5 Riddim Drop; not yet played through by the user.
LEDs turned (beat on strip LEDs 7, 8, 1, 2). Pot bar only for a 3-level turn. The user's taste:
heavy dubstep drops (Skrillex, "heaviest dubstep drops"), drum & bass interesting; a D&B beat
(~174) on another button was offered, not started. The toy's RECORD LED: for later (Future).

**State 2026-11-04 (end of session):** the hiccups are gone (root cause: the bridge's
blocking mDNS lookup, see the MIDI-link notes). New: 3 pots as a mixer and the 8-LED
strip (wired, flashed, every pot reads its full range). Not yet tried by ear: the beat and
keys pots while playing (pot 2's layer job is gone since 2026-11-06). Open: pot 3's
noise (Open items). The amp is off the wiring doc (not used).

**State 2026-11-03 (end of session):** the toy is a small groovebox now: EDM mode on
DISCO (four on the floor) and MARCH (breakbeat), both 124 bpm, keys in A minor, the
synth drum kit; the looper keeps time with the beat and stacks 4 layers (knob press, BLUE BIRD since 2026-11-08, =
undo); a Groove screen shows timing; MIDI goes over the USB cable and WiFi at once.
`docs/riffs.html` lists two songs (#1 on DISCO, #2 on MARCH), the user played #1 through
and found #2's fixed bass fine. The user continues tomorrow: what with, not said (ask).
Ideas offered and not taken up yet: more EDM sounds per layer (bass, pad, lead), a knob
effect (filter sweep) / drop button, 2 more knobs (a second MCP23017 on the same I2C,
the user's later topic), saving a loop on the toy. Not checked by ear yet: the cats and
swapped sounds (2026-10-31). The box plan below still stands.

State on 2026-10-13 (end of session): **MVP done.** Every key and button
reaches FluidSynth over WiFi (the service starts from the app menu, on demand
since 2026-10-24), DEMO plays the Megalovania intro. Latency (estimate from parts, 2026-10-13): ~10-20 ms
key to sound (scan+debounce 4-6, WiFi one way ~3 typ / ~8 worst by ping,
FluidSynth+PipeWire at 64 samples 3-5); the user finds it fine. Since
2026-10-24 the buffer is 256 samples (~5 ms a period, so a few ms more);
not yet judged by ear.
- Done 2026-10-13: the board was reseated and all 32 keys play with a normal
  press (key 20 feels a bit off). Recheck if keys go hard again.
- Looper (RECORD/REPLAY) done 2026-10-13, checked on the toy. In time with the beat and
  with layers (overdub) since 2026-11-03: host-tested and flashed, not yet tried by the user.
- SELECT drum beats done 2026-10-13, checked on the toy (sequence on the PC
  side; the user listened: fine). TEMPO+/- stay octave, each beat has a fixed
  tempo. A loop recorded over a beat isn't lined up with it (could come later).
- Milestone 4 under way (decisions 2026-10-17 above). Four checks: done
  2026-10-13.
- Measured 2026-10-17, all in `enclosure/pages/caliper.html` (same shape as
  mukklet's caliper guide; mark a step `done` with its value as numbers come
  in, part sizes also go to Homebox). The gist: 26 mm between the main board
  and the back shell's floor (12 front + 14 back), 14 mm tall capacitors, so
  12 mm over them. Two flat strips on the back shell floor, between the
  speaker and the round boss and between the boss and the battery box,
  ~36 × 117 each; the 37 mm next to the long rib lands over the key board
  (not measured there), the other 80 mm over the main board. The back shell
  closes as a left-right mirror: measure both shells from the keys-side wall.
- Layout sketch (`enclosure/pages/layout-sketch.jpg`, drawn from those
  numbers): amp under the speaker, ESP32 mini above the battery box, a short
  I2S + power bundle between them. Amp is 11.13 tall with its upright
  headers: solder wires to the pins (a Dupont plug adds ~14), or unsolder the
  headers (the user is fine with that).
- The old chip board's tabs sit in slots, not holes; its pads are 2.52 apart
  (1st to 8th 17.63), so an 8-pin 2.54 header strip soldered onto each slot
  pad group is 0.15 off over 8: fine. Replaces 17 loose wires.
- **Milestone 4 steps: `docs/MILESTONE4.md`**, one at a time, ticked with
  date and result. Step 1 (buck-boost) waits for the user to order one from
  the shortlist there; the user will say when it's in hand.
- **Diode on key 5: done 2026-10-23.** A 1N4148 in key 5's `EF4` line on the
  key board's bottom area (the pad area lies on the rubber strip, no soldering
  there): key 5's own part is pad → black bar → copper box B at the bar's
  lower end → one line left to key 13's box; cut just before key 13's box
  (after: 5–13 OL, 13–21 391 Ω), diode across, stripe on the key 5 side.
  Serial: 5+8+13 pressed 13 times, key 16 never lit; all 32 keys register.
  `matrix.c` knows (`matrix_diodes`): a rectangle corner whose diagonal corner
  has a diode is played, the others held back, so 5+8+13+16 pressed at once
  plays only 16 (the user heard it; by design: 5, 8, 13 could each be a fake).
  Per-key findings: `docs/key-board-map.html` (local page; `KEYS`/`LOG` data at
  the top of its script, each fact tagged measured / seen / photo / open).
  The user is thinking about all 32 diodes (then any chord plays; velocity
  stays impossible: one contact per key).
- **More diodes, 2026-10-25:** only keys whose bottom-comb line runs outside the
  screwed-down part of the key board can get one (2 and 9 can't). Key 1 done
  (cut on the bottom-edge stretch near key 1, 0.761 V from key 9's side in
  diode mode). Key 8 done (its own line, before key 16's box): 7+8+16 clean
  14/14. Rule for every key: diode in the bottom comb's own stretch, before
  another key's line joins it, stripe toward the pad. Keys 6 and 7 done
  2026-10-26 (meter only: 0.6 V across each, ~1.0-1.1 V from key 14 / 15,
  OL swapped). Keys 30, 31, 32 done 2026-10-27 (meter: about 0.6 V across each).
  `matrix_diodes` has 1, 5, 6, 7, 8, 30, 31, 32 (host tests pass; flashed
  2026-10-30; the user played it reassembled: "keys seem to be fine"). Chords
  not confirmed one by one. Still to do, by ear with the back closed: 7+8+15 and 6+8+14 with no fake 16,
  8+15+16 holding notes back instead of a fake 7, and 6+8+30, 7+8+31, 5+8+29
  with no fake 32. In `matrix_diodes` key k is row `(k-1)/8`, bit `(k-1)%8`.
  **Key 16 has no diode** (2026-10-26; the 2026-10-25 notes wrongly said "8 and
  16 soldered", and a plan to "move 16's diode" was built on that; re-measuring
  at the user's suggestion found it). Its bottom comb goes pad → bar → the box
  where key 8's line arrives: no copper carries 16 alone. Possible later: a
  diode on its other contact (a diode works on either contact; there the stripe
  points toward the shared line), or key 8 moved off the box. The user: skip
  such keys, do the easy ones.
  Next keys: any the user can reach (the user found 26–32 easy, 2026-10-27). Key k's diode makes
  k + any key in k's row + any key in k's column play clean (21 three-key
  chords per key).
  What the 8 diodes change (calculated 2026-10-30 from the grid and
  `matrix_ambiguous`, not played one by one): 4462 of 4960 three-key sets clean,
  4288 without diodes. Blocked shapes are those with two keys 8 apart plus one in
  a same row: major 1st inversion `k k+3 k+8`, minor 2nd inversion `k k+5 k+8`,
  augmented, and 7ths built on them. Those now work only at: `1 4 9`, `5 8 13`,
  `22 25 30`, `23 26 31`, `24 27 32` (major); `1 6 9`, `22 27 30`, `23 28 31`,
  `24 29 32` (minor). Every other close major or minor voicing plays anywhere.
- **State 2026-10-30 (end of session):** the toy is reassembled and played fine with
  all 8 diodes flashed; the knob and Play screen work (the user saw them). Next: the
  box. The draft is in `enclosure/README.md`; the user builds the cardboard mock-up
  (`enclosure/cardboard.py`, the notch is cut by hand) and tries it behind the toy.
  Soldering the 16 lines to the MCP23017, the 4-wire cable and the move to the mini
  come after. The amp is set aside (the user: "forget about the amp for now").
- **State 2026-10-22 (end of session):** the 16 toy lines run through the
  MCP23017, every key checked by the user; the amp passed its check on the
  WROVER. Next: the amp on the piano (wire it per `docs/wiring.html`, then
  milestone 4 step 4: I2S + a synth voice), then the NiMH batteries (step 1,
  the buck-boost, still to be bought).
- **The amp:** tested 2026-10-22 in `esp32-hw-checks` (tone plays, see
  `docs/MILESTONE4.md` step 2); on the piano: next (pins free since 2026-10-22). Chips printed on the board: ES7148 (DAC) + PAM8403 (amp);
  speaker outputs are headers `L+ L- R+ R-` (read off a sharp photo; the
  Homebox `Pins` field lists only the I2S/line pins). PAM8403 outputs are
  bridged: the speaker goes on `L+`/`L-`, neither to GND. VCC 3.3-5 V per the
  board, so feed it from a regulated rail, not straight from the batteries.
- Deferred: where the chip slot lands under the back shell (decides wire
  length; check by holding the back shell over the front one). Firmware
  updates with the case closed (OTA vs a USB hole), mounting.

Future improvements:
- The toy's own LED next to RECORD as a recording light (the user, 2026-11-05, for later). Its
  path from the old chip was never measured; the toy has 2 LEDs, the other lights straight from
  the batteries. Guess, not measured: the slot's `CD0` (the one non-key pin besides power/audio).
  When the toy is open: batteries out, a sharp photo of the main board at the LED's legs, then
  beep each leg to the slot pads (`CD0` first). If it reaches a pad, drive it from D25 through
  the slot (check for a series resistor first). Instead of a separate plain LED (not needed:
  the strip shows everything).
- A custom key board with a diode on all 32 keys (the user's idea, 2026-10-30,
  parked): the current board can't take them all. Same grid, the old board stays
  as a fallback. The hard part is the fit (pads under the domes, screw holes,
  outline), not the circuit. First: a 1:1 flatbed scan, the clearance behind the
  key board, what's on the domes' underside.

Open items:
- **The key scan runs every ~7 ms, not 2** (Debug screen / `STATS`, 2026-11-06, the toy idle): a pass's work is
  ~5.3 ms (worst 7.7) and core 0 ~60% busy, so debounce is ~21 ms, not ~6. Not the I2C clock (already 400 kHz).
  Not looked into: time each part of the pass (the 8 MCP23017 row writes + reads, the pots' 8-sample ADC reads,
  `show()`, the sequencers) with `esp_timer_get_time()` around each, printed in the STATS line. The user hasn't
  noticed lag.
  Worse since the beat or metronome runs (2026-11-07, matrix on): idle core 0 66-67%, pass ~6.4 ms (worst ~11),
  scan gap ~12 ms; once FROG/SELECT start, core 1 (the screen) jumps 2% -> ~25%, the pass to ~7.8 ms (worst ~20-27)
  and the longest gap to ~22 ms; `SCAN stalled` >20 ms was logged ~1600 times in one session. Whether the matrix
  (1 kHz timer, `gpio_set_level` x50 a column) or the screen redrawing on every click/step causes it: not measured.
  A key needs 3 equal scans, so ~60 ms with 20 ms gaps: a very short tap could be lost.
- Which way the knob turns isn't checked: with five screens (Status, Play, Groove, Layers, Debug)
  it now matters. Pressing it mutes a layer on the Layers screen (2026-11-08; it was undo, now BLUE BIRD).
- SELECT starts the beat at the press, not on a playing loop's bar line: stop the beat
  under a loop recorded with it and start it again, and they drift apart (seen in the
  code, 2026-11-03, not reported by the user). The song plans use REPLAY for the
  breakdown so the beat never stops. A fix: start the beat at the loop's next bar line
  (the looper would keep its bar length).
- **Pots jitter at rest, then stopped** (2026-11-04): not one bad pot. Pots 1 and 2 left
  mid-travel both jumped ~7 levels (pot 2 ~18 changes a second, 86-95); a pot turned to an
  end reads steady (clamped), which is why only pot 3 looked noisy at first. The jitter kept
  the LED bar up for good (no beat count). Not the LED strip (V unplugged: same jitter).
  Then the bridge lost the cable for ~3 min (20:57, `cable: gone`), the board restarted and
  came back garbled (service restart fixed it); since then every pot is still, with or
  without the beat and a loop playing (not WiFi load either). Guess, not proven: a bad USB
  contact making 3V3 noisy. If the bar sticks again: count `POT` lines over 10 s, hands
  off, then wiggle the USB plug. No cap on pot 2 since 2026-11-09: without its 1 uF cap it flipped 13<->14 ~23 times a second at rest; the user
  swapped the pot instead and the new one read still (12 s, no POT line from any pot). So that pot was bad, at least then.
  2026-11-05, again (board flashed ~1 h before, the user had turned pot 2): hands off, pot 2
  at ~92 hopped 89-95 (~90 changes / 10 s), at ~64 hopped 61-66 (68); pot 1 at ~58 at the same
  time only 55-59 (~15), its hops not at the same moments as pot 2's. A board restart (the user)
  cleared it: pot 2 at 70, 0 changes in 10 s. Twice now a restart ends it; what starts it isn't known.
  The cable came up garbled after that restart (service restart fixed it, see Traps).
  Later 2026-11-05: back with a loop + beat after turning pots; a reset from the PC (serial_log.py,
  nobody touching) cleared it too, so not only handling. Beat alone (no loop): no jitter.
  `tools/pot_watch.py` (background) stamps the log into `build-host/pot-watch.log` and flags
  `JITTER START/END` (>=6 reversals within 6 levels in 3 s). Caught: 20:19 pot 2 at ~27, ~11 s
  after a turn, loop + beat on; 20:26 pot 2 87-93 for ~4 s and the other two +-1 at the same
  moments while the user moved the modules and all wires (then settled). Suspect, not proven:
  the LED strip's current spikes on the shared 3V3/GND (a WS2812 runs on its data line with V
  unplugged, so the 2026-11-04 V-only test didn't rule it out). Test when the bar is stuck: pull
  the strip's V and data both.
  The log has no timestamps: take a window by byte offset (`stat -c %s`, `timeout 10 tail -f`,
  then `tail -c +offset | grep -a POT`), or read `build-host/pot-watch.log` (pot_watch.py).
  Symptom handled 2026-11-05 (flashed, not yet judged by the user over a session): a key press
  tipped resting pot 3 a level (118<->119 in step with DOWN/UP lines) and lit its white bar; now
  each read is the mean of 8 and the bar needs a 3-level move (`MIXER_TURN`), so 1-2 level hops
  no longer hold the bar up. The noise itself (key press / WiFi / screen load on 3V3?) is not
  found; if the bar sticks again, the hops are bigger than 2 levels: check pot-watch.log.
- The 2026-11-03 cable/WiFi winner flips and the 94 ms slow key: not seen again with the
  cable clean (2026-11-04, only `the cable` won); the 2-minute lookup stall
  explains slow keys like it. Closed unless they come back. **Back 2026-11-07:** the bridge flipped to `WiFi` and back
  every ~5-30 s for minutes (cable output still readable); not looked into. Again 2026-11-08 (20:55, every ~10 s).
- **"Plays 3 times normally, the 4th time cuts the beginning"** (the user, 2026-11-07, a 2-bar bass = #6 Cosmic's,
  on SLOW ROCK with the metronome): `arecordmidi` of the bridge's output showed 16 identical passes, each first note
  on the metronome's beat 1, nothing late in the bridge log. The user is sure something is wrong ("you see nothing,
  but there is an error somewhere"). Not checked: what comes out of the speakers. Next time: `pw-record` the sink for
  a minute while it happens and compare the start of every pass in the audio (FluidSynth voices, xruns).
  2026-11-08 a similar report ("it should have played without interruptions") was a 4-bar loop whose bar 4 held
  one note (MIDI and the headset's audio both regular); the next one ("the end notes get cut") was real, the 350 ms
  echo bug, found only by comparing the take with its playback. Next time: `tools/loop_check.py record` first.
- The live page works for the user (2026-11-08, opened at http://127.0.0.1:21929/; the 2026-11-07 "does not work"
  was never explained). Its trembling is fixed: the "now" line under the roll wrapped with many notes and resized
  the canvas several times a second (a pitch-range fix alone didn't change it). Still offered, not answered: the
  "Chicco Piano" menu entry also opening the page.
- FluidSynth renders #1 at `pc_synth.sh`'s gain 1.0 ~9 dB over full scale; whether the
  live output clips too isn't checked (`render_riffs.py` uses 0.3).
- **Fixed 2026-11-06 (a style button no longer touches a playing beat): a style switch while a loop plays moved the
  beat off the loop** (seen in a capture, the user noticed it too): DISCO (124) then SLOW ROCK (130) during Song #1, each switching at the beat's own bar line, left the beat and
  the metronome 86 ms (3/4 of a 16th) after the loop's start; a layer recorded then (layer 4) is on the beat, the older
  three are early. Fix idea: while a loop plays, a new beat starts on the loop's bar lines.
- No way to save a loop on the toy: a song is captured on the PC (`songs/README.md`)
  and the capture holds no program changes, so its sounds are assumed.
- The 1N4148 from the 2026-10-22 test in the `AB0` wire (`docs/wiring.html`,
  "Diode test"): out or still in? Not confirmed. Two diodes in series on key 5
  (~0.9 V) may exceed the MCP23017's input-low limit; key 5 played on
  2026-10-23, so either it's out or it's fine. Update the wiring doc either way.
- Homebox toy entry (needs the user's OK): the key board is modified, one trace
  cut and 1N4148s on keys 1, 5, 6, 7, 8, 30, 31, 32 (2026-10-23 to 27).
- Homebox, needs the user's OK: the WROVER's port is `/dev/ttyUSB0`, CH340
  (`udevadm` ID_MODEL `USB_Serial`; the DevKit's is `CP2102_USB_to_UART...`),
  the entry still says "port not checked".
- Homebox, TFT 1.69" (needs the user's OK): the CS-on-GND result (blank with SPI mode 0).
- Homebox, toy entry (needs the user's OK, not asked yet): key board
  314 × 29 (25 wide from 191 on); removed chip board 75.75 × 15, 11.47 tall,
  three 8-pad tabs, pads 2.52 apart.
- Main board marking `9105C` is read off a photo only; read it on the board
  next time the main board is unscrewed (not worth opening it just for this).
- Button domes: white rubber with a black carbon pill (user's photos
  2026-10-13, not measured); not in Homebox yet. Low value (every button
  already works); seeing a dome needs the toy open. Pressed resistance can be
  read from the breadboard without opening: DevKit USB unplugged, DEMO =
  GPIO17 + GPIO33, Ω mode.
