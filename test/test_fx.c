#include <stdbool.h>
#include <string.h>

#include "fx.h"
#include "keymap.h"
#include "midi_rec.h"
#include "unity.h"

static fx_t f;
static midi_rec_t rec;

void setUp(void)
{
    fx_init(&f);
    memset(&rec, 0, sizeof(rec));
    fx_set_grid(&f, 0, 100000); /* a 16th = 100 ms */
}

void tearDown(void)
{
}

static void key(bool on, uint8_t n, int64_t t)
{
    uint8_t msg[3] = {(uint8_t)((on ? 0x90 : 0x80) | KEY_CH), n, on ? 55 : 0};
    fx_key(&f, msg, 3, t, midi_rec, &rec);
}

static void tick(int64_t from, int64_t to)
{
    for (int64_t t = from; t <= to; t += 1000) fx_tick(&f, t, midi_rec, &rec);
}

static void press(fx_kind_t k, int64_t t)
{
    fx_button(&f, k, true, t, midi_rec, &rec);
    fx_button(&f, k, false, t, midi_rec, &rec);
}

/* How many note ons for note n on channel ch are in rec. */
static int ons(uint8_t ch, uint8_t n)
{
    int c = 0;
    for (size_t i = 0; i < rec.n; i++) {
        if (rec.e[i].len == 3 && rec.e[i].msg[0] == (0x90 | ch) && rec.e[i].msg[1] == n && rec.e[i].msg[2])
            c++;
    }
    return c;
}

static int offs(uint8_t ch, uint8_t n)
{
    int c = 0;
    for (size_t i = 0; i < rec.n; i++) {
        if (rec.e[i].len == 3 && rec.e[i].msg[0] == (0x80 | ch) && rec.e[i].msg[1] == n) c++;
    }
    return c;
}

static void test_without_effects_a_key_plays_as_is(void)
{
    key(true, 60, 0);
    key(false, 60, 50000);
    TEST_ASSERT_EQUAL(2, rec.n);
    TEST_ASSERT_EQUAL(1, ons(KEY_CH, 60));
    TEST_ASSERT_EQUAL(1, offs(KEY_CH, 60));
}

static void test_octave_adds_the_note_above_and_ends_it(void)
{
    press(FX_OCTAVE, 0);
    key(true, 60, 0);
    press(FX_OCTAVE, 10); /* off while held: the copy still ends with its key */
    key(false, 60, 50000);
    TEST_ASSERT_EQUAL(1, ons(KEY_CH, 72));
    TEST_ASSERT_EQUAL(1, offs(KEY_CH, 72));
}

static void test_echo_repeats_softer_a_dotted_eighth_apart(void)
{
    press(FX_ECHO, 0);
    key(true, 60, 0);
    key(false, 60, 100000);
    tick(0, 299000);
    TEST_ASSERT_EQUAL(0, ons(FX_ECHO_CH, 60)); /* the first comes 3 16ths later */
    tick(300000, 4000000);
    TEST_ASSERT_EQUAL(fx_echo_repeats(FX_ECHO_LEVEL), ons(FX_ECHO_CH, 60));
    TEST_ASSERT_EQUAL(fx_echo_repeats(FX_ECHO_LEVEL), offs(FX_ECHO_CH, 60));
    int last = 55;
    for (size_t i = 0; i < rec.n; i++) {
        if (rec.e[i].msg[0] != (0x90 | FX_ECHO_CH)) continue;
        TEST_ASSERT_LESS_THAN(last, rec.e[i].msg[2]);
        last = rec.e[i].msg[2];
    }
}

static void test_the_echo_pot_sets_how_many_repeats_and_how_soft(void)
{
    TEST_ASSERT_EQUAL(1, fx_echo_repeats(0));
    TEST_ASSERT_EQUAL(FX_ECHO_MAX, fx_echo_repeats(127));
    TEST_ASSERT_EQUAL(40, fx_echo_decay(0));
    TEST_ASSERT_EQUAL(90, fx_echo_decay(127));
    f.echo = 127;
    press(FX_ECHO, 0);
    key(true, 60, 0);
    key(false, 60, 100000);
    tick(0, 4000000);
    TEST_ASSERT_EQUAL(FX_ECHO_MAX, ons(FX_ECHO_CH, 60));
    TEST_ASSERT_EQUAL(FX_ECHO_MAX, offs(FX_ECHO_CH, 60));
}

static void test_wide_plays_two_copies_and_off_ends_them(void)
{
    press(FX_WIDE, 0);
    key(true, 60, 0);
    TEST_ASSERT_EQUAL(1, ons(FX_WIDE_CH, 60));
    TEST_ASSERT_EQUAL(1, ons(FX_WIDE2_CH, 60));
    press(FX_WIDE, 10);
    TEST_ASSERT_EQUAL(1, offs(FX_WIDE_CH, 60));
    TEST_ASSERT_EQUAL(1, offs(FX_WIDE2_CH, 60));
    key(false, 60, 50000);
    TEST_ASSERT_EQUAL(1, offs(FX_WIDE_CH, 60)); /* not twice */
}

static void test_setup_detunes_the_wide_channels_either_way(void)
{
    fx_setup(midi_rec, &rec);
    int up = -1, down = -1;
    for (size_t i = 0; i < rec.n; i++) {
        int v = rec.e[i].msg[2] << 7 | rec.e[i].msg[1];
        if (rec.e[i].msg[0] == (0xe0 | FX_WIDE_CH)) up = v;
        if (rec.e[i].msg[0] == (0xe0 | FX_WIDE2_CH)) down = v;
    }
    TEST_ASSERT_EQUAL(8192 + FX_DETUNE, up);
    TEST_ASSERT_EQUAL(8192 - FX_DETUNE, down);
}

