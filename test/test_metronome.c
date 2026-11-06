#include <stdbool.h>
#include <stdio.h>

#include "metronome.h"
#include "midi_rec.h"
#include "unity.h"

static metro_t m;
static midi_rec_t rec;
static int64_t due[64];

/* Records each message with the time it was given. */
static void send(const uint8_t *msg, size_t len, const char *what, void *ctx)
{
    if (rec.n < 64) due[rec.n] = *(const int64_t *)ctx;
    midi_rec(msg, len, what, &rec);
}

void setUp(void)
{
    m = (metro_t){0};
    memset(&rec, 0, sizeof(rec));
}

void tearDown(void)
{
}

/* Ticks every ms from t to t_end on a grid. */
static void run(int64_t t, int64_t t_end, int64_t origin, int64_t beat, int bpb)
{
    for (; t <= t_end; t += 1000) metro_tick(&m, t, origin, beat, bpb, send, &m.due_us);
}

/* The note ons in rec, from entry from: times into at, notes into note. Returns how many. */
static size_t clicks(size_t from, int64_t *at, uint8_t *note)
{
    size_t n = 0;
    for (size_t i = from; i < rec.n; i++) {
        if (rec.e[i].len == 3 && (rec.e[i].msg[0] & 0xf0) == 0x90) {
            at[n] = due[i];
            note[n++] = rec.e[i].msg[1];
        }
    }
    return n;
}

static void test_off_is_silent(void)
{
    run(0, 2000000, 0, 500000, 4);
    TEST_ASSERT_EQUAL(0, rec.n);
}

static void test_on_sends_the_woodblock_and_clicks_on_its_own_grid(void)
{
    metro_toggle(&m, 1000000, send, &m.due_us);
    TEST_ASSERT_EQUAL_HEX8(0xc0 | METRO_CH, rec.e[0].msg[0]);
    TEST_ASSERT_EQUAL(METRO_PROGRAM, rec.e[0].msg[1]);
    TEST_ASSERT_EQUAL_STRING("metronome on", rec.e[0].what);
    run(1000000, 3000000, m.origin_us, 500000, 4);
    int64_t at[16];
    uint8_t note[16];
    TEST_ASSERT_EQUAL(5, clicks(0, at, note)); /* 1.0, 1.5, 2.0, 2.5, 3.0 s */
    for (size_t i = 0; i < 5; i++) TEST_ASSERT_EQUAL_INT64(1000000 + (int64_t)i * 500000, at[i]);
    TEST_ASSERT_EQUAL(METRO_BAR_NOTE, note[0]);
    TEST_ASSERT_EQUAL(METRO_NOTE, note[1]);
    TEST_ASSERT_EQUAL(METRO_BAR_NOTE, note[4]); /* the next bar */
}

static void test_each_click_has_its_note_off(void)
{
    metro_toggle(&m, 0, send, &m.due_us);
    size_t before = rec.n;
    run(0, 0, 0, 500000, 4);
    TEST_ASSERT_EQUAL(before + 2, rec.n);
    TEST_ASSERT_EQUAL_HEX8(0x80 | METRO_CH, rec.e[before + 1].msg[0]);
    TEST_ASSERT_EQUAL_INT64(METRO_CLICK_US, due[before + 1]);
}

static void test_on_makes_its_channel_dry(void)
{
    metro_toggle(&m, 0, send, &m.due_us);
    bool cc91 = false, nrpn = false;
    int data = 0;
    for (size_t i = 0; i < rec.n; i++) {
        const uint8_t *x = rec.e[i].msg;
        if (x[0] != (0xb0 | METRO_CH)) continue;
        if (x[1] == 91 && x[2] == 0) cc91 = true;
        if (x[1] == 98 && x[2] == 16) nrpn = true; /* reverbEffectsSend */
        if (x[1] == 38) data |= x[2];
        if (x[1] == 6) data |= x[2] << 7;
    }
    TEST_ASSERT_TRUE(cc91 && nrpn);
    TEST_ASSERT_EQUAL(8192 - 1000, data); /* -100% */
}

static void test_pot_tempo_range(void)
{
    TEST_ASSERT_EQUAL(METRO_MIN_BPM, metro_bpm_for(0));
    TEST_ASSERT_EQUAL(METRO_MAX_BPM, metro_bpm_for(127));
    TEST_ASSERT_EQUAL(120, metro_bpm_for(64));
    TEST_ASSERT_EQUAL_INT64(500000, metro_beat_us(&m)); /* not read yet: 120 */
}

static void test_a_new_tempo_goes_on_from_the_last_click(void)
{
    metro_set_bpm(&m, 120);
    metro_toggle(&m, 0, send, &m.due_us);
    for (int64_t t = 0; t <= 600000; t += 1000)
        metro_tick(&m, t, m.origin_us, metro_beat_us(&m), 4, send, &m.due_us);
    metro_set_bpm(&m, 60); /* last click at 0.5 s: the next at 1.5 s */
    size_t before = rec.n;
    for (int64_t t = 601000; t <= 2600000; t += 1000)
        metro_tick(&m, t, m.origin_us, metro_beat_us(&m), 4, send, &m.due_us);
    int64_t at[16];
    uint8_t note[16];
    TEST_ASSERT_EQUAL(2, clicks(before, at, note));
    TEST_ASSERT_EQUAL_INT64(1500000, at[0]);
    TEST_ASSERT_EQUAL_INT64(2500000, at[1]);
}

