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
  status, how many and which are free), **including the toy itself**: its
  entry in the **Homebox inventory** (`http://192.168.0.139:34899`).
  `python3 ../grappim-watcher/docs/esp32/inventory/parts.py show Chicco`
  prints the toy's entry; plain `parts.py` lists what's free (`all`,
  `find <text>`). Read-only. Check it before suggesting a part or a
  purchase. Writes (new facts, `In use` / `Free` when this project takes a
  part) go through the API one entry at a time with the user's OK, never
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

## Parts likely to be used (all free in Homebox on 2026-10-11, recheck)
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
(4 MB flash, large single app partition) plus a 1000 Hz tick (the key scan runs
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
  channel 2, 100 bpm, velocity 70), `looper.c` (RECORD/REPLAY: a looper that
  starts at the first note; keys replay on channel 3 with the instrument they
  were recorded with, drums on 10; RESET erases it), `rhythm.c` (SELECT: a drum
  beat per style button, one bar of `x`/`X`/`.` steps per drum, switches at the
  bar line, velocity 70, not recorded by the looper). They send through
  `midi_out.h`'s callback. The line-pair finder that mapped the grid is in git (`233c8c3`).
- `main/wifi.c` (from mukklet, power save off: it holds packets up to a
  beacon interval) and `main/midi_link.c`: MIDI over UDP, protocol in
  `main/midi_link.h`. The toy is `chicco-piano.local` (mDNS, port 21928); the
  PC sends `HELLO` once a second, the toy sends each MIDI message twice with a
  sequence number back to where the HELLO came from.
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
- `docs/cheatsheet.html`: what every key and button does, the Megalovania
  opening as key numbers, a metronome, the chords of Sinan Sakić's "Ej, od kad
  sam se rodio" (from chord sites; no melody, none found). Published as a private artifact:
  https://claude.ai/artifact/Ttss3MnAfWboWeEaJDS2zC (republish from this file
  after a mapping change).
- `docs/how-scan-works.html`: animated explainer of the row-by-row scan and the ghost (local page).
- `docs/wiring.html`: **the wiring doc**, every DevKit pin and what goes on it
  (local page, open in a browser; not published). Update it with every
  wiring change. 2026-10-22: the 16 toy lines on the MCP23017 (`EFn` → `Bn`,
  `ABn` → `An`), the 10 kΩ pull-ups gone, all keys checked by the user. Screen (TFT 1.69",
  HSPI: SCL D14, SDA D13, CS D15, RES D5, DC D4, BLK open) works, shows
  `ui.c` (sound, volume, octave, beat, loop, DEMO, PC link); the user checked
  it. Amp (drawn, not wired on the piano yet): BCK D26, LCK D25, DIN D27.
- The BLE-MIDI version (NimBLE, `ble_midi.c`) is in git at `923500c`.
- **The four checks** (from mukklet, `FIRMWARE_PLAYBOOK.md`): `tools/test.sh`
  (host tests in `test/`, gcc + ASan/UBSan, Unity from `$IDF_PATH`),
  `tools/format.sh --check`, `tools/lint.sh` (clang-tidy, its own
  `build/clang` tree), `tools/size_check.sh` (46% flash free by CI's count,
  2026-10-13). Format and lint only see git-tracked files: `git add` new
  ones first. `.github/workflows/ci.yml` runs them on every push to
  https://github.com/Grigoriym/esp32-piano (public since 2026-10-22, like the
  siblings). First run green 2026-10-13. `tools/piano_bridge.c` is formatted, not
  linted or tested. Right after a push, `gh run list -L1` still shows the
  previous run for a few seconds: wait until the new commit's run is listed
  before `gh run watch`.
- Firmware: ESP-IDF in C, like the siblings. No Arduino.

### Traps found here
- **This PC runs ufw, which drops UDP that isn't a reply.** The toy must
  answer from the port the HELLO went to (21928); sending from a second socket
  (another source port) made every packet vanish with no error on either side.
- **Key tests: serial for what the grid reads, the PC side for what plays.**
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
  aseqdump shows order, not timing: tempos and bar-line timing are checked by
  the host tests and by ear.
- **A whole row or column of keys dead = check that line's jumper first.**
  2026-10-13: keys 25–32 silent, D25 (`AB3`) had come off the breadboard.
  Check it by continuity from the DevKit pin to the slot pad while wiggling
  the wire.
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
- **Every button is live**: a press of VOLUME− (or a style button) during a
  test can make the toy go quiet. RESET restores the defaults.
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

## Next step
State on 2026-10-13 (end of session): **MVP done.** Every key and button
reaches FluidSynth over WiFi (the service starts from the app menu, on demand
since 2026-10-24), DEMO plays the Megalovania intro. Latency (estimate from parts, 2026-10-13): ~10-20 ms
key to sound (scan+debounce 4-6, WiFi one way ~3 typ / ~8 worst by ping,
FluidSynth+PipeWire at 64 samples 3-5); the user finds it fine. Since
2026-10-24 the buffer is 256 samples (~5 ms a period, so a few ms more);
not yet judged by ear.
- Done 2026-10-13: the board was reseated and all 32 keys play with a normal
  press (key 20 feels a bit off). Recheck if keys go hard again.
- Looper (RECORD/REPLAY) done 2026-10-13, checked on the toy. No overdub yet.
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

Open items:
- The 1N4148 from the 2026-10-22 test in the `AB0` wire (`docs/wiring.html`,
  "Diode test"): out or still in? Not confirmed. Two diodes in series on key 5
  (~0.9 V) may exceed the MCP23017's input-low limit; key 5 played on
  2026-10-23, so either it's out or it's fine. Update the wiring doc either way.
- Homebox toy entry (needs the user's OK): the key board is modified, one trace
  cut and a 1N4148 on key 5 (2026-10-23).
- Homebox, needs the user's OK: the WROVER's port is `/dev/ttyUSB0`, CH340
  (`udevadm` ID_MODEL `USB_Serial`; the DevKit's is `CP2102_USB_to_UART...`),
  the entry still says "port not checked". The DevKit entry counts 2 in use
  (desk-display, flipclock) and 1 free, without esp32-piano: check whether the
  piano's DevKit is that free one.
- Homebox, TFT 1.69" (needs the user's OK): one more `In use`
  (esp32-piano); the CS-on-GND result (blank with SPI mode 0).
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
