#include "take.h"

#include <string.h>

#include "fx.h"
#include "keymap.h"

void take_init(take_t *t)
{
    memset(t, 0, sizeof(*t));
}

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

static void out3(midi_send_fn out, void *ctx, uint8_t status, uint8_t n, uint8_t vel)
{
    uint8_t m[3] = {status, n, vel};
    out(m, 3, NULL, ctx);
}

void take_msg(take_t *t, const uint8_t *msg, size_t len, midi_send_fn out, void *ctx)
{
    if (len != 3) return;
    uint8_t kind = msg[0] & 0xf0;
    uint8_t ch = msg[0] & 0x0f;
    if (kind == 0xb0 && ch == KEY_CH && msg[1] == 64) {
        t->pedal = msg[2] >= 64;
        if (t->pedal) return;
        for (int n = 0; n < 128; n++) {
            if (!get_bit(t->hung, (uint8_t)n)) continue;
            set_bit(t->hung, (uint8_t)n, false);
            out3(out, ctx, 0x80 | KEY_CH, (uint8_t)n, 0);
        }
        return;
    }
    if ((kind != 0x90 && kind != 0x80) || (ch != KEY_CH && ch != FX_ECHO_CH)) return;
    uint8_t n = msg[1] & 0x7f;
    bool echo = ch == FX_ECHO_CH;
    uint8_t *mine = echo ? &t->echo[n] : &t->key[n];
    if (kind == 0x90 && msg[2]) {
        if (echo && t->key[n]) { /* over the held key */
            if (t->skip[n] < 255) t->skip[n]++;
            return;
        }
        if (t->key[n] || t->echo[n] || get_bit(t->hung, n))
            out3(out, ctx, 0x80 | KEY_CH, n, 0); /* retriggered */
        set_bit(t->hung, n, false);
        if (*mine < 255) (*mine)++;
        out3(out, ctx, 0x90 | KEY_CH, n, msg[2]);
        return;
    }
    if (echo && t->skip[n]) {
        t->skip[n]--;
        return;
    }
    if (!*mine) return;
    (*mine)--;
    if (t->key[n] || t->echo[n]) return; /* the other source still holds it */
    if (t->pedal) {
        set_bit(t->hung, n, true);
    } else {
        out3(out, ctx, 0x80 | KEY_CH, n, 0);
    }
}
