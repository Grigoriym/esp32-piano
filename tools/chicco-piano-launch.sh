#!/usr/bin/env bash
# Run by tools/chicco-piano.desktop: starts (or restarts) chicco-piano.service,
# linking the unit first if it isn't; with "stop", stops it. Never enables it.
set -u
if [ "${1:-}" = stop ]; then
    systemctl --user stop chicco-piano && msg="Synth and bridge stopped"
else
    systemctl --user cat chicco-piano >/dev/null 2>&1 ||
        systemctl --user link "$(dirname "$(realpath "$0")")/chicco-piano.service"
    systemctl --user restart chicco-piano && msg="Synth and bridge started"
fi
notify-send -i audio-x-generic "Chicco Piano" "${msg:-Failed, see journalctl --user -u chicco-piano}"
