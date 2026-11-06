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
    TEST_ASSERT_EQUAL_STRING("chick 8", b);
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
    for (size_t i = 0; i < LOOP_CHANNELS; i++)
        assert_msg(2 + i, 0xb0 | (LOOP_CH + i), 123, 0); /* every layer */
    assert_msg(6, 0xb0 | KEY_CH, 7, DEFAULT_VOLUME);
    for (size_t i = 0; i < LOOP_CHANNELS; i++) assert_msg(8 + i, 0xb0 | (LOOP_CH + i), 7, DEFAULT_VOLUME);
    assert_msg(12, 0xc0 | KEY_CH, 0, 0);
    TEST_ASSERT_EQUAL(0, k.program);
    TEST_ASSERT_EQUAL(DEFAULT_STYLE, k.style);
}

static void test_buttons(void)
{
    TEST_ASSERT_EQUAL(DEFAULT_STYLE, k.style);
    TEST_ASSERT_EQUAL(KEYMAP_STYLE, keymap_event(&k, 4, 5, true, midi_rec, &rec)); /* WALTZ: accordion */
    assert_msg(0, 0xc0, 21, 0);
    assert_msg(1, 0xc9, 0, 0); /* the standard drum kit */
    TEST_ASSERT_EQUAL(21, k.program);
    TEST_ASSERT_EQUAL(5, k.style);
    keymap_event(&k, 7, 0, true, midi_rec, &rec); /* DOG: bass drum */
    keymap_event(&k, 7, 0, false, midi_rec, &rec);
    assert_msg(2, 0x90 | DRUM_CH, 36, VELOCITY);
    assert_msg(3, 0x80 | DRUM_CH, 36, 0);
    TEST_ASSERT_EQUAL(KEYMAP_DEMO, keymap_event(&k, 7, 5, true, midi_rec, &rec));
    TEST_ASSERT_EQUAL(KEYMAP_NONE, keymap_event(&k, 7, 5, false, midi_rec, &rec));
    TEST_ASSERT_EQUAL(KEYMAP_RECORD, keymap_event(&k, 5, 5, true, midi_rec, &rec));
    TEST_ASSERT_EQUAL(KEYMAP_REPLAY, keymap_event(&k, 5, 6, true, midi_rec, &rec));
    TEST_ASSERT_EQUAL(KEYMAP_SELECT, keymap_event(&k, 7, 4, true, midi_rec, &rec));
    TEST_ASSERT_EQUAL(4, rec.n);
}

static void test_slow_rock_is_edm_mode_with_techno(void)
{
    keymap_event(&k, 4, 5, true, midi_rec, &rec);           /* WALTZ: accordion */
    keymap_event(&k, 4, EDM_STYLE_3, true, midi_rec, &rec); /* SLOW ROCK */
    TEST_ASSERT_EQUAL(81, k.program);
    TEST_ASSERT_EQUAL_STRING("Saw lead, Am", keymap_program_name(k.program));
    TEST_ASSERT_EQUAL(33, keymap_key_note(&k, 0));          /* key 1 = A1 */
    keymap_event(&k, 4, EDM_STYLE_4, true, midi_rec, &rec); /* ROCK: dubstep */
    TEST_ASSERT_EQUAL_STRING("Growl bass, Am", keymap_program_name(k.program));
    TEST_ASSERT_EQUAL(33, keymap_key_note(&k, 0)); /* key 1 = A1 */
}

static void test_program_names(void)
{
    TEST_ASSERT_EQUAL_STRING("Piano", keymap_program_name(k.program));
    TEST_ASSERT_EQUAL_STRING("?", keymap_program_name(19)); /* ROCK's cat is gone (dubstep) */
    TEST_ASSERT_EQUAL_STRING("Siamese cat", keymap_program_name(88));
    TEST_ASSERT_EQUAL_STRING("?", keymap_program_name(56)); /* MARCH's trumpet is gone (EDM) */
    TEST_ASSERT_EQUAL_STRING("Electric piano", keymap_program_name(4));
    TEST_ASSERT_EQUAL_STRING("?", keymap_program_name(127));
}

static void test_edm_keys_play_a_minor(void)
{
    keymap_event(&k, 4, EDM_STYLE, true, midi_rec, &rec); /* DISCO */
    TEST_ASSERT_EQUAL_STRING("Saw lead, Am", keymap_program_name(k.program));
    key(1, true);  /* A1 */
    key(3, true);  /* C2 */
    key(8, true);  /* A2: the next octave starts at key 8 */
    key(32, true); /* 4 octaves and 3 steps up: D6 */
    assert_msg(2, 0x90, 33, EDM_VELOCITY);
    assert_msg(3, 0x90, 36, EDM_VELOCITY);
    assert_msg(4, 0x90, 45, EDM_VELOCITY);
    assert_msg(5, 0x90, 86, EDM_VELOCITY);
}

