#include <stdbool.h>
#include <string.h>

#include "fx.h"
#include "keymap.h"
#include "looper.h"
#include "midi_rec.h"
#include "take.h"
#include "unity.h"

static take_t t;
static midi_rec_t rec;

void setUp(void)
{
    take_init(&t);
    memset(&rec, 0, sizeof(rec));
}

void tearDown(void)
{
}

static void msg(uint8_t status, uint8_t a, uint8_t b)
{
    uint8_t m[3] = {status, a, b};
    take_msg(&t, m, 3, midi_rec, &rec);
}

static void expect(size_t i, uint8_t status, uint8_t n, uint8_t vel)
{
    TEST_ASSERT_TRUE(i < rec.n);
    TEST_ASSERT_EQUAL_HEX8(status, rec.e[i].msg[0]);
    TEST_ASSERT_EQUAL(n, rec.e[i].msg[1]);
    TEST_ASSERT_EQUAL(vel, rec.e[i].msg[2]);
}

static void test_echo_repeats_come_out_on_the_keys_channel(void)
{
    msg(0x90 | FX_ECHO_CH, 60, 38);
    msg(0x80 | FX_ECHO_CH, 60, 0);
    TEST_ASSERT_EQUAL(2, rec.n);
    expect(0, 0x90 | KEY_CH, 60, 38);
    expect(1, 0x80 | KEY_CH, 60, 0);
}

static void test_an_echo_over_a_held_key_is_left_out(void)
{
    msg(0x90 | KEY_CH, 60, 55);
    msg(0x90 | FX_ECHO_CH, 60, 38); /* the key still held: the held note rings on */
    msg(0x80 | FX_ECHO_CH, 60, 0);
    msg(0x90 | FX_ECHO_CH, 60, 26);
    msg(0x80 | KEY_CH, 60, 0); /* ends it, though that echo is still on */
    msg(0x80 | FX_ECHO_CH, 60, 0);
    msg(0x90 | FX_ECHO_CH, 60, 18); /* after the key: heard */
    msg(0x80 | FX_ECHO_CH, 60, 0);
    TEST_ASSERT_EQUAL(4, rec.n);
    expect(0, 0x90 | KEY_CH, 60, 55);
    expect(1, 0x80 | KEY_CH, 60, 0);
    expect(2, 0x90 | KEY_CH, 60, 18);
    expect(3, 0x80 | KEY_CH, 60, 0);
}

static void test_a_key_over_its_echo_retriggers_and_ends_with_the_last_off(void)
{
    msg(0x90 | FX_ECHO_CH, 60, 38);
    msg(0x90 | KEY_CH, 60, 55);
    msg(0x80 | FX_ECHO_CH, 60, 0); /* the key still holds it */
    msg(0x80 | KEY_CH, 60, 0);
    TEST_ASSERT_EQUAL(4, rec.n);
    expect(1, 0x80 | KEY_CH, 60, 0);
    expect(2, 0x90 | KEY_CH, 60, 55);
    expect(3, 0x80 | KEY_CH, 60, 0);
}

static void test_hold_turns_into_late_note_offs(void)
{
    msg(0xb0 | KEY_CH, 64, 127);
    msg(0x90 | KEY_CH, 60, 55);
    msg(0x80 | KEY_CH, 60, 0);
    TEST_ASSERT_EQUAL(1, rec.n);
    msg(0xb0 | KEY_CH, 64, 0);
    TEST_ASSERT_EQUAL(2, rec.n);
    expect(1, 0x80 | KEY_CH, 60, 0);
}

static void test_a_held_note_played_again_retriggers(void)
{
    msg(0xb0 | KEY_CH, 64, 127);
    msg(0x90 | KEY_CH, 60, 55);
    msg(0x80 | KEY_CH, 60, 0);
    msg(0x90 | KEY_CH, 60, 50);
    msg(0xb0 | KEY_CH, 64, 0); /* the key is down again: nothing to end */
    TEST_ASSERT_EQUAL(3, rec.n);
    expect(1, 0x80 | KEY_CH, 60, 0);
    expect(2, 0x90 | KEY_CH, 60, 50);
}

static void test_wide_copies_and_other_messages_are_not_passed_on(void)
{
    msg(0x90 | FX_WIDE_CH, 60, 55);
    msg(0x90 | FX_WIDE2_CH, 60, 55);
    msg(0xe0 | KEY_CH, 0, 0x50);
    msg(0xb0 | KEY_CH, 11, 90);
    msg(0x80 | KEY_CH, 61, 0); /* never started */
    TEST_ASSERT_EQUAL(0, rec.n);
}

