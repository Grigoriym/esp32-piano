#include "matrix.h"

#include <string.h>

/* Key 5 (EF4+AB0), 2026-10-23: in the bottom comb's line, stripe toward the key. */
const grid_t matrix_diodes = {[0] = 1u << 4};

void matrix_init(matrix_t *m)
{
    memset(m, 0, sizeof(*m));
}

static bool same(const grid_t a, const grid_t b)
{
    return memcmp(a, b, sizeof(grid_t)) == 0;
}

bool matrix_feed(matrix_t *m, const grid_t scan, grid_t prev)
{
    if (same(scan, m->candidate)) {
        m->equal_count++;
    } else {
        memcpy(m->candidate, scan, sizeof(grid_t));
        m->equal_count = 1;
    }
    if (m->equal_count != DEBOUNCE || same(m->candidate, m->stable)) return false;
    memcpy(prev, m->stable, sizeof(grid_t));
    memcpy(m->stable, m->candidate, sizeof(grid_t));
    return true;
}

bool matrix_has_rectangle(const grid_t g)
{
    for (size_t a = 0; a < N_ROWS; a++) {
        for (size_t b = a + 1; b < N_ROWS; b++) {
            uint8_t common = g[a] & g[b];
            if (common & (common - 1)) return true;
        }
    }
    return false;
}

bool matrix_ambiguous(const grid_t g, size_t r, size_t c)
{
    for (size_t r2 = 0; r2 < N_ROWS; r2++) {
        if (r2 == r || !(g[r2] & (1u << c))) continue;
        if ((g[r] & g[r2]) & ~(1u << c) & ~matrix_diodes[r2]) return true;
    }
    return false;
}

void matrix_sync(matrix_t *m, matrix_event_fn fn, void *ctx)
{
    for (size_t r = 0; r < N_ROWS; r++) {
        for (size_t c = 0; c < N_COLS; c++) {
            uint8_t bit = 1u << c;
            bool is = m->stable[r] & bit;
            bool was = m->sent[r] & bit;
            if (was && !is) {
                m->sent[r] &= ~bit;
                fn(r, c, false, ctx);
            } else if (is && !was && !matrix_ambiguous(m->stable, r, c)) {
                m->sent[r] |= bit;
                fn(r, c, true, ctx);
            }
        }
    }
}
