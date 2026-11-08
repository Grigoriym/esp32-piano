#include "leds.h"
#include "unity.h"

static leds_state_t s;
static leds_t out;

void setUp(void)
{
    s = (leds_state_t){.beat_step = -1, .recording_layer = -1, .target = -1, .pot = -1};
}

void tearDown(void)
{
}

/* out at LED i before the turn (leds.h: LEDS_TURN). */
#define AT(i) out[((i) + LEDS_TURN) % LEDS_N]

static bool lit(int i)
{
    return AT(i)[0] || AT(i)[1] || AT(i)[2];
}

static bool lit_on_strip(int i)
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
    uint8_t first = AT(4)[1];
    s.beat_step = 9; /* beat 3 */
    leds_draw(&s, out);
    TEST_ASSERT_EQUAL(1, count_lit());
    TEST_ASSERT_TRUE(lit(6));
    TEST_ASSERT_GREATER_THAN(AT(6)[1], first); /* beat 1 brighter */
    TEST_ASSERT_EQUAL(0, AT(6)[0]);            /* green */
}

static void test_the_beat_is_on_strip_leds_7_8_1_2(void)
{
    s.steps_per_beat = 4;
    s.beat_step = 0;
    leds_draw(&s, out);
    TEST_ASSERT_TRUE(lit_on_strip(6)); /* beat 1: LED 7 */
    s.beat_step = 12;
    leds_draw(&s, out);
    TEST_ASSERT_TRUE(lit_on_strip(1)); /* beat 4: LED 2 */
}

static void test_layers_in_their_colours_the_target_brighter(void)
{
    s.layers = 2;
    s.loop_playing = true;
    s.target = 1;
    leds_draw(&s, out);
    TEST_ASSERT_EQUAL(2, count_lit());
    TEST_ASSERT_EQUAL(0, AT(0)[0]); /* cyan */
    TEST_ASSERT_EQUAL(0, AT(1)[1]); /* magenta */
    TEST_ASSERT_GREATER_THAN(AT(0)[2], AT(1)[2]);
}

static void test_a_stopped_loop_is_dim(void)
{
    s.layers = 1;
    s.target = 0;
    leds_draw(&s, out);
    uint8_t stopped = AT(0)[1];
    s.loop_playing = true;
    leds_draw(&s, out);
    TEST_ASSERT_GREATER_THAN(stopped, AT(0)[1]);
}

static void test_a_muted_layer_is_dim(void)
{
    s.layers = 2;
    s.loop_playing = true;
    s.target = 1;
    leds_draw(&s, out);
    uint8_t on = AT(0)[1];
    s.muted = 0x01;
    leds_draw(&s, out);
    TEST_ASSERT_GREATER_THAN(AT(0)[1], on);
    TEST_ASSERT_TRUE(lit(0));
}

static void test_the_layer_being_recorded_blinks_red(void)
{
    s.layers = 2;
    s.loop_playing = true;
    s.recording_layer = 1;
    s.now_us = 0;
    leds_draw(&s, out);
    TEST_ASSERT_TRUE(AT(1)[0] && !AT(1)[1] && !AT(1)[2]);
    s.now_us = 130000;
    leds_draw(&s, out);
    TEST_ASSERT_FALSE(lit(1));
    TEST_ASSERT_TRUE(lit(0));
}

static void test_armed_blinks_led_1(void)
{
    s.armed = true;
    leds_draw(&s, out);
    TEST_ASSERT_TRUE(AT(0)[0]);
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

static void test_the_metronome_volume_bar_is_pink(void)
{
    s.pot = 1;
    s.level = 127;
    leds_draw(&s, out);
    TEST_ASSERT_EQUAL(8, count_lit());
    TEST_ASSERT_TRUE(AT(0)[0] > AT(0)[2] && AT(0)[2] > AT(0)[1] && AT(0)[1]);
}

static void test_filter_bar_is_blue_reverb_violet(void)
{
    s.level = 127;
    s.pot = 3;
    leds_draw(&s, out);
    TEST_ASSERT_TRUE(!AT(0)[0] && AT(0)[2] > AT(0)[1]);
    s.pot = 4;
    leds_draw(&s, out);
    TEST_ASSERT_TRUE(AT(0)[0] && !AT(0)[1] && AT(0)[2] > AT(0)[0]);
    TEST_ASSERT_EQUAL(8, count_lit());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_all_dark_with_nothing_going_on);
    RUN_TEST(test_never_brighter_than_the_cap);
    RUN_TEST(test_the_beat_counts_on_leds_5_to_8);
    RUN_TEST(test_the_beat_is_on_strip_leds_7_8_1_2);
    RUN_TEST(test_layers_in_their_colours_the_target_brighter);
    RUN_TEST(test_a_stopped_loop_is_dim);
    RUN_TEST(test_a_muted_layer_is_dim);
    RUN_TEST(test_the_layer_being_recorded_blinks_red);
    RUN_TEST(test_armed_blinks_led_1);
    RUN_TEST(test_a_pot_turned_shows_a_bar);
    RUN_TEST(test_the_metronome_volume_bar_is_pink);
    RUN_TEST(test_filter_bar_is_blue_reverb_violet);
    return UNITY_END();
}
