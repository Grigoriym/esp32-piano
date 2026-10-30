#!/usr/bin/env bash
# Builds and runs the host (PC) unit tests in test/ with gcc: the pure logic
# from main/ (matrix, keymap, demo, ui...), no ESP32 needed. Same shape as mukklet's.
# Needs IDF_PATH (for the Unity test framework ESP-IDF ships).
set -euo pipefail
cd "$(dirname "$0")/.."

: "${IDF_PATH:?source ~/esp/esp-idf/export.sh first (Unity comes from ESP-IDF)}"
UNITY="$IDF_PATH/components/unity/unity/src"

OUT=build/host_tests
mkdir -p "$OUT"
CFLAGS=(-std=gnu17 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer
        -Imain -Itest -I"$UNITY")

# test_<name>.c tests main/<name>.c, plus any pure sources it calls into.
extra_sources() {
    case "$1" in
        test_font) echo main/font_data.c ;;
        test_canvas) echo main/font.c main/font_data.c ;;
        test_ui) echo main/canvas.c main/font.c main/font_data.c main/chord.c ;;
    esac
}

failed=0
for t in test/test_*.c; do
    name=$(basename "$t" .c)
    # shellcheck disable=SC2046 # word splitting of the list is intended
    gcc "${CFLAGS[@]}" "$t" "main/${name#test_}.c" $(extra_sources "$name") "$UNITY/unity.c" -lm -o "$OUT/$name"
    echo "== $name"
    "$OUT/$name" || failed=1
done
exit $failed
