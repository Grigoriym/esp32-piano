# esp32-piano: new brains for a toy keyboard

## Concept
A Chicco toy keyboard (flea market, 2026-10-11) gets an ESP32 inside that
reads its keys and buttons. What it does with them is **not decided yet**,
the user picks once the toy is opened (milestone 1):
- **A. MIDI controller**: the ESP32 sends the keys as BLE-MIDI, and a synth
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
(4 MB flash) plus NimBLE (BLE only) and a 1000 Hz tick (the key scan runs
every 2 ms). First build in a fresh checkout: `. ~/esp/esp-idf/export.sh &&
idf.py set-target esp32 && idf.py build`. Flash with the `esp32-flash-verify`
skill. After changing `sdkconfig.defaults`: `rm sdkconfig` and build again.
- `main/main.c`: matrix scanner (rows `AB0`–`AB7` pulled low one at a time,
  columns `EF0`–`EF7` read, debounce 3 scans) and the key/button → MIDI
  mapping (comment at the top). Presses that may be a ghost are held back.
  The line-pair finder that mapped the grid is in git (`233c8c3`).
- `main/ble_midi.c`: BLE-MIDI peripheral "Chicco Piano", one notification per
  MIDI message.
- `tools/pc_synth.sh`: the PC side. FluidSynth (pipewire, small buffer) plus
  a once-a-second check that routes the toy's ALSA port to it (the port is
  recreated on every Bluetooth reconnect). Run it in the background.
- No tests, CI or the four checks yet.
- Firmware: ESP-IDF in C, like the siblings. No Arduino.

### Traps found here
- **BlueZ's default supervision timeout (420 ms) dropped the BLE link** every
  few seconds (NimBLE disconnect reason `0x208`; bluetoothd then logs
  "notifications not enabled ... unlikely error" / "Failed to read initial
  request", which are symptoms, not the cause). Fixed by the toy asking for
  4 s right after connecting (`ble_gap_update_params`).
- **DevKit on the USB hub: the link still dropped about once a minute**
  while playing; on the PC's own port, none in 4 min (2 idle, 2 playing).
  Keep it off the hub.
- **Every serial capture resets the board** (CP2102 auto-reset; setting
  DTR/RTS false before opening didn't prevent it), so it drops the BLE link
  and the user sees connect/disconnect notifications. With the BLE firmware
  the captures also came back as repeated stale chunks and half boot lines;
  ruled out: another process on the port (`fuser` empty), kernel USB errors
  (none). For key tests read the PC side instead (`aseqdump -p "Chicco
  Piano"`), no reset. Serial only for boot checks after a flash.
- Paired with `bluetoothctl` (pair fails with `ConnectionAttemptFailed`, but
  `trust` + `connect` works and BlueZ creates the MIDI port; no bond needed).
- FluidSynth fed `noteon` commands on stdin stayed silent; a small `.mid`
  file played. Default audio output is the user's G733 headset.

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
3. **Sound / MIDI**: whichever direction was chosen.
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
  synth) after A works. This PC: Bluetooth adapter present, bluetoothd built
  with MIDI support; no synth installed yet (FluidSynth/Qsynth + a
  soundfont, the user installs with sudo).

## Next step
State on 2026-10-12 (end of session):
- Milestones 0 and 2 done: every key and button mapped (`docs/MATRIX.md`),
  scanner with names, chord check (ghosting on 3 corners of a rectangle).
- Milestone 3, direction A: BLE-MIDI works end to end (toy → Bluetooth →
  `tools/pc_synth.sh` → headset). The user played the Megalovania opening;
  latency "fine for now". Key 1 = F3 (53) is inferred from the black-key
  pattern (3-2-3-2-3 from the left, photo); keys 2 and 9 black confirmed by
  the user.
- **Open: some keys don't sound under a normal press.** Scan sweep (serial,
  keys 1–25 captured): 14, 18, 21, 23 never registered. The user heard
  silence on 14, 18, 22, 24, 27, 31 (22/24 did register in the scan: count
  drift or lost MIDI, unresolved). All of them worked in the mapping run.
  Not the wires: they share lines with working keys. Next: measure key 14's
  contact (Ω across `EF5`–`AB1`, USB out, normal vs firm press) against key
  13 (`EF4`–`AB1`); then a sweep read with `aseqdump`, not serial.
- Earlier weak contacts: chick above WALTZ, key 9 (before the case-squeeze
  fix).

Open items:
- Main board marking `9105C` is read off a photo only.
