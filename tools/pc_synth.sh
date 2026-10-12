#!/usr/bin/env bash
# PC side of direction A: FluidSynth with a small audio buffer, and the toy's
# BLE-MIDI port kept routed to it. The toy's ALSA port is recreated on every
# Bluetooth reconnect, so the route is checked once a second.
# Usage: tools/pc_synth.sh   (Ctrl+C stops it and the synth)
set -u
SF2=${SF2:-/usr/share/sounds/sf2/FluidR3_GM.sf2}

fluidsynth -a pipewire -m alsa_seq -g 1.0 -is \
    -o audio.period-size=64 -o audio.periods=2 -o midi.autoconnect=0 \
    "$SF2" >/dev/null 2>&1 &
SYNTH=$!
trap 'kill $SYNTH 2>/dev/null' EXIT

client() { aconnect -l | sed -nE "s/^client ([0-9]+): '$1.*/\1/p" | head -1; }

echo "synth running; waiting for Chicco Piano (Ctrl+C to stop)"
while kill -0 $SYNTH 2>/dev/null; do
    synth=$(client 'FLUID Synth')
    toy=$(client 'Chicco Piano')
    if [ -n "$synth" ] && [ -n "$toy" ] && ! aconnect -l | grep -A2 "^client $toy:" | grep -q "Connecting To: $synth:0"; then
        aconnect "$toy:0" "$synth:0" && echo "$(date +%T) toy $toy:0 -> synth $synth:0"
    fi
    read -rt 1 <> <(:) || true
done
