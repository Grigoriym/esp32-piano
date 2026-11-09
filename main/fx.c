#include "fx.h"

#include <stdio.h>
#include <string.h>

#include "keymap.h"
#include "mixer.h"

static const uint8_t COPY_CH[] = {FX_ECHO_CH, FX_WIDE_CH, FX_WIDE2_CH};

static bool get_bit(const uint8_t *bits, uint8_t n)
{
    return bits[n >> 3] & (1u << (n & 7));
}

static void set_bit(uint8_t *bits, uint8_t n, bool on)
{
    if (on) {
        bits[n >> 3] |= (uint8_t)(1u << (n & 7));
    } else {
        bits[n >> 3] &= (uint8_t)~(1u << (n & 7));
    }
}

static int64_t floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

static void midi3(midi_send_fn send, void *ctx, uint8_t status, uint8_t d1, uint8_t d2, const char *what)
{
    uint8_t msg[3] = {status, d1, d2};
    send(msg, 3, what, ctx);
}

static void note(midi_send_fn send, void *ctx, uint8_t ch, bool on, uint8_t n, uint8_t vel)
{
    midi3(send, ctx, (uint8_t)((on ? 0x90 : 0x80) | ch), n, on ? vel : 0, NULL);
}

/* value: 0 is no bend; Wide's channels add their detune. */
static void bend(midi_send_fn send, void *ctx, uint8_t ch, int value)
{
    if (ch == FX_WIDE_CH) value += FX_DETUNE;
    if (ch == FX_WIDE2_CH) value -= FX_DETUNE;
    int v = 8192 + value;
    v = v < 0 ? 0 : v > 16383 ? 16383 : v;
    midi3(send, ctx, (uint8_t)(0xe0 | ch), (uint8_t)(v & 0x7f), (uint8_t)(v >> 7), NULL);
}

void fx_init(fx_t *f)
{
    memset(f, 0, sizeof(*f));
    f->filter = 127;
    f->echo = FX_ECHO_LEVEL;
    f->arp_note = -1;
}

int fx_echo_repeats(uint8_t level)
{
    return 1 + level * (FX_ECHO_MAX - 1) / 127;
}

int fx_echo_decay(uint8_t level)
{
    return 40 + level * 50 / 127;
}

const char *fx_name(fx_kind_t kind)
{
    static const char *const NAMES[FX_COUNT] = {"Echo", "Hold", "Octave",  "Arp",
                                                "Rise", "Tape", "Stutter", "Wide"};
    return kind < FX_COUNT ? NAMES[kind] : "?";
}

void fx_setup(midi_send_fn send, void *ctx)
{
    uint8_t chans[] = {KEY_CH,      1,          LOOP_CH,    LOOP_CH + 1, LOOP_CH + 2,
                       LOOP_CH + 3, FX_ECHO_CH, FX_WIDE_CH, FX_WIDE2_CH};
    for (size_t i = 0; i < sizeof(chans); i++) {
        uint8_t st = (uint8_t)(0xb0 | chans[i]);
        /* RPN 0 (pitch bend range) = FX_BEND_RANGE semitones, then RPN null. */
        const uint8_t msgs[6][3] = {{st, 101, 0}, {st, 100, 0},   {st, 6, FX_BEND_RANGE},
                                    {st, 38, 0},  {st, 101, 127}, {st, 100, 127}};
        for (int m = 0; m < 6; m++) send(msgs[m], 3, NULL, ctx);
        bend(send, ctx, chans[i], 0);
    }
    send(NULL, 0, "chicks: pitch bend range set", ctx);
}

void fx_set_grid(fx_t *f, int64_t origin_us, int64_t sixteenth_us)
{
    f->grid_origin_us = origin_us;
    f->sixteenth_us = sixteenth_us;
}

static int64_t step_us(const fx_t *f)
{
    return f->sixteenth_us > 0 ? f->sixteenth_us : FX_STEP_US;
}

/* Echoes waiting, kept by time; full: a note off goes out now (an echo cut short, never one stuck). */
static void queue(fx_t *f, int64_t t, uint8_t status, uint8_t n, uint8_t vel, midi_send_fn send, void *ctx)
{
    if (f->nq == FX_QUEUE) {
        if ((status & 0xf0) == 0x80) midi3(send, ctx, status, n, 0, NULL);
        return;
    }
    size_t i = f->nq++;
    while (i > 0 && f->q[i - 1].t_us > t) {
        f->q[i] = f->q[i - 1];
        i--;
    }
    f->q[i] = (fx_event_t){.t_us = t, .msg = {status, n, vel}};
}

