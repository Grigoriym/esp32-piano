/*
 * What the 8 WS2812 LEDs show. Pure (no ESP-IDF), host-tested; main.c sends
 * the colours out.
 *
 * Positions below are before the turn: the strip lies in a circle and everything
 * shows LEDS_TURN LEDs further along it (2: LED 1 below is LED 3 on the strip,
 * counted from the end the data wire goes into, so the beat is on 7, 8, 1, 2;
 * the user's pick, 2026-11-05).
 *
 * LEDs 1-4: the loop's layers, each in its colour (cyan, magenta, yellow,
 * orange), the newest brighter; the layer being recorded blinks red,
 * a RECORD waiting for its first note blinks LED 1 slowly. Dim while the loop
 * is stopped, and a muted layer (the Layers screen) dim.
 * LEDs 5-8: the beat's count in the bar, green, beat 1 brighter.
 * A pot just turned (LEDS_POT_SHOW_US): all 8 show its level as a bar in its
 * colour (beat green, metronome volume pink, keys white, filter blue, reverb
 * violet, metronome tempo warm white; on the ADS1115: resonance orange, echo teal, release lime,
 * wobble magenta).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define LEDS_N           8
#define LEDS_TURN        2  /* see above */
#define LEDS_MAX         24 /* brightest a channel gets: the strip runs on the DevKit's 3V3 */
#define LEDS_POT_SHOW_US 1500000

typedef struct {
    int64_t now_us;
    int beat_step; /* the step sounding now; -1: no beat */
    int steps_per_beat;
    int layers;          /* in the loop, the one being recorded included */
    int recording_layer; /* -1: none */
    bool armed;          /* RECORD pressed, waiting for the first note */
    bool loop_playing;
    int target;    /* the newest layer, shown brighter; -1: none */
    uint8_t muted; /* bit per layer: shown dim */
    int pot; /* the pot shown as a bar (0 beat, 1 click volume, 2 keys, 3 filter, 4 reverb, 5 click tempo, 6-9 resonance, echo, release, wobble); -1: none */
    int level; /* its part's level, 0-127 */
} leds_state_t;

typedef uint8_t leds_t[LEDS_N][3]; /* R, G, B */

void leds_draw(const leds_state_t *s, leds_t out);
