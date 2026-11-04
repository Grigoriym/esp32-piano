#include "mixer.h"
#include "unity.h"

static mixer_t m;

/* A raw reading for a level, in the middle of its span. */
static int raw_for(int level)
{
    return 100 + level * (MIXER_RAW - 200) / MIXER_LEVEL;
}

/* Turns a pot to a level and holds it there long enough to settle. */
static void turn(mixer_pot_t pot, int level, int64_t now)
{
    for (int i = 0; i < 40; i++) mixer_feed(&m, pot, raw_for(level), now);
}

void setUp(void)
{
    mixer_init(&m);
}

void tearDown(void)
{
}

static void test_starts_full(void)
{
    TEST_ASSERT_EQUAL(127, m.beat);
    TEST_ASSERT_EQUAL(127, m.keys);
    TEST_ASSERT_EQUAL(-1, m.target);
}

static void test_first_reading_sets_the_level_at_once(void)
{
    TEST_ASSERT_TRUE(mixer_feed(&m, MIXER_BEAT, raw_for(40), 5));
    TEST_ASSERT_EQUAL(40, m.beat);
    TEST_ASSERT_EQUAL(0, m.moved_us[MIXER_BEAT]); /* boot isn't a turn */
}

static void test_ends_are_reachable(void)
{
    turn(MIXER_KEYS, 0, 10);
    TEST_ASSERT_EQUAL(0, m.keys);
    turn(MIXER_KEYS, 127, 20);
    TEST_ASSERT_EQUAL(127, m.keys);
    mixer_feed(&m, MIXER_KEYS, 0, 30); /* past the dead end */
    turn(MIXER_KEYS, 0, 30);
    TEST_ASSERT_EQUAL(0, m.keys);
}

static void test_noise_does_not_flicker(void)
{
    turn(MIXER_BEAT, 64, 10);
    TEST_ASSERT_EQUAL(64, m.beat);
    int changes = 0;
    for (int i = 0; i < 1000; i++) {
        int noise = (i * 37 % 41) - 20; /* +-20 counts */
        changes += mixer_feed(&m, MIXER_BEAT, raw_for(64) + noise, 100 + i);
    }
    TEST_ASSERT_EQUAL(0, changes);
}

static void test_turning_is_followed(void)
{
    turn(MIXER_BEAT, 64, 10);
    turn(MIXER_BEAT, 90, 50);
    TEST_ASSERT_EQUAL(90, m.beat);
    TEST_ASSERT_EQUAL(50, m.moved_us[MIXER_BEAT]);
}

static void test_a_new_layer_takes_the_pot(void)
{
    turn(MIXER_LAYER, 50, 10);
    mixer_set_layers(&m, 1);
    TEST_ASSERT_EQUAL(0, m.target);
    TEST_ASSERT_EQUAL(50, m.layer[0]);
    turn(MIXER_LAYER, 80, 20);
    TEST_ASSERT_EQUAL(80, m.layer[0]);
    mixer_set_layers(&m, 2);
    TEST_ASSERT_EQUAL(80, m.layer[1]);
    turn(MIXER_LAYER, 30, 30);
    TEST_ASSERT_EQUAL(30, m.layer[1]);
    TEST_ASSERT_EQUAL(80, m.layer[0]); /* the older one stays */
}

static void test_undo_picks_up_the_older_layer(void)
{
    turn(MIXER_LAYER, 80, 10);
    mixer_set_layers(&m, 1);
    mixer_set_layers(&m, 2);
    turn(MIXER_LAYER, 30, 20); /* layer 2 at 30, layer 1 at 80 */
    mixer_set_layers(&m, 1);   /* undo */
    TEST_ASSERT_FALSE(m.attached);
    turn(MIXER_LAYER, 50, 30); /* below 80: nothing changes */
    TEST_ASSERT_EQUAL(80, m.layer[0]);
    turn(MIXER_LAYER, 85, 40); /* crossed it: picked up */
    TEST_ASSERT_TRUE(m.attached);
    TEST_ASSERT_EQUAL(85, m.layer[0]);
    turn(MIXER_LAYER, 60, 50);
    TEST_ASSERT_EQUAL(60, m.layer[0]);
}

static void test_no_layer_no_effect(void)
{
    turn(MIXER_LAYER, 20, 10);
    for (int i = 0; i < LOOPER_LAYERS; i++) TEST_ASSERT_EQUAL(127, m.layer[i]);
}

static void test_erase_resets_the_layers(void)
{
    turn(MIXER_LAYER, 20, 10);
    mixer_set_layers(&m, 1);
    mixer_set_layers(&m, 0);
    TEST_ASSERT_EQUAL(-1, m.target);
    TEST_ASSERT_EQUAL(127, m.layer[0]);
    turn(MIXER_LAYER, 40, 20);
    mixer_set_layers(&m, 1);
    TEST_ASSERT_EQUAL(40, m.layer[0]);
}

static void test_scale(void)
{
    TEST_ASSERT_EQUAL(70, mixer_scale(70, 127));
    TEST_ASSERT_EQUAL(35, mixer_scale(70, 64));
    TEST_ASSERT_EQUAL(1, mixer_scale(10, 1)); /* never silent above 0 */
    TEST_ASSERT_EQUAL(0, mixer_scale(70, 0));
}

static void test_apply(void)
{
    uint8_t out[3];
    const uint8_t on[3] = {0x99, 36, 100};
    TEST_ASSERT_EQUAL(3, mixer_apply(on, 3, 64, out));
    TEST_ASSERT_EQUAL(50, out[2]);
    TEST_ASSERT_EQUAL(0, mixer_apply(on, 3, 0, out)); /* muted: left out */
    const uint8_t off[3] = {0x89, 36, 0};
    TEST_ASSERT_EQUAL(3, mixer_apply(off, 3, 0, out)); /* a note off always goes */
    const uint8_t on0[3] = {0x99, 36, 0};              /* note on 0 = a note off */
    TEST_ASSERT_EQUAL(3, mixer_apply(on0, 3, 0, out));
    TEST_ASSERT_EQUAL(0, out[2]);
    const uint8_t pc[2] = {0xc3, 81};
    TEST_ASSERT_EQUAL(2, mixer_apply(pc, 2, 0, out));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_starts_full);
    RUN_TEST(test_first_reading_sets_the_level_at_once);
    RUN_TEST(test_ends_are_reachable);
    RUN_TEST(test_noise_does_not_flicker);
    RUN_TEST(test_turning_is_followed);
    RUN_TEST(test_a_new_layer_takes_the_pot);
    RUN_TEST(test_undo_picks_up_the_older_layer);
    RUN_TEST(test_no_layer_no_effect);
    RUN_TEST(test_erase_resets_the_layers);
    RUN_TEST(test_scale);
    RUN_TEST(test_apply);
    return UNITY_END();
}
