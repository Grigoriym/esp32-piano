#pragma once
/* Test helper: records what a module sends through midi_send_fn. */
#include <stdint.h>
#include <string.h>

typedef struct {
    uint8_t msg[3];
    size_t len;
    char what[40];
} midi_rec_entry_t;

typedef struct {
    midi_rec_entry_t e[64];
    size_t n;
} midi_rec_t;

static inline void midi_rec(const uint8_t *msg, size_t len, const char *what, void *ctx)
{
    midi_rec_t *r = ctx;
    if (r->n == sizeof(r->e) / sizeof(r->e[0])) return;
    midi_rec_entry_t *x = &r->e[r->n++];
    memset(x, 0, sizeof(*x));
    if (len) memcpy(x->msg, msg, len);
    x->len = len;
    if (what) snprintf(x->what, sizeof(x->what), "%s", what);
}
