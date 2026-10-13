#!/usr/bin/env bash
# PC side of direction A: FluidSynth with a small audio buffer, the WiFi
# bridge (tools/piano_bridge.c, ALSA port "Chicco Piano") and the route
# between them. Checked once a second, so a restarted synth gets routed again.
# Run by tools/chicco-piano.service; by hand: tools/pc_synth.sh (Ctrl+C stops).
set -u
cd "$(dirname "$0")/.."
SF2=${SF2:-/usr/share/sounds/sf2/FluidR3_GM.sf2}

if [ ! -x build-host/piano_bridge ] || [ tools/piano_bridge.c -nt build-host/piano_bridge ]; then
    mkdir -p build-host
    gcc -O2 -Wall -o build-host/piano_bridge tools/piano_bridge.c -lasound || exit 1
fi

fluidsynth -a pipewire -m alsa_seq -g 1.0 -is \
    -o audio.period-size=64 -o audio.periods=2 -o midi.autoconnect=0 \
    "$SF2" >/dev/null 2>&1 &
SYNTH=$!
build-host/piano_bridge &
BRIDGE=$!
trap 'kill $SYNTH $BRIDGE 2>/dev/null' EXIT

client() { aconnect -l | sed -nE "s/^client ([0-9]+): '$1.*/\1/p" | head -1; }

while kill -0 $SYNTH 2>/dev/null && kill -0 $BRIDGE 2>/dev/null; do
    synth=$(client 'FLUID Synth')
    toy=$(client 'Chicco Piano')
    if [ -n "$synth" ] && [ -n "$toy" ] && ! aconnect -l | grep -A2 "^client $toy:" | grep -q "Connecting To: $synth:0"; then
        aconnect "$toy:0" "$synth:0" && echo "$(date +%T) toy $toy:0 -> synth $synth:0"
    fi
    read -rt 1 <> <(:) || true
done