/* A note from the keys (or from Arp) ends, with the copies the effects added. */
static void note_off(fx_t *f, uint8_t n, int64_t now, midi_send_fn send, void *ctx)
{
    if (!get_bit(f->sounding, n)) return;
    int64_t d = FX_ECHO_STEPS * step_us(f);
    note(send, ctx, KEY_CH, false, n, 0);
    bool oct = get_bit(f->doubled, n);
    for (int o = 0; o <= (oct ? 1 : 0); o++) {
        uint8_t m = (uint8_t)(n + 12 * o);
        if (o) note(send, ctx, KEY_CH, false, m, 0);
        if (get_bit(f->widened, n)) {
            note(send, ctx, FX_WIDE_CH, false, m, 0);
            note(send, ctx, FX_WIDE2_CH, false, m, 0);
        }
        if (get_bit(f->echoed, n)) {
            for (int k = 1; k <= fx_echo_repeats(f->echo); k++)
                queue(f, now + k * d, 0x80 | FX_ECHO_CH, m, 0, send, ctx);
        }
    }
    set_bit(f->sounding, n, false);
    set_bit(f->doubled, n, false);
    set_bit(f->widened, n, false);
    set_bit(f->echoed, n, false);
}

/* A note from the keys (or from Arp) starts, with the copies the effects add. */
static void note_on(fx_t *f, uint8_t n, uint8_t vel, int64_t now, midi_send_fn send, void *ctx)
{
    int64_t d = FX_ECHO_STEPS * step_us(f);
    note_off(f, n, now, send, ctx); /* retriggered: its copies off first */
    note(send, ctx, KEY_CH, true, n, vel);
    set_bit(f->sounding, n, true);
    bool oct = f->on[FX_OCTAVE] && n + 12 < 128;
    set_bit(f->doubled, n, oct);
    set_bit(f->widened, n, f->on[FX_WIDE]);
    set_bit(f->echoed, n, f->on[FX_ECHO]);
    for (int o = 0; o <= (oct ? 1 : 0); o++) {
        uint8_t m = (uint8_t)(n + 12 * o);
        if (o) note(send, ctx, KEY_CH, true, m, vel);
        if (f->on[FX_WIDE]) {
            note(send, ctx, FX_WIDE_CH, true, m, vel);
            note(send, ctx, FX_WIDE2_CH, true, m, vel);
        }
        if (!f->on[FX_ECHO]) continue;
        unsigned v = vel;
        for (int k = 1; k <= fx_echo_repeats(f->echo); k++) {
            v = v * (unsigned)fx_echo_decay(f->echo) / 100;
            queue(f, now + k * d, 0x90 | FX_ECHO_CH, m, (uint8_t)(v ? v : 1), send, ctx);
        }
    }
}

static void out_note(fx_t *f, bool on, uint8_t n, uint8_t vel, int64_t now, midi_send_fn send, void *ctx)
{
    if (on) {
        note_on(f, n, vel, now, send, ctx);
    } else {
        note_off(f, n, now, send, ctx);
    }
}

static bool any(const uint8_t *bits)
{
    for (int i = 0; i < 16; i++) {
        if (bits[i]) return true;
    }
    return false;
}

static void arp_stop(fx_t *f, int64_t now, midi_send_fn send, void *ctx)
{
    if (f->arp_note >= 0) out_note(f, false, (uint8_t)f->arp_note, 0, now, send, ctx);
    f->arp_note = -1;
}

void fx_key(fx_t *f, const uint8_t *msg, size_t len, int64_t now_us, midi_send_fn send, void *ctx)
{
    uint8_t kind = len == 3 ? msg[0] & 0xf0 : 0;
    if ((kind != 0x90 && kind != 0x80) || (msg[0] & 0x0f) != KEY_CH) {
        send(msg, len, NULL, ctx);
        return;
    }
    uint8_t n = msg[1] & 0x7f;
    bool on = kind == 0x90 && msg[2];
    if (on) f->vel[n] = msg[2];
    set_bit(f->held, n, on);
    if (f->on[FX_ARP]) {
        if (!any(f->held)) arp_stop(f, now_us, send, ctx);
        if (!on && (int)n != f->arp_note)
            out_note(f, false, n, 0, now_us, send, ctx); /* pressed before Arp */
        return;
    }
    out_note(f, on, n, msg[2], now_us, send, ctx);
}

