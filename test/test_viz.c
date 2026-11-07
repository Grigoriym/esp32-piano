#include <string.h>

#include "unity.h"
#include "viz.h"

static viz_t v;

void setUp(void)
{
    viz_init(&v);
}

void tearDown(void)
{
}

static void midi(uint8_t status, uint8_t note, uint8_t vel, int64_t at, int64_t now)
{
    uint8_t m[3] = {status, note, vel};
    viz_midi(&v, m, 3, at, now);
}

/* Runs rows from t for n rows; returns the time after. */
static int64_t rows(int64_t t, int n)
{
    for (int i = 0; i < n; i++, t += VIZ_ROW_US) viz_update(&v, t);
    return t;
}

void test_columns_spread_the_edm_keys(void)
{
    TEST_ASSERT_EQUAL(0, viz_column(20));
    TEST_ASSERT_EQUAL(0, viz_column(33)); /* key 1 */
    TEST_ASSERT_EQUAL(7, viz_column(86)); /* key 32, D6 */
    TEST_ASSERT_EQUAL(7, viz_column(127));
    TEST_ASSERT_EQUAL(3, viz_column(33 + 21)); /* 7 semitones a column */
}

void test_a_tap_drops_one_dot_down_the_column(void)
{
    midi(0x90, 33, 100, 0, 0);
    midi(0x80, 33, 0, 0, 0); /* released before the next row */
    int64_t t = rows(0, 1);
    TEST_ASSERT_EQUAL_HEX8(0x01, v.cols[0]);
    t = rows(t, 3);
    TEST_ASSERT_EQUAL_HEX8(0x08, v.cols[0]); /* fourth row */
    rows(t, 5);
    TEST_ASSERT_EQUAL_HEX8(0x00, v.cols[0]); /* fell off the bottom */
}

void test_a_held_note_leaves_a_streak(void)
{
    midi(0x90, 86, 100, 0, 0);
    int64_t t = rows(0, 3);
    TEST_ASSERT_EQUAL_HEX8(0x07, v.cols[7]);
    midi(0x90, 86, 0, t, t); /* note on with velocity 0 = off */
    rows(t, 2);
    TEST_ASSERT_EQUAL_HEX8(0x1c, v.cols[7]);
}

void test_a_sequenced_note_waits_for_its_time(void)
{
    midi(0x92, 50, 100, 150000, 0); /* a loop layer, 150 ms ahead */
    viz_update(&v, 0);
    TEST_ASSERT_EQUAL_HEX8(0, v.cols[viz_column(50)]);
    midi(0x82, 50, 0, 160000, 0);
    viz_update(&v, VIZ_ROW_US); /* 100 ms: not yet */
    TEST_ASSERT_EQUAL_HEX8(0, v.cols[viz_column(50)]);
    viz_update(&v, 2 * VIZ_ROW_US); /* both due: still one dot */
    TEST_ASSERT_EQUAL_HEX8(0x01, v.cols[viz_column(50)]);
    TEST_ASSERT_EQUAL(0, v.queued);
}

void test_drums_metronome_and_chick_copies_are_left_out(void)
{
    midi(0x99, 36, 100, 0, 0);
    midi(0x98, 84, 100, 0, 0);
    midi(0x9a, 60, 100, 0, 0);
    rows(0, 1);
    for (int c = 0; c < 8; c++) TEST_ASSERT_EQUAL_HEX8(0, v.cols[c]);
}

void test_all_notes_off_ends_a_streak(void)
{
    midi(0x90, 60, 100, 0, 0);
    int64_t t = rows(0, 2);
    midi(0xb0, 123, 0, t, t);
    rows(t, 1);
    TEST_ASSERT_EQUAL_HEX8(0x06, v.cols[viz_column(60)]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_columns_spread_the_edm_keys);
    RUN_TEST(test_a_tap_drops_one_dot_down_the_column);
    RUN_TEST(test_a_held_note_leaves_a_streak);
    RUN_TEST(test_a_sequenced_note_waits_for_its_time);
    RUN_TEST(test_drums_metronome_and_chick_copies_are_left_out);
    RUN_TEST(test_all_notes_off_ends_a_streak);
    return UNITY_END();
}
