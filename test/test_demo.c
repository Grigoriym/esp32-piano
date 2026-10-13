#include <stdio.h>

#include "demo.h"
#include "midi_rec.h"
#include "unity.h"

static demo_t d;
static midi_rec_t rec;

void setUp(void)
{
    d = (demo_t){0};
    memset(&rec, 0, sizeof(rec));
}

void tearDown(void)
{
}

/* Runs the demo from t to t_end in 1 ms ticks, like the scan loop. */
static int64_t run(int64_t t, int64_t t_end)
{
    for (; t <= t_end; t += 1000) demo_tick(&d, t, midi_rec, &rec);
    return t;
}

static void test_bars_start_D_C_B_Bb_then_the_same_run(void)
{
    const uint8_t first_two[] = {62, 60, 59, 58};
    const uint8_t run_notes[] = {74, 69, 68, 67, 65, 62, 65, 67};
    uint8_t steps;
    for (size_t bar = 0; bar < 4; bar++) {
        TEST_ASSERT_EQUAL(first_two[bar], demo_note(bar * 10, &steps));
        TEST_ASSERT_EQUAL(first_two[bar], demo_note(bar * 10 + 1, &steps));
        for (size_t i = 0; i < 8; i++) TEST_ASSERT_EQUAL(run_notes[i], demo_note(bar * 10 + 2 + i, &steps));
    }
}

static void test_every_bar_is_sixteen_16ths(void)
{
    TEST_ASSERT_EQUAL(80, demo_length());
    for (size_t bar = 0; bar < 8; bar++) {
        int sum = 0;
        uint8_t steps;
        for (size_t i = 0; i < 10; i++) {
            demo_note(bar * 10 + i, &steps);
            sum += steps;
        }
        TEST_ASSERT_EQUAL(16, sum);
    }
}

typedef struct {
    int on, off, open; /* note ons, note offs, notes on right now */
} count_t;

static count_t cnt;

static void count(const uint8_t *msg, size_t len, const char *what, void *ctx)
{
    (void)what;
    (void)ctx;
    if (len != 3) return;
    if (msg[0] == (0x90 | DEMO_CH)) {
        cnt.on++;
        cnt.open++;
    } else if (msg[0] == (0x80 | DEMO_CH)) {
        cnt.off++;
        cnt.open--;
    }
    TEST_ASSERT_TRUE(cnt.open == 0 || cnt.open == 1); /* one note at a time */
}

static void test_plays_the_whole_piece_once_then_stops(void)
{
    cnt = (count_t){0};
    demo_toggle(&d, 0, count, NULL);
    /* 8 bars of 16 sixteenths, plus a little */
    int64_t end = 8LL * 16 * DEMO_STEP_US;
    for (int64_t t = 0; t <= end + 50000; t += 1000) demo_tick(&d, t, count, NULL);
    TEST_ASSERT_FALSE(d.on);
    TEST_ASSERT_EQUAL(80, cnt.on);
    TEST_ASSERT_EQUAL(80, cnt.off);
}

/* The recorder holds 64 entries, so check timing on the first notes only. */
static void test_note_off_comes_before_the_next_note_on(void)
{
    demo_toggle(&d, 0, midi_rec, &rec);
    run(0, 5 * DEMO_STEP_US);
    /* program, on 62, off 62 (at 1 step - gap), on 62, off, on 74 ... */
    TEST_ASSERT_EQUAL_HEX8(0x90 | DEMO_CH, rec.e[1].msg[0]);
    TEST_ASSERT_EQUAL(62, rec.e[1].msg[1]);
    TEST_ASSERT_EQUAL(DEMO_VELOCITY, rec.e[1].msg[2]);
    TEST_ASSERT_EQUAL_HEX8(0x80 | DEMO_CH, rec.e[2].msg[0]);
    TEST_ASSERT_EQUAL_HEX8(0x90 | DEMO_CH, rec.e[3].msg[0]);
    TEST_ASSERT_EQUAL(62, rec.e[3].msg[1]);
    TEST_ASSERT_EQUAL_HEX8(0x80 | DEMO_CH, rec.e[4].msg[0]);
    TEST_ASSERT_EQUAL(74, rec.e[5].msg[1]);
}

static void test_toggle_while_playing_stops_the_sounding_note(void)
{
    demo_toggle(&d, 0, midi_rec, &rec);
    demo_tick(&d, 0, midi_rec, &rec);
    demo_toggle(&d, 1000, midi_rec, &rec);
    TEST_ASSERT_FALSE(d.on);
    TEST_ASSERT_EQUAL_HEX8(0x80 | DEMO_CH, rec.e[2].msg[0]);
    TEST_ASSERT_EQUAL(62, rec.e[2].msg[1]);
    size_t n = rec.n;
    run(2000, 10 * DEMO_STEP_US);
    TEST_ASSERT_EQUAL(n, rec.n); /* silent once stopped */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_bars_start_D_C_B_Bb_then_the_same_run);
    RUN_TEST(test_every_bar_is_sixteen_16ths);
    RUN_TEST(test_plays_the_whole_piece_once_then_stops);
    RUN_TEST(test_note_off_comes_before_the_next_note_on);
    RUN_TEST(test_toggle_while_playing_stops_the_sounding_note);
    return UNITY_END();
}
