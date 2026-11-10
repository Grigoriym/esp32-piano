/*
 * The 8 chicks are effects on the live keys (2026-11-06, the user's ask; they
 * played drums before). Pure (no ESP-IDF), host-tested.
 *
 * Top row (chicks 1-4, from the left as the user counts them), each press on / off:
 *   Echo     each note comes back fx_echo_repeats() times, each fx_echo_decay() % of the one before, a dotted
 *            8th apart (on FX_ECHO_CH); how much: f->echo, FX_ECHO_LEVEL (its pot became Detune, 2026-11-09, then Attack)
 *   Hold     sustain (CC64): notes ring after the key is let go
 *   Octave   each key plays an octave higher too
 *   Arp      held keys play one at a time, lowest to highest, on every 16th of the grid
 * Bottom row (chicks 5-8, from the left):
 *   Rise     one bar: the keys bend up an octave and the filter opens; then back
 *   Tape     half a bar: keys and loop bend down an octave, the keys go dark; then the keys stop
 *   Stutter  while held: the keys still sounding retrigger on every 16th
 *   Wide     on / off: two copies of each note, detuned FX_DETUNE either way (on
 *            FX_WIDE_CH, FX_WIDE2_CH). CC93 chorus and CC10 pan changed nothing on
 *            FluidR3's saw lead (rendered 2026-11-06), so it is a thicker sound, not a stereo one.
 *
 * The looper records the notes they make (take.h: Echo, Hold, Octave, Arp, Stutter; not
 * Wide, Rise, Tape). The grid is the
 * beat's while it plays, else the metronome's (main.c, every scan). main.c copies
 * every control and program change on KEY_CH to the effect channels, so they
 * follow the sound, the volume, the keys' fader, the filter and the reverb.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "midi_out.h"

#define FX_ECHO_CH    10 /* MIDI channel 11 */
#define FX_WIDE_CH    11
#define FX_WIDE2_CH   12
#define FX_BEND_RANGE 12     /* semitones, set by RPN 0 on the keys', DEMO's, the loop's and these channels */
#define FX_DETUNE     82     /* pitch bend units: 12 cents at FX_BEND_RANGE */
#define FX_ECHO_MAX   8      /* repeats at the Echo pot's top */
#define FX_ECHO_LEVEL 64     /* the Echo pot's level before it is read */
#define FX_ECHO_STEPS 3      /* a dotted 8th */
#define FX_RISE_STEPS 16     /* one bar */
#define FX_TAPE_STEPS 8      /* half a bar */
#define FX_RAMP_US    20000  /* a ramp sends its bend and filter this often */
#define FX_STEP_US    125000 /* a 16th when there is no grid: 120 bpm */
#define FX_QUEUE      256

typedef enum {
    FX_ECHO,
    FX_HOLD,
    FX_OCTAVE,
    FX_ARP,
    FX_RISE,
    FX_TAPE,
    FX_STUTTER,
    FX_WIDE,
    FX_COUNT
} fx_kind_t;

typedef struct {
    int64_t t_us;
    uint8_t msg[3];
} fx_event_t;

typedef struct {
    bool on[FX_COUNT];                    /* toggles; Rise and Tape: running; Stutter: held */
    int64_t grid_origin_us, sixteenth_us; /* 16th lines for Arp and Stutter; 0: FX_STEP_US from the press */
    uint8_t filter;                       /* pot 4's level: where Rise and Tape start from */
    uint8_t pluck;                        /* the Pluck pot's level: Rise and Tape keep it */
    uint8_t echo;                         /* the Echo pot's level, 0-127 */
    int64_t ramp_start_us, ramp_len_us, ramp_sent_us;
    bool restore;                                 /* a ramp just ended: main.c sends the pots' filter again */
    uint8_t held[16];                             /* Arp: keys held, bit per note */
    uint8_t vel[128];                             /* each note's velocity as pressed */
    uint8_t sounding[16];                         /* notes on KEY_CH from the keys (not their copies) */
    uint8_t doubled[16], widened[16], echoed[16]; /* per sounding note: its copies */
    int arp_note;                                 /* sounding from Arp; -1 none */
    int64_t next_step_us;                         /* Arp / Stutter: the next 16th; 0: not running */
    fx_event_t q[FX_QUEUE];                       /* echoes still to send, by time */
    size_t nq;
} fx_t;

void fx_init(fx_t *f);

/* Echo at the Echo pot's level: 1 to FX_ECHO_MAX repeats, each 40% to 90% of the one before. */
int fx_echo_repeats(uint8_t level);
int fx_echo_decay(uint8_t level);

/* "Echo", "Hold" ... for the log and the screen. */
const char *fx_name(fx_kind_t kind);

/* Pitch bend range and Wide's detune on their channels: at boot and when the PC link comes up. */
void fx_setup(midi_send_fn send, void *ctx);

/* The 16th lines (origin_us, sixteenth_us apart); sixteenth_us 0: none. */
void fx_set_grid(fx_t *f, int64_t origin_us, int64_t sixteenth_us);

/* A chick pressed or let go. */
void fx_button(fx_t *f, fx_kind_t kind, bool down, int64_t now_us, midi_send_fn send, void *ctx);

/* A note on or off the keys play on KEY_CH: sent through the effects. */
void fx_key(fx_t *f, const uint8_t *msg, size_t len, int64_t now_us, midi_send_fn send, void *ctx);

/* Call often (every scan): echoes, Arp and Stutter steps, the ramps. */
void fx_tick(fx_t *f, int64_t now_us, midi_send_fn send, void *ctx);

/* RESET: every effect off. */
void fx_reset(fx_t *f, midi_send_fn send, void *ctx);
