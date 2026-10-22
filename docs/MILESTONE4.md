# Milestone 4: amp and batteries

One step at a time, top to bottom. Mark a step `[x]` with the date and the
result when it is done.

## Power and sound on the breadboard
- [ ] 1. **Pick and order a 5 V buck-boost module.** It has to cover 4 NiMH
      AAs from fresh to empty (~5.8 V down to ~4.0 V) on its input. Output
      fixed 5 V, enough for the DevKit (~350 mA peak, Homebox) plus the amp
      into the 8 Ω speaker. It feeds both the DevKit's VIN and the amp's VCC.
      Check NiMH AAs are on hand too (not in Homebox).
      Shortlist (2026-10-17): Pololu S13V20F5 (2 A, first choice; EXP-Tech
      €14.86), S13V15F5 (1.5 A), S13V10F5 or S9V11F5 (~1 A, tight at the end
      of a charge). Step-down-only (Mini560, MP1584, LM2596) and step-up-only
      (MT3608) boards don't work: NiMH runs from above 5 V to below it.
- [x] 2. **Amp test in `esp32-hw-checks`** (new module, rule): DevKit on
      USB, amp VCC from the DevKit, speaker on `L+`/`L-` (bridged output,
      neither to GND), plays a tone. Add the result to the amp's Homebox
      entry (with the user's OK).
      Done 2026-10-22 on the Freenove WROVER (the DevKit stays on the piano):
      BCK 26 / LCK 25 / DIN 22, SCK open, VCC 3.3 V, the kit's 8 Ω 2 W
      speaker on L+/L-. Tones play; 10% of full scale is quite loud at 3.3 V.
- [x] 3. **Move `EF` columns to the input-only pins** with 10 kΩ pull-ups
      to 3V3. Done 2026-10-21 with 4 columns: `EF0`–`EF3` on D34/D35/VP/VN
      (`docs/wiring.html`). All 32 keys checked by the user; buttons not
      rechecked one by one. The freed pins went to the screen (CLAUDE.md,
      decision 2026-10-21), so the amp has only D2 left. Replaced
      2026-10-22: all 16 lines on an MCP23017 (I2C, D21/D22), no pull-ups;
      every key checked by the user.
- [ ] 4. **Amp on the piano firmware**: I2S on 3 pins (pick them first:
      BCK D26 / LCK D25 / DIN D27, `docs/wiring.html`), a simple
      synth voice for the keys (direction B), MIDI over WiFi kept. How the
      two are chosen (setting, button): decide at this step.
- [ ] 5. **Battery power test**: NiMH → buck-boost → DevKit VIN + amp VCC,
      USB unplugged. Measure the current (idle, playing loud), watch for
      brownout resets.
- [ ] 6. **Battery life**: full set of charged cells, play until it stops,
      note the hours.

## Into the toy
- [ ] 7. Board choice: the DevKit (already wired, 51.49 × 28.36) or the
      ESP32 mini; check it against the flat strips (~36 × 117).
- [ ] 8. Permanent wiring: 8-pin header strips on the old chip slot pads,
      ESP32, amp, buck-boost, on/off switch.
- [ ] 9. Firmware updates with the case closed: OTA or a USB hole.
- [ ] 10. Mounting, case closed, everything retested.
- [ ] 11. Homebox: parts taken (`In use`), the toy's description updated
      (each with the user's OK).
