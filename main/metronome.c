#include "metronome.h"

static int64_t floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

void metro_toggle(metro_t *m, int64_t at_us, midi_send_fn send, void *ctx)
{
    m->on = !m->on;
    if (!m->on) {
        send(NULL, 0, "metronome off", ctx);
        return;
    }
    m->origin_us = at_us;
    m->last_us = INT64_MIN / 2;
    m->due_us = at_us;
    uint8_t program[2] = {0xc0 | METRO_CH, METRO_PROGRAM};
    send(program, 2, "metronome on", ctx);
}

void metro_tick(metro_t *m, int64_t until_us, int64_t origin_us, int64_t beat_us, int beats_per_bar,
                midi_send_fn send, void *ctx)
{
    if (!m->on || beat_us <= 0) return;
    if (beats_per_bar < 1) beats_per_bar = 1;
    for (;;) {
        /* The next grid line after the last click; on a new grid (the beat
         * started or changed) never closer than half a beat to it. */
        int64_t from = m->last_us + beat_us / 2;
        if (from < origin_us) from = origin_us - 1;
        int64_t k = floor_div(from - origin_us, beat_us) + 1;
        int64_t t = origin_us + k * beat_us;
        if (t > until_us) return;
        bool bar = floor_div(k, beats_per_bar) * beats_per_bar == k;
        uint8_t note = bar ? METRO_BAR_NOTE : METRO_NOTE;
        uint8_t on[3] = {0x90 | METRO_CH, note, bar ? METRO_BAR_VEL : METRO_VEL};
        uint8_t off[3] = {0x80 | METRO_CH, note, 0};
        m->due_us = t;
        send(on, 3, NULL, ctx);
        m->due_us = t + METRO_CLICK_US;
        send(off, 3, NULL, ctx);
        m->last_us = t;
    }
}
