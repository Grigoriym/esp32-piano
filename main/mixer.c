#include "mixer.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define DEAD       100 /* the ESP32's ADC reads ~0 for the last bit of travel at each end */
#define SMOOTH     4   /* each reading moves the smoothed value 1/4 of the way */
#define HYSTERESIS 8   /* of MIXER_RAW, 1/4 of a level past the edge: noise cannot flicker a level */

void mixer_init(mixer_t *m)
{
    memset(m, 0, sizeof(*m));
    m->beat = m->click = m->keys = m->filter = m->metro = MIXER_LEVEL;
    for (size_t i = 0; i < LOOPER_LAYERS; i++) m->layer_filter[i] = MIXER_LEVEL;
    for (size_t i = 0; i < MIXER_POTS; i++) m->pot[i] = m->rest[i] = MIXER_LEVEL;
    m->pot[MIXER_REVERB] = m->rest[MIXER_REVERB] = 0;
}

/* Raw reading -> 0..MIXER_RAW with the dead ends cut off. */
static int32_t span(int raw)
{
    if (raw < DEAD) return 0;
    if (raw > MIXER_RAW - DEAD) return MIXER_RAW;
    return (int32_t)(raw - DEAD) * MIXER_RAW / (MIXER_RAW - 2 * DEAD);
}

static void apply(mixer_t *m, mixer_pot_t pot)
{
    uint8_t v = m->pot[pot];
    switch (pot) {
        case MIXER_BEAT: m->beat = v; break;
        case MIXER_KEYS: m->keys = v; break;
        case MIXER_FILTER: m->filter = v; break;
        case MIXER_REVERB: m->reverb = v; break;
        case MIXER_METRO: m->metro = v; break;
        case MIXER_CLICK: m->click = v; break;
    }
}

bool mixer_feed(mixer_t *m, mixer_pot_t pot, int raw, int64_t now_us)
{
    int32_t x = span(raw) * 16;
    bool fresh = !m->primed[pot];
    if (fresh) {
        m->primed[pot] = true;
        m->filt[pot] = x;
    } else {
        m->filt[pot] += (x - m->filt[pot]) / SMOOTH;
    }
    int32_t f = m->filt[pot] / 16;
    int level = (f * MIXER_LEVEL + MIXER_RAW / 2) / MIXER_RAW;
    if (level == m->pot[pot]) return false;
    /* Past the current level's own value by more than half a level plus a margin,
     * so noise can't flicker it; the ends are always reachable. */
    int32_t d = f - (int32_t)m->pot[pot] * MIXER_RAW / MIXER_LEVEL;
    if (d < 0) d = -d;
    bool end = level == 0 || level == MIXER_LEVEL;
    if (!fresh && !end && d < MIXER_RAW / MIXER_LEVEL / 2 + HYSTERESIS) return false;
    m->pot[pot] = (uint8_t)level;
    /* A turn (shows the LED bar): MIXER_TURN levels from the last one, or an end.
     * A key press or a WiFi send can tip a resting pot a level (2026-11-05). */
    int from = level - m->rest[pot];
    if (fresh) {
        m->rest[pot] = (uint8_t)level;
    } else if (from >= MIXER_TURN || from <= -MIXER_TURN || (end && from != 0)) {
        m->rest[pot] = (uint8_t)level;
        m->moved_us[pot] = now_us ? now_us : 1;
    }
    apply(m, pot);
    return true;
}

uint8_t mixer_scale(uint8_t velocity, uint8_t level)
{
    if (velocity == 0 || level == 0) return 0;
    unsigned v = ((unsigned)velocity * level + MIXER_LEVEL / 2) / MIXER_LEVEL;
    return (uint8_t)(v ? v : 1);
}

size_t mixer_apply(const uint8_t *msg, size_t len, uint8_t level, uint8_t *out)
{
    memcpy(out, msg, len);
    if (len == 3 && (msg[0] & 0xf0) == 0x90 && msg[2]) {
        if (level == 0) return 0; /* muted: not sent (its note off is harmless) */
        out[2] = mixer_scale(msg[2], level);
    }
    return len;
}

