#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The toy's 8 x 8 grid, debounced, with ghost handling. Pure: no ESP-IDF.
 *
 * The toy has no diodes: three pressed corners of a rectangle make the fourth
 * read as pressed too (ghosting). The fake corner's current runs backwards
 * through the opposite (diagonal) corner. A press that may be that fake
 * corner is held back from the events until the rectangle is gone, unless the
 * diagonal corner is a key we gave a diode (matrix_diodes): that path is
 * blocked, so the press is real. */
#define N_ROWS   8
#define N_COLS   8
#define DEBOUNCE 3 /* equal scans in a row before a change counts */

typedef uint8_t grid_t[N_ROWS]; /* grid[row] bit col: pressed */

/* Keys with a diode soldered into their line (docs/key-board-map.html). */
extern const grid_t matrix_diodes;

typedef struct {
    grid_t stable;    /* debounced */
    grid_t candidate; /* last raw scan */
    grid_t sent;      /* what the events have reported as down */
    int equal_count;
} matrix_t;

void matrix_init(matrix_t *m);

/* Feeds one raw scan. Returns true when the debounced grid changed; then
 * prev holds the grid from before. */
bool matrix_feed(matrix_t *m, const grid_t scan, grid_t prev);

/* Two rows sharing two or more pressed columns: a rectangle, a possible ghost. */
bool matrix_has_rectangle(const grid_t g);

/* (r, c) is pressed together with the other three corners of some rectangle
 * whose diagonal corner has no diode: it may be the fake one. */
bool matrix_ambiguous(const grid_t g, size_t r, size_t c);

typedef void (*matrix_event_fn)(size_t r, size_t c, bool down, void *ctx);

/* Reports every cell where the debounced grid and what was sent differ:
 * releases always, presses unless ambiguous. */
void matrix_sync(matrix_t *m, matrix_event_fn fn, void *ctx);
