#include "viz.h"

#include <string.h>

void viz_init(viz_t *v)
{
    memset(v, 0, sizeof(*v));
}

int viz_column(uint8_t note)
{
    int c = ((int)note - VIZ_NOTE_LOW) * 8 / VIZ_NOTE_SPAN;
    if (note < VIZ_NOTE_LOW || c < 0) return 0;
    return c > 7 ? 7 : c;
}

static void apply(viz_t *v, uint8_t ch, uint8_t note, bool on)
{
    uint8_t bit = (uint8_t)(1u << (note & 7));
    if (on) {
        v->sounding[ch][note >> 3] |= bit;
        v->hit |= (uint8_t)(1u << viz_column(note));
    } else {
        v->sounding[ch][note >> 3] &= (uint8_t)~bit;
    }
}

void viz_midi(viz_t *v, const uint8_t *msg, size_t len, int64_t at_us, int64_t now_us)
{
    if (len < 3) return;
    uint8_t kind = msg[0] & 0xf0;
    uint8_t ch = msg[0] & 0x0f;
    if (ch >= VIZ_CHANNELS) return;
    if (kind == 0xb0 && (msg[1] == 120 || msg[1] == 123)) {
        memset(v->sounding[ch], 0, sizeof(v->sounding[ch]));
        return;
    }
    if (kind != 0x90 && kind != 0x80) return;
    bool on = kind == 0x90 && msg[2] > 0;
    uint8_t note = msg[1] & 0x7f;
    if (at_us <= now_us || v->queued == VIZ_QUEUE) { /* due, or no room: show it now */
        apply(v, ch, note, on);
        return;
    }
    v->queue[v->queued++] = (viz_event_t){.at_us = at_us, .ch = ch, .note = note, .on = on};
}

bool viz_update(viz_t *v, int64_t now_us)
{
    /* Due notes, oldest first: an on and its off due together still drop a dot. */
    for (;;) {
        size_t first = VIZ_QUEUE;
        for (size_t i = 0; i < v->queued; i++) {
            if (v->queue[i].at_us <= now_us
                && (first == VIZ_QUEUE || v->queue[i].at_us < v->queue[first].at_us)) {
                first = i;
            }
        }
        if (first == VIZ_QUEUE) break;
        viz_event_t e = v->queue[first];
        v->queue[first] = v->queue[--v->queued];
        apply(v, e.ch, e.note, e.on);
    }
    if (now_us < v->next_row_us) return false;
    v->next_row_us = (v->next_row_us && now_us - v->next_row_us < VIZ_ROW_US) ? v->next_row_us + VIZ_ROW_US
                                                                              : now_us + VIZ_ROW_US;
    uint8_t active = v->hit;
    v->hit = 0;
    for (size_t ch = 0; ch < VIZ_CHANNELS; ch++) {
        for (int n = 0; n < 128; n++) {
            if (v->sounding[ch][n >> 3] & (1u << (n & 7))) active |= (uint8_t)(1u << viz_column((uint8_t)n));
        }
    }
    bool changed = false;
    for (int c = 0; c < 8; c++) {
        uint8_t next = (uint8_t)(v->cols[c] << 1 | ((active >> c) & 1));
        changed |= next != v->cols[c];
        v->cols[c] = next;
    }
    return changed;
}
