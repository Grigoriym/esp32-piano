#include "unity.h"
#include "encoder_decode.h"

#define POLL_MS     10
#define DEBOUNCE_MS 30

// Pin states, (CLK << 1) | DT.
#define REST 0x3

static enc_rotation_t rot;
static enc_button_t btn;

void setUp(void)
{
    enc_rotation_init(&rot, REST);
    enc_button_init(&btn);
}

void tearDown(void)
{
}

// Feeds a sequence of pin states; returns the sum of the clicks it produced.
static int feed(const uint8_t *states, int n)
{
    int clicks = 0;
    for (int i = 0; i < n; i++) {
        clicks += enc_rotation_update(&rot, states[i]);
    }
    return clicks;
}

// One detent is a full Gray-code cycle from rest back to rest. Clockwise
// (as mounted, checked at bring-up) is DT falling first.
static const uint8_t DETENT_CW[] = {0x1, 0x0, 0x2, 0x3};
static const uint8_t DETENT_CCW[] = {0x2, 0x0, 0x1, 0x3};

// --- rotation ---

static void test_one_detent_each_way(void)
{
    TEST_ASSERT_EQUAL_INT(+1, feed(DETENT_CW, 4));
    TEST_ASSERT_EQUAL_INT(-1, feed(DETENT_CCW, 4));
}

static void test_click_only_at_rest(void)
{
    TEST_ASSERT_EQUAL_INT(0, enc_rotation_update(&rot, 0x1));
    TEST_ASSERT_EQUAL_INT(0, enc_rotation_update(&rot, 0x0));
    TEST_ASSERT_EQUAL_INT(0, enc_rotation_update(&rot, 0x2));
    TEST_ASSERT_EQUAL_INT(+1, enc_rotation_update(&rot, REST));
}

static void test_quick_spin_counts_every_detent(void)
{
    int clicks = 0;
    for (int i = 0; i < 20; i++) clicks += feed(DETENT_CW, 4);
    TEST_ASSERT_EQUAL_INT(20, clicks);
}

static void test_contact_bounce_cancels_out(void)
{
    // DT chatters 1->0->1->0 before the step sticks, then CLK does too.
    const uint8_t bouncy[] = {0x1, 0x3, 0x1, 0x3, 0x1, 0x0, 0x1, 0x0, 0x2, 0x3};
    TEST_ASSERT_EQUAL_INT(+1, feed(bouncy, sizeof(bouncy)));
}

static void test_repeated_state_is_ignored(void)
{
    const uint8_t repeats[] = {0x3, 0x1, 0x1, 0x0, 0x0, 0x2, 0x2, 0x3, 0x3};
    TEST_ASSERT_EQUAL_INT(+1, feed(repeats, sizeof(repeats)));
}

static void test_half_turn_and_back_is_no_click(void)
{
    const uint8_t wobble[] = {0x1, 0x0, 0x1, 0x3};
    TEST_ASSERT_EQUAL_INT(0, feed(wobble, sizeof(wobble)));
}

static void test_skipped_state_does_not_reverse(void)
{
    // An ISR missed 0x0 (0x1 -> 0x2 is two steps at once): counts 0 for that
    // pair, but the other steps still say clockwise.
    const uint8_t skipped[] = {0x1, 0x2, 0x3};
    TEST_ASSERT_EQUAL_INT(+1, feed(skipped, sizeof(skipped)));
}

// --- button ---

// Polls the same level n times; returns how many presses were reported.
static int hold(int level, int polls)
{
    int presses = 0;
    for (int i = 0; i < polls; i++) {
        presses += enc_button_update(&btn, level, POLL_MS, DEBOUNCE_MS);
    }
    return presses;
}

static void test_press_after_debounce(void)
{
    // First low poll sees the edge, then 3 more polls (30 ms) to accept it.
    TEST_ASSERT_EQUAL_INT(0, hold(0, 3));
    TEST_ASSERT_EQUAL_INT(1, hold(0, 1));
}

static void test_one_press_per_hold(void)
{
    TEST_ASSERT_EQUAL_INT(1, hold(0, 100));
    TEST_ASSERT_EQUAL_INT(0, hold(1, 10)); // release is not a press
    TEST_ASSERT_EQUAL_INT(1, hold(0, 10));
}

static void test_short_glitch_is_ignored(void)
{
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_INT(0, hold(0, 2));
        TEST_ASSERT_EQUAL_INT(0, hold(1, 2));
    }
}

static void test_bouncy_press_counts_once(void)
{
    const int levels[] = {0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1};
    int presses = 0;
    for (unsigned i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
        presses += enc_button_update(&btn, levels[i], POLL_MS, DEBOUNCE_MS);
    }
    TEST_ASSERT_EQUAL_INT(1, presses);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_one_detent_each_way);
    RUN_TEST(test_click_only_at_rest);
    RUN_TEST(test_quick_spin_counts_every_detent);
    RUN_TEST(test_contact_bounce_cancels_out);
    RUN_TEST(test_repeated_state_is_ignored);
    RUN_TEST(test_half_turn_and_back_is_no_click);
    RUN_TEST(test_skipped_state_does_not_reverse);
    RUN_TEST(test_press_after_debounce);
    RUN_TEST(test_one_press_per_hold);
    RUN_TEST(test_short_glitch_is_ignored);
    RUN_TEST(test_bouncy_press_counts_once);
    return UNITY_END();
}
