#include <stdio.h>

#include "keymap.h"
#include "midi_rec.h"
#include "rhythm.h"
#include "unity.h"

#define DUBSTEP 1
#define EDM     3

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

static void test_edm_buttons_have_edm_beats(void)
{
    TEST_ASSERT_EQUAL_STRING("EDM", rhythm_style(EDM_STYLE)->name);
    TEST_ASSERT_EQUAL_STRING("BREAKS", rhythm_style(EDM_STYLE_2)->name);
    TEST_ASSERT_EQUAL_STRING("TECHNO", rhythm_style(EDM_STYLE_3)->name);
    TEST_ASSERT_EQUAL_STRING("DUBSTEP", rhythm_style(EDM_STYLE_4)->name);
    TEST_ASSERT_EQUAL(130, rhythm_style(EDM_STYLE_3)->bpm);
}

static void test_dubstep_starts_with_kick_and_hat_then_hat_an_eighth_later(void)
{
    rhythm_toggle(&r, DUBSTEP, 0, midi_rec, &rec);
    rhythm_tick(&r, 0, midi_rec, &rec);
    /* log, kick, hat (accent) */
    TEST_ASSERT_EQUAL(3, rec.n);
    TEST_ASSERT_EQUAL_HEX8(0x90 | DRUM_CH, rec.e[1].msg[0]);
    TEST_ASSERT_EQUAL(36, rec.e[1].msg[1]);
    TEST_ASSERT_EQUAL(127, rec.e[1].msg[2]); /* accent */
    TEST_ASSERT_EQUAL(42, rec.e[2].msg[1]);
    TEST_ASSERT_EQUAL(110, rec.e[2].msg[2]);
    int64_t step = rhythm_step_us(rhythm_style(DUBSTEP));
    TEST_ASSERT_EQUAL(60000000 / 140 / 4, step);
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
    int64_t step = rhythm_step_us(rhythm_style(DUBSTEP));
    rhythm_toggle(&r, DUBSTEP, 0, midi_rec, &rec);
    run(0, 4 * step);
    rhythm_select(&r, EDM);
    run(4 * step + 1000, 16 * step - 1000);
    TEST_ASSERT_EQUAL(DUBSTEP, r.style);
    rec.n = 0;
    run(16 * step, 16 * step);
    TEST_ASSERT_EQUAL(EDM, r.style);
    TEST_ASSERT_EQUAL(1, count_on(0, 36));
    TEST_ASSERT_EQUAL(0, count_on(0, 42)); /* EDM: hats only between the beats */
    TEST_ASSERT_EQUAL(16 * step + rhythm_step_us(rhythm_style(EDM)), r.next_us);
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
    rhythm_toggle(&r, DUBSTEP, 0, midi_rec, &rec);
    run(0, 0);
    rhythm_toggle(&r, DUBSTEP, 1000, midi_rec, &rec);
    size_t n = rec.n;
    run(2000, 2000000);
    TEST_ASSERT_EQUAL(n, rec.n);
}

static void test_edm_beat_is_loud(void)
{
    rhythm_toggle(&r, EDM, 0, midi_rec, &rec);
    rhythm_tick(&r, 0, midi_rec, &rec);
    TEST_ASSERT_EQUAL(1, count_on(0, 36));
    for (size_t i = 0; i < rec.n; i++) {
        if (rec.e[i].len == 3 && rec.e[i].msg[1] == 36) TEST_ASSERT_EQUAL(127, rec.e[i].msg[2]);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_pattern_is_one_bar_of_hits);
    RUN_TEST(test_edm_buttons_have_edm_beats);
    RUN_TEST(test_dubstep_starts_with_kick_and_hat_then_hat_an_eighth_later);
    RUN_TEST(test_a_style_change_waits_for_the_bar_line);
    RUN_TEST(test_every_style_stops_every_hit);
    RUN_TEST(test_silent_once_stopped);
    RUN_TEST(test_edm_beat_is_loud);
    return UNITY_END();
}
