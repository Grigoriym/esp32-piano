#!/usr/bin/env bash
# Flash budget: fails when the firmware leaves less than MIN_FREE_PCT (default
# 15%) of its app partition free. The build itself only fails at 100%; this
# warns while there's still room to react (bigger partition, -Os, dropping a
# library). The APK-size-budget of this repo. Run after `idf.py build`.
set -euo pipefail
cd "$(dirname "$0")/.."

: "${IDF_PATH:?source ~/esp/esp-idf/export.sh first}"
MIN_FREE_PCT=${MIN_FREE_PCT:-15}
BIN=$(ls build/*.bin | grep -v -e bootloader -e partition | head -1)
TABLE=build/partition_table/partition-table.bin
[ -f "$BIN" ] && [ -f "$TABLE" ] || { echo "no build output: run 'idf.py build' first" >&2; exit 2; }

python "$IDF_PATH/components/partition_table/gen_esp32part.py" "$TABLE" 2>/dev/null \
    | python3 -c '
import os, sys
units = {"K": 1024, "M": 1024 * 1024}
def size(s):
    s = s.strip()
    return int(s[:-1], 0) * units[s[-1]] if s[-1] in units else int(s, 0)
apps = [size(r.split(",")[4]) for r in sys.stdin if r.strip() and not r.startswith("#") and r.split(",")[1] == "app"]
part = min(apps)  # the smallest app slot is the one that has to fit
app = os.path.getsize(sys.argv[1])
free = part - app
pct = 100 * free / part
print(f"firmware {app // 1024} KB of {part // 1024} KB app partition: {free // 1024} KB ({pct:.0f}%) free, budget {sys.argv[2]}%")
sys.exit(0 if pct >= float(sys.argv[2]) else 1)
' "$BIN" "$MIN_FREE_PCT" || { echo "flash budget exceeded (partition choice: sdkconfig.defaults)" >&2; exit 1; }
