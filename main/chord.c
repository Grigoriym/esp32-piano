#include "chord.h"

#include <stdio.h>

static const char *const NOTE[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
#define KEY1_PITCH 5 /* F */

#define BITS3(a, b, c) (1u | 1u << (a) | 1u << (b) | 1u << (c))
#define BITS2(a, b)    (1u | 1u << (a) | 1u << (b))
static const struct {
    uint16_t shape; /* bit n: n semitones above the root */
    const char *suffix;
} SHAPES[] = {
    {BITS2(4, 7), ""},       {BITS2(3, 7), "m"},       {BITS2(3, 6), "dim"},      {BITS2(4, 8), "aug"},
    {BITS2(5, 7), "sus4"},   {BITS2(2, 7), "sus2"},    {BITS3(4, 7, 10), "7"},    {BITS3(4, 7, 11), "maj7"},
    {BITS3(3, 7, 10), "m7"}, {BITS3(3, 6, 9), "dim7"}, {BITS3(3, 6, 10), "m7b5"}, {1u | 1u << 7, "5"},
};

/* The pitches relative to root: bit n = n semitones above it. */
static uint16_t relative(uint16_t pitches, int root)
{
    uint16_t out = 0;
    for (int p = 0; p < 12; p++) {
        if (pitches & (1u << p)) out |= (uint16_t)(1u << ((p - root + 12) % 12));
    }
    return out;
}

void chord_name(uint32_t keys, char *out, size_t size)
{
    uint16_t pitches = 0;
    int bass = -1;
    int count = 0;
    for (int k = 0; k < 32; k++) {
        if (!(keys & (1ul << k))) continue;
        int p = (KEY1_PITCH + k) % 12;
        if (bass < 0) bass = p;
        if (!(pitches & (1u << p))) count++;
        pitches |= (uint16_t)(1u << p);
    }
    if (count == 0) {
        snprintf(out, size, "%s", "");
        return;
    }
    if (count == 1) {
        snprintf(out, size, "%s", NOTE[bass]);
        return;
    }
    /* The bass first: shapes that fit several roots (aug, dim7, sus) take it. */
    for (int i = 0; i < 12; i++) {
        int root = (bass + i) % 12;
        if (!(pitches & (1u << root))) continue;
        uint16_t rel = relative(pitches, root);
        for (size_t s = 0; s < sizeof(SHAPES) / sizeof(SHAPES[0]); s++) {
            if (rel == SHAPES[s].shape) {
                snprintf(out, size, "%s%s", NOTE[root], SHAPES[s].suffix);
                return;
            }
        }
    }
    snprintf(out, size, "?");
}
