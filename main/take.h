#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "midi_out.h"

/* What the looper records of the live keys: the notes the chicks' effects play
 * (the user, 2026-11-08: a take with Echo and Octave played back without them).
 * Pure (no ESP-IDF), host-tested.
 *
 * main feeds it everything fx sends; it passes on, as notes on KEY_CH, the notes
 * on KEY_CH (the keys, Octave, Arp, Stutter) and on FX_ECHO_CH (Echo's repeats),
 * and turns Hold (CC64 on KEY_CH) into late note offs: a layer has one channel and
 * no pedal. Wide's copies (detuned on their own channels) and the bends of Rise and
 * Tape are not passed on: they colour the whole channel, not notes.
 *
 * One note from two sources is one note on the layer. An echo of a note whose key is
 * still held is left out: on one channel it would cut the held note short (2026-11-08,
 * held notes played back 350 ms long, Echo's spacing); live it plays on its own
 * channel over the held one. Otherwise a second note on retriggers it (off, then on)
 * and it ends with the last note off. */
typedef struct {
    uint8_t key[128];  /* note ons on KEY_CH not yet ended, per note */
    uint8_t echo[128]; /* the same for Echo's repeats */
    uint8_t skip[128]; /* Echo's repeats left out (over a held key): their note offs too */
    uint8_t hung[16];  /* bit per note: ended while Hold was down, off still to send */
    bool pedal;
} take_t;

void take_init(take_t *t);

/* One message fx sent; out gets what the looper should record. */
void take_msg(take_t *t, const uint8_t *msg, size_t len, midi_send_fn out, void *ctx);
