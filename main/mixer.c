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
    for (size_t i = 0; i < LOOPER_LAYERS; i++) m->layer_filter[i] = m->layer_keys[i] = MIXER_LEVEL;
    for (size_t i = 0; i < MIXER_POTS; i++) m->pot[i] = m->rest[i] = MIXER_LEVEL;
    m->pot[MIXER_REVERB] = m->rest[MIXER_REVERB] = 0;
    m->pot[MIXER_PLUCK] = m->rest[MIXER_PLUCK] = 0;
    m->shape_sent = -1;
    m->pot[MIXER_TREMOLO] = m->rest[MIXER_TREMOLO] = 0;
    m->pot[MIXER_GLIDE] = m->rest[MIXER_GLIDE] = 0;
    m->pot[MIXER_RELEASE] = m->rest[MIXER_RELEASE] = m->release = 64;
    for (size_t i = 0; i < LOOPER_LAYERS; i++) m->layer_release[i] = 64;
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
        case MIXER_PLUCK: m->pluck = v; break;
        case MIXER_GLIDE: m->glide = v; break;
        case MIXER_RELEASE: m->release = v; break;
        case MIXER_TREMOLO: m->tremolo = v; break;
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
    int step = level - m->pot[pot];
    bool settled = now_us - m->changed_us[pot] >= MIXER_SETTLE_US;
    if (!fresh && !end && settled && step < MIXER_WAKE && step > -MIXER_WAKE) return false;
    m->changed_us[pot] = now_us;
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

/* Pluck: gen 8 lowered by the depth, the filter envelope opening it back to the
 * filter pot's cutoff at once and closing in ~200 ms. */
static int pluck_steps(uint8_t level)
{
    return level * MIXER_PLUCK_CENTS / MIXER_LEVEL
           / 2; /* initialFilterFc, modEnvToFilterFc: 2 cents a step */
}

/* What gen 8 gets: the filter pot's cutoff lowered by the pluck's depth. */
static int cutoff_offset(uint8_t filter, uint8_t pluck)
{
    return filter_offset(filter) - pluck_steps(pluck);
}

static int release_offset(uint8_t level)
{
    return (2 * level - MIXER_LEVEL) * MIXER_RELEASE_TC / MIXER_LEVEL; /* releaseVolEnv: timecents */
}

static int tremolo_offset(uint8_t level)
{
    return level * MIXER_TREMOLO_STEPS / MIXER_LEVEL; /* modLfoToVolume: 1 cB a step */
}

/* Glide: portamento on (CC65) with its time in ms (CC5 MSB, CC37 LSB); level 0 off. pc_synth.sh
 * sets FluidSynth's portamento mode to each note: its default glides only between overlapping notes. */
static void send_glide(uint8_t ch, uint8_t level, const char *what, midi_send_fn send, void *ctx)
{
    unsigned ms = (unsigned)level * MIXER_GLIDE_MS / MIXER_LEVEL;
    uint8_t st = (uint8_t)(0xb0 | ch);
    const uint8_t msgs[3][3] = {
        {st, 5, (uint8_t)(ms >> 7)}, {st, 37, (uint8_t)(ms & 0x7f)}, {st, 65, level ? 127 : 0}};
    for (int i = 0; i < 3; i++) send(msgs[i], 3, i == 2 ? what : NULL, ctx);
}

#define GEN_FILTER  8
#define GEN_PLUCK   11 /* modEnvToFilterFc */
#define GEN_TREMOLO 13
#define GEN_REVERB  16
#define GEN_RELEASE 38

/* The pluck's filter envelope (delay, attack, hold: none; decay ~200 ms; sustain 0),
 * as offsets on generators 25-29. Off (all 0) leaves the preset's own: FluidR3's saw
 * lead has a slow one of its own, which kept a pluck dark (rendered 2026-11-10). */
static void send_shape(uint8_t ch, bool on, midi_send_fn send, void *ctx)
{
    static const int SHAPE[5] = {-6000, -6000, -6000, 4607, 1000};
    for (int i = 0; i < 5; i++) send_nrpn(ch, (uint8_t)(25 + i), on ? SHAPE[i] : 0, NULL, send, ctx);
}

/* The NRPN pots but the filter and pluck: their generator and offset at a level; false for the others. */
static bool nrpn_of(mixer_pot_t pot, uint8_t level, uint8_t *gen, int *offset)
{
    switch (pot) {
        case MIXER_REVERB:
            *gen = GEN_REVERB;
            *offset = reverb_offset(level);
            return true;
        case MIXER_RELEASE:
            *gen = GEN_RELEASE;
            *offset = release_offset(level);
            return true;
        case MIXER_TREMOLO:
            *gen = GEN_TREMOLO;
            *offset = tremolo_offset(level);
            return true;
        default: return false;
    }
}

void mixer_send_filter_to(uint8_t ch, uint8_t level, uint8_t pluck, midi_send_fn send, void *ctx)
{
    send_nrpn(ch, GEN_FILTER, cutoff_offset(level, pluck), NULL, send, ctx);
}

