#include "looper.h"

#include <stdio.h>
#include <string.h>

#include "mixer.h"

void looper_init(looper_t *l)
{
    int64_t origin = l->grid_origin_us;
    int64_t bar = l->grid_bar_us;
    uint8_t pot_now[LOOPER_POTS];
    memcpy(pot_now, l->pot_now, sizeof(pot_now));
    uint8_t known = l->pot_known;
    memset(l, 0, sizeof(*l));
    l->grid_origin_us = origin;
    l->grid_bar_us = bar;
    memcpy(l->pot_now, pot_now, sizeof(pot_now));
    l->pot_known = known;
    for (size_t i = 0; i < LOOPER_LAYERS; i++) l->gain[i] = MIXER_LEVEL;
}

void looper_set_grid(looper_t *l, int64_t origin_us, int64_t bar_us)
{
    l->grid_origin_us = origin_us;
    l->grid_bar_us = bar_us;
}

size_t looper_layers(const looper_t *l)
{
    return l->n_layers + (l->dubbing ? 1 : 0);
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

static bool is_note(const uint8_t *msg)
{
    return (msg[0] & 0xf0) == 0x90 || (msg[0] & 0xf0) == 0x80;
}

/* A recorded pot move's pot (mixer_pot_t), or -1. */
static int pot_of(const uint8_t *msg)
{
    if ((msg[0] & 0xf0) != 0xb0) return -1;
    int pot = (int)msg[1] - LOOPER_POT_CC;
    return pot >= MIXER_KEYS && pot < MIXER_KEYS + LOOPER_POTS ? pot : -1;
}

static int64_t floor_div(int64_t a, int64_t b)
{
    return a / b - (a % b != 0 && (a < 0) != (b < 0));
}

static int64_t mod(int64_t a, int64_t b)
{
    return a - floor_div(a, b) * b;
}

static void log_msg(midi_send_fn send, void *ctx, const char *what)
{
    send(NULL, 0, what, ctx);
}

/* Note offs for every loop note sounding now. */
static void silence(looper_t *l, midi_send_fn send, void *ctx)
{
    for (unsigned ch = 0; ch < 16; ch++) {
        for (unsigned note = 0; note < 128; note++) {
            if (!get_bit(l->sounding[ch], (uint8_t)note)) continue;
            uint8_t msg[3] = {(uint8_t)(0x80 | ch), (uint8_t)note, 0};
            send(msg, 3, NULL, ctx);
        }
    }
    memset(l->sounding, 0, sizeof(l->sounding));
}

/* Note offs at a pass's end for the loop notes sounding then, except those the
 * loop itself ends early in the next pass: a pickup held over the loop's start
 * rings on (2026-11-07: a note pressed 9 ms before the pass start was cut to 9 ms
 * at the wrap). A note with no off of its own still stops here. */
static void silence_at_wrap(looper_t *l, midi_send_fn send, void *ctx)
{
    for (unsigned ch = 0; ch < 16; ch++) {
        for (unsigned note = 0; note < 128; note++) {
            if (!get_bit(l->sounding[ch], (uint8_t)note)) continue;
            bool ends_later = false;
            for (size_t i = 0; i < l->n; i++) {
                const uint8_t *m = l->ev[i].msg;
                if (!is_note(m) || (m[0] & 0x0f) != ch || m[1] != note) continue;
                ends_later = (m[0] & 0xf0) == 0x80 || !m[2]; /* the first of this note: an off */
                break;
            }
            if (ends_later) continue;
            uint8_t msg[3] = {(uint8_t)(0x80 | ch), (uint8_t)note, 0};
            send(msg, 3, NULL, ctx);
            set_bit(l->sounding[ch], (uint8_t)note, false);
        }
    }
}

/* The first event at or after a time into the pass. */
static size_t first_after(const looper_t *l, int64_t t)
{
    size_t i = 0;
    while (i < l->n && (int64_t)l->ev[i].t_us <= t) i++;
    return i;
}

/* Plays from start_us on, events up to played_us into the pass counted as played. */
static void start_playing(looper_t *l, int64_t start_us, int64_t played_us, midi_send_fn send, void *ctx)
{
    l->state = LOOPER_PLAYING;
    l->dubbing = false;
    l->start_us = start_us;
    l->played_us = played_us;
    l->pos = played_us < 0 ? 0 : first_after(l, played_us);
    l->program_due = (uint8_t)((1u << l->n_layers) - 1);
    unsigned notes = 0;
    for (size_t i = 0; i < l->n; i++) notes += (l->ev[i].msg[0] & 0xf0) == 0x90;
    char what[56];
    snprintf(what, sizeof(what), "loop plays (%u notes, %u ms, %u layers)", notes,
             (unsigned)(l->length_us / 1000), (unsigned)l->n_layers);
    log_msg(send, ctx, what);
}

/* Where the first note is (a pot move stored at the take's start can come before it). */
static uint32_t first_note_t(const looper_t *l)
{
    for (size_t i = 0; i < l->n; i++) {
        if (is_note(l->ev[i].msg)) return l->ev[i].t_us;
    }
    return 0;
}

/* Notes played past the loop's length: those before where the loop's first note
 * comes again finish a phrase that started mid-bar and go to the pass's start;
 * the rest are the next pass played before a late RECORD and are dropped. */
static void wrap_tail(looper_t *l)
{
    uint32_t first = first_note_t(l);
    uint32_t sixteenth = (uint32_t)(l->grid_bar_us / 16);
    size_t out = 0;
    for (size_t i = 0; i < l->n; i++) {
        looper_event_t e = l->ev[i];
        if (e.t_us >= l->length_us) {
            e.t_us -= l->length_us;
            if (e.t_us >= l->length_us || e.t_us + sixteenth >= first) continue;
        }
        l->ev[out++] = e;
    }
    l->n = out;
    for (size_t i = 1; i < l->n; i++) { /* the wrapped ones to the front, in order */
        looper_event_t e = l->ev[i];
        size_t j = i;
        for (; j > 0 && l->ev[j - 1].t_us > e.t_us; j--) l->ev[j] = l->ev[j - 1];
        l->ev[j] = e;
    }
}

/* Ends the first layer's recording: the loop plays on, or nothing was recorded. */
static void close_loop(looper_t *l, int64_t now_us, midi_send_fn send, void *ctx)
{
    if (l->state == LOOPER_ARMED || l->n == 0) {
        looper_init(l);
        log_msg(send, ctx, "loop: nothing recorded");
        return;
    }
    int64_t played = now_us - l->start_us;
    l->n_layers = 1;
    if (l->grid_bar_us) {
        /* Bars as played: from the first note, which may come mid-bar. */
        int64_t phrase = played - first_note_t(l);
        int64_t bars = (phrase + l->grid_bar_us / 2) / l->grid_bar_us;
        l->length_us = (uint32_t)((bars < 1 ? 1 : bars) * l->grid_bar_us);
        wrap_tail(l);
        /* Carry on where the beat is: what was just played live isn't played again. */
        int64_t pass = floor_div(played, l->length_us);
        int64_t start = l->start_us + pass * l->length_us;
        start_playing(l, start, now_us - start, send, ctx);
    } else {
        l->length_us = (uint32_t)(played > 0 ? played : 1);
        start_playing(l, now_us, -1, send, ctx);
    }
}

/* The pass start nearest t. */
static int64_t nearest_pass(const looper_t *l, int64_t t)
{
    int64_t len = l->length_us;
    return l->start_us + floor_div(t - l->start_us + len / 2, len) * len;
}

/* Closes the layer being added: the loop grows to the passes it was played over
 * (each older layer repeated), then the layer goes in. */
static void end_dub(looper_t *l, int64_t now_us, midi_send_fn send, void *ctx)
{
    l->dubbing = false;
    if (!l->dub_events) {
        log_msg(send, ctx, "loop: nothing added");
        return;
    }
    int64_t len = l->length_us;
    int64_t passes = (now_us - l->dub_base_us + len / 2) / len;
    if (passes < 1) passes = 1;
    if (passes > LOOPER_MAX_PASSES) passes = LOOPER_MAX_PASSES;
    while (passes > 1 && l->n * (size_t)passes + l->dub_events > LOOPER_MAX_EVENTS) passes--;
    size_t n = l->n;
    for (int64_t p = 1; p < passes; p++) { /* the older layers, once a pass */
        for (size_t i = 0; i < n; i++) {
            l->ev[l->n] = l->ev[i];
            l->ev[l->n++].t_us += (uint32_t)(p * len);
        }
    }
    int64_t total = passes * len;
    l->length_us = (uint32_t)total;
    uint8_t dropped[16] = {0}; /* notes on past the end, so their offs go too */
    for (size_t i = 0; i < l->dub_events && l->n < LOOPER_MAX_EVENTS; i++) {
        looper_event_t e = {.msg = {l->dub[i].msg[0], l->dub[i].msg[1], l->dub[i].msg[2]},
                            .layer = (uint8_t)l->n_layers};
        bool on = (e.msg[0] & 0xf0) == 0x90 && e.msg[2];
        int64_t t = l->dub[i].t_us;
        if (t < 0) t += total; /* a pickup: before the loop's start comes round again */
        if (t < 0) t = 0;
        if (!is_note(e.msg)) { /* a pot move: past the end it is the next pass's */
            if (t >= total) continue;
        } else if (t >= total) {
            if (on) {
                set_bit(dropped, e.msg[1], true);
                continue;
            }
            if (get_bit(dropped, e.msg[1])) {
                set_bit(dropped, e.msg[1], false);
                continue;
            }
            t = total - 1;
        } else if (on) {
            set_bit(dropped, e.msg[1], false);
        }
        e.t_us = (uint32_t)t;
        l->ev[l->n++] = e;
    }
    for (size_t i = 1; i < l->n; i++) { /* in time order, stable */
        looper_event_t e = l->ev[i];
        size_t j = i;
        for (; j > 0 && l->ev[j - 1].t_us > e.t_us; j--) l->ev[j] = l->ev[j - 1];
        l->ev[j] = e;
    }
    l->n_layers++;
    char what[48];
    snprintf(what, sizeof(what), "loop: layer added (%d pass%s)", (int)passes, passes > 1 ? "es" : "");
    log_msg(send, ctx, what);
    /* Carry on where the loop is: the new length counts from the layer's first pass, and
     * what the old loop already sent (up to its start + played) isn't sent again. */
    int64_t sent_until = l->start_us + l->played_us;
    int64_t start = now_us - mod(now_us - l->dub_base_us, total);
    start_playing(l, start, sent_until - start, send, ctx);
    l->program_due = (uint8_t)(1u << (l->n_layers - 1)); /* the older layers' are sent already */
}

void looper_record(looper_t *l, int64_t now_us, midi_send_fn send, void *ctx)
{
    switch (l->state) {
        case LOOPER_ARMED:
        case LOOPER_RECORDING: close_loop(l, now_us, send, ctx); return;
        case LOOPER_PLAYING:
            if (l->dubbing) {
                end_dub(l, now_us, send, ctx);
            } else if (l->n_layers == LOOPER_LAYERS) {
                log_msg(send, ctx, "loop: full, undo a layer first");
            } else {
                l->dubbing = true;
                l->dub_events = 0;
                l->dub_base_us = nearest_pass(l, now_us);
                l->pot_taken = 0;
                memset(l->rec_held, 0, sizeof(l->rec_held));
                log_msg(send, ctx, "loop: adding a layer");
            }
            return;
        case LOOPER_IDLE: break;
    }
    silence(l, send, ctx);
    looper_init(l);
    l->state = LOOPER_ARMED;
    log_msg(send, ctx, "loop: recording from the first note");
}

void looper_replay(looper_t *l, int64_t now_us, midi_send_fn send, void *ctx)
{
    switch (l->state) {
        case LOOPER_ARMED:
        case LOOPER_RECORDING: close_loop(l, now_us, send, ctx); break;
        case LOOPER_PLAYING:
            if (l->dubbing) end_dub(l, now_us, send, ctx);
            silence(l, send, ctx);
            l->state = LOOPER_IDLE;
            log_msg(send, ctx, "loop stopped");
            break;
        case LOOPER_IDLE:
            if (!l->length_us) {
                log_msg(send, ctx, "loop: empty, RECORD first");
            } else if (l->grid_bar_us) { /* from the next bar line */
                int64_t bar = floor_div(now_us - l->grid_origin_us + l->grid_bar_us - 1, l->grid_bar_us);
                start_playing(l, l->grid_origin_us + bar * l->grid_bar_us, -1, send, ctx);
            } else {
                start_playing(l, now_us, -1, send, ctx);
            }
            break;
    }
}

/* Removes every event of layers >= layer. */
static void drop_layers(looper_t *l, size_t layer)
{
    size_t out = 0;
    size_t pos = l->pos;
    for (size_t i = 0; i < l->n; i++) {
        if (l->ev[i].layer >= layer) {
            if (i < l->pos) pos--;
            continue;
        }
        l->ev[out++] = l->ev[i];
    }
    l->n = out;
    l->pos = pos;
}

void looper_undo(looper_t *l, midi_send_fn send, void *ctx)
{
    if (l->state == LOOPER_ARMED || l->state == LOOPER_RECORDING) {
        looper_clear(l, send, ctx);
        log_msg(send, ctx, "loop: recording dropped");
        return;
    }
    if (l->dubbing) {
        l->dubbing = false; /* its notes were never in the loop */
        log_msg(send, ctx, "loop: new layer dropped");
    } else if (l->n_layers > 1) {
        silence(l, send, ctx);
        l->n_layers--;
        drop_layers(l, l->n_layers);
        l->muted &= (uint8_t)((1u << l->n_layers) - 1);
        l->mute_flip &= (uint8_t)((1u << l->n_layers) - 1);
        log_msg(send, ctx, "loop: last layer dropped");
    } else if (l->n_layers == 1) {
        looper_clear(l, send, ctx);
        log_msg(send, ctx, "loop: erased");
    }
}

void looper_mute(looper_t *l, size_t layer, int64_t now_us, midi_send_fn send, void *ctx)
{
    if (layer >= l->n_layers) return;
    l->mute_flip ^= (uint8_t)(1u << layer);
    l->mute_at_us = now_us;
    if (l->grid_bar_us)
        l->mute_at_us =
            l->grid_origin_us + (floor_div(now_us - l->grid_origin_us, l->grid_bar_us) + 1) * l->grid_bar_us;
    if (l->state != LOOPER_PLAYING) { /* stopped: nothing to wait for */
        l->muted ^= l->mute_flip;
        l->mute_flip = 0;
    }
    char what[48];
    bool will_mute = (l->muted ^ l->mute_flip) & (1u << layer);
    snprintf(what, sizeof(what), "loop: layer %u %s", (unsigned)layer + 1, will_mute ? "muted" : "back");
    log_msg(send, ctx, what);
}

/* The mutes asked for take effect now (l->mute_at_us): note offs for a muted layer's notes. */
static void apply_mutes(looper_t *l, midi_send_fn send, void *ctx)
{
    uint8_t now_muted = l->mute_flip & ~l->muted;
    l->muted ^= l->mute_flip;
    l->mute_flip = 0;
    l->due_us = l->mute_at_us;
    for (unsigned layer = 0; layer < LOOPER_LAYERS; layer++) {
        if (!(now_muted & (1u << layer))) continue;
        unsigned ch = LOOP_CH + layer;
        for (unsigned note = 0; note < 128; note++) {
            if (!get_bit(l->sounding[ch], (uint8_t)note)) continue;
            uint8_t msg[3] = {(uint8_t)(0x80 | ch), (uint8_t)note, 0};
            send(msg, 3, NULL, ctx);
            set_bit(l->sounding[ch], (uint8_t)note, false);
        }
    }
}

void looper_clear(looper_t *l, midi_send_fn send, void *ctx)
{
    silence(l, send, ctx);
    looper_init(l);
}

void looper_capture(looper_t *l, int64_t now_us, const uint8_t *msg, size_t len, uint8_t program)
{
    bool first_layer = l->state == LOOPER_ARMED || l->state == LOOPER_RECORDING;
    if (!first_layer && !l->dubbing) return;
    if (len != 3) return;
    uint8_t kind = msg[0] & 0xf0;
    uint8_t ch = msg[0] & 0x0f;
    if ((kind != 0x90 && kind != 0x80) || (ch != KEY_CH && ch != DRUM_CH)) return;
    bool on = kind == 0x90 && msg[2];
    uint8_t *held = l->rec_held[ch == DRUM_CH];
    if (!on && !get_bit(held, msg[1])) return; /* pressed before recording started */
    size_t used = first_layer ? l->n : l->dub_events;
    size_t room = first_layer ? LOOPER_MAX_EVENTS : LOOPER_DUB_EVENTS;
    if (used == room) return;
    if (on && used == room - 1) return; /* keep room for a note off */
    size_t layer = first_layer ? 0 : l->n_layers;
    if (l->state == LOOPER_ARMED) {
        memset(l->rec_held, 0, sizeof(l->rec_held));
        l->state = LOOPER_RECORDING;
        l->start_us = now_us;
        if (l->grid_bar_us) { /* the bar line of the first note, a 16th early counting as on it */
            int64_t bar = floor_div(now_us - l->grid_origin_us + l->grid_bar_us / 16, l->grid_bar_us);
            l->start_us = l->grid_origin_us + bar * l->grid_bar_us;
        }
    }
    if (first_layer ? l->n == 0 : l->dub_events == 0) l->program[layer] = program;
    set_bit(held, msg[1], on);
    int64_t t = now_us - l->start_us;
    uint8_t status = (uint8_t)(kind | (ch == KEY_CH ? LOOP_CH + layer : DRUM_CH));
    if (first_layer) {
        l->ev[l->n++] =
            (looper_event_t){.t_us = (uint32_t)(t < 0 ? 0 : t), .msg = {status, msg[1], msg[2]}, .layer = 0};
    } else { /* kept apart until RECORD closes it: then the loop's length is known */
        l->dub[l->dub_events].t_us = (int32_t)(now_us - l->dub_base_us);
        memcpy(l->dub[l->dub_events].msg, (uint8_t[3]){status, msg[1], msg[2]}, 3);
        l->dub_events++;
    }
}

void looper_tick(looper_t *l, int64_t now_us, midi_send_fn send, void *ctx)
{
    if (l->state != LOOPER_PLAYING) return;
    for (;;) {
        int64_t t = now_us - l->start_us;
        if (t < 0) return; /* waits for its bar line */
        l->due_us = l->start_us;
        for (size_t layer = 0; layer < LOOPER_LAYERS; layer++) {
            if (!(l->program_due & (1u << layer))) continue;
            uint8_t msg[2] = {(uint8_t)(0xc0 | (LOOP_CH + layer)), l->program[layer]};
            send(msg, 2, NULL, ctx);
        }
        l->program_due = 0;
        while (l->pos < l->n && l->ev[l->pos].t_us <= t) {
            const looper_event_t *e = &l->ev[l->pos];
            if (l->mute_flip && l->start_us + e->t_us >= l->mute_at_us) apply_mutes(l, send, ctx);
            l->pos++;
            l->due_us = l->start_us + e->t_us;
            int pot = pot_of(e->msg);
            if (pot >= 0) {
                mixer_send_pot_to(e->msg[0] & 0x0f, (mixer_pot_t)pot, e->msg[2], send, ctx);
                continue;
            }
            if (is_note(e->msg)) {
                bool on = (e->msg[0] & 0xf0) == 0x90 && e->msg[2];
                if (on && (l->muted & (1u << e->layer))) continue;
                set_bit(l->sounding[e->msg[0] & 0x0f], e->msg[1], on);
            }
            uint8_t msg[3];
            if (mixer_apply(e->msg, 3, l->gain[e->layer], msg)) send(msg, 3, NULL, ctx);
        }
        l->played_us = t;
        int64_t pass_end = l->start_us + l->length_us;
        if (l->mute_flip && l->mute_at_us <= now_us && l->mute_at_us <= pass_end) apply_mutes(l, send, ctx);
        if (t < (int64_t)l->length_us) return;
        l->due_us = l->start_us + l->length_us;
        silence_at_wrap(l, send, ctx);
        l->start_us += l->length_us;
        l->pos = 0;
        l->played_us = -1;
    }
}

/* Stores a pot move in the take: t from the take's start (the loop's start for the
 * first layer, dub_base_us for one added). Keeps a slot free for a note off. */
static void store_pot(looper_t *l, int64_t t, int pot, uint8_t level)
{
    bool first_layer = l->state == LOOPER_RECORDING;
    size_t layer = first_layer ? 0 : l->n_layers;
    uint8_t msg[3] = {(uint8_t)(0xb0 | (LOOP_CH + layer)), (uint8_t)(LOOPER_POT_CC + pot), level};
    if (first_layer) {
        if (l->n + 1 >= LOOPER_MAX_EVENTS) return;
        uint32_t at = (uint32_t)(t < 0 ? 0 : t);
        size_t j = l->n++; /* in time order: the take's start value goes before the notes */
        for (; j > 0 && l->ev[j - 1].t_us > at; j--) l->ev[j] = l->ev[j - 1];
        l->ev[j] = (looper_event_t){.t_us = at, .msg = {msg[0], msg[1], msg[2]}, .layer = 0};
    } else {
        if (l->dub_events + 1 >= LOOPER_DUB_EVENTS) return;
        l->dub[l->dub_events].t_us = (int32_t)t;
        memcpy(l->dub[l->dub_events].msg, msg, 3);
        l->dub_events++;
    }
}

void looper_pot(looper_t *l, int64_t now_us, int pot, uint8_t level)
{
    int i = pot - MIXER_KEYS;
    if (i < 0 || i >= LOOPER_POTS) return;
    uint8_t bit = (uint8_t)(1u << i);
    uint8_t was = l->pot_now[i];
    bool known = l->pot_known & bit;
    l->pot_now[i] = level;
    l->pot_known |= bit;
    if (known && was == level) return;
    bool first_layer = l->state == LOOPER_RECORDING;
    if (!first_layer && !l->dubbing) return;
    if (known && !(l->pot_taken & bit)) store_pot(l, 0, pot, was); /* where it was when the take began */
    l->pot_taken |= bit;
    store_pot(l, now_us - (first_layer ? l->start_us : l->dub_base_us), pot, level);
}
