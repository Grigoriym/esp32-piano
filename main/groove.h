#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The Groove screen's timing: where the beat is and where the player's notes
 * land on its steps. Pure: the caller passes the time (host-tested).
 *
 * A press counts for its nearest step, how far off it was is kept as the last
 * offset. Its mark stays about a bar: a step's mark is wiped 2 steps before the
 * beat comes round to it again (a press for a step can come half a step early,
 * so wiping at the step itself would lose it). */
#define GROOVE_MAX_STEPS  16
#define GROOVE_ON_TIME_US 30000 /* within this of its step: on time */

typedef struct {
    int64_t origin_us; /* a bar line */
    int64_t step_us;   /* 0: no beat */
    int steps;         /* in a bar */
    int64_t wiped;     /* the last absolute step whose look-ahead wipe ran */
    uint32_t on_time;  /* bit per step: a press on time there */
    uint32_t off;      /* bit per step: a press early or late there */
    int last_offset_us;
    bool has_last;
} groove_t;

void groove_init(groove_t *g);

/* The beat's grid; step_us 0: no beat. A new grid wipes the marks. */
void groove_set_grid(groove_t *g, int64_t origin_us, int64_t step_us, int steps);

/* A key pressed now. */
void groove_press(groove_t *g, int64_t now_us);

/* Call often: wipes the marks the beat is coming round to. */
void groove_tick(groove_t *g, int64_t now_us);

/* The step playing now, 0..steps-1; -1: no beat. */
int groove_step(const groove_t *g, int64_t now_us);
