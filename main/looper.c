#include "looper.h"

#include <stdio.h>

#include "keymap.h"

void looper_init(looper_t *l)
{
    l->state = LOOPER_IDLE;
    l->n = 0;
    l->length_us = 0;
}

static bool get_bit(const uint8_t *bits, uint8_t note)
{
    return bits[note / 8] & (1u << (note % 8));
}

static void set_bit(uint8_t *bits, uint8_t note, bool on)
{
    if (on) {
        bits[note / 8] |= (uint8_t)(1u << (note % 8));
    } else {
        bits[note / 8] &= (uint8_t)~(1u << (note % 8));
    }
}

static void clear_held(looper_t *l)
{
    for (size_t i = 0; i < 2; i++) {
        for (size_t j = 0; j < 16; j++) l->held[i][j] = 0;
    }
}

/* Note offs for every loop note sounding now. */
static void silence(looper_t *l, midi_send_fn send, void *ctx)
{
    static const uint8_t CH[2] = {LOOP_CH, DRUM_CH};
    for (size_t i = 0; i < 2; i++) {
        for (unsigned note = 0; note < 128; note++) {
            if (!get_bit(l->held[i], (uint8_t)note)) continue;
            uint8_t msg[3] = {0x80 | CH[i], (uint8_t)note, 0};
            send(msg, 3, NULL, ctx);
        }
    }
    clear_held(l);
}

static void play(looper_t *l, int64_t now_us, midi_send_fn send, void *ctx)
{
    uint8_t msg[2] = {0xc0 | LOOP_CH, l->program};
    send(msg, 2, NULL, ctx);
    unsigned notes = 0;
    for (size_t i = 0; i < l->n; i++) notes += (l->ev[i].msg[0] & 0xf0) == 0x90;
    char what[48];
    snprintf(what, sizeof(what), "loop plays (%u notes, %u ms)", notes, (unsigned)(l->length_us / 1000));
    send(NULL, 0, what, ctx);
    clear_held(l);
    l->state = LOOPER_PLAYING;
    l->start_us = now_us;
    l->pos = 0;
}

/* Ends a recording: the loop plays from now, or nothing was recorded. */
static void close_loop(looper_t *l, int64_t now_us, midi_send_fn send, void *ctx)
{
    if (l->state == LOOPER_ARMED || l->n == 0) {
        l->state = LOOPER_IDLE;
        send(NULL, 0, "loop: nothing recorded", ctx);
        return;
    }
    l->length_us = (uint32_t)(now_us - l->start_us);
    if (l->length_us == 0) l->length_us = 1; /* looper_tick steps by it */
    play(l, now_us, send, ctx);
}

void looper_record(looper_t *l, int64_t now_us, midi_send_fn send, void *ctx)
{
    if (l->state == LOOPER_ARMED || l->state == LOOPER_RECORDING) {
        close_loop(l, now_us, send, ctx);
        return;
    }
    silence(l, send, ctx);
    l->state = LOOPER_ARMED;
    l->n = 0;
    l->length_us = 0;
    send(NULL, 0, "loop: recording from the first note", ctx);
}

void looper_replay(looper_t *l, int64_t now_us, midi_send_fn send, void *ctx)
{
    switch (l->state) {
        case LOOPER_ARMED:
        case LOOPER_RECORDING: close_loop(l, now_us, send, ctx); break;
        case LOOPER_PLAYING:
            silence(l, send, ctx);
            l->state = LOOPER_IDLE;
            send(NULL, 0, "loop stopped", ctx);
            break;
        case LOOPER_IDLE:
            if (l->length_us) {
                play(l, now_us, send, ctx);
            } else {
                send(NULL, 0, "loop: empty, RECORD first", ctx);
            }
            break;
    }
}

void looper_clear(looper_t *l, midi_send_fn send, void *ctx)
{
    silence(l, send, ctx);
    looper_init(l);
}

void looper_capture(looper_t *l, int64_t now_us, const uint8_t *msg, size_t len, uint8_t program)
{
    if (l->state != LOOPER_ARMED && l->state != LOOPER_RECORDING) return;
    if (len != 3) return;
    uint8_t kind = msg[0] & 0xf0;
    uint8_t ch = msg[0] & 0x0f;
    if ((kind != 0x90 && kind != 0x80) || (ch != KEY_CH && ch != DRUM_CH)) return;
    bool on = kind == 0x90 && msg[2];
    uint8_t *held = l->held[ch == DRUM_CH];
    if (!on && !get_bit(held, msg[1])) return; /* pressed before recording started */
    if (l->n == LOOPER_MAX_EVENTS) return;
    if (on && l->n == LOOPER_MAX_EVENTS - 1) return; /* keep room for a note off */
    if (l->state == LOOPER_ARMED) {
        clear_held(l);
        l->state = LOOPER_RECORDING;
        l->start_us = now_us;
        l->program = program;
    }
    set_bit(held, msg[1], on);
    looper_event_t *e = &l->ev[l->n++];
    e->t_us = (uint32_t)(now_us - l->start_us);
    e->msg[0] = (uint8_t)(kind | (ch == KEY_CH ? LOOP_CH : DRUM_CH));
    e->msg[1] = msg[1];
    e->msg[2] = msg[2];
}

void looper_tick(looper_t *l, int64_t now_us, midi_send_fn send, void *ctx)
{
    if (l->state != LOOPER_PLAYING) return;
    for (;;) {
        int64_t t = now_us - l->start_us;
        while (l->pos < l->n && l->ev[l->pos].t_us <= t) {
            const looper_event_t *e = &l->ev[l->pos++];
            set_bit(l->held[(e->msg[0] & 0x0f) == DRUM_CH], e->msg[1],
                    (e->msg[0] & 0xf0) == 0x90 && e->msg[2]);
            send(e->msg, 3, NULL, ctx);
        }
        if (t < (int64_t)l->length_us) return;
        silence(l, send, ctx);
        l->start_us += l->length_us;
        l->pos = 0;
    }
}
