# Key matrix map

Status: piano keys measured (2026-10-11): an 8 × 4 grid, every key on exactly one wire of each ribbon. Buttons not mapped yet.

## What is known
- 32 piano keys, one rubber dome each. Under each dome, a round bare-metal
  pad on the key board made of two interleaved combs; the dome bridges them
  (photos: `teardown/05-key-board-flipped-rubber-strip.jpg`,
  `teardown/06-key-board-pads-closeup.jpg`). Key board marked `9105B 1995.7`.
- The key board reaches the main board through two grey ribbons: one with
  8 wires, one with 4 (map below).
- No diodes on the key board: it carries only traces, pads and black
  printed bars. The only diode seen on the main board is D1 (1N4148) next
  to the `SPK` pads, one part, not one per key
  (`teardown/08-main-board-components.jpg`).
- Upright daughterboard (the sound chip's carrier) on the main board: 24
  labels printed next to it, top to bottom, in three groups
  (`teardown/12-upright-board-labels.jpg`):
  - `EF7`, `EF6`, `EF5`, `EF4`, `EF3`, `EF2`, `EF1`, `EF0`
  - `AB7`, `CD0`, `AB6`, `AB5`, `AB4`, `AB3`, `CPUVDD`, `AB2`
  - `AB1`, `AB0`, `VDD`, `OSC`, `RESET`, `AUD1`, `AUD2`, `VSS`
  Solder side: `teardown/11-upright-board-solder-side.jpg`. Which ribbon wire
  lands on which label: to trace.
- Buttons: two small end boards, each with a 3-wire ribbon. Labels at the
  wires: volume board `E3`, `E2`, `A5`; tempo board `E1`, `E0`, `A5`. The
  other buttons: not traced yet.

## Key map
Wire numbering: key board held with its pads up and the ribbons at the
bottom (`teardown/07-key-board-ribbon-solder-points.jpg`); wire 1 is the
leftmost solder point of each ribbon. Key 1 is the leftmost key seen from
the front of the toy. Measured by continuity from the solder point to each
pad's combs. Some connections read a few hundred Ω and don't beep
(wire 3 to key 17: 287 Ω), so a silent wire gets checked in Ω mode. Readings
in the kΩ–MΩ range are not counted as connections: the ribbons are still
soldered to the main board.

### 4-wire ribbon
| Wire | Keys (one comb of each pad) |
|---|---|
| 1 | 1–8 (no beep on 9–32) |
| 2 | 9–16 |
| 3 | 17–24 (about 280 Ω each, no beep) |
| 4 | 25–32 |

### 8-wire ribbon
| Wire | Keys (the other comb) |
|---|---|
| 1 | 1 (beep), 9 (288 Ω), 17 (304 Ω), 25 (about 300 Ω). The trace to 9 visibly runs through a black printed bar |
| 2 | 2, 10, 18, 26 |
| 3 | 3, 11, 19, 27 |
| 4 | 4, 12, 20, 28 |
| 5 | 5, 13, 21, 29 |
| 6 | 6, 14, 22, 30 |
| 7 | 7, 15, 23, 31 |
| 8 | 8, 16, 24, 32 |

In short: key = (4-wire number − 1) × 8 + 8-wire number. Example: key 20 is
4-wire 3 with 8-wire 4.

## Buttons
Measured by continuity at the small board's solder points while pressing.

| Button | Connects |
|---|---|
| Volume − | `A5` – `E3` |
| Volume + | `A5` – `E2` |
| Tempo − | `A5` – `E1` |
| Tempo + | `A5` – `E0` |

## Chip slot pads (sound chip board removed 2026-10-11)
The 24 pads along the empty slot follow the label order top to bottom, on
both sides of the board. Checked by continuity, batteries out: `VSS` (last
pad) beeps to the white battery wire; `VDD` (3rd pad of the bottom group)
reads 79 Ω to the red battery wire with the on/off switch on, open with it
off.

## ESP32 breadboard wiring (milestone 2)
DevKit 30-pin, powered from USB, toy batteries out. GPIOs from
`WIRING_RULES.md` (no flash, UART or strapping pins 0/2/12/15).

Pad order on the copper side, top to bottom (same as the printed labels):
- top group: `EF7`, `EF6`, `EF5`, `EF4`, `EF3`, `EF2`, `EF1`, `EF0`
- middle group: `AB7`, `CD0`, `AB6`, `AB5`, `AB4`, `AB3`, `CPUVDD`, `AB2`
- bottom group: `AB1`, `AB0`, `VDD`, `OSC`, `RESET`, `AUD1`, `AUD2`, `VSS`;
  on the board this group has 7 solder places, `AUD1` and `AUD2` share one

| Toy pad | ESP32 GPIO | DevKit pin label |
|---|---|---|
| `EF0` | 4 | D4 |
| `EF1` | 5 | D5 |
| `EF2` | 13 | D13 |
| `EF3` | 14 | D14 |
| `EF4` | 16 | RX2 (usually; D16 on some boards) |
| `EF5` | 17 | TX2 (usually; D17 on some boards) |
| `EF6` | 18 | D18 |
| `EF7` | 19 | D19 |
| `AB0` | 21 | D21 |
| `AB1` | 22 | D22 |
| `AB2` | 23 | D23 |
| `AB3` | 25 | D25 |
| `AB4` | 26 | D26 |
| `AB5` | 27 | D27 |
| `AB6` | 32 | D32 |
| `AB7` | 33 | D33 |
| `CD0` | 34 | D34, plus a 10 kΩ resistor from D34 to 3V3 (input-only pin, no internal pull-up) |
| `VSS` | GND | GND |

Not connected: `CPUVDD`, `VDD`, `OSC`, `RESET`, `AUD1`, `AUD2`.
