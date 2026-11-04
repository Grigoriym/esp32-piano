#include "leds.h"
#include "unity.h"

static leds_state_t s;
static leds_t out;

void setUp(void)
{
    s = (leds_state_t){.beat_step = -1, .recording_layer = -1, .target = -1, .pot = -1, .following = true};
}

void tearDown(void)
{
}

static bool lit(int i)
{
    return out[i][0] || out[i][1] || out[i][2];
}

static int count_lit(void)
{
    int n = 0;
    for (int i = 0; i < LEDS_N; i++) n += lit(i);
    return n;
}

static void test_all_dark_with_nothing_going_on(void)
{
    leds_draw(&s, out);
    TEST_ASSERT_EQUAL(0, count_lit());
}

static void test_never_brighter_than_the_cap(void)
{
    s.layers = 4;
    s.loop_playing = true;
    s.target = 3;
    s.beat_step = 0;
    s.steps_per_beat = 4;
    leds_draw(&s, out);
    for (int i = 0; i < LEDS_N; i++) {
        for (int k = 0; k < 3; k++) TEST_ASSERT_LESS_OR_EQUAL(LEDS_MAX, out[i][k]);
    }
}

static void test_the_beat_counts_on_leds_5_to_8(void)
{
    s.steps_per_beat = 4;
    s.beat_step = 0;
    leds_draw(&s, out);
    TEST_ASSERT_TRUE(lit(4));
    uint8_t first = out[4][1];
    s.beat_step = 9; /* beat 3 */
    leds_draw(&s, out);
    TEST_ASSERT_EQUAL(1, count_lit());
    TEST_ASSERT_TRUE(lit(6));
    TEST_ASSERT_GREATER_THAN(out[6][1], first); /* beat 1 brighter */
    TEST_ASSERT_EQUAL(0, out[6][0]);            /* green */
}

static void test_layers_in_their_colours_the_target_brighter(void)
{
    s.layers = 2;
    s.loop_playing = true;
    s.target = 1;
    leds_draw(&s, out);
    TEST_ASSERT_EQUAL(2, count_lit());
    TEST_ASSERT_EQUAL(0, out[0][0]); /* cyan */
    TEST_ASSERT_EQUAL(0, out[1][1]); /* magenta */
    TEST_ASSERT_GREATER_THAN(out[0][2], out[1][2]);
}

static void test_a_stopped_loop_is_dim(void)
{
    s.layers = 1;
    s.target = 0;
    leds_draw(&s, out);
    uint8_t stopped = out[0][1];
    s.loop_playing = true;
    leds_draw(&s, out);
    TEST_ASSERT_GREATER_THAN(stopped, out[0][1]);
}

static void test_the_layer_being_recorded_blinks_red(void)
{
    s.layers = 2;
    s.loop_playing = true;
    s.recording_layer = 1;
    s.now_us = 0;
    leds_draw(&s, out);
    TEST_ASSERT_TRUE(out[1][0] && !out[1][1] && !out[1][2]);
    s.now_us = 130000;
    leds_draw(&s, out);
    TEST_ASSERT_FALSE(lit(1));
    TEST_ASSERT_TRUE(lit(0));
}

static void test_armed_blinks_led_1(void)
{
    s.armed = true;
    leds_draw(&s, out);
    TEST_ASSERT_TRUE(out[0][0]);
    s.now_us = 600000;
    leds_draw(&s, out);
    TEST_ASSERT_FALSE(lit(0));
}

static void test_a_pot_turned_shows_a_bar(void)
{
    s.layers = 2;
    s.beat_step = 0;
    s.steps_per_beat = 4;
    s.pot = 0;
    s.level = 64;
    leds_draw(&s, out);
    TEST_ASSERT_EQUAL(4, count_lit());
    TEST_ASSERT_TRUE(lit(0) && lit(3) && !lit(4));
    s.level = 127;
    leds_draw(&s, out);
    TEST_ASSERT_EQUAL(8, count_lit());
    s.level = 2; /* not silent: one LED */
    leds_draw(&s, out);
    TEST_ASSERT_EQUAL(1, count_lit());
    s.level = 0;
    leds_draw(&s, out);
    TEST_ASSERT_EQUAL(0, count_lit());
}

static void test_the_layer_bar_is_in_the_layers_colour_and_blinks_until_picked_up(void)
{
    s.pot = 1;
    s.target = 2; /* yellow */
    s.level = 127;
    s.following = false;
    s.now_us = 0;
    leds_draw(&s, out);
    TEST_ASSERT_TRUE(out[0][0] && out[0][1] && !out[0][2]);
    s.now_us = 250000;
    leds_draw(&s, out);
    TEST_ASSERT_EQUAL(0, count_lit());
    s.following = true;
    leds_draw(&s, out);
    TEST_ASSERT_EQUAL(8, count_lit());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_all_dark_with_nothing_going_on);
    RUN_TEST(test_never_brighter_than_the_cap);
    RUN_TEST(test_the_beat_counts_on_leds_5_to_8);
    RUN_TEST(test_layers_in_their_colours_the_target_brighter);
    RUN_TEST(test_a_stopped_loop_is_dim);
    RUN_TEST(test_the_layer_being_recorded_blinks_red);
    RUN_TEST(test_armed_blinks_led_1);
    RUN_TEST(test_a_pot_turned_shows_a_bar);
    RUN_TEST(test_the_layer_bar_is_in_the_layers_colour_and_blinks_until_picked_up);
    return UNITY_END();
}
