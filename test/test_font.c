#include <string.h>
#include "unity.h"
#include "font.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static uint32_t decode_one(const char *s, int *consumed)
{
    const char *p = s;
    uint32_t cp = utf8_next(&p);
    *consumed = (int)(p - s);
    return cp;
}

static void test_utf8_lengths(void)
{
    int n;
    TEST_ASSERT_EQUAL_HEX32('A', decode_one("A", &n));
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL_HEX32(0x0416, decode_one("Ж", &n)); // Cyrillic Zhe
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL_HEX32(0x20AC, decode_one("€", &n));
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_HEX32(0x1F3B5, decode_one("🎵", &n));
    TEST_ASSERT_EQUAL(4, n);
    TEST_ASSERT_EQUAL_HEX32(0, decode_one("", &n));
    TEST_ASSERT_EQUAL(0, n);
}

static void test_utf8_malformed_never_overruns(void)
{
    int n;
    // Stray continuation byte: skipped alone.
    TEST_ASSERT_EQUAL_HEX32(FONT_MISSING, decode_one("\x80"
                                                     "A",
                                                     &n));
    TEST_ASSERT_EQUAL(1, n);
    // Sequence cut short by the end of the string: stops at the NUL.
    TEST_ASSERT_EQUAL_HEX32(FONT_MISSING, decode_one("\xD0", &n));
    TEST_ASSERT_EQUAL(1, n);
    // Cut short by a new character: that character is kept.
    const char *p = "\xE2\x82"
                    "B";
    TEST_ASSERT_EQUAL_HEX32(FONT_MISSING, utf8_next(&p));
    TEST_ASSERT_EQUAL_HEX32('B', utf8_next(&p));
    TEST_ASSERT_EQUAL_HEX32(0, utf8_next(&p));
}

static void check_font(const font_t *f)
{
    for (int i = 1; i < f->count; i++) TEST_ASSERT_TRUE(f->glyphs[i - 1].cp < f->glyphs[i].cp);
    // Every printable ASCII character, Russian/Ukrainian letters, Latin
    // letters with diacritics from tags, typographic punctuation.
    const font_glyph_t *missing = font_glyph(f, FONT_MISSING);
    TEST_ASSERT_EQUAL_HEX32(FONT_MISSING, missing->cp);
    for (uint32_t c = 0x20; c < 0x7F; c++) TEST_ASSERT_TRUE_MESSAGE(font_glyph(f, c) != missing, "ASCII");
    for (uint32_t c = 0x0410; c <= 0x044F; c++)
        TEST_ASSERT_TRUE_MESSAGE(font_glyph(f, c) != missing, "Cyrillic");
    const char *extra = "ЁёЇїІіЄєҐґÄÖÜßéñçŁłŠšŽžČčŐőĞğȘșȚț…‘’“”„–—€№";
    uint32_t cp;
    while ((cp = utf8_next(&extra)) != 0) TEST_ASSERT_TRUE(font_glyph(f, cp) != missing);
    // Bitmaps stay inside the table and the line.
    for (int i = 0; i < f->count; i++) {
        const font_glyph_t *g = &f->glyphs[i];
        TEST_ASSERT_TRUE(g->y >= 0 && g->y + g->h <= f->line_h + 1);
        TEST_ASSERT_TRUE(g->advance > 0);
    }
}

static void test_tables_sorted_and_complete(void)
{
    check_font(&FONT_TITLE);
    check_font(&FONT_TEXT);
}

static void test_missing(void)
{
    const font_glyph_t *missing = font_glyph(&FONT_TEXT, FONT_MISSING);
    TEST_ASSERT_EQUAL_PTR(missing, font_glyph(&FONT_TEXT, 0x1F3B5)); // emoji
    TEST_ASSERT_EQUAL_PTR(missing, font_glyph(&FONT_TEXT, 0x65E5));  // CJK
    TEST_ASSERT_TRUE(missing->w > 0 && missing->h > 0);
}

static void test_glyph_shape(void)
{
    // 'l': one vertical stroke, (nearly) fully covered in the middle.
    const font_glyph_t *g = font_glyph(&FONT_TEXT, 'l');
    TEST_ASSERT_TRUE(g->h > 8);
    const uint8_t *rows = &FONT_TEXT.bitmap[g->offset];
    int stride = (g->w + 1) / 2;
    int max = 0;
    for (int c = 0; c < g->w; c++) {
        uint8_t b = rows[(g->h / 2) * stride + c / 2];
        int a = c % 2 ? b & 0x0F : b >> 4;
        if (a > max) max = a;
    }
    TEST_ASSERT_TRUE(max >= 12);
    TEST_ASSERT_EQUAL(0, font_glyph(&FONT_TEXT, ' ')->w); // nothing to draw
}

static void test_text_width_sums_advances(void)
{
    TEST_ASSERT_EQUAL(0, font_text_width(&FONT_TEXT, NULL));
    TEST_ASSERT_EQUAL(0, font_text_width(&FONT_TEXT, ""));
    int a = font_glyph(&FONT_TEXT, 'a')->advance;
    int k = font_glyph(&FONT_TEXT, 0x041A)->advance; // К
    TEST_ASSERT_EQUAL(3 * a, font_text_width(&FONT_TEXT, "aaa"));
    TEST_ASSERT_EQUAL(k + a, font_text_width(&FONT_TEXT, "Кa"));
    TEST_ASSERT_TRUE(font_text_width(&FONT_TITLE, "aaa") > font_text_width(&FONT_TEXT, "aaa"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_utf8_lengths);
    RUN_TEST(test_utf8_malformed_never_overruns);
    RUN_TEST(test_tables_sorted_and_complete);
    RUN_TEST(test_missing);
    RUN_TEST(test_glyph_shape);
    RUN_TEST(test_text_width_sums_advances);
    return UNITY_END();
}
