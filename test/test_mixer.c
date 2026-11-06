#include <stdio.h>

#include "midi_rec.h"
#include "mixer.h"
#include "unity.h"

static mixer_t m;

/* A raw reading for a level, in the middle of its span. */
static int raw_for(int level)
{
    return 100 + level * (MIXER_RAW - 200) / MIXER_LEVEL;
}

/* Turns a pot to a level and holds it there long enough to settle. */
static void turn(mixer_pot_t pot, int level, int64_t now)
{
    for (int i = 0; i < 40; i++) mixer_feed(&m, pot, raw_for(level), now);
}

void setUp(void)
{
    mixer_init(&m);
}

void tearDown(void)
{
}

static void test_starts_full(void)
{
    TEST_ASSERT_EQUAL(127, m.beat);
    TEST_ASSERT_EQUAL(127, m.keys);
    TEST_ASSERT_EQUAL(127, m.click);
}

static void test_first_reading_sets_the_level_at_once(void)
{
    TEST_ASSERT_TRUE(mixer_feed(&m, MIXER_BEAT, raw_for(40), 5));
    TEST_ASSERT_EQUAL(40, m.beat);
    TEST_ASSERT_EQUAL(0, m.moved_us[MIXER_BEAT]); /* boot isn't a turn */
}

static void test_ends_are_reachable(void)
{
    turn(MIXER_KEYS, 0, 10);
    TEST_ASSERT_EQUAL(0, m.keys);
    turn(MIXER_KEYS, 127, 20);
    TEST_ASSERT_EQUAL(127, m.keys);
    mixer_feed(&m, MIXER_KEYS, 0, 30); /* past the dead end */
    turn(MIXER_KEYS, 0, 30);
    TEST_ASSERT_EQUAL(0, m.keys);
}

static void test_noise_does_not_flicker(void)
{
    turn(MIXER_BEAT, 64, 10);
    TEST_ASSERT_EQUAL(64, m.beat);
    int changes = 0;
    for (int i = 0; i < 1000; i++) {
        int noise = (i * 37 % 41) - 20; /* +-20 counts */
        changes += mixer_feed(&m, MIXER_BEAT, raw_for(64) + noise, 100 + i);
    }
    TEST_ASSERT_EQUAL(0, changes);
}

static void test_turning_is_followed(void)
{
    turn(MIXER_BEAT, 64, 10);
    turn(MIXER_BEAT, 90, 50);
    TEST_ASSERT_EQUAL(90, m.beat);
    TEST_ASSERT_EQUAL(50, m.moved_us[MIXER_BEAT]);
}

static void test_a_pot_left_alone_holds_against_a_one_level_hop(void)
{
    turn(MIXER_METRO, 74, 10);
    TEST_ASSERT_EQUAL(74, m.pot[MIXER_METRO]);
    turn(MIXER_METRO, 75, 10 + MIXER_SETTLE_US); /* left alone, then a hop: held */
    TEST_ASSERT_EQUAL(74, m.pot[MIXER_METRO]);
    turn(MIXER_METRO, 76, 20 + MIXER_SETTLE_US); /* turned: it moves */
    TEST_ASSERT_EQUAL(76, m.pot[MIXER_METRO]);
    turn(MIXER_METRO, 75, 30 + MIXER_SETTLE_US); /* and follows every level while turned */
    TEST_ASSERT_EQUAL(75, m.pot[MIXER_METRO]);
}

static void test_a_one_level_hop_is_not_a_turn(void)
{
    turn(MIXER_KEYS, 100, 10);
    int64_t turned = m.moved_us[MIXER_KEYS];
    turn(MIXER_KEYS, 101, 500);
    turn(MIXER_KEYS, 99, 600);
    turn(MIXER_KEYS, 102, 700);
    TEST_ASSERT_EQUAL(102, m.keys); /* the level follows */
    TEST_ASSERT_EQUAL(turned, m.moved_us[MIXER_KEYS]);
    turn(MIXER_KEYS, 103, 800); /* 3 from where it rested */
    TEST_ASSERT_EQUAL(800, m.moved_us[MIXER_KEYS]);
}

static void test_click_pot_is_cc11_on_the_metronome_channel(void)
{
    midi_rec_t r = {0};
    turn(MIXER_CLICK, 40, 10);
    TEST_ASSERT_EQUAL(40, m.click);
    mixer_send_fx(&m, MIXER_CLICK, midi_rec, &r);
    TEST_ASSERT_EQUAL(1, r.n);
    TEST_ASSERT_EQUAL_HEX8(0xb0 | METRO_CH, r.e[0].msg[0]);
    TEST_ASSERT_EQUAL(11, r.e[0].msg[1]);
    TEST_ASSERT_EQUAL(40, r.e[0].msg[2]);
    TEST_ASSERT_EQUAL_STRING("metronome volume 40", r.e[0].what);
}

