#pragma once

#include <stdbool.h>

#include "canvas.h"

/* The piano's screen: what the toy is set to right now. Pure: no ESP-IDF
 * (host-tested in test/test_ui.c); screen.c feeds it and sends the frames.
 *
 *   Chicco Piano          ● PC      header: DEMO when it plays; the PC link
 *   ───────────────────────────
 *   Sound                           the keys' instrument
 *   Trumpet
 *   Volume   ██████░░░░    100
 *   Octave       □ □ ■ □ □
 *   Beat           ROCK 116 bpm     or "off"
 *   Loop               recording
 */

typedef enum {
    UI_LOOP_EMPTY,
    UI_LOOP_STOPPED, /* a loop, not playing */
    UI_LOOP_WAITING, /* RECORD pressed, waiting for the first note */
    UI_LOOP_RECORDING,
    UI_LOOP_PLAYING,
} ui_loop_t;

typedef struct {
    bool pc;           /* the PC bridge answers */
    bool demo;         /* DEMO plays */
    const char *sound; /* the keys' instrument */
    int volume;        /* 0..127 (MIDI), shown as % */
    int octave;        /* -2..2 */
    const char *beat;  /* the drum beat playing, NULL: off */
    int beat_bpm;
    ui_loop_t loop;
} ui_state_t;

/* Where the volume bar sits, for the tests. */
#define UI_VOL_X 100
#define UI_VOL_W 72 /* ends 5 px before a right-aligned "100%" */
#define UI_VOL_Y 117
#define UI_VOL_H 10

/* Colours, for the tests. */
#define UI_BG    RGB(16, 20, 40)
#define UI_GREEN RGB(95, 209, 138)
#define UI_RED   RGB(255, 90, 95)

void ui_render(canvas_t *c, const ui_state_t *s);

/* MIDI volume 0..127 as 0..100 %, rounded. */
int ui_percent(int midi);
