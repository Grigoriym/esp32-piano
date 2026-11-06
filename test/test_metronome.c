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
    TEST_ASSERT_EQUAL(5, clicks(1, at, note)); /* 1.0, 1.5, 2.0, 2.5, 3.0 s */
    for (size_t i = 0; i < 5; i++) TEST_ASSERT_EQUAL_INT64(1000000 + (int64_t)i * 500000, at[i]);
    TEST_ASSERT_EQUAL(METRO_BAR_NOTE, note[0]);
    TEST_ASSERT_EQUAL(METRO_NOTE, note[1]);
    TEST_ASSERT_EQUAL(METRO_BAR_NOTE, note[4]); /* the next bar */
}

static void test_each_click_has_its_note_off(void)
{
    metro_toggle(&m, 0, send, &m.due_us);
    run(0, 0, 0, 500000, 4);
    TEST_ASSERT_EQUAL(3, rec.n);
    TEST_ASSERT_EQUAL_HEX8(0x80 | METRO_CH, rec.e[2].msg[0]);
    TEST_ASSERT_EQUAL_INT64(METRO_CLICK_US, due[2]);
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
    TEST_ASSERT_EQUAL_STRING("metronome off", rec.e[1].what);
    size_t before = rec.n;
    run(0, 1000000, 0, 500000, 4);
    TEST_ASSERT_EQUAL(before, rec.n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_off_is_silent);
    RUN_TEST(test_on_sends_the_woodblock_and_clicks_on_its_own_grid);
    RUN_TEST(test_each_click_has_its_note_off);
    RUN_TEST(test_follows_the_beat_grid_without_a_double_click);
    RUN_TEST(test_toggle_off);
    return UNITY_END();
}
