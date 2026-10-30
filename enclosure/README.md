# Enclosure (draft)

A draft to change as we go (the user, 2026-10-30: "we can always edit"). Nothing here
is built, measured for this project or printed.

Starting point: esp32-mukklet's case v2 (`../../esp32-mukklet/enclosure/`), same TFT and
knob, second print, assembled and fine. `enclosure.scad` is its model with one
addition, a cable hole in the back. Sizes come from the parts' Homebox entries.

```
enclosure/export.sh     # clash check + pre-print checks, STLs in stl/enclosure/ (gitignored), renders/
enclosure/cardboard.py  # 1:1 A4 templates of the box, cardboard.pdf (gitignored)
```

## The plan so far
| What | Where | Status |
|---|---|---|
| ESP32 mini, TFT, knob | the box, as on mukklet | proposed |
| MCP23017 | at the toy, near the hole; the 16 lines soldered straight to it | proposed |
| Toy to box | 4 wires (3V3, GND, SDA, SCL), through the toy's back hole, a plug inside the box | proposed; I2C over this cable untested |
| Box position | right behind the toy | proposed |
| Amp | later; not in this draft | the user: forget it for now |

The ~13 cm wires and their female-pin board are temporary (the user), and the hole in
the toy's back can be widened, so neither limits this.

## Pins on the mini (draft)
Mukklet's, already proven on this board and case: TFT SCL 18, SDA 23, RES 17, DC 16,
CS 5, BLK 19; knob CLK 27, DT 25, SW 32. I2C SDA 21, SCL 22. The mini has one 3V3 pad,
shared by the TFT, knob and MCP23017 through a split wire. The firmware's pins change
when the board does (`main/display.c`, `main/encoder.c`, `main/main.c`); the DevKit
pins in `docs/wiring.html` stay until then. The amp's 3 pins get chosen when it comes back.

## Open
- The cable: its real thickness and plug decide `cable_hole` and `cable_z`, both ASSUMED.
- Does a mukklet-sized box have room for the extra 4-wire plug? The model has no stand-in
  for it yet.
- Case v2's lessons apply: `../../grappim-watcher/docs/esp32/ENCLOSURE_PLAYBOOK.md`. Before
  ordering: cardboard mock-up, then the `enclosure-preprint-review` skill.
