#include "groove.h"

#include <string.h>

static int64_t floor_div(int64_t a, int64_t b)
{
    return a / b - (a % b != 0 && (a < 0) != (b < 0));
}

void groove_init(groove_t *g)
{
    memset(g, 0, sizeof(*g));
}

void groove_set_grid(groove_t *g, int64_t origin_us, int64_t step_us, int steps)
{
    if (steps > GROOVE_MAX_STEPS) steps = GROOVE_MAX_STEPS;
    bool same = step_us == g->step_us && steps == g->steps
                && (!step_us || (origin_us - g->origin_us) % (step_us * steps) == 0);
    if (same) return; /* the same bar lines: keep the origin, so step numbers run on */
    g->origin_us = origin_us;
    g->step_us = step_us;
    g->steps = steps;
    g->on_time = g->off = 0;
    g->has_last = false;
    g->wiped = INT64_MIN; /* the first tick starts from wherever the beat is */
}

static int64_t abs_step(const groove_t *g, int64_t t)
{
    return floor_div(t - g->origin_us, g->step_us);
}

int groove_step(const groove_t *g, int64_t now_us)
{
    if (!g->step_us) return -1;
    int64_t s = abs_step(g, now_us) % g->steps;
    return (int)(s < 0 ? s + g->steps : s);
}

void groove_tick(groove_t *g, int64_t now_us)
{
    if (!g->step_us) return;
    int64_t now = abs_step(g, now_us);
    if (g->wiped == INT64_MIN || now - g->wiped > g->steps) g->wiped = now - 1;
    for (; g->wiped < now; g->wiped++) {
        int64_t s = (g->wiped + 1 + 2) % g->steps; /* 2 steps ahead of the one starting */
        if (s < 0) s += g->steps;
        uint32_t bit = 1u << s;
        g->on_time &= ~bit;
        g->off &= ~bit;
    }
}

void groove_press(groove_t *g, int64_t now_us)
{
    if (!g->step_us) return;
    int64_t nearest = floor_div(now_us - g->origin_us + g->step_us / 2, g->step_us);
    int64_t offset = now_us - (g->origin_us + nearest * g->step_us);
    int64_t s = nearest % g->steps;
    if (s < 0) s += g->steps;
    uint32_t bit = 1u << s;
    bool on_time = offset <= GROOVE_ON_TIME_US && offset >= -GROOVE_ON_TIME_US;
    g->on_time = on_time ? g->on_time | bit : g->on_time & ~bit;
    g->off = on_time ? g->off & ~bit : g->off | bit;
    g->last_offset_us = (int)offset;
    g->has_last = true;
}
