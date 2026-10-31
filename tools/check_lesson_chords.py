#!/usr/bin/env python3
"""Check every chord in docs/learn-piano.html against main/matrix.c.

Some key combinations are held back by the toy (no diode on every key), so a
lesson asking for one would wait forever. For each chord: press its keys, add
the fake keys the wiring makes (a diode on the diagonal corner blocks one), run
the real matrix_sync, and require exactly the pressed keys to play.
Run after changing a lesson or matrix_diodes. Exit 1 on any failure.
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
LETTER = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include "matrix.h"
static uint8_t played[8];
static void ev(size_t r, size_t c, bool down, void *x) { (void)x; if (down) played[r] |= 1u << c; }
int main(void) {
    char line[256]; int bad = 0;
    while (fgets(line, sizeof line, stdin)) {
        grid_t g = {0}, real; char *p = line, *bar = strchr(line, '|'); int k, off;
        while (p < bar && sscanf(p, "%d%n", &k, &off) == 1) { g[(k - 1) / 8] |= 1u << ((k - 1) % 8); p += off; }
        memcpy(real, g, sizeof real);
        for (int ch = 1; ch;) { ch = 0;
            for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) { if (g[r] & (1u << c)) continue;
                for (int r2 = 0; r2 < 8; r2++) for (int c2 = 0; c2 < 8; c2++) { if (r2 == r || c2 == c) continue;
                    if ((g[r] & (1u << c2)) && (g[r2] & (1u << c2)) && (g[r2] & (1u << c)) && !(matrix_diodes[r2] & (1u << c2))) { g[r] |= 1u << c; ch = 1; } } } }
        matrix_t m; grid_t prev; matrix_init(&m);
        for (int i = 0; i < DEBOUNCE; i++) matrix_feed(&m, g, prev);
        memset(played, 0, 8); matrix_sync(&m, ev, NULL);
        if (memcmp(played, real, 8)) { bad++; printf("FAIL %s", line); }
    }
    return bad != 0;
}
"""


def midi(name):
    m = re.fullmatch(r"([A-G])([#b]?)(\d)", name)
    return 12 * (int(m[3]) + 1) + LETTER[m[1]] + {"#": 1, "b": -1, "": 0}[m[2]]


def main():
    page = (ROOT / "docs/learn-piano.html").read_text()
    chords = set()
    for score in re.findall(r"score: '([^']+)'", page):
        for tok in score.split():
            names = tok.split(":")[-1].split("/")[0].split("+")
            keys = [midi(n) - 52 for n in names]
            if not all(1 <= k <= 32 for k in keys):
                sys.exit(f"off the keyboard: {tok}")
            if len(keys) > 1:
                chords.add(" ".join(map(str, sorted(keys))) + " | " + " ".join(names))
    with tempfile.TemporaryDirectory() as d:
        src, exe = pathlib.Path(d, "h.c"), pathlib.Path(d, "h")
        src.write_text(HARNESS)
        subprocess.run(["gcc", f"-I{ROOT}/main", "-o", exe, src, ROOT / "main/matrix.c"], check=True)
        r = subprocess.run([exe], input="\n".join(sorted(chords)) + "\n", text=True, capture_output=True)
    print(r.stdout, end="")
    print(f"{len(chords)} chords, {'all play' if r.returncode == 0 else 'some held back'}")
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
