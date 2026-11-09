#include "fireworks.h"
#include "unity.h"

static fw_t f;

void setUp(void)
{
    fw_init(&f, 12345);
    fw_tick(&f, 1000);
}

void tearDown(void)
{
}

static void run(int64_t from, int64_t to)
{
    for (int64_t t = from; t <= to; t += 10000) fw_tick(&f, t);
}

static int lit(const uint8_t pic[FW_H])
{
    int n = 0;
    for (int y = 0; y < FW_H; y++)
        for (int x = 0; x < FW_W; x++) n += (pic[y] >> x) & 1;
    return n;
}

static void test_columns_go_low_left_high_right(void)
{
    TEST_ASSERT_EQUAL(0, fw_column(20));
    TEST_ASSERT_EQUAL(0, fw_column(FW_NOTE_LOW));
    TEST_ASSERT_EQUAL(FW_W - 1, fw_column(FW_NOTE_LOW + FW_NOTE_SPAN));
    TEST_ASSERT_EQUAL(FW_W - 1, fw_column(127));
    TEST_ASSERT_TRUE(fw_column(60) > fw_column(45));
}

static void test_a_note_launches_a_rocket_that_climbs_from_the_bottom(void)
{
    fw_note(&f, 60, 0);
    uint8_t pic[FW_H];
    fw_draw(&f, pic);
    TEST_ASSERT_EQUAL(1, f.n_rockets);
    TEST_ASSERT_TRUE(pic[FW_H - 1] & (1u << fw_column(60)));
    run(1000, 200000);
    TEST_ASSERT_TRUE(f.rocket[0].y < FW_H - 5);
}

static void test_higher_notes_fly_higher(void)
{
    fw_note(&f, 40, 0);
    fw_note(&f, 85, 0);
    run(1000, 500000); /* both up, neither burst yet (FW_WAIT_US) */
    TEST_ASSERT_EQUAL(2, f.n_rockets);
    TEST_ASSERT_TRUE(f.rocket[1].y < f.rocket[0].y);
}

static void test_a_chord_sends_one_rocket_per_column(void)
{
    fw_note(&f, 60, 0);
    fw_note(&f, 59, 0); /* same band */
    TEST_ASSERT_EQUAL(fw_column(60), fw_column(59));
    TEST_ASSERT_EQUAL(1, f.n_rockets);
}

static void test_the_beat_bursts_rockets_into_sparks_that_fall_and_fade(void)
{
    fw_note(&f, 70, 0);
    run(1000, 300000);
    fw_beat(&f, false, 300000);
    TEST_ASSERT_EQUAL(0, f.n_rockets);
    TEST_ASSERT_EQUAL(f.burst, f.n_sparks);
    uint8_t pic[FW_H];
    fw_draw(&f, pic);
    TEST_ASSERT_TRUE(lit(pic) > 0);
    run(300000, 2000000);
    TEST_ASSERT_EQUAL(0, f.n_sparks);
    fw_draw(&f, pic);
    TEST_ASSERT_EQUAL(0, lit(pic));
}

static void test_without_a_beat_a_rocket_bursts_on_its_own(void)
{
    fw_note(&f, 70, 0);
    run(1000, 3000000);
    TEST_ASSERT_EQUAL(0, f.n_rockets);
}

static void test_a_sequenced_note_launches_at_its_own_time(void)
{
    fw_note(&f, 60, 150000);
    run(1000, 140000);
    TEST_ASSERT_EQUAL(0, f.n_rockets);
    run(150000, 160000);
    TEST_ASSERT_EQUAL(1, f.n_rockets);
}

static void test_settings_are_rolled_again_every_few_bars(void)
{
    float g = f.gravity;
    int changed = 0;
    for (int bar = 1; bar <= 4 * FW_REROLL_BARS; bar++) {
        fw_beat(&f, true, bar);
        if (f.gravity != g) changed++;
        g = f.gravity;
    }
    TEST_ASSERT_EQUAL(4, changed);
    TEST_ASSERT_TRUE(f.gravity >= 18.0f && f.gravity <= 40.0f);
    TEST_ASSERT_TRUE(f.burst >= 6 && f.burst < 14);
}

static void test_never_more_than_its_room(void)
{
    for (int i = 0; i < 200; i++) {
        fw_note(&f, (uint8_t)(33 + i % 60), 0);
        if (i % 3 == 0) fw_beat(&f, false, 0);
    }
    TEST_ASSERT_TRUE(f.n_rockets <= FW_ROCKETS);
    TEST_ASSERT_TRUE(f.n_sparks <= FW_SPARKS);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_columns_go_low_left_high_right);
    RUN_TEST(test_a_note_launches_a_rocket_that_climbs_from_the_bottom);
    RUN_TEST(test_higher_notes_fly_higher);
    RUN_TEST(test_a_chord_sends_one_rocket_per_column);
    RUN_TEST(test_the_beat_bursts_rockets_into_sparks_that_fall_and_fade);
    RUN_TEST(test_without_a_beat_a_rocket_bursts_on_its_own);
    RUN_TEST(test_a_sequenced_note_launches_at_its_own_time);
    RUN_TEST(test_settings_are_rolled_again_every_few_bars);
    RUN_TEST(test_never_more_than_its_room);
    return UNITY_END();
}