void mixer_send_pot_to(uint8_t ch, mixer_pot_t pot, uint8_t level, uint8_t pluck, midi_send_fn send,
                       void *ctx)
{
    if (pot == MIXER_KEYS) {
        uint8_t cc[3] = {(uint8_t)(0xb0 | ch), 11, level};
        send(cc, 3, NULL, ctx);
        return;
    }
    if (pot == MIXER_FILTER) {
        mixer_send_filter_to(ch, level, pluck, send, ctx);
        return;
    }
    uint8_t gen;
    int offset;
    if (nrpn_of(pot, level, &gen, &offset)) send_nrpn(ch, gen, offset, NULL, send, ctx);
}

void mixer_send_fx(mixer_t *m, mixer_pot_t pot, midi_send_fn send, void *ctx)
{
    char what[40];
    uint8_t gen;
    int offset;
    if (pot == MIXER_FILTER) {
        gen = GEN_FILTER;
        offset = cutoff_offset(m->filter, m->pluck);
        snprintf(what, sizeof(what), "filter %u (%d cents)", (unsigned)m->filter,
                 filter_offset(m->filter) * 2);
    } else if (pot == MIXER_PLUCK) {
        bool on = m->pluck > 0;
        bool shape = m->shape_sent != (on ? 1 : 0); /* only when it turns on or off: the flood */
        m->shape_sent = on ? 1 : 0;
        snprintf(what, sizeof(what), "pluck %u (%d cents)", (unsigned)m->pluck, pluck_steps(m->pluck) * 2);
        for (uint8_t ch = 0; ch < MIXER_FX_LIVE; ch++) {
            if (shape) send_shape(ch, on, send, ctx);
            send_nrpn(ch, GEN_FILTER, cutoff_offset(m->filter, m->pluck), NULL, send, ctx);
            send_nrpn(ch, GEN_PLUCK, pluck_steps(m->pluck), ch == 0 ? what : NULL, send, ctx);
        }
        return;
    } else if (pot == MIXER_REVERB) {
        gen = GEN_REVERB;
        offset = reverb_offset(m->reverb);
        snprintf(what, sizeof(what), "reverb %u (send %+d.%d%%)", (unsigned)m->reverb, offset / 10,
                 (offset < 0 ? -offset : offset) % 10);
    } else if (pot == MIXER_GLIDE) {
        snprintf(what, sizeof(what), "glide %u (%u ms)", (unsigned)m->glide,
                 (unsigned)m->glide * MIXER_GLIDE_MS / MIXER_LEVEL);
        for (uint8_t ch = 0; ch < MIXER_FX_LIVE; ch++)
            send_glide(ch, m->glide, ch == 0 ? what : NULL, send, ctx);
        return;
    } else if (pot == MIXER_RELEASE || pot == MIXER_TREMOLO) {
        static const char *const NAME[] = {[MIXER_RELEASE] = "release", [MIXER_TREMOLO] = "tremolo"};
        uint8_t level = m->pot[pot];
        nrpn_of(pot, level, &gen, &offset);
        snprintf(what, sizeof(what), "%s %u (%+d)", NAME[pot], (unsigned)level, offset);
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
    char what[112];
    snprintf(what, sizeof(what),
             "layer %u keeps keys %u, filter %u, reverb %u, pluck %u, release %u, tremolo %u, glide %u",
             (unsigned)layer + 1, (unsigned)m->layer_keys[layer], (unsigned)m->layer_filter[layer],
             (unsigned)m->layer_reverb[layer], (unsigned)m->layer_pluck[layer],
             (unsigned)m->layer_release[layer], (unsigned)m->layer_tremolo[layer],
             (unsigned)m->layer_glide[layer]);
    uint8_t ch = (uint8_t)(LOOP_CH + layer);
    uint8_t pluck = m->layer_pluck[layer];
    /* Pot 3's fader as it was (CC11 on the keys' channel isn't in the recording). */
    uint8_t cc[3] = {(uint8_t)(0xb0 | ch), 11, m->layer_keys[layer]};
    send(cc, 3, NULL, ctx);
    send_shape(ch, pluck > 0, send, ctx);
    send_nrpn(ch, GEN_FILTER, cutoff_offset(m->layer_filter[layer], pluck), NULL, send, ctx);
    send_nrpn(ch, GEN_PLUCK, pluck_steps(pluck), NULL, send, ctx);
    send_nrpn(ch, GEN_RELEASE, release_offset(m->layer_release[layer]), NULL, send, ctx);
    send_nrpn(ch, GEN_TREMOLO, tremolo_offset(m->layer_tremolo[layer]), NULL, send, ctx);
    send_glide(ch, m->layer_glide[layer], NULL, send, ctx);
    send_nrpn(ch, GEN_REVERB, reverb_offset(m->layer_reverb[layer]), what, send, ctx);
}

void mixer_freeze_layer(mixer_t *m, size_t layer, midi_send_fn send, void *ctx)
{
    if (layer >= LOOPER_LAYERS) return;
    m->layer_filter[layer] = m->filter;
    m->layer_reverb[layer] = m->reverb;
    m->layer_keys[layer] = m->keys;
    m->layer_pluck[layer] = m->pluck;
    m->layer_release[layer] = m->release;
    m->layer_tremolo[layer] = m->tremolo;
    m->layer_glide[layer] = m->glide;
    mixer_send_layer_fx(m, layer, send, ctx);
}
