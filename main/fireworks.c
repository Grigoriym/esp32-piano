#include "fireworks.h"

#include <math.h>
#include <string.h>

static uint32_t next(fw_t *f)
{
    uint32_t x = f->rng; /* xorshift32 */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return f->rng = x;
}

static float rnd(fw_t *f, float a, float b)
{
    return a + (b - a) * (float)(next(f) >> 8) / (float)(1u << 24);
}

void fw_reroll(fw_t *f)
{
    f->gravity = rnd(f, 18.0f, 40.0f);
    f->burst = (int)rnd(f, 6.0f, 14.0f);
}

void fw_init(fw_t *f, uint32_t seed)
{
    memset(f, 0, sizeof(*f));
    f->rng = seed ? seed : 1;
    fw_reroll(f);
}

int fw_column(uint8_t note)
{
    if (note < FW_NOTE_LOW) return 0;
    int c = ((int)note - FW_NOTE_LOW) * FW_W / FW_NOTE_SPAN;
    return c >= FW_W ? FW_W - 1 : c;
}

static void launch(fw_t *f, uint8_t note)
{
    if (f->n_rockets == FW_ROCKETS) return;
    int c = fw_column(note);
    for (int i = 0; i < f->n_rockets; i++)
        if ((int)f->rocket[i].x == c) return; /* a chord: one rocket per column */
    /* Higher notes fly higher: the top note reaches ~row 1, the lowest ~two thirds down. */
    int span = (int)note - FW_NOTE_LOW;
    if (span < 0) span = 0;
    if (span > FW_NOTE_SPAN) span = FW_NOTE_SPAN;
    float top = 1.0f + (float)(FW_NOTE_SPAN - span) * (FW_H * 0.7f) / FW_NOTE_SPAN;
    f->rocket[f->n_rockets++] = (fw_rocket_t){.x = (float)c, .y = FW_H - 1, .top = top};
}

void fw_note(fw_t *f, uint8_t note, int64_t at_us)
{
    if (at_us <= f->last_us || f->n_pending == FW_PENDING) {
        launch(f, note);
        return;
    }
    f->pending[f->n_pending++] = (fw_pending_t){.at_us = at_us, .note = note};
}

static void burst(fw_t *f, int i)
{
    const fw_rocket_t *r = &f->rocket[i];
    for (int k = 0; k < f->burst && f->n_sparks < FW_SPARKS; k++) {
        float a = rnd(f, 0.0f, 6.2832f);
        float v = rnd(f, 4.0f, 12.0f);
        f->spark[f->n_sparks++] = (fw_spark_t){
            .x = r->x, .y = r->y, .vx = cosf(a) * v * 0.5f, .vy = sinf(a) * v, .life = rnd(f, 0.6f, 1.3f)};
    }
    f->rocket[i] = f->rocket[--f->n_rockets];
}

void fw_beat(fw_t *f, bool bar_line, int64_t now_us)
{
    (void)now_us;
    while (f->n_rockets) burst(f, f->n_rockets - 1);
    if (bar_line && ++f->bars % FW_REROLL_BARS == 0) fw_reroll(f);
}

void fw_tick(fw_t *f, int64_t now_us)
{
    float dt = f->last_us ? (float)(now_us - f->last_us) / 1e6f : 0.0f;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;
    f->last_us = now_us;
    for (int i = 0; i < f->n_pending;) {
        if (f->pending[i].at_us <= now_us) {
            launch(f, f->pending[i].note);
            f->pending[i] = f->pending[--f->n_pending];
        } else {
            i++;
        }
    }
    for (int i = 0; i < f->n_rockets;) {
        fw_rocket_t *r = &f->rocket[i];
        if (r->y > r->top) {
            r->y -= FW_CLIMB * dt;
            if (r->y <= r->top) {
                r->y = r->top;
                r->topped_us = now_us;
            }
        }
        if (r->topped_us && now_us - r->topped_us >= FW_WAIT_US) {
            burst(f, i); /* no beat came: burst on its own */
            continue;
        }
        i++;
    }
    for (int i = 0; i < f->n_sparks;) {
        fw_spark_t *s = &f->spark[i];
        s->x += s->vx * dt;
        s->y += s->vy * dt;
        s->vy += f->gravity * dt;
        s->life -= dt;
        if (s->life <= 0.0f || s->y >= FW_H + 1) {
            f->spark[i] = f->spark[--f->n_sparks];
            continue;
        }
        i++;
    }
}

static void dot(uint8_t out[FW_H], float fx, float fy)
{
    int x = (int)lroundf(fx);
    int y = (int)lroundf(fy);
    if (x >= 0 && x < FW_W && y >= 0 && y < FW_H) out[y] |= (uint8_t)(1u << x);
}

void fw_draw(const fw_t *f, uint8_t out[FW_H])
{
    memset(out, 0, FW_H);
    for (int i = 0; i < f->n_rockets; i++) {
        dot(out, f->rocket[i].x, f->rocket[i].y);
        dot(out, f->rocket[i].x, f->rocket[i].y + 1.0f); /* a short tail */
    }
    for (int i = 0; i < f->n_sparks; i++) dot(out, f->spark[i].x, f->spark[i].y);
}
