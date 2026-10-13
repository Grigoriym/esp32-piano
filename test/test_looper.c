#include <stdio.h>

#include "keymap.h"
#include "looper.h"
#include "midi_rec.h"
#include "unity.h"

static looper_t l;
static midi_rec_t rec;

void setUp(void)
{
    looper_init(&l);
    memset(&rec, 0, sizeof(rec));
}

void tearDown(void)
{
}

static void note(int64_t t, uint8_t ch, uint8_t n, bool on)
{
    uint8_t msg[3] = {(uint8_t)((on ? 0x90 : 0x80) | ch), n, on ? VELOCITY : 0};
    looper_capture(&l, t, msg, 3, 21);
}

/* Messages with this status and note in rec, from index i on. */
static size_t count(size_t i, uint8_t status, uint8_t n)
{
    size_t c = 0;
    for (; i < rec.n; i++) c += rec.e[i].len >= 2 && rec.e[i].msg[0] == status && rec.e[i].msg[1] == n;
    return c;
}

static int64_t run(int64_t t, int64_t t_end)
{
    for (; t <= t_end; t += 1000) looper_tick(&l, t, midi_rec, &rec);
    return t;
}

/* RECORD at 0, key 60 at 1 s for 0.5 s, RECORD at 3 s: a 2 s loop. */
static void record_one_note(void)
{
    looper_record(&l, 0, midi_rec, &rec);
    note(1000000, KEY_CH, 60, true);
    note(1500000, KEY_CH, 60, false);
    looper_record(&l, 3000000, midi_rec, &rec);
}

static void test_recording_starts_at_the_first_note(void)
{
    record_one_note();
    TEST_ASSERT_EQUAL(LOOPER_PLAYING, l.state);
    TEST_ASSERT_EQUAL(2000000, l.length_us);
    TEST_ASSERT_EQUAL(0, l.ev[0].t_us);
    TEST_ASSERT_EQUAL(500000, l.ev[1].t_us);
}

static void test_keys_replay_on_the_loop_channel_with_their_instrument(void)
{
    record_one_note();
    size_t start = rec.n;
    run(3000000, 3000000);
    TEST_ASSERT_EQUAL(1, count(start, 0x90 | LOOP_CH, 60));
    TEST_ASSERT_EQUAL(0, count(start, 0x90 | KEY_CH, 60));
    TEST_ASSERT_EQUAL(1, count(0, 0xc0 | LOOP_CH, 21));
}

static void test_the_loop_repeats(void)
{
    record_one_note();
    size_t start = rec.n;
    run(3000000, 3000000 + 3 * 2000000 - 1000); /* three passes */
    TEST_ASSERT_EQUAL(3, count(start, 0x90 | LOOP_CH, 60));
    TEST_ASSERT_EQUAL(3, count(start, 0x80 | LOOP_CH, 60));
}

static void test_a_note_held_at_the_end_stops_there(void)
{
    looper_record(&l, 0, midi_rec, &rec);
    note(1000000, KEY_CH, 60, true);
    looper_record(&l, 2000000, midi_rec, &rec);
    note(2500000, KEY_CH, 60, false); /* let go after the loop closed: not recorded */
    TEST_ASSERT_EQUAL(1, l.n);
    size_t start = rec.n;
    run(2000000, 3000000);
    TEST_ASSERT_EQUAL(1, count(start, 0x80 | LOOP_CH, 60));
}

static void test_drums_stay_drums(void)
{
    looper_record(&l, 0, midi_rec, &rec);
    note(0, DRUM_CH, 36, true);
    note(100000, DRUM_CH, 36, false);
    looper_record(&l, 1000000, midi_rec, &rec);
    size_t start = rec.n;
    run(1000000, 1000000);
    TEST_ASSERT_EQUAL(1, count(start, 0x90 | DRUM_CH, 36));
}

static void test_a_release_from_before_recording_is_not_stored(void)
{
    looper_record(&l, 0, midi_rec, &rec);
    note(1000000, KEY_CH, 62, false);
    TEST_ASSERT_EQUAL(LOOPER_ARMED, l.state);
    TEST_ASSERT_EQUAL(0, l.n);
}

static void test_other_messages_are_not_stored(void)
{
    looper_record(&l, 0, midi_rec, &rec);
    uint8_t program[2] = {0xc0, 4};
    looper_capture(&l, 0, program, 2, 0);
    uint8_t volume[3] = {0xb0, 7, 100};
    looper_capture(&l, 0, volume, 3, 0);
    uint8_t demo[3] = {0x91, 60, 70};
    looper_capture(&l, 0, demo, 3, 0);
    TEST_ASSERT_EQUAL(0, l.n);
}