static void test_a_new_tempo_keeps_the_bar_count(void)
{
    metro_set_bpm(&m, 120);
    metro_toggle(&m, 0, send, &m.due_us);
    for (int64_t t = 0; t <= 1100000; t += 1000) /* clicks at 0 (beat 1), 0.5, 1.0 (beat 3) */
        metro_tick(&m, t, m.origin_us, metro_beat_us(&m), 4, send, &m.due_us);
    metro_set_bpm(&m, 60);
    size_t before = rec.n;
    for (int64_t t = 1101000; t <= 3100000; t += 1000)
        metro_tick(&m, t, m.origin_us, metro_beat_us(&m), 4, send, &m.due_us);
    int64_t at[16];
    uint8_t note[16];
    TEST_ASSERT_EQUAL(2, clicks(before, at, note));
    TEST_ASSERT_EQUAL_INT64(2000000, at[0]);
    TEST_ASSERT_EQUAL(METRO_NOTE, note[0]); /* beat 4 */
    TEST_ASSERT_EQUAL_INT64(3000000, at[1]);
    TEST_ASSERT_EQUAL(METRO_BAR_NOTE, note[1]); /* beat 1 again */
}

static void test_aligned_on_a_loop_it_clicks_on_the_loops_bars(void)
{
    metro_set_bpm(&m, 120);
    metro_toggle(&m, 1300000, send, &m.due_us);
    metro_align(&m, 0, 1300000); /* the loop started at 0: beat 1 at 0, 2.0, ... */
    size_t before = rec.n;
    run(1300000, 2100000, m.origin_us, metro_beat_us(&m), 4);
    int64_t at[16];
    uint8_t note[16];
    TEST_ASSERT_EQUAL(2, clicks(before, at, note));
    TEST_ASSERT_EQUAL_INT64(1500000, at[0]); /* not at the press, not in the past */
    TEST_ASSERT_EQUAL(METRO_NOTE, note[0]);
    TEST_ASSERT_EQUAL_INT64(2000000, at[1]);
    TEST_ASSERT_EQUAL(METRO_BAR_NOTE, note[1]);
}

static void test_follows_the_beat_grid_without_a_double_click(void)
{
    metro_toggle(&m, 0, send, &m.due_us);
    run(0, 1000000, 0, 500000, 4); /* clicks at 0, 0.5, 1.0 */
    size_t before = rec.n;
    /* The beat starts at 1.1 s, a beat 460 ms (130 bpm): 1.1 is too close to 1.0. */
    run(1001000, 2100000, 1100000, 460000, 4);
    int64_t at[16];
    uint8_t note[16];
    size_t n = clicks(before, at, note);
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL_INT64(1560000, at[0]);
    TEST_ASSERT_EQUAL(METRO_NOTE, note[0]); /* the beat's second beat */
    TEST_ASSERT_EQUAL_INT64(2020000, at[1]);
}

static void test_toggle_off(void)
{
    metro_toggle(&m, 0, send, &m.due_us);
    metro_toggle(&m, 10, send, &m.due_us);
    TEST_ASSERT_FALSE(m.on);
    TEST_ASSERT_EQUAL_STRING("metronome off", rec.e[rec.n - 1].what);
    size_t before = rec.n;
    run(0, 1000000, 0, 500000, 4);
    TEST_ASSERT_EQUAL(before, rec.n);
}

static void test_the_screen_dot_flashes_with_the_click_beat_1_apart(void)
{
    metro_toggle(&m, 0, send, &m.due_us);
    TEST_ASSERT_EQUAL(0, metro_flash(&m, 0));      /* nothing sent yet */
    run(0, 650000, 0, 500000, 4);                  /* clicks at 0 and 0.5 s sent */
    TEST_ASSERT_EQUAL(2, metro_flash(&m, 50000));  /* beat 1, the one before the last */
    TEST_ASSERT_EQUAL(0, metro_flash(&m, 300000)); /* between clicks */
    TEST_ASSERT_EQUAL(1, metro_flash(&m, 510000)); /* beat 2 */
    metro_toggle(&m, 700000, send, &m.due_us);
    TEST_ASSERT_EQUAL(0, metro_flash(&m, 510000)); /* off */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_off_is_silent);
    RUN_TEST(test_on_sends_the_woodblock_and_clicks_on_its_own_grid);
    RUN_TEST(test_each_click_has_its_note_off);
    RUN_TEST(test_on_makes_its_channel_dry);
    RUN_TEST(test_pot_tempo_range);
    RUN_TEST(test_a_new_tempo_goes_on_from_the_last_click);
    RUN_TEST(test_a_new_tempo_keeps_the_bar_count);
    RUN_TEST(test_aligned_on_a_loop_it_clicks_on_the_loops_bars);
    RUN_TEST(test_follows_the_beat_grid_without_a_double_click);
    RUN_TEST(test_toggle_off);
    RUN_TEST(test_the_screen_dot_flashes_with_the_click_beat_1_apart);
    return UNITY_END();
}
