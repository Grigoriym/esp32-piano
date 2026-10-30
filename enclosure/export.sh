#!/usr/bin/env bash
# Checks the enclosure model and exports it: fails if any module stand-in
# overlaps a printed part, then writes one STL per part to stl/<name>/
# (gitignored) and preview images to renders/<name>-*.png.
# Usage: enclosure/export.sh [file.scad] [var=value ...]   (needs openscad)
#   enclosure/export.sh                                  -> name "enclosure"
#   enclosure/export.sh enclosure_v2.scad carrier=whole  -> "enclosure_v2-whole"
set -euo pipefail
cd "$(dirname "$0")"
scad=${1:-enclosure.scad}
shift || true
name=$(basename "$scad" .scad)
defs=()
for kv in "$@"; do
    defs+=(-D "${kv%%=*}=\"${kv#*=}\"")
    name+="-${kv#*=}"
done

echo "clash check"
# an empty result makes openscad exit non-zero: that is the pass case
log=$(openscad "${defs[@]}" -D 'part="clash"' -o /tmp/enclosure_clash.stl "$scad" 2>&1 || true)
if grep -qE "WARNING: (Ignoring unknown|undefined operation|Unable to convert)" <<<"$log"; then
    echo "$log" | grep WARNING >&2
    echo "FAIL: model warnings (a variable used before it's defined?)" >&2
    exit 1
fi
if grep -q "ERROR:" <<<"$log"; then
    echo "$log" | grep -E "ERROR:" >&2
    echo "FAIL: a pre-print check in the model failed" >&2
    exit 1
fi
grep -o "CHECKS: .*" <<<"$log" | sed 's/"$//' || true
echo "values not measured (ASSUMED in $scad):"
grep -n "ASSUMED" "$scad" | grep -v "^[0-9]*:// Sizes" | sed 's/^/  /' || true
if ! grep -q "Current top level object is empty" <<<"$log"; then
    echo "FAIL: parts overlap a module stand-in (open /tmp/enclosure_clash.stl)" >&2
    exit 1
fi

# stl/<name>/ holds exactly the parts to print; optional test pieces go in
# optional/. Wiped first, so parts the model no longer has don't linger
rm -rf "stl/$name"
mkdir -p "stl/$name/optional" renders
for part in body front base optional/test_knob; do
    echo "stl/$name/$part.stl"
    log=$(openscad "${defs[@]}" -D "part=\"$(basename "$part")\"" -o "stl/$name/$part.stl" "$scad" 2>&1)
    if grep -qiE "warning|error" <<<"$log"; then
        echo "$log" >&2
        exit 1
    fi
done

render() { # view, camera, extra openscad args...
    local view=$1 cam=$2
    shift 2
    echo "renders/$name-$view.png"
    openscad "${defs[@]}" "$@" -o "renders/$name-$view.png" --imgsize=1200,900 --camera="$cam" "$scad" >/dev/null 2>&1
}
render front 25,41,40,70,0,330,300
render back 25,41,40,60,0,150,300
render inside 25,41,36,50,0,300,260 -D cut=25
render section 25,41,36,90,0,270,250 --projection=o -D cut=25
render exploded 25,41,50,65,0,320,360 -D explode=45 -D explode_front=30 -D show_labels=false
echo OK
