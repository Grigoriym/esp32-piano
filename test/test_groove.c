#include "groove.h"
#include "unity.h"

static groove_t g;

#define STEP 100000 /* 100 ms a step, 16 a bar, bar lines at 0, 1.6 s, ... */

void setUp(void)
{
    groove_init(&g);
    groove_set_grid(&g, 0, STEP, 16);
}

void tearDown(void)
{
}

static void test_no_beat_no_step(void)
{
    groove_set_grid(&g, 0, 0, 0);
    TEST_ASSERT_EQUAL(-1, groove_step(&g, 12345));
    groove_press(&g, 12345);
    TEST_ASSERT_FALSE(g.has_last);
}

static void test_the_step_playing(void)
{
    TEST_ASSERT_EQUAL(0, groove_step(&g, 0));
    TEST_ASSERT_EQUAL(3, groove_step(&g, 350000));
    TEST_ASSERT_EQUAL(0, groove_step(&g, 1600000));
    TEST_ASSERT_EQUAL(15, groove_step(&g, -50000));
}

static void test_a_press_counts_for_its_nearest_step(void)
{
    groove_press(&g, 210000); /* 10 ms late for step 2 */
    TEST_ASSERT_EQUAL(1u << 2, g.on_time);
    TEST_ASSERT_EQUAL(10000, g.last_offset_us);
    groove_press(&g, 560000); /* 40 ms early for step 6 */
    TEST_ASSERT_EQUAL(1u << 6, g.off);
    TEST_ASSERT_EQUAL(-40000, g.last_offset_us);
}

static void test_early_for_the_bar_line_is_step_0(void)
{
    groove_press(&g, 1580000);
    TEST_ASSERT_EQUAL(1u << 0, g.on_time);
    TEST_ASSERT_EQUAL(-20000, g.last_offset_us);
}

static void test_a_mark_stays_about_a_bar(void)
{
    groove_tick(&g, 150000);
    groove_press(&g, 210000); /* step 2 */
    for (int64_t t = 150000; t < 1600000; t += 1000) groove_tick(&g, t);
    TEST_ASSERT_EQUAL(1u << 2, g.on_time); /* still there at the end of the bar */
    groove_tick(&g, 1600000);
    TEST_ASSERT_EQUAL(0, g.on_time); /* wiped as the next bar starts, 2 steps before step 2 */
}

static void test_an_early_press_is_not_wiped_by_its_own_step(void)
{
    for (int64_t t = 0; t < 560000; t += 1000) groove_tick(&g, t);
    groove_press(&g, 560000); /* early for step 6 */
    for (int64_t t = 560000; t < 700000; t += 1000) groove_tick(&g, t);
    TEST_ASSERT_EQUAL(1u << 6, g.off);
}

static void test_the_same_bar_lines_keep_the_marks(void)
{
    groove_press(&g, 210000);
    groove_set_grid(&g, 1600000, STEP, 16); /* main passes the next bar line */
    TEST_ASSERT_EQUAL(1u << 2, g.on_time);
    groove_set_grid(&g, 0, 2 * STEP, 16); /* another beat */
    TEST_ASSERT_EQUAL(0, g.on_time);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_beat_no_step);
    RUN_TEST(test_the_step_playing);
    RUN_TEST(test_a_press_counts_for_its_nearest_step);
    RUN_TEST(test_early_for_the_bar_line_is_step_0);
    RUN_TEST(test_a_mark_stays_about_a_bar);
    RUN_TEST(test_an_early_press_is_not_wiped_by_its_own_step);
    RUN_TEST(test_the_same_bar_lines_keep_the_marks);
    return UNITY_END();
}
