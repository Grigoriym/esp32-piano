#include "chord.h"
#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static uint32_t keys(int a, int b, int c, int d)
{
    uint32_t k = 1ul << (a - 1) | 1ul << (b - 1) | 1ul << (c - 1);
    return d ? k | 1ul << (d - 1) : k;
}

static void assert_name(const char *want, uint32_t k)
{
    char got[16];
    chord_name(k, got, sizeof(got));
    TEST_ASSERT_EQUAL_STRING(want, got);
}

static void test_nothing_and_single_notes(void)
{
    assert_name("", 0);
    assert_name("F", 1ul << 0);
    assert_name("C", 1ul << 7);
    assert_name("C", 1ul << 7 | 1ul << 19); /* two octaves of one note */
    assert_name("C", 1ul << 31);
}

static void test_the_cheat_sheet_chords(void)
{
    assert_name("Am", keys(5, 8, 12, 0));
    assert_name("F", keys(1, 5, 8, 0));
    assert_name("G", keys(3, 7, 10, 0));
    assert_name("C", keys(8, 12, 15, 0));
    assert_name("Dm", keys(1, 5, 10, 0));
}

static void test_inversions_keep_the_name(void)
{
    assert_name("F", keys(5, 8, 13, 0));
    assert_name("C", keys(24, 27, 32, 0));
    assert_name("Am", keys(24, 29, 32, 0));
}

static void test_other_shapes(void)
{
    assert_name("C7", keys(24, 27, 30, 32));
    assert_name("Fmaj7", keys(1, 5, 8, 12));
    assert_name("Am7", keys(5, 8, 12, 15));
    assert_name("Fsus4", keys(1, 6, 8, 0));
    assert_name("F5", 1ul << 0 | 1ul << 7);
    assert_name("Fdim", keys(1, 4, 7, 0));
    assert_name("Faug", keys(1, 5, 9, 0));
}

static void test_unknown_is_a_question_mark(void)
{
    assert_name("?", keys(1, 2, 3, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_nothing_and_single_notes);
    RUN_TEST(test_the_cheat_sheet_chords);
    RUN_TEST(test_inversions_keep_the_name);
    RUN_TEST(test_other_shapes);
    RUN_TEST(test_unknown_is_a_question_mark);
    return UNITY_END();
}
