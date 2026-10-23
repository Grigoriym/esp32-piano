#include <string.h>

#include "matrix.h"
#include "unity.h"

static matrix_t m;

typedef struct {
    size_t r[16], c[16];
    bool down[16];
    size_t n;
} events_t;

static void record(size_t r, size_t c, bool down, void *ctx)
{
    events_t *e = ctx;
    e->r[e->n] = r;
    e->c[e->n] = c;
    e->down[e->n] = down;
    e->n++;
}

/* Feeds the same scan n times; returns how many of them changed the stable grid. */
static int feed(const grid_t g, int n)
{
    grid_t prev;
    int changes = 0;
    for (int i = 0; i < n; i++) changes += matrix_feed(&m, g, prev);
    return changes;
}

void setUp(void)
{
    matrix_init(&m);
}

void tearDown(void)
{
}

static void test_press_counts_after_debounce_scans(void)
{
    grid_t key1 = {0x01};
    TEST_ASSERT_EQUAL(0, feed(key1, DEBOUNCE - 1));
    TEST_ASSERT_EQUAL(1, feed(key1, 1));
    TEST_ASSERT_EQUAL_HEX8(0x01, m.stable[0]);
    TEST_ASSERT_EQUAL(0, feed(key1, 10)); /* held: no more changes */
}

static void test_bounce_restarts_the_count(void)
{
    grid_t key1 = {0x01}, none = {0};
    feed(key1, DEBOUNCE - 1);
    feed(none, 1);
    TEST_ASSERT_EQUAL(0, feed(key1, DEBOUNCE - 1));
    TEST_ASSERT_EQUAL(1, feed(key1, 1));
}

static void test_prev_holds_the_grid_before(void)
{
    grid_t a = {0x01}, b = {0x03}, prev;
    feed(a, DEBOUNCE);
    for (int i = 0; i < DEBOUNCE - 1; i++) matrix_feed(&m, b, prev);
    TEST_ASSERT_TRUE(matrix_feed(&m, b, prev));
    TEST_ASSERT_EQUAL_HEX8(0x01, prev[0]);
}

static void test_rectangle_needs_two_rows_with_two_shared_columns(void)
{
    grid_t three_corners = {0x03, 0x01}; /* (0,0) (0,1) (1,0) */
    grid_t four = {0x03, 0x03};
    grid_t two_rows_one_col = {0x01, 0x01};
    TEST_ASSERT_FALSE(matrix_has_rectangle(three_corners));
    TEST_ASSERT_TRUE(matrix_has_rectangle(four));
    TEST_ASSERT_FALSE(matrix_has_rectangle(two_rows_one_col));
}

static void test_every_corner_of_a_rectangle_is_ambiguous(void)
{
    grid_t four = {[2] = 0x21, [5] = 0x21}; /* rows 2 and 5, cols 0 and 5 */
    TEST_ASSERT_TRUE(matrix_ambiguous(four, 2, 0));
    TEST_ASSERT_TRUE(matrix_ambiguous(four, 5, 5));
    grid_t chord = {0x07}; /* three keys in one row */
    TEST_ASSERT_FALSE(matrix_ambiguous(chord, 0, 1));
}

/* Key 5 (row 0, col 4) has a diode: a ghost can't run backwards through it,
 * so the corner diagonal to it is real. The other corners stay ambiguous. */
static void test_corner_diagonal_to_a_diode_is_real(void)
{
    grid_t four = {[0] = 0x90, [1] = 0x90};          /* keys 5, 8, 13, 16 */
    TEST_ASSERT_FALSE(matrix_ambiguous(four, 1, 7)); /* 16, across from 5 */
    TEST_ASSERT_TRUE(matrix_ambiguous(four, 0, 4));  /* 5, across from 16 */
    TEST_ASSERT_TRUE(matrix_ambiguous(four, 1, 4));  /* 13, across from 8 */
    TEST_ASSERT_TRUE(matrix_ambiguous(four, 0, 7));  /* 8, across from 13 */
}

static void test_sync_reports_presses_and_releases(void)
{
    events_t e = {0};
    grid_t chord = {0x05};
    feed(chord, DEBOUNCE);
    matrix_sync(&m, record, &e);
    TEST_ASSERT_EQUAL(2, e.n);
    TEST_ASSERT_TRUE(e.down[0] && e.down[1]);
    TEST_ASSERT_EQUAL(0, e.c[0]);
    TEST_ASSERT_EQUAL(2, e.c[1]);

    grid_t none = {0};
    feed(none, DEBOUNCE);
    matrix_sync(&m, record, &e);
    TEST_ASSERT_EQUAL(4, e.n);
    TEST_ASSERT_FALSE(e.down[2] || e.down[3]);
}

/* Three real presses make a ghost fourth: the press that completes the
 * rectangle is held back until it's gone, then sent if still down. */
static void test_ghost_corner_is_held_back(void)
{
    events_t e = {0};
    grid_t three = {[0] = 0x03, [1] = 0x01};
    feed(three, DEBOUNCE);
    matrix_sync(&m, record, &e);
    TEST_ASSERT_EQUAL(3, e.n);

    grid_t four = {[0] = 0x03, [1] = 0x03}; /* (1,1) appears: real or ghost */
    feed(four, DEBOUNCE);
    matrix_sync(&m, record, &e);
    TEST_ASSERT_EQUAL(3, e.n);

    grid_t after = {[0] = 0x02, [1] = 0x03}; /* (0,0) let go: no rectangle */
    feed(after, DEBOUNCE);
    matrix_sync(&m, record, &e);
    TEST_ASSERT_EQUAL(5, e.n);
    TEST_ASSERT_FALSE(e.down[3]); /* (0,0) up */
    TEST_ASSERT_EQUAL(0, e.r[3]);
    TEST_ASSERT_TRUE(e.down[4]); /* (1,1) down, now unambiguous */
    TEST_ASSERT_EQUAL(1, e.r[4]);
    TEST_ASSERT_EQUAL(1, e.c[4]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_press_counts_after_debounce_scans);
    RUN_TEST(test_bounce_restarts_the_count);
    RUN_TEST(test_prev_holds_the_grid_before);
    RUN_TEST(test_rectangle_needs_two_rows_with_two_shared_columns);
    RUN_TEST(test_every_corner_of_a_rectangle_is_ambiguous);
    RUN_TEST(test_corner_diagonal_to_a_diode_is_real);
    RUN_TEST(test_sync_reports_presses_and_releases);
    RUN_TEST(test_ghost_corner_is_held_back);
    return UNITY_END();
}
