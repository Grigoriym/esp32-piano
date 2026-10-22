#include <string.h>
#include "unity.h"
#include "canvas.h"
#include "font.h"

#define WHITE RGB(255, 255, 255)

static uint16_t full_px[CANVAS_W * CANVAS_H];
static canvas_t full = {.px = full_px, .y0 = 0, .h = CANVAS_H};

void setUp(void)
{
    memset(full_px, 0, sizeof(full_px));
}

void tearDown(void)
{
}

static void test_rgb565(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xF800, RGB(255, 0, 0));
    TEST_ASSERT_EQUAL_HEX16(0x07E0, RGB(0, 255, 0));
    TEST_ASSERT_EQUAL_HEX16(0x001F, RGB(0, 0, 255));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, WHITE);
}

static void test_fill_clips_to_screen(void)
{
    canvas_fill(&full, -5, -5, 10, 10, WHITE);
    TEST_ASSERT_EQUAL_HEX16(WHITE, canvas_get(&full, 4, 4));
    TEST_ASSERT_EQUAL_HEX16(0, canvas_get(&full, 5, 5));
    canvas_fill(&full, CANVAS_W - 2, CANVAS_H - 2, 10, 10, WHITE); // no overrun (ASan)
    TEST_ASSERT_EQUAL_HEX16(WHITE, canvas_get(&full, CANVAS_W - 1, CANVAS_H - 1));
}

static void test_band_only_touches_its_rows(void)
{
    uint16_t px[CANVAS_W * 20 + 1];
    px[CANVAS_W * 20] = 0x1234; // canary right after the band
    canvas_t band = {.px = px, .y0 = 100, .h = 20};
    canvas_fill(&band, 0, 0, CANVAS_W, CANVAS_H, WHITE);
    canvas_text(&band, &FONT_TITLE, 0, 110, "Wolke 7 ЖЖЖ", 0);
    TEST_ASSERT_EQUAL_HEX16(0x1234, px[CANVAS_W * 20]);
    TEST_ASSERT_EQUAL_HEX16(0, canvas_get(&band, 5, 99)); // outside reads as 0
    TEST_ASSERT_EQUAL_HEX16(WHITE, canvas_get(&band, 5, 100));
}

static void test_text_blends_and_reports_width(void)
{
    int w = canvas_text(&full, &FONT_TEXT, 10, 50, "Il", WHITE);
    TEST_ASSERT_EQUAL(font_text_width(&FONT_TEXT, "Il"), w);
    int full_on = 0, partial = 0;
    for (int y = 50; y < 50 + FONT_TEXT.line_h; y++) {
        for (int x = 0; x < 40; x++) {
            uint16_t c = canvas_get(&full, x, y);
            if (c == WHITE) full_on++;
            else if (c) partial++;
        }
    }
    TEST_ASSERT_TRUE(full_on > 10);
    TEST_ASSERT_TRUE(partial > 0); // anti-aliased edges
}

static void test_text_clip(void)
{
    canvas_text_clipped(&full, &FONT_TEXT, 0, 0, "MMMMMMMMMM", WHITE, 20, 40);
    for (int y = 0; y < FONT_TEXT.line_h; y++) {
        TEST_ASSERT_EQUAL_HEX16(0, canvas_get(&full, 19, y));
        TEST_ASSERT_EQUAL_HEX16(0, canvas_get(&full, 40, y));
    }
}

static void test_image_big_endian_and_clipped(void)
{
    // 3x2, pixel i = 0xA000 + i, sent high byte first.
    uint8_t img[3 * 2 * 2];
    for (int i = 0; i < 6; i++) {
        img[i * 2] = 0xA0;
        img[i * 2 + 1] = (uint8_t)i;
    }
    canvas_image_be(&full, 10, 20, 3, 2, img);
    TEST_ASSERT_EQUAL_HEX16(0xA000, canvas_get(&full, 10, 20));
    TEST_ASSERT_EQUAL_HEX16(0xA002, canvas_get(&full, 12, 20));
    TEST_ASSERT_EQUAL_HEX16(0xA005, canvas_get(&full, 12, 21));
    TEST_ASSERT_EQUAL_HEX16(0, canvas_get(&full, 13, 20));
    TEST_ASSERT_EQUAL_HEX16(0, canvas_get(&full, 10, 22));

    // Hanging off the left edge and cut by a band: the right pixels land.
    uint16_t px[CANVAS_W * 1];
    canvas_t band = {.px = px, .y0 = 21, .h = 1};
    canvas_fill(&band, 0, 0, CANVAS_W, CANVAS_H, 0);
    canvas_image_be(&band, -1, 20, 3, 2, img);
    TEST_ASSERT_EQUAL_HEX16(0xA004, canvas_get(&band, 0, 21));
    TEST_ASSERT_EQUAL_HEX16(0xA005, canvas_get(&band, 1, 21));
    TEST_ASSERT_EQUAL_HEX16(0, canvas_get(&band, 2, 21));
}

static void test_ellipse_antialiased_and_clipped(void)
{
    canvas_ellipse(&full, 100, 100, 20, 9.5f, WHITE, 0, CANVAS_H);
    TEST_ASSERT_EQUAL_HEX16(WHITE, canvas_get(&full, 100, 100));
    TEST_ASSERT_EQUAL_HEX16(WHITE, canvas_get(&full, 118, 100)); // inside, near the end of the long axis
    TEST_ASSERT_EQUAL_HEX16(0, canvas_get(&full, 121, 100));     // outside
    TEST_ASSERT_EQUAL_HEX16(0, canvas_get(&full, 100, 111));
    uint16_t rim = canvas_get(&full, 100, 90); // the edge runs through this pixel's middle: half
    TEST_ASSERT_TRUE(rim != 0 && rim != WHITE);
    TEST_ASSERT_EQUAL_HEX16(0, canvas_get(&full, 81, 91)); // corner of the box: outside

    // Only the clip rows, and never past the screen (ASan).
    memset(full_px, 0, sizeof(full_px));
    canvas_ellipse(&full, 5, 5, 30, 30, WHITE, 3, 8);
    TEST_ASSERT_EQUAL_HEX16(0, canvas_get(&full, 5, 2));
    TEST_ASSERT_EQUAL_HEX16(WHITE, canvas_get(&full, 5, 3));
    TEST_ASSERT_EQUAL_HEX16(WHITE, canvas_get(&full, 5, 7));
    TEST_ASSERT_EQUAL_HEX16(0, canvas_get(&full, 5, 8));
    canvas_ellipse(&full, CANVAS_W, CANVAS_H, 30, 30, WHITE, 0, CANVAS_H + 50);
    TEST_ASSERT_EQUAL_HEX16(WHITE, canvas_get(&full, CANVAS_W - 1, CANVAS_H - 1));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rgb565);
    RUN_TEST(test_fill_clips_to_screen);
    RUN_TEST(test_band_only_touches_its_rows);
    RUN_TEST(test_text_blends_and_reports_width);
    RUN_TEST(test_text_clip);
    RUN_TEST(test_image_big_endian_and_clipped);
    RUN_TEST(test_ellipse_antialiased_and_clipped);
    return UNITY_END();
}
