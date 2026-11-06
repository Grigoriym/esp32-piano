#include "metronome.h"

#include <stddef.h>

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
    m->last_us = m->prev_us = INT64_MIN / 2;
    m->due_us = at_us;
    metro_setup(send, ctx);
}

void metro_setup(midi_send_fn send, void *ctx)
{
    uint8_t program[2] = {0xc0 | METRO_CH, METRO_PROGRAM};
    send(program, 2, "metronome on", ctx);
    /* No reverb or chorus (its volume is pot 2's CC11, main/mixer.h), and the SF2 reverb send NRPN at -100%
     * (the preset's own send too; see mixer.c), LSB before MSB. */
    int v = 8192 - 1000;
    const uint8_t cc[][3] = {
        {0xb0 | METRO_CH, 91, 0},
        {0xb0 | METRO_CH, 93, 0},
        {0xb0 | METRO_CH, 99, 120},
        {0xb0 | METRO_CH, 98, 16},
        {0xb0 | METRO_CH, 38, (uint8_t)(v & 0x7f)},
        {0xb0 | METRO_CH, 6, (uint8_t)(v >> 7)},
    };
    for (size_t i = 0; i < sizeof(cc) / sizeof(cc[0]); i++) send(cc[i], 3, NULL, ctx);
}

int metro_bpm_for(int level)
{
    if (level < 0) level = 0;
    if (level > 127) level = 127;
    return METRO_MIN_BPM + (level * (METRO_MAX_BPM - METRO_MIN_BPM) + 63) / 127;
}

void metro_set_bpm(metro_t *m, int bpm)
{
    if (bpm == m->bpm) return;
    /* Re-anchored on the last click: the next one comes a new beat after it, and
     * the bar keeps counting where it was (a loop on its bars stays on them). */
    if (m->on && m->last_us > INT64_MIN / 4) {
        int64_t old = metro_beat_us(m);
        int64_t k = floor_div(m->last_us - m->origin_us + old / 2, old);
        int64_t into_bar = k - floor_div(k, METRO_BEATS) * METRO_BEATS;
        m->bpm = bpm;
        m->origin_us = m->last_us - into_bar * metro_beat_us(m);
        return;
    }
    m->bpm = bpm;
}

void metro_align(metro_t *m, int64_t bar_us, int64_t at_us)
{
    m->origin_us = bar_us;
    m->last_us = at_us - metro_beat_us(m) / 2 - 1; /* the next click: the first beat at or after at_us */
}

int64_t metro_beat_us(const metro_t *m)
{
    return 60000000LL / (m->bpm > 0 ? m->bpm : 120);
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
        m->prev_us = m->last_us;
        m->prev_bar = m->last_bar;
        m->last_us = t;
        m->last_bar = bar;
    }
}

int metro_flash(const metro_t *m, int64_t now_us)
{
    if (!m->on) return 0;
    /* The last click may still be ahead (sent MIDI_LINK_LEAD_US early): then the one before. */
    bool last = now_us >= m->last_us;
    int64_t t = last ? m->last_us : m->prev_us;
    if (now_us < t || now_us - t >= METRO_FLASH_US) return 0;
    return (last ? m->last_bar : m->prev_bar) ? 2 : 1;
}