/* ---- The whole path, as main.c wires it: keys -> fx -> take -> looper -> playback ----
 * A real take (2026-11-08, captured with arecordmidi: SLOW ROCK at 130 bpm, Echo and
 * Octave on): its held notes came back 350 ms long. Each round of the loop must play
 * every note the take made, at its place, as long as it was held. */

#define BAR_US  1846154               /* 130 bpm, 4 beats */
#define T0      (2 * (int64_t)BAR_US) /* the take's bar line */
#define ECHO_US (FX_ECHO_STEPS * (BAR_US / 16))

typedef struct {
    int32_t on_ms, len_ms; /* from T0 */
    uint8_t note;
} played_t;

/* Key presses on the keys' channel, from the capture (times from the bar line). */
static const played_t TAKE[] = {
    {-10, 1524, 21}, {1354, 599, 28}, {1883, 1397, 24}, {3239, 111, 28},
    {3247, 512, 31}, {3664, 557, 28}, {4174, 515, 24},  {4638, 1365, 21},
};
#define TAKE_N   (sizeof(TAKE) / sizeof(TAKE[0]))
#define CLOSE_MS 5661 /* RECORD again, with the last key still held */

static fx_t e_fx;
static take_t e_take;
static looper_t e_loop;
static int64_t e_now;

typedef struct {
    int64_t on_us, off_us;
    uint8_t note, vel;
} heard_t;
static heard_t heard[512];
static size_t n_heard;
static int64_t sounding_since[128];
static uint8_t sounding_vel[128];

static void loop_out(const uint8_t *msg, size_t len, const char *what, void *ctx)
{
    (void)what;
    (void)ctx;
    if (len != 3 || (msg[0] & 0x0f) != LOOP_CH) return;
    uint8_t kind = msg[0] & 0xf0, n = msg[1];
    bool on = kind == 0x90 && msg[2];
    if (kind != 0x90 && kind != 0x80) return;
    if (sounding_since[n] >= 0 && n_heard < 512) { /* an off, or a retrigger */
        heard[n_heard++] = (heard_t){sounding_since[n], e_loop.due_us, n, sounding_vel[n]};
        sounding_since[n] = -1;
    }
    if (on) {
        sounding_since[n] = e_loop.due_us;
        sounding_vel[n] = msg[2];
    }
}

static void to_looper(const uint8_t *msg, size_t len, const char *what, void *ctx)
{
    (void)what;
    (void)ctx;
    looper_capture(&e_loop, e_now, msg, len, 81);
}

static void from_fx(const uint8_t *msg, size_t len, const char *what, void *ctx)
{
    (void)what;
    (void)ctx;
    take_msg(&e_take, msg, len, to_looper, NULL);
}

static void play_take(void)
{
    fx_init(&e_fx);
    take_init(&e_take);
    looper_init(&e_loop);
    memset(&rec, 0, sizeof(rec));
    n_heard = 0;
    for (int i = 0; i < 128; i++) sounding_since[i] = -1;
    fx_set_grid(&e_fx, 0, BAR_US / 16);
    looper_set_grid(&e_loop, 0, BAR_US);
    fx_button(&e_fx, FX_ECHO, true, 0, from_fx, NULL);
    fx_button(&e_fx, FX_OCTAVE, true, 0, from_fx, NULL);
    looper_record(&e_loop, T0 - 1000000, loop_out, NULL);
    for (e_now = T0 - 500000; e_now < T0 + 6 * 3 * (int64_t)BAR_US; e_now += 1000) {
        for (size_t i = 0; i < TAKE_N; i++) {
            int64_t on = T0 + TAKE[i].on_ms * 1000LL, off = on + TAKE[i].len_ms * 1000LL;
            if (e_now == on || e_now == off) {
                uint8_t m[3] = {(uint8_t)((e_now == on ? 0x90 : 0x80) | KEY_CH), TAKE[i].note,
                                e_now == on ? 55 : 0};
                fx_key(&e_fx, m, 3, e_now, from_fx, NULL);
            }
        }
        if (e_now == T0 + CLOSE_MS * 1000LL) looper_record(&e_loop, e_now, loop_out, NULL);
        fx_tick(&e_fx, e_now, from_fx, NULL);
        looper_tick(&e_loop, e_now, loop_out, NULL);
    }
}

/* A note the take made: a key or its octave, or an echo of one. */
typedef struct {
    int64_t on_us, min_len_us, max_len_us; /* from the pass start */
    uint8_t note, vel;
} want_t;

