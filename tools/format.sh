#!/usr/bin/env bash
# Formats this project's C sources with clang-format (style in .clang-format).
#   tools/format.sh           rewrite files in place
#   tools/format.sh --check   only report unformatted files, exit 1 if any (CI)
# clang-format comes from ESP-IDF's esp-clang tool:
#   python $IDF_PATH/tools/idf_tools.py install esp-clang
set -euo pipefail
cd "$(dirname "$0")/.."

# esp-clang first: a different clang-format version can format differently.
CF=${CLANG_FORMAT:-$(ls "${IDF_TOOLS_PATH:-$HOME/.espressif}"/tools/esp-clang/*/esp-clang/bin/clang-format 2>/dev/null | tail -1)}
if [ -z "$CF" ]; then
    echo "clang-format not found (install esp-clang, or set CLANG_FORMAT)" >&2
    exit 2
fi

# Tracked sources only: skips gitignored *_secrets.h and managed_components/.
mapfile -t files < <(git ls-files 'main/*.c' 'main/*.h' 'tools/*.c' 'tools/*.h' 'test/*.c' 'test/*.h')

if [ "${1:-}" = "--check" ]; then
    "$CF" --dry-run --Werror "${files[@]}"
    echo "formatting OK (${#files[@]} files)"
else
    "$CF" -i "${files[@]}"
fi