/* The next 16th line at or after t. */
static int64_t next_line(const fx_t *f, int64_t t)
{
    int64_t s = step_us(f);
    if (f->sixteenth_us <= 0) return t;
    return f->grid_origin_us + floor_div(t - f->grid_origin_us + s - 1, s) * s;
}

static void arp_step(fx_t *f, int64_t now, midi_send_fn send, void *ctx)
{
    if (!any(f->held)) {
        arp_stop(f, now, send, ctx);
        return;
    }
    int from = f->arp_note;
    int next = -1;
    for (int i = 1; i <= 128 && next < 0; i++) {
        int n = (from + i + 128) % 128;
        if (get_bit(f->held, (uint8_t)n)) next = n;
    }
    arp_stop(f, now, send, ctx);
    out_note(f, true, (uint8_t)next, f->vel[next] ? f->vel[next] : 100, now, send, ctx);
    f->arp_note = next;
}

static void stutter_step(fx_t *f, int64_t now, midi_send_fn send, void *ctx)
{
    for (int n = 0; n < 128; n++) {
        if (!get_bit(f->sounding, (uint8_t)n)) continue;
        out_note(f, true, (uint8_t)n, f->vel[n] ? f->vel[n] : 100, now, send, ctx); /* off, then on again */
    }
}

static void ramp_send(fx_t *f, int64_t now, midi_send_fn send, void *ctx)
{
    int64_t t = now - f->ramp_start_us;
    if (t > f->ramp_len_us) t = f->ramp_len_us;
    /* p squared: slow at first, fast at the end, as a riser or a tape slowing down. */
    int64_t p2 = t * t * 1000 / (f->ramp_len_us * f->ramp_len_us); /* 0-1000 */
    int depth = (int)(p2 * 8191 / 1000);
    uint8_t level;
    if (f->on[FX_RISE]) {
        level = (uint8_t)(f->filter + (127 - f->filter) * t / f->ramp_len_us);
        for (size_t i = 0; i < sizeof(COPY_CH); i++) bend(send, ctx, COPY_CH[i], depth);
        bend(send, ctx, KEY_CH, depth);
    } else {
        level = (uint8_t)(f->filter - f->filter * t / f->ramp_len_us);
        for (size_t i = 0; i < sizeof(COPY_CH); i++) bend(send, ctx, COPY_CH[i], -depth);
        bend(send, ctx, KEY_CH, -depth);
        for (uint8_t ch = LOOP_CH; ch < LOOP_CH + LOOP_CHANNELS; ch++) bend(send, ctx, ch, -depth);
    }
    mixer_send_filter_to(KEY_CH, level, send, ctx); /* main.c copies it to the effect channels */
    f->ramp_sent_us = now;
}

static void ramp_end(fx_t *f, midi_send_fn send, void *ctx)
{
    bool tape = f->on[FX_TAPE];
    f->on[FX_RISE] = f->on[FX_TAPE] = false;
    bend(send, ctx, KEY_CH, 0);
    for (size_t i = 0; i < sizeof(COPY_CH); i++) bend(send, ctx, COPY_CH[i], 0);
    if (tape) {
        for (uint8_t ch = LOOP_CH; ch < LOOP_CH + LOOP_CHANNELS; ch++) bend(send, ctx, ch, 0);
        /* The tape has stopped: what the keys hold stops too. */
        midi3(send, ctx, 0xb0 | KEY_CH, 123, 0, NULL);
        memset(f->sounding, 0, sizeof(f->sounding));
        f->arp_note = -1;
    }
    f->restore = true;
    send(NULL, 0, tape ? "chick: Tape done" : "chick: Rise done", ctx);
}

/* Wide's copies off now; Echo and Octave copies end with their notes. */
static void wide_off(fx_t *f, midi_send_fn send, void *ctx)
{
    for (int n = 0; n < 128; n++) {
        if (!get_bit(f->widened, (uint8_t)n)) continue;
        for (int o = 0; o <= (get_bit(f->doubled, (uint8_t)n) ? 1 : 0); o++) {
            note(send, ctx, FX_WIDE_CH, false, (uint8_t)(n + 12 * o), 0);
            note(send, ctx, FX_WIDE2_CH, false, (uint8_t)(n + 12 * o), 0);
        }
        set_bit(f->widened, (uint8_t)n, false);
    }
}