static bool key_held(uint8_t n, int64_t t_us)
{
    for (size_t i = 0; i < TAKE_N; i++) {
        int64_t on = TAKE[i].on_ms * 1000LL, off = on + TAKE[i].len_ms * 1000LL;
        if ((TAKE[i].note == n || TAKE[i].note + 12 == n) && on < t_us && t_us < off) return true;
    }
    return false;
}

static size_t wanted(want_t *w, int64_t length)
{
    size_t n = 0;
    for (size_t i = 0; i < TAKE_N; i++) {
        int64_t on = TAKE[i].on_ms * 1000LL, len = TAKE[i].len_ms * 1000LL;
        for (int o = 0; o < 2; o++) {
            uint8_t note = (uint8_t)(TAKE[i].note + 12 * o);
            int64_t at = on < 0 ? 0 : on;             /* a 16th early counts as on the bar line */
            int64_t cap = length - at;                /* held over RECORD: to the loop's end */
            int64_t sounds = on < 0 ? len + on : len; /* moved onto the bar line, it ends where it ended */
            int64_t min = sounds < cap ? sounds : cap;
            w[n++] = (want_t){at, min - 5000, len + 5000, note, 55};
            unsigned v = 55;
            for (int k = 1; k <= fx_echo_repeats(FX_ECHO_LEVEL); k++) {
                v = v * (unsigned)fx_echo_decay(FX_ECHO_LEVEL) / 100;
                int64_t t = on + k * ECHO_US;
                if (t > CLOSE_MS * 1000LL || key_held(note, t)) continue; /* past the take, or over its key */
                if (t >= length)
                    continue; /* past the loop's end, after the first note's place: dropped (looper.h) */
                w[n++] = (want_t){t, 0, INT64_MAX, note, (uint8_t)v};
            }
        }
    }
    return n;
}

static void test_a_real_take_with_echo_and_octave_plays_back_as_played(void)
{
    play_take();
    TEST_ASSERT_EQUAL(LOOPER_PLAYING, e_loop.state);
    int64_t length = e_loop.length_us;
    TEST_ASSERT_EQUAL_INT64(3 * (int64_t)BAR_US, length);
    want_t w[128];
    size_t nw = wanted(w, length);
    char why[160];
    for (int pass = 2; pass <= 4; pass++) { /* pass 1 was under way when RECORD closed it */
        int64_t start = T0 + pass * length;
        size_t in_pass = 0;
        for (size_t h = 0; h < n_heard; h++)
            in_pass += heard[h].on_us >= start && heard[h].on_us < start + length;
        for (size_t i = 0; i < nw; i++) {
            const heard_t *hit = NULL;
            for (size_t h = 0; h < n_heard; h++) {
                int64_t d = heard[h].on_us - (start + w[i].on_us);
                if (heard[h].note == w[i].note && d > -3000 && d < 3000) hit = &heard[h];
            }
            snprintf(why, sizeof(why), "pass %d: note %u at %lld ms vel %u", pass, w[i].note,
                     (long long)(w[i].on_us / 1000), w[i].vel);
            TEST_ASSERT_NOT_NULL_MESSAGE(hit, why);
            TEST_ASSERT_EQUAL_MESSAGE(w[i].vel, hit->vel, why);
            int64_t len = hit->off_us - hit->on_us;
            snprintf(why, sizeof(why), "pass %d: note %u at %lld ms: %lld ms long, played %lld-%lld", pass,
                     w[i].note, (long long)(w[i].on_us / 1000), (long long)(len / 1000),
                     (long long)(w[i].min_len_us / 1000), (long long)(w[i].max_len_us / 1000));
            TEST_ASSERT_TRUE_MESSAGE(len >= w[i].min_len_us && len <= w[i].max_len_us, why);
        }
        snprintf(why, sizeof(why), "pass %d: %zu notes, the take made %zu", pass, in_pass, nw);
        TEST_ASSERT_EQUAL_MESSAGE(nw, in_pass, why);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_echo_repeats_come_out_on_the_keys_channel);
    RUN_TEST(test_an_echo_over_a_held_key_is_left_out);
    RUN_TEST(test_a_key_over_its_echo_retriggers_and_ends_with_the_last_off);
    RUN_TEST(test_hold_turns_into_late_note_offs);
    RUN_TEST(test_a_held_note_played_again_retriggers);
    RUN_TEST(test_wide_copies_and_other_messages_are_not_passed_on);
    RUN_TEST(test_a_real_take_with_echo_and_octave_plays_back_as_played);
    return UNITY_END();
}
