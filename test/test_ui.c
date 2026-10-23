#include <stdlib.h>

#include "ui.h"
#include "unity.h"

static uint16_t px[CANVAS_W * CANVAS_H];
static canvas_t c = {.px = px, .y0 = 0, .h = CANVAS_H};
static ui_state_t s;

void setUp(void)
{
    s = (ui_state_t){.pc = true, .sound = "Piano", .volume = 100, .loop = UI_LOOP_EMPTY};
}

void tearDown(void)
{
}

static int count(uint16_t color)
{
    int n = 0;
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) n += px[i] == color;
    return n;
}

static void test_volume_bar_follows_the_volume(void)
{
    const int mid = UI_VOL_Y + UI_VOL_H / 2;
    s.volume = 127;
    ui_render(&c, &s);
    TEST_ASSERT_NOT_EQUAL(canvas_get(&c, UI_VOL_X + 1, mid), canvas_get(&c, UI_VOL_X + UI_VOL_W + 2, mid));
    uint16_t full = canvas_get(&c, UI_VOL_X + 1, mid);
    TEST_ASSERT_EQUAL_HEX16(full, canvas_get(&c, UI_VOL_X + UI_VOL_W - 1, mid));

    s.volume = 0;
    ui_render(&c, &s);
    TEST_ASSERT_NOT_EQUAL(full, canvas_get(&c, UI_VOL_X + 1, mid));

    s.volume = 64;
    ui_render(&c, &s);
    TEST_ASSERT_EQUAL_HEX16(full, canvas_get(&c, UI_VOL_X + UI_VOL_W / 2 - 2, mid));
    TEST_ASSERT_NOT_EQUAL(full, canvas_get(&c, UI_VOL_X + UI_VOL_W / 2 + 2, mid));
}

static void test_percent_from_midi_volume(void)
{
    TEST_ASSERT_EQUAL_INT(0, ui_percent(0));
    TEST_ASSERT_EQUAL_INT(79, ui_percent(100));
    TEST_ASSERT_EQUAL_INT(50, ui_percent(64));
    TEST_ASSERT_EQUAL_INT(100, ui_percent(127));
}

static void test_green_dot_only_while_the_pc_answers(void)
{
    ui_render(&c, &s);
    TEST_ASSERT_TRUE(count(UI_GREEN) > 20);
    s.pc = false;
    ui_render(&c, &s);
    TEST_ASSERT_EQUAL(0, count(UI_GREEN));
}

static void test_red_only_while_recording(void)
{
    s.loop = UI_LOOP_PLAYING;
    ui_render(&c, &s);
    TEST_ASSERT_EQUAL(0, count(UI_RED));
    s.loop = UI_LOOP_RECORDING;
    ui_render(&c, &s);
    TEST_ASSERT_TRUE(count(UI_RED) > 20);
}

static void test_every_state_draws_inside_the_screen(void)
{
    /* ASan catches a write off the canvas; long names and the extremes. */
    const char *sounds[] = {"Distortion guitar", "Electric piano", NULL};
    for (int i = 0; i < 3; i++) {
        for (int o = -2; o <= 2; o++) {
            for (int l = UI_LOOP_EMPTY; l <= UI_LOOP_PLAYING; l++) {
                s = (ui_state_t){.pc = o & 1,
                                 .demo = l & 1,
                                 .sound = sounds[i],
                                 .volume = 127 * (l & 1),
                                 .octave = o,
                                 .beat = i ? "SLOW ROCK" : NULL,
                                 .beat_bpm = 116,
                                 .loop = (ui_loop_t)l};
                ui_render(&c, &s);
            }
        }
    }
    TEST_ASSERT_EQUAL_HEX16(UI_BG, canvas_get(&c, CANVAS_W / 2, CANVAS_H - 2));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_volume_bar_follows_the_volume);
    RUN_TEST(test_percent_from_midi_volume);
    RUN_TEST(test_green_dot_only_while_the_pc_answers);
    RUN_TEST(test_red_only_while_recording);
    RUN_TEST(test_every_state_draws_inside_the_screen);
    return UNITY_END();
}
