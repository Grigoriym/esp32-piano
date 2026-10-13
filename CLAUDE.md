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
- `main/main.c`: GPIO scan (rows `AB0`–`AB7` pulled low one at a time,
  columns `EF0`–`EF7` read), serial log, glue. The logic is in pure files
  (no ESP-IDF includes, host-tested): `matrix.c` (debounce 3 scans, ghost
  rectangles; a press that may be a ghost is held back), `keymap.c` (names
  and what every key and button plays), `demo.c` (DEMO: Megalovania intro,
  channel 2, 100 bpm, velocity 70). They send through `midi_out.h`'s
  callback. The line-pair finder that mapped the grid is in git (`233c8c3`).
- `main/wifi.c` (from mukklet, power save off: it holds packets up to a
  beacon interval) and `main/midi_link.c`: MIDI over UDP, protocol in
  `main/midi_link.h`. The toy is `chicco-piano.local` (mDNS, port 21928); the
  PC sends `HELLO` once a second, the toy sends each MIDI message twice with a
  sequence number back to where the HELLO came from.
- `tools/piano_bridge.c` (PC, links `-lasound`): owns the ALSA port
  "Chicco Piano" (stays while it runs, toy on or off), says HELLO, drops the
  copies. `tools/pc_synth.sh` builds it into `build-host/`, starts it with
  FluidSynth and keeps the route between them.
- `tools/chicco-piano.service`: systemd user service running `pc_synth.sh`,
  installed (symlink in `~/.config/systemd/user/`, enabled 2026-10-13). Log:
  `journalctl --user -u chicco-piano`. The path in it is hardcoded to
  `~/proj/grappim/esp32-piano`.
- `docs/cheatsheet.html`: what every key and button does, the Megalovania
  opening as key numbers, a metronome. Published as a private artifact:
  https://claude.ai/artifact/Ttss3MnAfWboWeEaJDS2zC (republish from this file
  after a mapping change).
- The BLE-MIDI version (NimBLE, `ble_midi.c`) is in git at `923500c`.
- **The four checks** (from mukklet, `FIRMWARE_PLAYBOOK.md`): `tools/test.sh`
  (host tests in `test/`, gcc + ASan/UBSan, Unity from `$IDF_PATH`),
  `tools/format.sh --check`, `tools/lint.sh` (clang-tidy, its own
  `build/clang` tree), `tools/size_check.sh` (46% flash free by CI's count,
  2026-10-13). Format and lint only see git-tracked files: `git add` new
  ones first. `.github/workflows/ci.yml` runs them on every push to
  https://github.com/Grigoriym/esp32-piano (private; the siblings are
  public). First run green 2026-10-13. `tools/piano_bridge.c` is formatted, not
  linted or tested.
- Firmware: ESP-IDF in C, like the siblings. No Arduino.

### Traps found here
- **This PC runs ufw, which drops UDP that isn't a reply.** The toy must
  answer from the port the HELLO went to (21928); sending from a second socket
  (another source port) made every packet vanish with no error on either side.
- **Key tests: read the PC side, not serial.** Every serial capture resets
  the board (CP2102 auto-reset) and comes back with a screenful of stale
  `rst:` lines before the real boot log. For keys: `aseqdump` subscribed to
  "Chicco Piano", through `stdbuf -oL` when written to a file (block-buffered
  otherwise, lost on kill). Serial only for boot checks after a flash.
- **A whole row or column of keys dead = check that line's jumper first.**
  2026-10-13: keys 25–32 silent, D25 (`AB3`) had come off the breadboard.
  Check it by continuity from the DevKit pin to the slot pad while wiggling
  the wire.
- **Keys that need a hard press (14, 21, 23, 30 on 2026-10-13) are the key
  board's seating, not the contacts or the firmware.** Pressed, they read
  510–570 Ω across their DevKit pins (key 13, which works, the same), open at
  rest; a hard press always registers. The user moved the board and it
  changed. After the user reseated it, all 32 played with a normal press
  (2026-10-13); key 20 still felt a bit off but registered.
- **Every button is live**: a press of VOLUME− (or a style button) during a
  test can make the toy go quiet. RESET restores the defaults.
- esptool prints `Detected crystal freq 15.44 MHz ... Unsupported crystal`
  on this DevKit; flashing and boot work regardless.
- FluidSynth fed `noteon` commands on stdin stayed silent; a small `.mid`
  file played. Audio output: the G733 headset was the default; on 2026-10-13 PipeWire
  played through the onboard `analog-stereo` sink (27 xruns in `pw-top`; raise
  `audio.period-size` to 128 if it crackles).

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

## Next step
State on 2026-10-13 (end of session): **MVP done.** Every key and button
reaches FluidSynth over WiFi, the service starts at login, DEMO plays the
Megalovania intro. Latency (estimate from parts, 2026-10-13): ~10-20 ms
key to sound (scan+debounce 4-6, WiFi one way ~3 typ / ~8 worst by ping,
FluidSynth+PipeWire at 64 samples 3-5); the user finds it fine.
- Done 2026-10-13: the board was reseated and all 32 keys play with a normal
  press (key 20 feels a bit off). Recheck if keys go hard again.
- Ideas the user hasn't picked from yet: RECORD/REPLAY as a looper, SELECT
  (e.g. cycle DEMO songs or a drum pattern), direction B (own synth on the
  I2S amp into the toy's speaker, no PC needed), then milestone 4 (inside
  the toy: power, mounting, battery life). Four checks: done 2026-10-13.

Open items:
- Main board marking `9105C` is read off a photo only.
- BlueZ still knows the toy's old BLE identity (`70:4B:CA:4D:F0:1E`);
  harmless, `bluetoothctl remove` when the user agrees.
- Button domes: white rubber with a black carbon pill (user's photos
  2026-10-13, not measured); not in Homebox yet.
