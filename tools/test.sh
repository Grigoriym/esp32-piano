#!/usr/bin/env bash
# Builds and runs the host (PC) unit tests in test/ with gcc: the pure logic
# from main/ (matrix, keymap, demo), no ESP32 needed. Same shape as mukklet's.
# Needs IDF_PATH (for the Unity test framework ESP-IDF ships).
set -euo pipefail
cd "$(dirname "$0")/.."

: "${IDF_PATH:?source ~/esp/esp-idf/export.sh first (Unity comes from ESP-IDF)}"
UNITY="$IDF_PATH/components/unity/unity/src"

OUT=build/host_tests
mkdir -p "$OUT"
CFLAGS=(-std=gnu17 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer
        -Imain -Itest -I"$UNITY")

# test_<name>.c tests main/<name>.c.
failed=0
for t in test/test_*.c; do
    name=$(basename "$t" .c)
    gcc "${CFLAGS[@]}" "$t" "main/${name#test_}.c" "$UNITY/unity.c" -o "$OUT/$name"
    echo "== $name"
    "$OUT/$name" || failed=1
done
exit $failed