static void test_hold_is_sustain_on_and_off(void)
{
    press(FX_HOLD, 0);
    TEST_ASSERT_EQUAL_HEX8(0xb0 | KEY_CH, rec.e[0].msg[0]);
    TEST_ASSERT_EQUAL(64, rec.e[0].msg[1]);
    TEST_ASSERT_EQUAL(127, rec.e[0].msg[2]);
    press(FX_HOLD, 10);
    TEST_ASSERT_EQUAL(0, rec.e[rec.n - 1].msg[2]);
}

static void test_arp_plays_the_held_keys_up_on_the_16ths(void)
{
    press(FX_ARP, 0);
    key(true, 64, 1000);
    key(true, 60, 2000);
    key(true, 67, 3000);
    TEST_ASSERT_EQUAL(0, ons(KEY_CH, 60)); /* nothing until the next 16th */
    tick(4000, 350000);                    /* 16ths at 100, 200, 300 ms */
    uint8_t played[8];
    size_t n = 0;
    for (size_t i = 0; i < rec.n && n < 8; i++) {
        if (rec.e[i].msg[0] == (0x90 | KEY_CH) && rec.e[i].msg[2]) played[n++] = rec.e[i].msg[1];
    }
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL(60, played[0]);
    TEST_ASSERT_EQUAL(64, played[1]);
    TEST_ASSERT_EQUAL(67, played[2]);
    key(false, 60, 360000);
    key(false, 64, 361000);
    key(false, 67, 362000); /* the last one let go: the arp stops */
    TEST_ASSERT_EQUAL(1, offs(KEY_CH, 67));
}

static void test_stutter_retriggers_while_held(void)
{
    key(true, 60, 0);
    fx_button(&f, FX_STUTTER, true, 50000, midi_rec, &rec);
    tick(50000, 450000); /* 16ths at 100 .. 400 ms */
    fx_button(&f, FX_STUTTER, false, 450000, midi_rec, &rec);
    tick(451000, 800000);
    TEST_ASSERT_EQUAL(5, ons(KEY_CH, 60)); /* the press and 4 retriggers */
}

static void test_rise_bends_up_then_restores(void)
{
    f.filter = 40;
    press(FX_RISE, 0);
    tick(0, 1599000);
    int top = 0;
    for (size_t i = 0; i < rec.n; i++) {
        if (rec.e[i].msg[0] == (0xe0 | KEY_CH)) {
            int v = rec.e[i].msg[2] << 7 | rec.e[i].msg[1];
            TEST_ASSERT_GREATER_OR_EQUAL(top, v); /* only ever up */
            top = v;
        }
    }
    TEST_ASSERT_GREATER_THAN(8192 + 7000, top);
    TEST_ASSERT_FALSE(f.restore);
    tick(1600000, 1600000); /* one bar: done */
    TEST_ASSERT_TRUE(f.restore);
    TEST_ASSERT_FALSE(f.on[FX_RISE]);
}

static void test_tape_bends_the_loop_down_and_stops_the_keys(void)
{
    key(true, 60, 0);
    press(FX_TAPE, 0);
    tick(0, 800000); /* half a bar */
    bool loop_bent = false;
    for (size_t i = 0; i < rec.n; i++) {
        if (rec.e[i].msg[0] == (0xe0 | LOOP_CH) && (rec.e[i].msg[2] << 7 | rec.e[i].msg[1]) < 8192 - 4000)
            loop_bent = true;
    }
    TEST_ASSERT_TRUE(loop_bent);
    TEST_ASSERT_TRUE(f.restore);
    size_t last = rec.n;
    key(false, 60, 900000); /* already stopped: no second note off */
    TEST_ASSERT_EQUAL(last, rec.n);
}

static void test_reset_turns_everything_off(void)
{
    press(FX_ECHO, 0);
    press(FX_ARP, 0);
    press(FX_HOLD, 0);
    fx_reset(&f, midi_rec, &rec);
    for (int k = 0; k < FX_COUNT; k++) TEST_ASSERT_FALSE(f.on[k]);
    memset(&rec, 0, sizeof(rec));
    key(true, 60, 0);
    TEST_ASSERT_EQUAL(1, rec.n); /* plain again */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_without_effects_a_key_plays_as_is);
    RUN_TEST(test_octave_adds_the_note_above_and_ends_it);
    RUN_TEST(test_echo_repeats_softer_a_dotted_eighth_apart);
    RUN_TEST(test_the_echo_pot_sets_how_many_repeats_and_how_soft);
    RUN_TEST(test_wide_plays_two_copies_and_off_ends_them);
    RUN_TEST(test_setup_detunes_the_wide_channels_either_way);
    RUN_TEST(test_hold_is_sustain_on_and_off);
    RUN_TEST(test_arp_plays_the_held_keys_up_on_the_16ths);
    RUN_TEST(test_stutter_retriggers_while_held);
    RUN_TEST(test_rise_bends_up_then_restores);
    RUN_TEST(test_tape_bends_the_loop_down_and_stops_the_keys);
    RUN_TEST(test_reset_turns_everything_off);
    return UNITY_END();
}
