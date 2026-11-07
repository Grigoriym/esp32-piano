#include <stdio.h>

#include "keymap.h"
#include "looper.h"
#include "mixer.h"
#include "midi_rec.h"
#include "unity.h"

static looper_t l;
static midi_rec_t rec;

void setUp(void)
{
    looper_init(&l);
    looper_set_grid(&l, 0, 0); /* init keeps the grid: main sets it every scan */
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

/* A layer plays at its level (pot 2): velocities scaled, nothing at 0. */
static void test_a_layer_plays_at_its_level(void)
{
    record_one_note();
    l.gain[0] = 64;
    size_t start = rec.n;
    run(3000000, 3000000);
    TEST_ASSERT_EQUAL(1, count(start, 0x90 | LOOP_CH, 60));
    for (size_t i = start; i < rec.n; i++) {
        if (rec.e[i].msg[0] == (0x90 | LOOP_CH))
            TEST_ASSERT_EQUAL(mixer_scale(VELOCITY, 64), rec.e[i].msg[2]);
    }
    l.gain[0] = 0;
    start = rec.n;
    run(3001000, 5000000); /* the next pass */
    TEST_ASSERT_EQUAL(0, count(start, 0x90 | LOOP_CH, 60));
    TEST_ASSERT_EQUAL(1, count(start, 0x80 | LOOP_CH, 60)); /* its note off still goes */
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

static void test_record_while_stopped_starts_a_new_loop(void)
{
    record_one_note();
    looper_replay(&l, 3100000, midi_rec, &rec); /* stop */
    looper_record(&l, 3200000, midi_rec, &rec);
    TEST_ASSERT_EQUAL(LOOPER_ARMED, l.state);
    TEST_ASSERT_EQUAL(0, l.n);
    TEST_ASSERT_EQUAL(0, l.length_us);
}

/* With the beat: bars of 2 s from 0. */
#define BAR 2000000

static void note_p(int64_t t, uint8_t n, bool on, uint8_t program)
{
    uint8_t msg[3] = {(uint8_t)((on ? 0x90 : 0x80) | KEY_CH), n, on ? VELOCITY : 0};
    looper_capture(&l, t, msg, 3, program);
}

static void test_with_the_beat_the_loop_starts_on_the_bar_line(void)
{
    looper_set_grid(&l, 0, BAR);
    looper_record(&l, 500000, midi_rec, &rec);
    note(2250000, KEY_CH, 60, true); /* an 8th into bar 1 */
    note(2500000, KEY_CH, 60, false);
    looper_record(&l, 6100000, midi_rec, &rec); /* 4.1 s after the bar line: 2 bars */
    TEST_ASSERT_EQUAL(LOOPER_PLAYING, l.state);
    TEST_ASSERT_EQUAL(2 * BAR, l.length_us);
    TEST_ASSERT_EQUAL(250000, l.ev[0].t_us);
    size_t start = rec.n;
    run(6100000, 6300000); /* the next pass started at 6 s */
    TEST_ASSERT_EQUAL(1, count(start, 0x90 | LOOP_CH, 60));
}

static void test_a_16th_early_counts_as_on_the_bar(void)
{
    looper_set_grid(&l, 0, BAR);
    looper_record(&l, 500000, midi_rec, &rec);
    note(1900000, KEY_CH, 60, true); /* 100 ms early, a 16th is 125 ms */
    TEST_ASSERT_EQUAL(BAR, l.start_us);
    TEST_ASSERT_EQUAL(0, l.ev[0].t_us);
}

static void test_a_late_record_drops_the_next_pass_notes(void)
{
    looper_set_grid(&l, 0, BAR);
    looper_record(&l, 500000, midi_rec, &rec);
    note(2000000, KEY_CH, 60, true);
    note(2500000, KEY_CH, 60, false);
    note(6050000, KEY_CH, 60, true); /* the next pass's first note, played before RECORD */
    looper_record(&l, 6200000, midi_rec, &rec);
    TEST_ASSERT_EQUAL(2 * BAR, l.length_us);
    TEST_ASSERT_EQUAL(2, l.n);
}

static void test_an_early_record_rounds_up_and_waits_for_the_pass_end(void)
{
    looper_set_grid(&l, 0, BAR);
    looper_record(&l, 500000, midi_rec, &rec);
    note(2250000, KEY_CH, 60, true);
    note(2500000, KEY_CH, 60, false);
    looper_record(&l, 5500000, midi_rec, &rec); /* 3.5 s: 2 bars */
    TEST_ASSERT_EQUAL(2 * BAR, l.length_us);
    size_t start = rec.n;
    run(5500000, 6200000);
    TEST_ASSERT_EQUAL(0, count(start, 0x90 | LOOP_CH, 60)); /* played live already */
    run(6200000, 6300000);
    TEST_ASSERT_EQUAL(1, count(start, 0x90 | LOOP_CH, 60));
}

/* A 4-bar phrase starting 1.5 s into bar 1 (the bass on the "and" of beat 2):
 * notes 60..67 every second from 3.5 s, the last one in bar 5. */
static void test_a_phrase_begun_mid_bar_keeps_its_length_and_wraps(void)
{
    looper_set_grid(&l, 0, BAR);
    looper_record(&l, 500000, midi_rec, &rec);
    for (int i = 0; i < 8; i++) {
        note(3500000 + i * 1000000, KEY_CH, (uint8_t)(60 + i), true);
        note(3700000 + i * 1000000, KEY_CH, (uint8_t)(60 + i), false);
    }
    looper_record(&l, 11200000, midi_rec, &rec); /* 7.7 s after the first note */
    TEST_ASSERT_EQUAL(4 * BAR, l.length_us);
    TEST_ASSERT_EQUAL(16, l.n);
    TEST_ASSERT_EQUAL(500000, l.ev[0].t_us); /* the last note, wrapped to the front */
    TEST_ASSERT_EQUAL(67, l.ev[0].msg[1]);
    size_t start = rec.n;
    run(11200000, 18400000);
    TEST_ASSERT_EQUAL(0, count(start, 0x90 | LOOP_CH, 67)); /* played live in this pass */
    TEST_ASSERT_EQUAL(1, count(start, 0x90 | LOOP_CH, 60)); /* at 11.5 s */
    run(18400000, 18600000);
    TEST_ASSERT_EQUAL(1, count(start, 0x90 | LOOP_CH, 67)); /* at 18.5 s, no empty bar */
}

static void test_replay_with_the_beat_starts_at_the_next_bar_line(void)
{
    looper_set_grid(&l, 0, BAR);
    looper_record(&l, 500000, midi_rec, &rec);
    note(2000000, KEY_CH, 60, true);
    note(2500000, KEY_CH, 60, false);
    looper_record(&l, 4000000, midi_rec, &rec);
    looper_replay(&l, 4100000, midi_rec, &rec); /* stop */
    size_t start = rec.n;
    looper_replay(&l, 7300000, midi_rec, &rec);
    run(7300000, 7990000);
    TEST_ASSERT_EQUAL(0, count(start, 0x90 | LOOP_CH, 60));
    run(8000000, 8000000);
    TEST_ASSERT_EQUAL(1, count(start, 0x90 | LOOP_CH, 60));
}

/* record_one_note's loop (2 s from 3 s), then a layer: key 64 on organ (19) at
 * 0.5 s into the pass, recorded in the pass from 3 s. */
static void add_layer(void)
{
    record_one_note();
    run(3000000, 3100000);
    looper_record(&l, 3100000, midi_rec, &rec);
    TEST_ASSERT_TRUE(l.dubbing);
    run(3100000, 3500000); /* the loop is ahead of the keys, as on the toy */
    note_p(3500000, 64, true, 19);
    note_p(3700000, 64, false, 19);
    run(3500000, 5200000);
    looper_record(&l, 5200000, midi_rec, &rec);
}

static void test_a_layer_plays_on_its_own_channel_with_its_instrument(void)
{
    add_layer();
    TEST_ASSERT_FALSE(l.dubbing);
    TEST_ASSERT_EQUAL(2, l.n_layers);
    size_t start = rec.n;
    run(5200000, 6900000); /* the pass from 5 s */
    TEST_ASSERT_EQUAL(1, count(start, 0x90 | (LOOP_CH + 1), 64));
    TEST_ASSERT_EQUAL(1, count(0, 0xc0 | (LOOP_CH + 1), 19));
    TEST_ASSERT_EQUAL(1, count(0, 0xc0 | LOOP_CH, 21));
}

static void test_a_layer_note_is_not_played_again_in_its_own_pass(void)
{
    add_layer();
    TEST_ASSERT_EQUAL(0, count(0, 0x90 | (LOOP_CH + 1), 64)); /* by 5.2 s: only live */
}

static void test_the_first_layer_keeps_playing_while_adding(void)
{
    add_layer();
    TEST_ASSERT_EQUAL(2, count(0, 0x90 | LOOP_CH, 60)); /* passes from 3 s and 5 s */
}

static void test_undo_drops_the_last_layer_then_the_loop(void)
{
    add_layer();
    looper_undo(&l, midi_rec, &rec);
    TEST_ASSERT_EQUAL(1, l.n_layers);
    TEST_ASSERT_EQUAL(2, l.n);
    TEST_ASSERT_EQUAL(LOOPER_PLAYING, l.state);
    looper_undo(&l, midi_rec, &rec);
    TEST_ASSERT_EQUAL(LOOPER_IDLE, l.state);
    TEST_ASSERT_EQUAL(0, l.length_us);
}

static void test_undo_while_adding_drops_the_new_layer(void)
{
    record_one_note();
    run(3000000, 3100000);
    looper_record(&l, 3100000, midi_rec, &rec);
    run(3100000, 3500000);
    note_p(3500000, 64, true, 19);
    looper_undo(&l, midi_rec, &rec);
    TEST_ASSERT_FALSE(l.dubbing);
    TEST_ASSERT_EQUAL(1, l.n_layers);
    TEST_ASSERT_EQUAL(2, l.n);
}

static void test_a_layer_with_nothing_played_is_not_added(void)
{
    record_one_note();
    looper_record(&l, 3100000, midi_rec, &rec);
    looper_record(&l, 4000000, midi_rec, &rec);
    TEST_ASSERT_EQUAL(1, l.n_layers);
}

static void test_no_more_than_four_layers(void)
{
    record_one_note();
    int64_t t = 3000000;
    for (int i = 0; i < 4; i++) {
        looper_record(&l, t, midi_rec, &rec);
        note_p(t + 100000, (uint8_t)(70 + i), true, 0);
        note_p(t + 200000, (uint8_t)(70 + i), false, 0);
        looper_record(&l, t + 300000, midi_rec, &rec);
        t += 400000;
    }
    TEST_ASSERT_EQUAL(LOOPER_LAYERS, l.n_layers);
    looper_record(&l, t, midi_rec, &rec);
    TEST_ASSERT_FALSE(l.dubbing);
}

/* record_one_note's 2 s loop plays from 3 s; RECORD at 3.05 s: the layer's passes count from 3 s. */
static void start_layer(void)
{
    record_one_note();
    run(3000000, 3050000);
    looper_record(&l, 3050000, midi_rec, &rec);
}

static void test_a_layer_over_two_passes_doubles_the_loop(void)
{
    start_layer();
    note_p(3500000, 64, true, 19); /* pass 1: a note */
    note_p(3700000, 64, false, 19);
    run(3050000, 6990000); /* pass 2: silence */
    looper_record(&l, 7000000, midi_rec, &rec);
    TEST_ASSERT_EQUAL(4000000, l.length_us);
    TEST_ASSERT_EQUAL(2, l.n_layers);
    size_t start = rec.n;
    run(7000000, 10990000);                                       /* one pass of the new loop */
    TEST_ASSERT_EQUAL(2, count(start, 0x90 | LOOP_CH, 60));       /* the old layer, twice */
    TEST_ASSERT_EQUAL(1, count(start, 0x90 | (LOOP_CH + 1), 64)); /* the new one, once */
}

static void test_a_layer_is_heard_once_closed(void)
{
    start_layer();
    note_p(3500000, 64, true, 19);
    note_p(3700000, 64, false, 19);
    run(3050000, 6990000);
    TEST_ASSERT_EQUAL(0, count(0, 0x90 | (LOOP_CH + 1), 64)); /* not played back while recording */
}

static void test_a_record_near_the_pass_end_counts_from_the_next_pass(void)
{
    record_one_note();
    run(3000000, 4900000);
    looper_record(&l, 4900000, midi_rec, &rec); /* 0.1 s before the pass from 5 s */
    note_p(4950000, 64, true, 19);              /* a pickup */
    note_p(5050000, 64, false, 19);
    note_p(5500000, 65, true, 19);
    note_p(5600000, 65, false, 19);
    run(4900000, 6990000);
    looper_record(&l, 7000000, midi_rec, &rec);
    TEST_ASSERT_EQUAL(2000000, l.length_us); /* one pass: 5 s to 7 s */
    size_t start = rec.n;
    run(7000000, 8990000);
    TEST_ASSERT_EQUAL(1, count(start, 0x90 | (LOOP_CH + 1), 65));
    TEST_ASSERT_EQUAL(1, count(start, 0x90 | (LOOP_CH + 1), 64)); /* the pickup, at the pass's end */
}

static void test_a_pickup_held_over_the_pass_start_rings_on(void)
{
    record_one_note();
    run(3000000, 4900000);
    looper_record(&l, 4900000, midi_rec, &rec);
    note_p(4991000, 64, true, 19); /* 9 ms before the pass from 5 s, held 0.8 s */
    note_p(5791000, 64, false, 19);
    run(4900000, 6990000);
    looper_record(&l, 7000000, midi_rec, &rec);
    run(7000000, 8990000);
    size_t on = rec.n;
    run(8990000, 9500000); /* its on at 8.991 s, then the wrap at 9 s */
    TEST_ASSERT_EQUAL(1, count(on, 0x90 | (LOOP_CH + 1), 64));
    TEST_ASSERT_EQUAL(0, count(on, 0x80 | (LOOP_CH + 1), 64)); /* not cut at the wrap */
    run(9500000, 9900000);
    TEST_ASSERT_EQUAL(1, count(on, 0x80 | (LOOP_CH + 1), 64)); /* its own off, at 9.791 s */
}

/* Reverb moves (CC 98 = 16 on the layer's channel, then the data entry MSB) from rec[i] on. */
static size_t reverb_moves(size_t i, uint8_t ch, uint8_t *last_msb)
{
    size_t c = 0;
    for (; i < rec.n; i++) {
        const uint8_t *m = rec.e[i].msg;
        if (rec.e[i].len != 3 || m[0] != (0xb0 | ch)) continue;
        if (m[1] == 98 && m[2] == 16) c++;
        if (m[1] == 6 && c) *last_msb = m[2];
    }
    return c;
}

static void test_a_layer_keeps_the_reverb_turned_while_recording(void)
{
    looper_pot(&l, 0, MIXER_REVERB, 0); /* before: dry */
    start_layer();                      /* from 3.05 s, its pass from 3 s */
    note_p(3500000, 64, true, 19);
    looper_pot(&l, 3600000, MIXER_REVERB, 64);
    looper_pot(&l, 3800000, MIXER_REVERB, 127);
    note_p(3900000, 64, false, 19);
    run(3050000, 4990000);
    looper_record(&l, 5000000, midi_rec, &rec);
    size_t start = rec.n;
    uint8_t msb = 0;
    run(5000000, 5400000);                                        /* the pass from 5 s, before the turn */
    TEST_ASSERT_EQUAL(1, reverb_moves(start, LOOP_CH + 1, &msb)); /* back to dry at the start */
    TEST_ASSERT_EQUAL((8192 - MIXER_REVERB_SEND) >> 7, msb);      /* the send's offset: -100% */
    run(5400000, 6990000);
    TEST_ASSERT_EQUAL(3, reverb_moves(start, LOOP_CH + 1, &msb));
    TEST_ASSERT_EQUAL((8192 + MIXER_REVERB_SEND) >> 7, msb); /* +100% at the end, as turned */
    TEST_ASSERT_EQUAL(1, count(start, 0x90 | (LOOP_CH + 1), 64));
}

static void test_pots_left_alone_add_nothing(void)
{
    looper_pot(&l, 0, MIXER_REVERB, 40);
    start_layer();
    looper_pot(&l, 3500000, MIXER_REVERB, 40); /* sent again, same level */
    note_p(3600000, 64, true, 19);
    note_p(3700000, 64, false, 19);
    run(3050000, 4990000);
    looper_record(&l, 5000000, midi_rec, &rec);
    TEST_ASSERT_EQUAL(4, l.n); /* two notes of each layer, no pot moves */
}

static void test_the_first_layer_keeps_its_pot_moves_without_moving_its_start(void)
{
    looper_pot(&l, 0, MIXER_FILTER, 127);
    looper_set_grid(&l, 0, 2000000);
    looper_record(&l, 0, midi_rec, &rec);
    note(1000000, KEY_CH, 60, true); /* the loop starts on the bar line at 0 */
    looper_pot(&l, 1200000, MIXER_FILTER, 30);
    note(1500000, KEY_CH, 60, false);
    looper_record(&l, 3000000, midi_rec, &rec);
    TEST_ASSERT_EQUAL(2000000, l.length_us); /* the phrase from the first note, as before */
    TEST_ASSERT_EQUAL(4, l.n);               /* the filter at the start, the note, the move, the off */
    TEST_ASSERT_EQUAL(LOOPER_POT_CC + MIXER_FILTER, l.ev[0].msg[1]);
    TEST_ASSERT_EQUAL(127, l.ev[0].msg[2]);
}

static void test_a_late_record_drops_the_layers_next_pass(void)
{
    start_layer();
    note_p(3500000, 64, true, 19);
    note_p(3700000, 64, false, 19);
    run(3050000, 5300000);
    note_p(5100000, 66, true, 19); /* the next pass begun before a late RECORD */
    note_p(5200000, 66, false, 19);
    looper_record(&l, 5300000, midi_rec, &rec);
    TEST_ASSERT_EQUAL(2000000, l.length_us);
    size_t start = rec.n;
    run(5300000, 9000000);
    TEST_ASSERT_EQUAL(0, count(start, 0x90 | (LOOP_CH + 1), 66));
    TEST_ASSERT_EQUAL(2, count(start, 0x90 | (LOOP_CH + 1), 64));
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
    RUN_TEST(test_a_layer_plays_at_its_level);
    RUN_TEST(test_drums_stay_drums);
    RUN_TEST(test_a_release_from_before_recording_is_not_stored);
    RUN_TEST(test_other_messages_are_not_stored);
    RUN_TEST(test_nothing_is_stored_unless_recording);
    RUN_TEST(test_record_with_no_notes_leaves_no_loop);
    RUN_TEST(test_replay_stops_and_restarts_from_the_top);
    RUN_TEST(test_replay_while_recording_closes_the_loop);
    RUN_TEST(test_record_while_stopped_starts_a_new_loop);
    RUN_TEST(test_with_the_beat_the_loop_starts_on_the_bar_line);
    RUN_TEST(test_a_16th_early_counts_as_on_the_bar);
    RUN_TEST(test_a_late_record_drops_the_next_pass_notes);
    RUN_TEST(test_an_early_record_rounds_up_and_waits_for_the_pass_end);
    RUN_TEST(test_a_phrase_begun_mid_bar_keeps_its_length_and_wraps);
    RUN_TEST(test_replay_with_the_beat_starts_at_the_next_bar_line);
    RUN_TEST(test_a_layer_plays_on_its_own_channel_with_its_instrument);
    RUN_TEST(test_a_layer_note_is_not_played_again_in_its_own_pass);
    RUN_TEST(test_the_first_layer_keeps_playing_while_adding);
    RUN_TEST(test_undo_drops_the_last_layer_then_the_loop);
    RUN_TEST(test_undo_while_adding_drops_the_new_layer);
    RUN_TEST(test_a_layer_with_nothing_played_is_not_added);
    RUN_TEST(test_no_more_than_four_layers);
    RUN_TEST(test_a_layer_over_two_passes_doubles_the_loop);
    RUN_TEST(test_a_layer_is_heard_once_closed);
    RUN_TEST(test_a_record_near_the_pass_end_counts_from_the_next_pass);
    RUN_TEST(test_a_pickup_held_over_the_pass_start_rings_on);
    RUN_TEST(test_a_late_record_drops_the_layers_next_pass);
    RUN_TEST(test_a_layer_keeps_the_reverb_turned_while_recording);
    RUN_TEST(test_pots_left_alone_add_nothing);
    RUN_TEST(test_the_first_layer_keeps_its_pot_moves_without_moving_its_start);
    RUN_TEST(test_clear_forgets_the_loop);
    RUN_TEST(test_a_full_loop_keeps_room_for_the_last_note_off);
    return UNITY_END();
}