static void test_scale(void)
{
    TEST_ASSERT_EQUAL(70, mixer_scale(70, 127));
    TEST_ASSERT_EQUAL(35, mixer_scale(70, 64));
    TEST_ASSERT_EQUAL(1, mixer_scale(10, 1)); /* never silent above 0 */
    TEST_ASSERT_EQUAL(0, mixer_scale(70, 0));
}

static void test_apply(void)
{
    uint8_t out[3];
    const uint8_t on[3] = {0x99, 36, 100};
    TEST_ASSERT_EQUAL(3, mixer_apply(on, 3, 64, out));
    TEST_ASSERT_EQUAL(50, out[2]);
    TEST_ASSERT_EQUAL(0, mixer_apply(on, 3, 0, out)); /* muted: left out */
    const uint8_t off[3] = {0x89, 36, 0};
    TEST_ASSERT_EQUAL(3, mixer_apply(off, 3, 0, out)); /* a note off always goes */
    const uint8_t on0[3] = {0x99, 36, 0};              /* note on 0 = a note off */
    TEST_ASSERT_EQUAL(3, mixer_apply(on0, 3, 0, out));
    TEST_ASSERT_EQUAL(0, out[2]);
    const uint8_t pc[2] = {0xc3, 81};
    TEST_ASSERT_EQUAL(2, mixer_apply(pc, 2, 0, out));
}

static void test_effects_start_open_and_dry(void)
{
    TEST_ASSERT_EQUAL(127, m.filter);
    TEST_ASSERT_EQUAL(0, m.reverb);
}

static void test_effect_pots_set_their_levels(void)
{
    turn(MIXER_FILTER, 30, 1);
    turn(MIXER_REVERB, 90, 1);
    TEST_ASSERT_EQUAL(30, m.filter);
    TEST_ASSERT_EQUAL(90, m.reverb);
    TEST_ASSERT_EQUAL(127, m.beat); /* the others untouched */
    TEST_ASSERT_EQUAL(127, m.keys);
}

/* The 14-bit NRPN value sent on a channel for a generator: LSB (38) first, then MSB (6). */
static int nrpn_value(const midi_rec_t *r, uint8_t ch, uint8_t gen)
{
    const midi_rec_entry_t *e = &r->e[ch * 4];
    TEST_ASSERT_EQUAL_HEX8(0xb0 | ch, e[0].msg[0]);
    TEST_ASSERT_EQUAL(99, e[0].msg[1]);
    TEST_ASSERT_EQUAL(120, e[0].msg[2]);
    TEST_ASSERT_EQUAL(98, e[1].msg[1]);
    TEST_ASSERT_EQUAL(gen, e[1].msg[2]);
    TEST_ASSERT_EQUAL(38, e[2].msg[1]);
    TEST_ASSERT_EQUAL(6, e[3].msg[1]);
    return e[3].msg[2] << 7 | e[2].msg[2];
}
#define filter_value(r, ch) nrpn_value(r, ch, 8)

static void test_reverb_is_an_nrpn_send_from_dry_to_full(void)
{
    midi_rec_t r = {0};
    mixer_send_fx(&m, MIXER_REVERB, midi_rec, &r); /* 0: dry */
    TEST_ASSERT_EQUAL(4 * MIXER_FX_LIVE, r.n);
    for (uint8_t ch = 0; ch < MIXER_FX_LIVE; ch++)
        TEST_ASSERT_EQUAL(8192 - MIXER_REVERB_SEND, nrpn_value(&r, ch, 16));
    TEST_ASSERT_EQUAL_STRING("reverb 0 (send -100.0%)", r.e[3].what);
    TEST_ASSERT_EQUAL_STRING("", r.e[7].what); /* logged once */
    memset(&r, 0, sizeof(r));
    m.reverb = 127;
    mixer_send_fx(&m, MIXER_REVERB, midi_rec, &r);
    TEST_ASSERT_EQUAL(8192 + MIXER_REVERB_SEND, nrpn_value(&r, 1, 16));
}

static void test_filter_is_an_nrpn_cutoff_offset(void)
{
    midi_rec_t r = {0};
    mixer_send_fx(&m, MIXER_FILTER, midi_rec, &r); /* open: no offset */
    TEST_ASSERT_EQUAL(4 * MIXER_FX_LIVE, r.n);
    for (uint8_t ch = 0; ch < MIXER_FX_LIVE; ch++) TEST_ASSERT_EQUAL(8192, filter_value(&r, ch));
    TEST_ASSERT_EQUAL_STRING("filter 127 (0 cents)", r.e[3].what);

    memset(&r, 0, sizeof(r));
    m.filter = 0; /* closed: MIXER_FILTER_CENTS, 2 cents a step */
    mixer_send_fx(&m, MIXER_FILTER, midi_rec, &r);
    TEST_ASSERT_EQUAL(8192 + MIXER_FILTER_CENTS / 2, filter_value(&r, 1));
}