static void test_edm_mode_switches_the_drum_kit(void)
{
    keymap_event(&k, 4, EDM_STYLE, true, midi_rec, &rec);
    assert_msg(1, 0xc9, EDM_KIT, 0);
    keymap_event(&k, 4, 7, true, midi_rec, &rec); /* BLUES */
    assert_msg(3, 0xc9, 0, 0);
    keymap_event(&k, 4, EDM_STYLE, true, midi_rec, &rec);
    rec.n = 0;
    keymap_event(&k, 5, 4, true, midi_rec, &rec); /* RESET */
    TEST_ASSERT_EQUAL_HEX8(0xc9, rec.e[rec.n - 1].msg[0]);
    TEST_ASSERT_EQUAL(0, rec.e[rec.n - 1].msg[1]);
}

static void test_a_style_button_leaves_a_playing_beats_kit_alone(void)
{
    k.beat_on = true;
    keymap_event(&k, 4, 7, true, midi_rec, &rec); /* BLUES */
    TEST_ASSERT_EQUAL(1, rec.n);                  /* the keys' program only */
    assert_msg(0, 0xc0, 4, 0);
    TEST_ASSERT_EQUAL(53, keymap_key_note(&k, 0)); /* the keys still follow the style */
    TEST_ASSERT_EQUAL(0, keymap_kit(7));
    TEST_ASSERT_EQUAL(EDM_KIT, keymap_kit(EDM_STYLE));
}

static void test_edm_mode_ends_with_another_style_or_reset(void)
{
    keymap_event(&k, 4, EDM_STYLE, true, midi_rec, &rec);
    keymap_event(&k, 4, 7, true, midi_rec, &rec); /* BLUES */
    TEST_ASSERT_EQUAL(53, keymap_key_note(&k, 0));
    keymap_event(&k, 4, EDM_STYLE, true, midi_rec, &rec);
    keymap_event(&k, 5, 4, true, midi_rec, &rec); /* RESET */
    TEST_ASSERT_EQUAL(53, keymap_key_note(&k, 0));
}

static void test_edm_octave_buttons_still_shift(void)
{
    keymap_event(&k, 4, EDM_STYLE, true, midi_rec, &rec);
    keymap_event(&k, 5, 0, true, midi_rec, &rec); /* TEMPO+ */
    TEST_ASSERT_EQUAL(45, keymap_key_note(&k, 0));
}

static void test_march_is_edm_mode_too(void)
{
    keymap_event(&k, 4, EDM_STYLE_2, true, midi_rec, &rec); /* MARCH */
    assert_msg(0, 0xc0, 81, 0);
    assert_msg(1, 0xc9, EDM_KIT, 0);
    TEST_ASSERT_EQUAL(33, keymap_key_note(&k, 0)); /* key 1 = A1 */
    key(1, true);
    assert_msg(2, 0x90, 33, EDM_VELOCITY);
}

static void test_frog_is_the_metronome_and_sends_nothing(void)
{
    TEST_ASSERT_EQUAL(KEYMAP_METRO, keymap_event(&k, 7, 1, true, midi_rec, &rec));
    TEST_ASSERT_EQUAL(KEYMAP_NONE, keymap_event(&k, 7, 1, false, midi_rec, &rec));
    TEST_ASSERT_EQUAL(0, rec.n);
    TEST_ASSERT_EQUAL(KEYMAP_SELECT, keymap_event(&k, 7, 4, true, midi_rec, &rec));
    TEST_ASSERT_EQUAL(KEYMAP_RECORD, keymap_event(&k, 5, 5, true, midi_rec, &rec));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_frog_is_the_metronome_and_sends_nothing);
    RUN_TEST(test_names);
    RUN_TEST(test_key_1_is_F3_and_key_32_is_C6);
    RUN_TEST(test_note_off_matches_the_note_on_after_an_octave_change);
    RUN_TEST(test_octave_stops_at_two);
    RUN_TEST(test_volume_steps_and_clamps);
    RUN_TEST(test_reset_restores_the_defaults);
    RUN_TEST(test_buttons);
    RUN_TEST(test_slow_rock_is_edm_mode_with_techno);
    RUN_TEST(test_program_names);
    RUN_TEST(test_edm_keys_play_a_minor);
    RUN_TEST(test_edm_mode_switches_the_drum_kit);
    RUN_TEST(test_a_style_button_leaves_a_playing_beats_kit_alone);
    RUN_TEST(test_edm_mode_ends_with_another_style_or_reset);
    RUN_TEST(test_edm_octave_buttons_still_shift);
    RUN_TEST(test_march_is_edm_mode_too);
    return UNITY_END();
}