static void test_nothing_is_stored_unless_recording(void)
{
    note(0, KEY_CH, 60, true);
    TEST_ASSERT_EQUAL(0, l.n);
    record_one_note();
    note(4000000, KEY_CH, 64, true);
    TEST_ASSERT_EQUAL(2, l.n);
}

static void test_record_with_no_notes_leaves_no_loop(void)
{
    looper_record(&l, 0, midi_rec, &rec);
    looper_record(&l, 1000000, midi_rec, &rec);
    TEST_ASSERT_EQUAL(LOOPER_IDLE, l.state);
    looper_replay(&l, 2000000, midi_rec, &rec);
    TEST_ASSERT_EQUAL(LOOPER_IDLE, l.state);
}

static void test_replay_stops_and_restarts_from_the_top(void)
{
    record_one_note();
    run(3000000, 3100000); /* inside the note */
    size_t start = rec.n;
    looper_replay(&l, 3100000, midi_rec, &rec);
    TEST_ASSERT_EQUAL(LOOPER_IDLE, l.state);
    TEST_ASSERT_EQUAL(1, count(start, 0x80 | LOOP_CH, 60)); /* cut off */
    run(3100000, 9000000);
    TEST_ASSERT_EQUAL(0, count(start, 0x90 | LOOP_CH, 60));
    looper_replay(&l, 9000000, midi_rec, &rec);
    run(9000000, 9000000);
    TEST_ASSERT_EQUAL(1, count(start, 0x90 | LOOP_CH, 60));
}

static void test_replay_while_recording_closes_the_loop(void)
{
    looper_record(&l, 0, midi_rec, &rec);
    note(0, KEY_CH, 60, true);
    looper_replay(&l, 1000000, midi_rec, &rec);
    TEST_ASSERT_EQUAL(LOOPER_PLAYING, l.state);
    TEST_ASSERT_EQUAL(1000000, l.length_us);
}

static void test_record_while_playing_starts_a_new_loop(void)
{
    record_one_note();
    run(3000000, 3100000);
    size_t start = rec.n;
    looper_record(&l, 3100000, midi_rec, &rec);
    TEST_ASSERT_EQUAL(LOOPER_ARMED, l.state);
    TEST_ASSERT_EQUAL(1, count(start, 0x80 | LOOP_CH, 60));
    TEST_ASSERT_EQUAL(0, l.n);
}

static void test_clear_forgets_the_loop(void)
{
    record_one_note();
    run(3000000, 3100000);
    size_t start = rec.n;
    looper_clear(&l, midi_rec, &rec);
    TEST_ASSERT_EQUAL(1, count(start, 0x80 | LOOP_CH, 60));
    TEST_ASSERT_EQUAL(LOOPER_IDLE, l.state);
    looper_replay(&l, 4000000, midi_rec, &rec);
    TEST_ASSERT_EQUAL(LOOPER_IDLE, l.state);
}

static void test_a_full_loop_keeps_room_for_the_last_note_off(void)
{
    looper_record(&l, 0, midi_rec, &rec);
    for (int64_t i = 0; l.n < LOOPER_MAX_EVENTS - 2; i++) {
        note(i * 2, KEY_CH, 60, true);
        note(i * 2 + 1, KEY_CH, 60, false);
    }
    note(5000000, KEY_CH, 61, true);
    note(5000000, KEY_CH, 62, true); /* no room for it and its note off */
    TEST_ASSERT_EQUAL(LOOPER_MAX_EVENTS - 1, l.n);
    note(5500000, KEY_CH, 62, false);
    note(5500000, KEY_CH, 61, false);
    TEST_ASSERT_EQUAL(LOOPER_MAX_EVENTS, l.n);
    looper_record(&l, 6000000, midi_rec, &rec);
    TEST_ASSERT_EQUAL(LOOPER_PLAYING, l.state);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_recording_starts_at_the_first_note);
    RUN_TEST(test_keys_replay_on_the_loop_channel_with_their_instrument);
    RUN_TEST(test_the_loop_repeats);
    RUN_TEST(test_a_note_held_at_the_end_stops_there);
    RUN_TEST(test_drums_stay_drums);
    RUN_TEST(test_a_release_from_before_recording_is_not_stored);
    RUN_TEST(test_other_messages_are_not_stored);
    RUN_TEST(test_nothing_is_stored_unless_recording);
    RUN_TEST(test_record_with_no_notes_leaves_no_loop);
    RUN_TEST(test_replay_stops_and_restarts_from_the_top);
    RUN_TEST(test_replay_while_recording_closes_the_loop);
    RUN_TEST(test_record_while_playing_starts_a_new_loop);
    RUN_TEST(test_clear_forgets_the_loop);
    RUN_TEST(test_a_full_loop_keeps_room_for_the_last_note_off);
    return UNITY_END();
}
