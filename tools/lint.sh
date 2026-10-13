#!/usr/bin/env bash
# Static analysis with clang-tidy (checks in .clang-tidy): the detekt / Android
# lint of this repo. Any finding fails (exit 1).
#   tools/lint.sh              all tracked main/*.c
#   tools/lint.sh main/foo.c   just these files
# Needs IDF_PATH (source ~/esp/esp-idf/export.sh) and ESP-IDF's esp-clang tool.
# clang-tidy needs clang-compatible compile flags, so it uses a separate
# build/clang tree configured with IDF_TOOLCHAIN=clang (configure only, the
# firmware is still built by gcc in build/).
set -euo pipefail
cd "$(dirname "$0")/.."

: "${IDF_PATH:?source ~/esp/esp-idf/export.sh first}"
CT=${CLANG_TIDY:-$(ls "${IDF_TOOLS_PATH:-$HOME/.espressif}"/tools/esp-clang/*/esp-clang/bin/clang-tidy 2>/dev/null | tail -1)}
[ -n "$CT" ] || { echo "clang-tidy not found: python \$IDF_PATH/tools/idf_tools.py install esp-clang" >&2; exit 2; }

# Its own copy of sdkconfig: configuring with clang rewrites the toolchain
# options, and doing that to the shared ./sdkconfig forces a full rebuild of
# the real firmware afterwards.
if [ ! -f build/clang/compile_commands.json ] || [ sdkconfig -nt build/clang/sdkconfig ] \
    || [ CMakeLists.txt -nt build/clang/compile_commands.json ] \
    || [ main/CMakeLists.txt -nt build/clang/compile_commands.json ]; then
    mkdir -p build/clang
    cp sdkconfig build/clang/sdkconfig
    echo "configuring build/clang (first run only, ~20s)..."
    if ! log=$(idf.py -B build/clang -D IDF_TOOLCHAIN=clang -D SDKCONFIG="$PWD/build/clang/sdkconfig" \
        reconfigure 2>&1); then
        echo "$log" >&2
        exit 2
    fi
fi

if [ $# -gt 0 ]; then files=("$@"); else mapfile -t files < <(git ls-files 'main/*.c'); fi
# Drop per-file progress and "N warnings generated" (those are the
# suppressed ones in ESP-IDF headers); findings still show.
status=0
out=$("$CT" -p build/clang --quiet "${files[@]}" 2>&1) || status=$?
grep -vE '^\[[0-9]+/[0-9]+\] Processing|warnings? generated\.$' <<<"$out" || true
[ "$status" -eq 0 ] || { echo "lint FAILED" >&2; exit 1; }
echo "lint OK (${#files[@]} files)"
