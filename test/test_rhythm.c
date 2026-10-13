#include <stdio.h>

#include "keymap.h"
#include "midi_rec.h"
#include "rhythm.h"
#include "unity.h"

#define ROCK  1
#define DISCO 3

static rhythm_t r;
static midi_rec_t rec;

void setUp(void)
{
    r = (rhythm_t){0};
    memset(&rec, 0, sizeof(rec));
}

void tearDown(void)
{
}

/* Runs the beat from t to t_end in 1 ms ticks, like the scan loop. */
static void run(int64_t t, int64_t t_end)
{
    for (; t <= t_end; t += 1000) rhythm_tick(&r, t, midi_rec, &rec);
}

/* Drum note ons of note in rec from entry from on. */
static size_t count_on(size_t from, uint8_t note)
{
    size_t n = 0;
    for (size_t i = from; i < rec.n; i++)
        n += rec.e[i].len == 3 && rec.e[i].msg[0] == (0x90 | DRUM_CH) && rec.e[i].msg[1] == note;
    return n;
}

static void test_every_pattern_is_one_bar_of_hits(void)
{
    for (size_t i = 0; i < RHYTHM_STYLES; i++) {
        const rhythm_style_t *s = rhythm_style(i);
        TEST_ASSERT_EQUAL_MESSAGE(0, s->steps % s->steps_per_beat, s->name);
        TEST_ASSERT_NOT_EQUAL(0, s->lane[0].note);
        for (size_t l = 0; l < RHYTHM_LANES; l++) {
            if (!s->lane[l].note) continue;
            TEST_ASSERT_EQUAL_MESSAGE(s->steps, strlen(s->lane[l].hits), s->name);
            TEST_ASSERT_EQUAL_MESSAGE(s->steps, strspn(s->lane[l].hits, ".xX"), s->name);
        }
    }
}

static void test_rock_starts_with_kick_and_hat_then_hat_an_eighth_later(void)
{
    rhythm_toggle(&r, ROCK, 0, midi_rec, &rec);
    rhythm_tick(&r, 0, midi_rec, &rec);
    /* log, kick, hat (accent) */
    TEST_ASSERT_EQUAL(3, rec.n);
    TEST_ASSERT_EQUAL_HEX8(0x90 | DRUM_CH, rec.e[1].msg[0]);
    TEST_ASSERT_EQUAL(36, rec.e[1].msg[1]);
    TEST_ASSERT_EQUAL(RHYTHM_VELOCITY, rec.e[1].msg[2]);
    TEST_ASSERT_EQUAL(42, rec.e[2].msg[1]);
    TEST_ASSERT_EQUAL(RHYTHM_ACCENT, rec.e[2].msg[2]);
    int64_t step = rhythm_step_us(rhythm_style(ROCK));
    TEST_ASSERT_EQUAL(60000000 / 116 / 4, step);
    run(1000, 2 * step - 1000);
    /* step 1: offs for kick and hat, nothing on */
    TEST_ASSERT_EQUAL(5, rec.n);
    TEST_ASSERT_EQUAL_HEX8(0x80 | DRUM_CH, rec.e[3].msg[0]);
    TEST_ASSERT_EQUAL_HEX8(0x80 | DRUM_CH, rec.e[4].msg[0]);
    run(2 * step, 2 * step);
    TEST_ASSERT_EQUAL(1, count_on(5, 42));
}

static void test_a_style_change_waits_for_the_bar_line(void)
{
    int64_t step = rhythm_step_us(rhythm_style(ROCK));
    rhythm_toggle(&r, ROCK, 0, midi_rec, &rec);
    run(0, 4 * step);
    rhythm_select(&r, DISCO);
    run(4 * step + 1000, 16 * step - 1000);
    TEST_ASSERT_EQUAL(ROCK, r.style);
    rec.n = 0;
    run(16 * step, 16 * step);
    TEST_ASSERT_EQUAL(DISCO, r.style);
    TEST_ASSERT_EQUAL(1, count_on(0, 36));
    TEST_ASSERT_EQUAL(1, count_on(0, 42));
    TEST_ASSERT_EQUAL(16 * step + rhythm_step_us(rhythm_style(DISCO)), r.next_us);
}

typedef struct {
    int open[128];
} count_t;

static count_t cnt;

static void count(const uint8_t *msg, size_t len, const char *what, void *ctx)
{
    (void)what;
    (void)ctx;
    if (len != 3) return;
    TEST_ASSERT_EQUAL(DRUM_CH, msg[0] & 0x0f);
    if ((msg[0] & 0xf0) == 0x90) cnt.open[msg[1]]++;
    if ((msg[0] & 0xf0) == 0x80) cnt.open[msg[1]]--;
    TEST_ASSERT_TRUE(cnt.open[msg[1]] == 0 || cnt.open[msg[1]] == 1);
}

static void test_every_style_stops_every_hit(void)
{
    for (size_t i = 0; i < RHYTHM_STYLES; i++) {
        cnt = (count_t){0};
        r = (rhythm_t){0};
        rhythm_toggle(&r, i, 0, count, NULL);
        for (int64_t t = 0; t < 5000000; t += 1000) rhythm_tick(&r, t, count, NULL);
        rhythm_toggle(&r, i, 5000000, count, NULL);
        TEST_ASSERT_FALSE(r.on);
        for (size_t n = 0; n < 128; n++) TEST_ASSERT_EQUAL_MESSAGE(0, cnt.open[n], rhythm_style(i)->name);
    }
}

static void test_silent_once_stopped(void)
{
    rhythm_toggle(&r, ROCK, 0, midi_rec, &rec);
    run(0, 0);
    rhythm_toggle(&r, ROCK, 1000, midi_rec, &rec);
    size_t n = rec.n;
    run(2000, 2000000);
    TEST_ASSERT_EQUAL(n, rec.n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_pattern_is_one_bar_of_hits);
    RUN_TEST(test_rock_starts_with_kick_and_hat_then_hat_an_eighth_later);
    RUN_TEST(test_a_style_change_waits_for_the_bar_line);
    RUN_TEST(test_every_style_stops_every_hit);
    RUN_TEST(test_silent_once_stopped);
    return UNITY_END();
}
