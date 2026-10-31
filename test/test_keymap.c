#include <stdio.h>

#include "keymap.h"
#include "midi_rec.h"
#include "unity.h"

static keymap_t k;
static midi_rec_t rec;

void setUp(void)
{
    keymap_init(&k);
    memset(&rec, 0, sizeof(rec));
}

void tearDown(void)
{
}

/* Press (or release) key n, 1..32. */
static keymap_action_t key(int n, bool down)
{
    return keymap_event(&k, (size_t)(n - 1) / N_COLS, (size_t)(n - 1) % N_COLS, down, midi_rec, &rec);
}

static void assert_msg(size_t i, uint8_t s, uint8_t d1, uint8_t d2)
{
    TEST_ASSERT_TRUE(i < rec.n);
    TEST_ASSERT_EQUAL_HEX8(s, rec.e[i].msg[0]);
    TEST_ASSERT_EQUAL(d1, rec.e[i].msg[1]);
    if (rec.e[i].len == 3) TEST_ASSERT_EQUAL(d2, rec.e[i].msg[2]);
}

static void test_names(void)
{
    char b[16];
    keymap_name(1, 5, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("key 14", b);
    keymap_name(7, 5, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("DEMO", b);
    keymap_name(6, 5, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("chick T1", b);
    keymap_name(7, 7, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("?", b);
}

static void test_key_1_is_F3_and_key_32_is_C6(void)
{
    key(1, true);
    key(32, true);
    assert_msg(0, 0x90, 53, VELOCITY);
    assert_msg(1, 0x90, 84, VELOCITY);
}

static void test_note_off_matches_the_note_on_after_an_octave_change(void)
{
    key(10, true);                                /* D4 = 62 */
    keymap_event(&k, 5, 0, true, midi_rec, &rec); /* TEMPO+ while held */
    key(10, false);
    assert_msg(0, 0x90, 62, VELOCITY);
    assert_msg(2, 0x80, 62, 0);
    key(10, true);
    assert_msg(3, 0x90, 74, VELOCITY);
}

static void test_octave_stops_at_two(void)
{
    for (int i = 0; i < 5; i++) keymap_event(&k, 5, 0, true, midi_rec, &rec);
    TEST_ASSERT_EQUAL(MAX_OCTAVE, k.octave);
    for (int i = 0; i < 9; i++) keymap_event(&k, 5, 1, true, midi_rec, &rec);
    TEST_ASSERT_EQUAL(-MAX_OCTAVE, k.octave);
}

static void test_volume_steps_and_clamps(void)
{
    keymap_event(&k, 5, 3, true, midi_rec, &rec); /* VOLUME- from the default 127 */
    assert_msg(0, 0xb0 | KEY_CH, 7, 111);
    assert_msg(1, 0xb0 | DRUM_CH, 7, 111);
    keymap_event(&k, 5, 2, true, midi_rec, &rec); /* VOLUME+ */
    keymap_event(&k, 5, 2, true, midi_rec, &rec);
    TEST_ASSERT_EQUAL(127, k.volume);
    for (int i = 0; i < 20; i++) keymap_event(&k, 5, 3, true, midi_rec, &rec);
    TEST_ASSERT_EQUAL(0, k.volume);
}

static void test_reset_restores_the_defaults(void)
{
    keymap_event(&k, 5, 0, true, midi_rec, &rec);
    keymap_event(&k, 5, 3, true, midi_rec, &rec);
    keymap_event(&k, 4, 6, true, midi_rec, &rec); /* SAMBA */
    rec.n = 0;
    TEST_ASSERT_EQUAL(KEYMAP_RESET, keymap_event(&k, 5, 4, true, midi_rec, &rec));
    TEST_ASSERT_EQUAL(0, k.octave);
    TEST_ASSERT_EQUAL(DEFAULT_VOLUME, k.volume);
    assert_msg(0, 0xb0 | KEY_CH, 123, 0);
    assert_msg(1, 0xb0 | DRUM_CH, 123, 0);
    assert_msg(2, 0xb0 | LOOP_CH, 123, 0);
    assert_msg(5, 0xb0 | LOOP_CH, 7, DEFAULT_VOLUME);
    assert_msg(6, 0xc0 | KEY_CH, 0, 0);
    TEST_ASSERT_EQUAL(0, k.program);
    TEST_ASSERT_EQUAL(DEFAULT_STYLE, k.style);
}

static void test_buttons(void)
{
    TEST_ASSERT_EQUAL(DEFAULT_STYLE, k.style);
    TEST_ASSERT_EQUAL(KEYMAP_STYLE, keymap_event(&k, 4, 5, true, midi_rec, &rec)); /* WALTZ: accordion */
    assert_msg(0, 0xc0, 21, 0);
    TEST_ASSERT_EQUAL(21, k.program);
    TEST_ASSERT_EQUAL(5, k.style);
    keymap_event(&k, 7, 0, true, midi_rec, &rec); /* DOG: bass drum */
    keymap_event(&k, 7, 0, false, midi_rec, &rec);
    assert_msg(1, 0x90 | DRUM_CH, 36, VELOCITY);
    assert_msg(2, 0x80 | DRUM_CH, 36, 0);
    TEST_ASSERT_EQUAL(KEYMAP_DEMO, keymap_event(&k, 7, 5, true, midi_rec, &rec));
    TEST_ASSERT_EQUAL(KEYMAP_NONE, keymap_event(&k, 7, 5, false, midi_rec, &rec));
    TEST_ASSERT_EQUAL(KEYMAP_RECORD, keymap_event(&k, 5, 5, true, midi_rec, &rec));
    TEST_ASSERT_EQUAL(KEYMAP_REPLAY, keymap_event(&k, 5, 6, true, midi_rec, &rec));
    TEST_ASSERT_EQUAL(KEYMAP_SELECT, keymap_event(&k, 7, 4, true, midi_rec, &rec));
    TEST_ASSERT_EQUAL(3, rec.n);
}

static void test_program_names(void)
{
    TEST_ASSERT_EQUAL_STRING("Piano", keymap_program_name(k.program));
    TEST_ASSERT_EQUAL_STRING("Cat", keymap_program_name(19));
    TEST_ASSERT_EQUAL_STRING("Trumpet", keymap_program_name(56));
    TEST_ASSERT_EQUAL_STRING("Electric piano", keymap_program_name(4));
    TEST_ASSERT_EQUAL_STRING("?", keymap_program_name(127));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_names);
    RUN_TEST(test_key_1_is_F3_and_key_32_is_C6);
    RUN_TEST(test_note_off_matches_the_note_on_after_an_octave_change);
    RUN_TEST(test_octave_stops_at_two);
    RUN_TEST(test_volume_steps_and_clamps);
    RUN_TEST(test_reset_restores_the_defaults);
    RUN_TEST(test_buttons);
    RUN_TEST(test_program_names);
    return UNITY_END();
}
