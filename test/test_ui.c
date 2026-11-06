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
            for (int l = UI_LOOP_EMPTY; l <= UI_LOOP_ADDING; l++) {
                s = (ui_state_t){.pc = o & 1,
                                 .demo = l & 1,
                                 .sound = sounds[i],
                                 .volume = 127 * (l & 1),
                                 .octave = o,
                                 .beat = i ? "SLOW ROCK" : NULL,
                                 .beat_bpm = 116,
                                 .metro_on = o > 0,
                                 .metro_bpm = o ? 180 : 0, /* the header's click, on and off, with DEMO too */
                                 .metro_flash = o > 1 ? 2 : o,
                                 .loop = (ui_loop_t)l};
                ui_render(&c, &s);
            }
        }
    }
    TEST_ASSERT_EQUAL_HEX16(UI_BG, canvas_get(&c, CANVAS_W / 2, CANVAS_H - 2));
}

static void test_play_screen_lights_the_pressed_keys(void)
{
    const int y = UI_STRIP_Y + UI_STRIP_H - 2;
    const int x1 = 18 + 2;                /* key 1 */
    const int x2 = 18 + 7 * 204 / 32 + 2; /* key 8, a white key too */
    s.screen = UI_SCREEN_PLAY;
    ui_render(&c, &s);
    uint16_t idle = canvas_get(&c, x1, y);
    TEST_ASSERT_EQUAL_HEX16(idle, canvas_get(&c, x2, y));

    s.keys = 1ul << 0;
    ui_render(&c, &s);
    TEST_ASSERT_NOT_EQUAL(idle, canvas_get(&c, x1, y));
    TEST_ASSERT_EQUAL_HEX16(idle, canvas_get(&c, x2, y));

    s.keys = 0;
    s.held = 1ul << 7;
    ui_render(&c, &s);
    TEST_ASSERT_EQUAL_HEX16(UI_RED, canvas_get(&c, x2, y));
}

static void test_play_screen_draws_inside_the_screen(void)
{
    s.screen = UI_SCREEN_PLAY;
    s.sound = "Distortion guitar";
    s.beat = "SLOW ROCK";
    s.beat_bpm = 116;
    s.keys = 0xfffffffful;
    s.held = 0xfffffffful;
    ui_render(&c, &s);
    /* every chord's longest name fits: A# (the widest root) with the longest suffix */
    s.keys = 1ul << 5 | 1ul << 8 | 1ul << 11 | 1ul << 15; /* A#m7b5 */
    ui_render(&c, &s);
    TEST_ASSERT_EQUAL_HEX16(UI_BG, canvas_get(&c, CANVAS_W / 2, CANVAS_H - 2));
}

static void test_groove_screen_draws_inside_the_screen(void)
{
    s.screen = UI_SCREEN_GROOVE;
    ui_render(&c, &s); /* no beat */
    const int steps[] = {16, 12, 8};
    const int per_beat[] = {4, 3, 4};
    for (int k = 0; k < 3; k++) {
        for (int step = 0; step < steps[k]; step++) {
            for (int ms = -200; ms <= 200; ms += 50) {
                s.g_steps = steps[k];
                s.g_steps_per_beat = per_beat[k];
                s.g_step = step;
                s.g_lane[0] = s.g_lane[1] = (1u << steps[k]) - 1;
                s.g_lane_name[0] = "shake";
                s.g_lane_name[1] = NULL;
                s.g_on_time = 0x5555 & ((1u << steps[k]) - 1);
                s.g_off = 0xaaaa & ((1u << steps[k]) - 1);
                s.g_has_last = true;
                s.g_last_ms = ms;
                ui_render(&c, &s);
            }
        }
    }
    TEST_ASSERT_EQUAL_HEX16(UI_BG, canvas_get(&c, CANVAS_W / 2, CANVAS_H - 2));
}

static void test_groove_marks_a_press_by_its_timing(void)
{
    s.screen = UI_SCREEN_GROOVE;
    s.g_steps = 16;
    s.g_steps_per_beat = 4;
    s.g_step = 8;
    s.g_on_time = 1u << 2;
    s.g_off = 1u << 6;
    ui_render(&c, &s);
    const int x0 = 18 + 46, pitch = (CANVAS_W - 18 - x0) / 16;
    TEST_ASSERT_EQUAL_HEX16(UI_GREEN, canvas_get(&c, x0 + 2 * pitch + (pitch - 2) / 2, 160 + 7));
    TEST_ASSERT_NOT_EQUAL(UI_GREEN, canvas_get(&c, x0 + 6 * pitch + (pitch - 2) / 2, 160 + 7));
    TEST_ASSERT_NOT_EQUAL(UI_BG, canvas_get(&c, x0 + 6 * pitch + (pitch - 2) / 2, 160 + 7));
}

static void test_chicks_light_on_every_screen(void)
{
    for (int scr = 0; scr < UI_SCREEN_COUNT; scr++) {
        s.screen = (ui_screen_t)scr;
        s.fx = 0;
        ui_render(&c, &s);
        for (int k = 0; k < 8; k++)
            TEST_ASSERT_EQUAL_HEX16(UI_BG, canvas_get(&c, ui_fx_x(k) + 2, UI_FX_Y + 2));
        s.fx = (1u << 1) | (1u << 6); /* Hold, Stutter */
        ui_render(&c, &s);
        for (int k = 0; k < 8; k++) {
            uint16_t want = k == 1 || k == 6 ? UI_FX : UI_BG;
            TEST_ASSERT_EQUAL_HEX16(want, canvas_get(&c, ui_fx_x(k) + 2, UI_FX_Y + 2));
        }
    }
    TEST_ASSERT_TRUE(ui_fx_x(0) >= 18 && ui_fx_x(7) + UI_FX_W <= CANVAS_W - 18);
}

static void test_debug_screen_draws_inside_the_screen(void)
{
    /* ASan catches a write off the canvas; the extremes and past them. */
    s.screen = UI_SCREEN_DEBUG;
    s.dbg = (ui_debug_t){.load = {150, -5},
                         .heap_kb = 999999,
                         .heap_min_kb = 999999,
                         .rssi = -100,
                         .uptime_s = 999999,
                         .scan_gap_ms = 99999,
                         .scan_work_us = 99999,
                         .scan_work_max_us = 999999};
    ui_render(&c, &s);
    s.dbg = (ui_debug_t){0};
    ui_render(&c, &s);
    TEST_ASSERT_EQUAL_HEX16(UI_BG, canvas_get(&c, CANVAS_W / 2, CANVAS_H - 2));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_debug_screen_draws_inside_the_screen);
    RUN_TEST(test_chicks_light_on_every_screen);
    RUN_TEST(test_volume_bar_follows_the_volume);
    RUN_TEST(test_percent_from_midi_volume);
    RUN_TEST(test_green_dot_only_while_the_pc_answers);
    RUN_TEST(test_red_only_while_recording);
    RUN_TEST(test_every_state_draws_inside_the_screen);
    RUN_TEST(test_play_screen_lights_the_pressed_keys);
    RUN_TEST(test_play_screen_draws_inside_the_screen);
    RUN_TEST(test_groove_screen_draws_inside_the_screen);
    RUN_TEST(test_groove_marks_a_press_by_its_timing);
    return UNITY_END();
}
