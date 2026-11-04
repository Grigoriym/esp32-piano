#!/usr/bin/env bash
# PC side of direction A: FluidSynth, the WiFi
# bridge (tools/piano_bridge.c, ALSA port "Chicco Piano") and the route
# between them. Checked once a second, so a restarted synth gets routed again.
# Run by tools/chicco-piano.service, started on demand by the app-menu launcher
# (never at login); by hand: tools/pc_synth.sh (Ctrl+C stops).
# period-size 256 (~5 ms at 48 kHz): PipeWire runs the whole graph at the
# smallest quantum any client asks for, and 64 made all desktop audio crackle.
set -u
cd "$(dirname "$0")/.."
SF2=${SF2:-/usr/share/sounds/sf2/FluidR3_GM.sf2}
# The toy's DevKit on the USB cable (CP2102): MIDI comes that way too when plugged
# in. The bridge holds it exclusively: stop the service to flash.
CABLE=${CABLE:-/dev/serial/by-id/usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_Controller_0001-if00-port0}

if [ ! -x build-host/piano_bridge ] || [ tools/piano_bridge.c -nt build-host/piano_bridge ]; then
    mkdir -p build-host
    gcc -O2 -Wall -o build-host/piano_bridge tools/piano_bridge.c -lasound -lpthread || exit 1
fi

CAT=build-host/cat.sf2
if [ ! -f "$CAT" ] || [ tools/gen_cat_sf2.py -nt "$CAT" ] || [ -n "$(find tools/sounds -name '*.wav' -newer "$CAT")" ]; then
    python3 tools/gen_cat_sf2.py "$CAT" || exit 1
fi

KIT=build-host/edm_kit.sf2
if [ ! -f "$KIT" ] || [ tools/gen_edm_kit_sf2.py -nt "$KIT" ]; then
    python3 tools/gen_edm_kit_sf2.py "$KIT" || exit 1
fi

fluidsynth -a pipewire -m alsa_seq -g 1.0 -is \
    -o audio.period-size=256 -o audio.periods=2 -o midi.autoconnect=0 \
    "$SF2" "$CAT" "$KIT" >/dev/null 2>&1 &
SYNTH=$!
build-host/piano_bridge chicco-piano.local "$CABLE" &
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
# One of the two died: fail, so systemd restarts the pair.
exit 1
