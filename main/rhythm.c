#include "rhythm.h"

#include <stdio.h>

#include "keymap.h"

/* General MIDI drum notes. */
#define KICK      36
#define SIDESTICK 37
#define SNARE     38
#define CLAP      39
#define LOW_TOM   41
#define HAT       42
#define OPEN_HAT  46
#define RIDE      51
#define AGOGO_HI  67
#define AGOGO_LO  68
#define MARACAS   70

/* Style buttons' order. Hand-aligned so a beat's lanes line up step by step. */
// clang-format off
static const rhythm_style_t STYLES[RHYTHM_STYLES] = {
    {"SLOW ROCK", 66, 3, 12, { /* 12/8 */
        {KICK,      "x.....x.x..."},
        {SNARE,     "...X.....X.."},
        {HAT,       "Xxxxxxxxxxxx"}}},
    {"ROCK", 116, 4, 16, {
        {KICK,      "x.......x.x....."},
        {SNARE,     "....X.......X..."},
        {HAT,       "X.x.x.x.X.x.x.x."}}},
    {"NEW AGE", 80, 4, 16, {
        {KICK,      "x.........x....."},
        {SIDESTICK, "....x.......x..."},
        {RIDE,      "x...x...x...x.x."}}},
    {"DISCO", 120, 4, 16, {
        {KICK,      "X...x...X...x..."},
        {CLAP,      "....x.......x..."},
        {HAT,       "x...x...x...x..."},
        {OPEN_HAT,  "..x...x...x...x."}}},
    {"MARCH", 112, 4, 16, {
        {KICK,      "X.......x......."},
        {SNARE,     "X.xxx.x.X.xxX.xx"}}},
    {"WALTZ", 96, 2, 6, { /* 3/4 */
        {KICK,      "X....."},
        {HAT,       "..x.x."},
        {SNARE,     "..x.x."}}},
    {"SAMBA", 100, 4, 16, {
        {LOW_TOM,   "X..xX..xX..xX..x"},
        {MARACAS,   "xxXxxxXxxxXxxxXx"},
        {AGOGO_HI,  "x.x...x...x.x..."},
        {AGOGO_LO,  "....x...x.....x."}}},
    {"BLUES", 88, 3, 12, { /* shuffle */
        {KICK,      "x.....x....."},
        {SNARE,     "...X.....X.."},
        {HAT,       "x.xx.xx.xx.x"}}},
};
// clang-format on

const rhythm_style_t *rhythm_style(size_t style)
{
    return &STYLES[style];
}

int64_t rhythm_step_us(const rhythm_style_t *s)
{
    return 60000000LL / ((int64_t)s->bpm * s->steps_per_beat);
}

static void midi3(midi_send_fn send, void *ctx, uint8_t status, uint8_t note, uint8_t velocity)
{
    uint8_t msg[3] = {status, note, velocity};
    send(msg, 3, NULL, ctx);
}

/* Note offs for the hits of the last step. */
static void stop_hits(rhythm_t *r, midi_send_fn send, void *ctx)
{
    const rhythm_style_t *s = &STYLES[r->style];
    for (size_t i = 0; i < RHYTHM_LANES; i++) {
        if (r->sounding & (1u << i)) midi3(send, ctx, 0x80 | DRUM_CH, s->lane[i].note, 0);
    }
    r->sounding = 0;
}

static void log_style(const char *verb, size_t style, midi_send_fn send, void *ctx)
{
    char what[40];
    snprintf(what, sizeof(what), "beat %s %s (%u bpm)", verb, STYLES[style].name, STYLES[style].bpm);
    send(NULL, 0, what, ctx);
}

void rhythm_toggle(rhythm_t *r, size_t style, int64_t now_us, midi_send_fn send, void *ctx)
{
    stop_hits(r, send, ctx);
    r->on = !r->on;
    if (r->on) {
        r->style = r->next_style = style;
        r->step = 0;
        r->next_us = now_us;
        log_style("start", style, send, ctx);
    } else {
        send(NULL, 0, "beat stop", ctx);
    }
}

void rhythm_select(rhythm_t *r, size_t style)
{
    r->next_style = style;
}

void rhythm_tick(rhythm_t *r, int64_t now_us, midi_send_fn send, void *ctx)
{
    if (!r->on || now_us < r->next_us) return;
    stop_hits(r, send, ctx);
    if (r->step == 0 && r->style != r->next_style) {
        r->style = r->next_style;
        log_style("now", r->style, send, ctx);
    }
    const rhythm_style_t *s = &STYLES[r->style];
    for (size_t i = 0; i < RHYTHM_LANES; i++) {
        if (!s->lane[i].note) continue;
        char hit = s->lane[i].hits[r->step];
        if (hit == '.') continue;
        midi3(send, ctx, 0x90 | DRUM_CH, s->lane[i].note, hit == 'X' ? RHYTHM_ACCENT : RHYTHM_VELOCITY);
        r->sounding |= (uint8_t)(1u << i);
    }
    r->next_us += rhythm_step_us(s);
    r->step = (r->step + 1) % s->steps;
}