void fx_button(fx_t *f, fx_kind_t kind, bool down, int64_t now_us, midi_send_fn send, void *ctx)
{
    if (kind >= FX_COUNT) return;
    char what[32];
    if (kind == FX_STUTTER) {
        if (f->on[FX_STUTTER] == down) return;
        f->on[FX_STUTTER] = down;
        if (down && !f->on[FX_ARP]) f->next_step_us = next_line(f, now_us + 1);
        send(NULL, 0, down ? "chick: Stutter" : "chick: Stutter off", ctx);
        return;
    }
    if (!down) return;
    if (kind == FX_RISE || kind == FX_TAPE) {
        if (f->on[FX_RISE] || f->on[FX_TAPE]) ramp_end(f, send, ctx); /* a new ramp starts from the pots */
        f->on[kind] = true;
        f->ramp_start_us = now_us;
        f->ramp_len_us = (kind == FX_RISE ? FX_RISE_STEPS : FX_TAPE_STEPS) * step_us(f);
        f->ramp_sent_us = 0;
        snprintf(what, sizeof(what), "chick: %s", fx_name(kind));
        send(NULL, 0, what, ctx);
        return;
    }
    f->on[kind] = !f->on[kind];
    bool on = f->on[kind];
    snprintf(what, sizeof(what), "chick: %s %s", fx_name(kind), on ? "on" : "off");
    switch (kind) {
        case FX_HOLD:
            midi3(send, ctx, 0xb0 | KEY_CH, 64, on ? 127 : 0, what);
            return; /* copied to the effect channels */
        case FX_ARP:
            if (on) {
                f->next_step_us = next_line(f, now_us);
            } else {
                arp_stop(f, now_us, send, ctx);
            }
            break;
        case FX_WIDE:
            if (!on) wide_off(f, send, ctx);
            break;
        default: break;
    }
    send(NULL, 0, what, ctx);
}

void fx_tick(fx_t *f, int64_t now_us, midi_send_fn send, void *ctx)
{
    while (f->nq && f->q[0].t_us <= now_us) {
        send(f->q[0].msg, 3, NULL, ctx);
        memmove(f->q, f->q + 1, (f->nq - 1) * sizeof(f->q[0]));
        f->nq--;
    }
    if (f->on[FX_ARP] || f->on[FX_STUTTER]) {
        int64_t s = step_us(f);
        if (f->next_step_us == 0 || now_us - f->next_step_us > s) f->next_step_us = next_line(f, now_us);
        if (now_us >= f->next_step_us) {
            if (f->on[FX_ARP]) {
                arp_step(f, now_us, send, ctx);
            } else {
                stutter_step(f, now_us, send, ctx);
            }
            f->next_step_us = next_line(f, f->next_step_us + 1);
            if (f->next_step_us <= now_us) f->next_step_us = now_us + s;
        }
    } else {
        f->next_step_us = 0;
    }
    if (f->on[FX_RISE] || f->on[FX_TAPE]) {
        if (now_us - f->ramp_start_us >= f->ramp_len_us) {
            ramp_end(f, send, ctx);
        } else if (f->ramp_sent_us == 0 || now_us - f->ramp_sent_us >= FX_RAMP_US) {
            ramp_send(f, now_us, send, ctx);
        }
    }
}

void fx_reset(fx_t *f, midi_send_fn send, void *ctx)
{
    uint8_t filter = f->filter;
    uint8_t echo = f->echo;
    bool ramp = f->on[FX_RISE] || f->on[FX_TAPE];
    for (size_t i = 0; i < sizeof(COPY_CH); i++) {
        midi3(send, ctx, (uint8_t)(0xb0 | COPY_CH[i]), 123, 0, NULL);
        bend(send, ctx, COPY_CH[i], 0);
    }
    bend(send, ctx, KEY_CH, 0);
    for (uint8_t ch = LOOP_CH; ch < LOOP_CH + LOOP_CHANNELS; ch++) bend(send, ctx, ch, 0);
    midi3(send, ctx, 0xb0 | KEY_CH, 64, 0, "chicks: all off");
    fx_init(f);
    f->filter = filter;
    f->echo = echo;
    f->restore = ramp;
}