/* An SF2 NRPN: an offset (in the generator's units, here 1 a step) to a generator. */
static void send_nrpn(uint8_t ch, uint8_t gen, int offset, const char *what, midi_send_fn send, void *ctx)
{
    /* 99 = 120, 98 = the generator, then a 14-bit data entry centred on 8192.
     * FluidSynth applies it on the MSB (6) with the LSB (38) it already has, so
     * the LSB goes first. */
    int v = 8192 + offset;
    uint8_t st = (uint8_t)(0xb0 | ch);
    const uint8_t msgs[4][3] = {
        {st, 99, 120}, {st, 98, gen}, {st, 38, (uint8_t)(v & 0x7f)}, {st, 6, (uint8_t)(v >> 7)}};
    for (int i = 0; i < 4; i++) send(msgs[i], 3, i == 3 ? what : NULL, ctx);
}

static int filter_offset(uint8_t level)
{
    return (MIXER_LEVEL - level) * MIXER_FILTER_CENTS / MIXER_LEVEL / 2; /* initialFilterFc: 2 cents a step */
}

static int reverb_offset(uint8_t level)
{
    return (2 * level - MIXER_LEVEL) * MIXER_REVERB_SEND / MIXER_LEVEL; /* reverbEffectsSend: 0.1% a step */
}

#define GEN_FILTER 8
#define GEN_REVERB 16

void mixer_send_fx(const mixer_t *m, mixer_pot_t pot, midi_send_fn send, void *ctx)
{
    char what[40];
    uint8_t gen;
    int offset;
    if (pot == MIXER_FILTER) {
        gen = GEN_FILTER;
        offset = filter_offset(m->filter);
        snprintf(what, sizeof(what), "filter %u (%d cents)", (unsigned)m->filter, offset * 2);
    } else if (pot == MIXER_REVERB) {
        gen = GEN_REVERB;
        offset = reverb_offset(m->reverb);
        snprintf(what, sizeof(what), "reverb %u (send %+d.%d%%)", (unsigned)m->reverb, offset / 10,
                 (offset < 0 ? -offset : offset) % 10);
    } else if (pot == MIXER_KEYS || pot == MIXER_CLICK) {
        bool keys = pot == MIXER_KEYS;
        uint8_t level = keys ? m->keys : m->click;
        snprintf(what, sizeof(what), "%s %u", keys ? "keys" : "metronome volume", (unsigned)level);
        uint8_t cc[3] = {(uint8_t)(0xb0 | (keys ? KEY_CH : METRO_CH)), 11, level};
        send(cc, 3, what, ctx);
        return;
    } else {
        return;
    }
    for (uint8_t ch = 0; ch < MIXER_FX_LIVE; ch++)
        send_nrpn(ch, gen, offset, ch == 0 ? what : NULL, send, ctx);
}

void mixer_send_layer_fx(const mixer_t *m, size_t layer, midi_send_fn send, void *ctx)
{
    if (layer >= LOOPER_LAYERS) return;
    char what[48];
    snprintf(what, sizeof(what), "layer %u keeps filter %u, reverb %u", (unsigned)layer + 1,
             (unsigned)m->layer_filter[layer], (unsigned)m->layer_reverb[layer]);
    uint8_t ch = (uint8_t)(LOOP_CH + layer);
    send_nrpn(ch, GEN_FILTER, filter_offset(m->layer_filter[layer]), NULL, send, ctx);
    send_nrpn(ch, GEN_REVERB, reverb_offset(m->layer_reverb[layer]), what, send, ctx);
}

void mixer_freeze_layer(mixer_t *m, size_t layer, midi_send_fn send, void *ctx)
{
    if (layer >= LOOPER_LAYERS) return;
    m->layer_filter[layer] = m->filter;
    m->layer_reverb[layer] = m->reverb;
    mixer_send_layer_fx(m, layer, send, ctx);
}
