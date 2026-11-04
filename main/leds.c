#include "leds.h"

#include <string.h>

#define DIM   3 /* a stopped loop's layers, a layer pot 2 isn't on */
#define MID   8
#define FULL  LEDS_MAX
#define BEATS 4 /* LEDs 5-8 */

/* Colours as fractions of 4: scaled to a brightness by set(). */
static const uint8_t LAYER[4][3] = {{0, 4, 4}, {4, 0, 4}, {4, 4, 0}, {4, 1, 0}};
static const uint8_t GREEN[3] = {0, 4, 0};
static const uint8_t WHITE[3] = {3, 3, 3};
static const uint8_t RED[3] = {4, 0, 0};

static void set(leds_t out, int i, const uint8_t c[3], int bright)
{
    for (int k = 0; k < 3; k++) out[i][k] = (uint8_t)(c[k] * bright / 4);
}

static bool blink(int64_t now_us, int64_t period_us)
{
    return (now_us / (period_us / 2)) % 2 == 0;
}

static void draw_bar(const leds_state_t *s, leds_t out)
{
    const uint8_t *c = s->pot == 0 ? GREEN : s->pot == 2 ? WHITE : LAYER[s->target >= 0 ? s->target % 4 : 0];
    if (!s->following && !blink(s->now_us, 400000)) return;
    int lit = (s->level * LEDS_N + 63) / 127;
    if (s->level > 0 && lit == 0) lit = 1;
    for (int i = 0; i < lit; i++) set(out, i, c, MID);
}

void leds_draw(const leds_state_t *s, leds_t out)
{
    memset(out, 0, sizeof(leds_t));
    if (s->pot >= 0) {
        draw_bar(s, out);
        return;
    }
    for (int i = 0; i < s->layers && i < 4; i++) {
        if (i == s->recording_layer) {
            if (blink(s->now_us, 250000)) set(out, i, RED, MID);
        } else {
            set(out, i, LAYER[i], !s->loop_playing ? DIM : i == s->target ? FULL : MID);
        }
    }
    if (s->armed && s->layers == 0 && blink(s->now_us, 1000000)) set(out, 0, RED, MID);
    if (s->beat_step >= 0 && s->steps_per_beat > 0) {
        int beat = (s->beat_step / s->steps_per_beat) % BEATS;
        set(out, 4 + beat, GREEN, beat == 0 ? FULL : MID);
    }
}