static void test_other_pots_send_no_effects(void)
{
    midi_rec_t r = {0};
    mixer_send_fx(&m, MIXER_BEAT, midi_rec, &r);
    TEST_ASSERT_EQUAL(0, r.n);
}

static void test_a_layer_keeps_the_effects_it_was_recorded_with(void)
{
    midi_rec_t r = {0};
    m.filter = 40;
    m.reverb = 127;
    m.keys = 30; /* pot 3, the live keys' fader */
    mixer_freeze_layer(&m, 1, midi_rec, &r);
    TEST_ASSERT_EQUAL(9, r.n); /* the fader, filter, reverb, on the layer's own channel */
    TEST_ASSERT_EQUAL_HEX8(0xb0 | (LOOP_CH + 1), r.e[0].msg[0]);
    TEST_ASSERT_EQUAL(11, r.e[0].msg[1]);
    TEST_ASSERT_EQUAL(30, r.e[0].msg[2]);
    TEST_ASSERT_EQUAL_HEX8(0xb0 | (LOOP_CH + 1), r.e[1].msg[0]);
    TEST_ASSERT_EQUAL(8, r.e[2].msg[2]);
    TEST_ASSERT_EQUAL(16, r.e[6].msg[2]);
    TEST_ASSERT_EQUAL(8192 + MIXER_REVERB_SEND, r.e[8].msg[2] << 7 | r.e[7].msg[2]);
    TEST_ASSERT_EQUAL_STRING("layer 2 keeps keys 30, filter 40, reverb 127", r.e[8].what);

    /* The pots move on: the live channels change, the layer doesn't. */
    memset(&r, 0, sizeof(r));
    turn(MIXER_FILTER, 120, 1);
    mixer_send_fx(&m, MIXER_FILTER, midi_rec, &r);
    for (size_t i = 0; i < r.n; i++) TEST_ASSERT_TRUE((r.e[i].msg[0] & 0x0f) < MIXER_FX_LIVE);
    TEST_ASSERT_EQUAL(40, m.layer_filter[1]);

    memset(&r, 0, sizeof(r));
    turn(MIXER_KEYS, 127, 1);
    mixer_send_layer_fx(&m, 1, midi_rec, &r); /* again, after a reconnect */
    TEST_ASSERT_EQUAL(30, r.e[0].msg[2]);
    TEST_ASSERT_EQUAL_STRING("layer 2 keeps keys 30, filter 40, reverb 127", r.e[8].what);
}

static void test_keys_pot_is_cc11_on_the_keys_channel(void)
{
    midi_rec_t r = {0};
    m.keys = 50;
    mixer_send_fx(&m, MIXER_KEYS, midi_rec, &r);
    TEST_ASSERT_EQUAL(1, r.n);
    TEST_ASSERT_EQUAL_HEX8(0xb0 | KEY_CH, r.e[0].msg[0]);
    TEST_ASSERT_EQUAL(11, r.e[0].msg[1]);
    TEST_ASSERT_EQUAL(50, r.e[0].msg[2]);
    TEST_ASSERT_EQUAL_STRING("keys 50", r.e[0].what);
}

static void test_metronome_pot_only_keeps_its_level(void)
{
    midi_rec_t r = {0};
    turn(MIXER_METRO, 33, 1);
    TEST_ASSERT_EQUAL(33, m.metro);
    mixer_send_fx(&m, MIXER_METRO, midi_rec, &r); /* its tempo is main's (metronome.h) */
    TEST_ASSERT_EQUAL(0, r.n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_starts_full);
    RUN_TEST(test_a_pot_left_alone_holds_against_a_one_level_hop);
    RUN_TEST(test_a_one_level_hop_is_not_a_turn);
    RUN_TEST(test_first_reading_sets_the_level_at_once);
    RUN_TEST(test_ends_are_reachable);
    RUN_TEST(test_noise_does_not_flicker);
    RUN_TEST(test_turning_is_followed);
    RUN_TEST(test_click_pot_is_cc11_on_the_metronome_channel);
    RUN_TEST(test_scale);
    RUN_TEST(test_apply);
    RUN_TEST(test_effects_start_open_and_dry);
    RUN_TEST(test_effect_pots_set_their_levels);
    RUN_TEST(test_reverb_is_an_nrpn_send_from_dry_to_full);
    RUN_TEST(test_filter_is_an_nrpn_cutoff_offset);
    RUN_TEST(test_other_pots_send_no_effects);
    RUN_TEST(test_keys_pot_is_cc11_on_the_keys_channel);
    RUN_TEST(test_metronome_pot_only_keeps_its_level);
    RUN_TEST(test_a_layer_keeps_the_effects_it_was_recorded_with);
    return UNITY_END();
}
