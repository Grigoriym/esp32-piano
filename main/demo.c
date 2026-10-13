#include "demo.h"

#define PASSES 2
#define BARS   4

typedef struct {
    uint8_t note; /* 0: the bar's lead-in note */
    uint8_t steps;
} step_t;

/* One bar; its first two notes change per bar: D, C, B, Bb. */
static const uint8_t LEAD_IN[BARS] = {62, 60, 59, 58};
static const step_t BAR[] = {
    {0, 1}, {0, 1}, {74, 2}, {69, 3}, {68, 2}, {67, 2}, {65, 2}, {62, 1}, {65, 1}, {67, 1},
};
#define BAR_LEN (sizeof(BAR) / sizeof(BAR[0]))

size_t demo_length(void)
{
    return PASSES * BARS * BAR_LEN;
}

uint8_t demo_note(size_t pos, uint8_t *steps)
{
    size_t i = pos % BAR_LEN;
    *steps = BAR[i].steps;
    return BAR[i].note ? BAR[i].note : LEAD_IN[(pos / BAR_LEN) % BARS];
}

static void note_off(demo_t *d, midi_send_fn send, void *ctx)
{
    if (!d->sounding) return;
    uint8_t msg[3] = {0x80 | DEMO_CH, d->sounding, 0};
    send(msg, 3, NULL, ctx);
    d->sounding = 0;
}

void demo_toggle(demo_t *d, int64_t now_us, midi_send_fn send, void *ctx)
{
    note_off(d, send, ctx);
    d->on = !d->on;
    if (d->on) {
        uint8_t msg[2] = {0xc0 | DEMO_CH, DEMO_PROGRAM};
        send(msg, 2, "demo start", ctx);
        d->pos = 0;
        d->next_us = now_us;
    } else {
        send(NULL, 0, "demo stop", ctx);
    }
}

void demo_tick(demo_t *d, int64_t now_us, midi_send_fn send, void *ctx)
{
    if (!d->on) return;
    if (d->sounding && now_us >= d->next_us - DEMO_GAP_US) note_off(d, send, ctx);
    if (now_us < d->next_us) return;
    if (d->pos == demo_length()) {
        demo_toggle(d, now_us, send, ctx);
        return;
    }
    uint8_t steps;
    uint8_t note = demo_note(d->pos, &steps);
    uint8_t msg[3] = {0x90 | DEMO_CH, note, DEMO_VELOCITY};
    send(msg, 3, NULL, ctx);
    d->sounding = note;
    d->next_us += (int64_t)steps * DEMO_STEP_US;
    d->pos++;
}
