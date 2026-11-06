/*
 * esp32-piano: key matrix scanner + MIDI over WiFi (milestone 3, direction A).
 *
 * The toy's keys and buttons sit on an 8 x 8 grid of the sound chip's slot
 * lines (docs/MATRIX.md): AB0-AB7 are the rows, EF0-EF7 the columns. Toy
 * batteries out: the only voltage on the lines is the ESP32's 3.3 V.
 *
 * The 16 lines go to an MCP23017 on I2C (docs/wiring.html): AB0-AB7 on its
 * port A, EF0-EF7 on port B. Every line rests as an input with the chip's
 * pull-up. A scan pulls one AB row low at a time and reads the EF columns: a
 * column that follows it low has a pressed key or button at that crossing.
 * Lines are never driven high: a row turns into an output only with its latch
 * at 0.
 *
 * The pure parts, host-tested (tools/test.sh): matrix.c (debounce, ghosts),
 * keymap.c (names, what each key and button plays), demo.c (DEMO button),
 * looper.c (RECORD / REPLAY), rhythm.c (SELECT: drum beats), mixer.c (the five
 * pots: beat, metronome volume, live keys, filter, reverb, metronome tempo), metronome.c
 * (FROG), leds.c (the LED strip).
 * MIDI goes out as UDP over WiFi to tools/piano_bridge.c (main/midi_link.h).
 *
 * Output on serial (115200):
 *   DOWN  <name>   (<EFx>+<ABy>)
 *   UP    <name>
 *   GHOST? <names>  the pressed set contains a rectangle; one of them may be fake
 *   MIDI  <what>   what was sent (or "(not connected)")
 *   KNOB  CW / CCW / PRESS
 *   POT   beat|click|keys|filter|reverb|metro <level>   a pot turned (0-127)
 * Changes are reported after DEBOUNCE equal scans in a row.
 *
 * The line-pair finder that mapped the grid is in git history (233c8c3).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "demo.h"
#include "driver/i2c_master.h"
#include "encoder.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "groove.h"
#include "keymap.h"
#include "leds.h"
#include "looper.h"
#include "matrix.h"
#include "metronome.h"
#include "midi_link.h"
#include "mixer.h"
#include "rhythm.h"
#include "screen.h"
#include "strip.h"
#include "wifi.h"

/* The MCP23017 (address 0x20: A0-A2 pulled down on the board). IOCON.BANK = 0,
 * the reset default: each A register is followed by its B twin. */
#define I2C_SDA    GPIO_NUM_21
#define I2C_SCL    GPIO_NUM_22
#define I2C_HZ     400000
#define MCP_ADDR   0x20
#define MCP_IODIRA 0x00 /* 1 = input */
#define MCP_IODIRB 0x01
#define MCP_GPPUA  0x0C /* 1 = 100k pull-up */
#define MCP_GPPUB  0x0D
#define MCP_OLATA  0x14 /* what an output drives */
#define MCP_GPIOB  0x13
/* Rows on A, columns on B. If keys come out transposed, the holes of each pair were the other way round. */
#define ROW_IODIR MCP_IODIRA
#define COL_GPIO  MCP_GPIOB
/* CD0 carries no key or button: not wired. */

#define WIFI_TIMEOUT_MS 10000 /* boot waits this long, then scans anyway */
#define SETTLE_US       50    /* pull-up (100k) against the toy's wiring */
#define SCAN_PERIOD_MS  2
#define SCAN_STALL_US   20000 /* a scan pass this late is logged */
#define POT_PERIOD_US   10000 /* the pots are read this often */
#define POT_READS       8     /* averaged per pot per read (~10 us each) */
#define FX_PERIOD_US    30000 /* the filter and reverb are sent at most this often */
#define LEDS_PERIOD_US  20000 /* the strip is redrawn this often (when it changed) */

/* The pots' wipers on ADC1 (ADC2 doesn't work with WiFi on): every ADC1 pin a DevKit has. */
static const adc_channel_t POT_CH[MIXER_POTS] = {
    [MIXER_BEAT] = ADC_CHANNEL_4,   /* D32 */
    [MIXER_CLICK] = ADC_CHANNEL_5,  /* D33 */
    [MIXER_KEYS] = ADC_CHANNEL_6,   /* D34 */
    [MIXER_FILTER] = ADC_CHANNEL_7, /* D35 */
    [MIXER_REVERB] = ADC_CHANNEL_0, /* VP (GPIO36) */
    [MIXER_METRO] = ADC_CHANNEL_3,  /* VN (GPIO39) */
};
static const char *const POT_NAME[MIXER_POTS] = {"beat", "click", "keys", "filter", "reverb", "metro"};

static keymap_t keymap;
static demo_t demo;
static looper_t looper;
static rhythm_t rhythm;
static groove_t groove;
static mixer_t mixer;
static metro_t metro;
static adc_oneshot_unit_handle_t adc;

static i2c_master_dev_handle_t mcp;

static esp_err_t mcp_write(uint8_t reg, uint8_t v)
{
    uint8_t buf[2] = {reg, v};
    return i2c_master_transmit(mcp, buf, sizeof(buf), 10);
}

static void mcp_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MCP_ADDR,
        .scl_speed_hz = I2C_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &mcp));
    /* Everything in with pull-ups first, then the row latches at 0. */
    esp_err_t err = mcp_write(MCP_IODIRA, 0xFF);
    if (err == ESP_OK) err = mcp_write(MCP_IODIRB, 0xFF);
    if (err == ESP_OK) err = mcp_write(MCP_GPPUA, 0xFF);
    if (err == ESP_OK) err = mcp_write(MCP_GPPUB, 0xFF);
    if (err == ESP_OK) err = mcp_write(MCP_OLATA, 0x00);
    printf("mcp23017 at 0x%02x: %s\n", MCP_ADDR, esp_err_to_name(err));
}

/* An I2C error reads as nothing pressed, so a lost chip can't hold a note. */
static void scan(grid_t g)
{
    static bool failed;
    esp_err_t err = ESP_OK;
    for (size_t r = 0; r < N_ROWS && err == ESP_OK; r++) {
        err = mcp_write(ROW_IODIR, (uint8_t)~(1u << r));
        esp_rom_delay_us(SETTLE_US);
        uint8_t reg = COL_GPIO;
        uint8_t in = 0xFF;
        if (err == ESP_OK) err = i2c_master_transmit_receive(mcp, &reg, 1, &in, 1, 10);
        g[r] = (uint8_t)~in;
    }
    if (mcp_write(ROW_IODIR, 0xFF) != ESP_OK && err == ESP_OK) err = ESP_FAIL;
    if (err != ESP_OK) {
        memset(g, 0, sizeof(grid_t));
        if (!failed) printf("mcp23017: %s, keys read as up\n", esp_err_to_name(err));
    } else if (failed) {
        printf("mcp23017: back\n");
    }
    failed = err != ESP_OK;
}

static void report(const grid_t old_g, const grid_t new_g)
{
    char name[16];
    for (size_t r = 0; r < N_ROWS; r++) {
        for (size_t c = 0; c < N_COLS; c++) {
            bool was = old_g[r] & (1u << c);
            bool is = new_g[r] & (1u << c);
            if (was == is) continue;
            keymap_name(r, c, name, sizeof(name));
            if (is) {
                printf("DOWN  %-10s (EF%u+AB%u)\n", name, (unsigned)c, (unsigned)r);
            } else {
                printf("UP    %s\n", name);
            }
        }
    }
    if (!matrix_has_rectangle(new_g)) return;
    printf("GHOST?");
    for (size_t r = 0; r < N_ROWS; r++) {
        for (size_t c = 0; c < N_COLS; c++) {
            if (!(new_g[r] & (1u << c))) continue;
            keymap_name(r, c, name, sizeof(name));
            printf(" [%s]", name);
        }
    }
    printf("\n");
}

static void send_midi(const uint8_t *msg, size_t len, const char *what, void *ctx)
{
    if (len) midi_link_send(msg, len);
    if (what) printf("MIDI  %s%s\n", what, midi_link_connected() ? "" : " (not connected)");
}

/* Sequenced MIDI (beat, DEMO, loop), worked out MIDI_LINK_LEAD_US ahead: ctx
 * points to the time it should sound. */
static void send_at(const uint8_t *msg, size_t len, const char *what, void *ctx)
{
    if (len) midi_link_send_at(msg, len, *(const int64_t *)ctx);
    if (what) printf("MIDI  %s%s\n", what, midi_link_connected() ? "" : " (not connected)");
}

/* The beat, at pot 1's level. */
static void send_beat(const uint8_t *msg, size_t len, const char *what, void *ctx)
{
    uint8_t out[3];
    if (len == 0 || len > sizeof(out)) { /* nothing to scale */
        send_at(msg, len, what, ctx);
        return;
    }
    send_at(out, mixer_apply(msg, len, mixer.beat, out), what, ctx);
}

/* What keymap sends: played as is (pot 3 is CC11 on the keys' channel) and
 * recorded (pot 2 sets the layer's level). */
static void send_key(const uint8_t *msg, size_t len, const char *what, void *ctx)
{
    send_midi(msg, len, what, ctx);
    looper_capture(&looper, esp_timer_get_time(), msg, len, keymap.program);
}

static void pots_init(void)
{
    adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
    if (adc_oneshot_new_unit(&unit, &adc) != ESP_OK) {
        printf("pots: no ADC\n");
        adc = NULL;
        return;
    }
    adc_oneshot_chan_cfg_t ch = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12};
    for (size_t i = 0; i < MIXER_POTS; i++) adc_oneshot_config_channel(adc, POT_CH[i], &ch);
    mixer_init(&mixer);
}

static void pots_read(int64_t now)
{
    static bool fx_due[MIXER_POTS];
    if (!adc) return;
    for (size_t i = 0; i < MIXER_POTS; i++) {
        /* The mean of POT_READS readings: one oneshot read is noisy. */
        int sum = 0;
        int n = 0;
        for (int r = 0; r < POT_READS; r++) {
            int raw;
            if (adc_oneshot_read(adc, POT_CH[i], &raw) == ESP_OK) {
                sum += raw;
                n++;
            }
        }
        if (!n) continue;
        if (mixer_feed(&mixer, (mixer_pot_t)i, sum / n, now)) {
            printf("POT   %s %u\n", POT_NAME[i], (unsigned)mixer.pot[i]);
            if (i != MIXER_BEAT && i != MIXER_METRO) fx_due[i] = true;
            if (i == MIXER_METRO) metro_set_bpm(&metro, metro_bpm_for(mixer.metro));
        }
    }
    /* A PC that just said HELLO (the bridge or FluidSynth restarted) gets the effects again. */
    static bool was_connected;
    bool connected = midi_link_connected();
    if (connected && !was_connected) {
        for (size_t i = 0; i < MIXER_POTS; i++) fx_due[i] = i != MIXER_BEAT && i != MIXER_METRO;
        if (metro.on) metro_setup(send_midi, NULL);
        for (size_t l = 0; l < looper.n_layers; l++) mixer_send_layer_fx(&mixer, l, send_midi, NULL);
    }
    was_connected = connected;
    /* A turn sends the latest setting at most every FX_PERIOD_US: the filter is 8
     * messages a step, too many for the cable at every reading. */
    static int64_t fx_sent;
    if (now - fx_sent < FX_PERIOD_US) return;
    for (size_t i = 0; i < MIXER_POTS; i++) {
        if (!fx_due[i]) continue;
        fx_due[i] = false;
        fx_sent = now;
        mixer_send_fx(&mixer, (mixer_pot_t)i, send_midi, NULL);
    }
}

/* The LED strip, from the state of the beat, the loop and the pots. */
static void leds_update(int64_t now)
{
    static leds_t last;
    static bool drawn;
    leds_state_t s = {
        .now_us = now,
        .beat_step = -1,
        .layers = (int)looper_layers(&looper),
        .recording_layer = looper.state == LOOPER_RECORDING ? 0
                           : looper.dubbing                 ? (int)looper.n_layers
                                                            : -1,
        .armed = looper.state == LOOPER_ARMED,
        .loop_playing = looper.state == LOOPER_PLAYING,
        .target = (int)looper_layers(&looper) - 1,
        .pot = -1,
    };
    if (rhythm.on) {
        s.beat_step = groove_step(&groove, now);
        s.steps_per_beat = rhythm_style(rhythm.style)->steps_per_beat;
    }
    int64_t latest = 0;
    for (size_t i = 0; i < MIXER_POTS; i++) {
        if (mixer.moved_us[i] && now - mixer.moved_us[i] < LEDS_POT_SHOW_US && mixer.moved_us[i] > latest) {
            latest = mixer.moved_us[i];
            s.pot = (int)i;
        }
    }
    if (s.pot == MIXER_BEAT) s.level = mixer.beat;
    if (s.pot == MIXER_KEYS) s.level = mixer.keys;
    if (s.pot == MIXER_FILTER) s.level = mixer.filter;
    if (s.pot == MIXER_REVERB) s.level = mixer.reverb;
    if (s.pot == MIXER_METRO) s.level = mixer.metro;
    if (s.pot == MIXER_CLICK) s.level = mixer.click;
    leds_t c;
    leds_draw(&s, c);
    if (drawn && memcmp(c, last, sizeof(c)) == 0) return;
    memcpy(last, c, sizeof(c));
    drawn = true;
    strip_show(c);
}

static ui_screen_t screen_shown;

/* What the screen shows, from the state of every part. */
static void show(const matrix_t *m)
{
    static const ui_loop_t LOOP[] = {
        [LOOPER_ARMED] = UI_LOOP_WAITING,
        [LOOPER_RECORDING] = UI_LOOP_RECORDING,
        [LOOPER_PLAYING] = UI_LOOP_PLAYING,
    };
    const rhythm_style_t *beat = rhythm.on ? rhythm_style(rhythm.style) : NULL;
    ui_state_t s;
    memset(&s, 0, sizeof(s));
    s.pc = midi_link_connected();
    s.demo = demo.on;
    s.sound = keymap_program_name(keymap.program);
    s.volume = keymap.volume;
    s.octave = keymap.octave;
    s.beat = beat ? beat->name : NULL;
    s.beat_bpm = beat ? beat->bpm : 0;
    s.metro_bpm = !metro.on ? 0 : beat ? beat->bpm : (int)(60000000LL / metro_beat_us(&metro));
    s.metro_flash = metro_flash(&metro, esp_timer_get_time());
    s.screen = screen_shown;
    if (rhythm.on) {
        const rhythm_style_t *st = rhythm_style(rhythm.style);
        int64_t t = esp_timer_get_time();
        groove_tick(&groove, t);
        s.g_steps = st->steps;
        s.g_steps_per_beat = st->steps_per_beat;
        s.g_step = groove_step(&groove, t);
        for (size_t i = 0; i < 2; i++) {
            s.g_lane[i] = rhythm_lane_mask(st, i);
            s.g_lane_name[i] = s.g_lane[i] ? rhythm_drum_name(st->lane[i].note) : NULL;
        }
        s.g_on_time = groove.on_time;
        s.g_off = groove.off;
        s.g_has_last = groove.has_last;
        s.g_last_ms = groove.last_offset_us / 1000;
    }
    for (size_t r = 0; r < 4; r++) {
        s.keys |= (uint32_t)m->sent[r] << (8 * r);
        s.held |= (uint32_t)(m->stable[r] & ~m->sent[r]) << (8 * r);
    }
    for (size_t k = 0; k < UI_KEYS; k++) {
        if (s.keys & (1ul << k)) s.notes[k] = keymap.key_note[k];
    }
    s.loop = looper.dubbing                ? UI_LOOP_ADDING
             : looper.state != LOOPER_IDLE ? LOOP[looper.state]
             : looper.length_us            ? UI_LOOP_STOPPED
                                           : UI_LOOP_EMPTY;
    s.loop_layers = (int)looper_layers(&looper);
    screen_set(&s);
}

static void on_cell(size_t r, size_t c, bool down, void *ctx)
{
    int64_t now = esp_timer_get_time();
    if (r < 4 && down) groove_press(&groove, now); /* the Groove screen's marks */
    /* What the sequencers send from here (their note offs on a stop) goes after
     * what they already sent ahead, so it can't arrive before a note on. */
    int64_t at = now + MIDI_LINK_LEAD_US;
    switch (keymap_event(&keymap, r, c, down, send_key, NULL)) {
        case KEYMAP_DEMO: demo_toggle(&demo, now, send_at, &at); break;
        case KEYMAP_RECORD: looper_record(&looper, now, send_at, &at); break;
        case KEYMAP_REPLAY: looper_replay(&looper, now, send_at, &at); break;
        case KEYMAP_SELECT: rhythm_toggle(&rhythm, keymap.style, now, send_at, &at); break;
        case KEYMAP_STYLE: rhythm_select(&rhythm, keymap.style); break;
        case KEYMAP_METRO:
            metro_toggle(&metro, at, send_at, &metro.due_us);
            if (metro.on) mixer_send_fx(&mixer, MIXER_CLICK, send_midi, NULL);
            break;
        case KEYMAP_RESET:
            if (demo.on) demo_toggle(&demo, now, send_at, &at);
            if (rhythm.on) rhythm_toggle(&rhythm, keymap.style, now, send_at, &at);
            looper_clear(&looper, send_at, &at);
            break;
        case KEYMAP_NONE: break;
    }
}

void app_main(void)
{
    mcp_init();
    ESP_ERROR_CHECK(encoder_init());
    pots_init();
    esp_err_t strip_err = strip_init();
    if (strip_err != ESP_OK) printf("LED strip: %s\n", esp_err_to_name(strip_err));
    screen_init();
    wifi_connect(WIFI_TIMEOUT_MS);
    midi_link_init("chicco-piano");
    printf("\nesp32-piano key scanner: %u x %u, MIDI over WiFi and the USB cable\n", N_ROWS, N_COLS);
    /* The ROM's own rst: line is at 115200, unreadable at the console's 921600. */
    printf("reset reason: %d (1 power on, 3 software, 4 panic, 9 brownout: esp_reset_reason_t)\n",
           (int)esp_reset_reason());

    matrix_t matrix;
    matrix_init(&matrix);
    keymap_init(&keymap);
    looper_init(&looper);
    groove_init(&groove);
    grid_t now;
    grid_t prev;
    int64_t last_pass = esp_timer_get_time();
    int64_t next_pots = 0;
    int64_t next_leds = 0;
    while (true) {
        int64_t pass = esp_timer_get_time();
        if (pass - last_pass > SCAN_STALL_US) { /* the beat and keys waited this long */
            printf("SCAN  stalled %lld ms\n", (long long)(pass - last_pass) / 1000);
        }
        last_pass = pass;
        scan(now);
        /* The beat's bar lines, for a loop in time with it. */
        if (rhythm.on) {
            const rhythm_style_t *beat = rhythm_style(rhythm.style);
            int64_t step = rhythm_step_us(beat);
            int64_t origin = rhythm.next_us - (int64_t)rhythm.step * step;
            looper_set_grid(&looper, origin, (int64_t)beat->steps * step);
            groove_set_grid(&groove, origin, step, beat->steps);
        } else {
            looper_set_grid(&looper, 0, 0);
            groove_set_grid(&groove, 0, 0, 0);
        }
        if (matrix_feed(&matrix, now, prev)) {
            report(prev, matrix.stable);
            matrix_sync(&matrix, on_cell, NULL);
        }
        knob_event_t knob;
        while (encoder_poll(&knob)) {
            printf("KNOB  %s\n", knob == KNOB_CW ? "CW" : knob == KNOB_CCW ? "CCW" : "PRESS");
            if (knob == KNOB_PRESS) {
                int64_t at = esp_timer_get_time() + MIDI_LINK_LEAD_US;
                looper_undo(&looper, send_at, &at);
            } else {
                int step = knob == KNOB_CW ? 1 : UI_SCREEN_COUNT - 1;
                screen_shown = (ui_screen_t)((screen_shown + step) % UI_SCREEN_COUNT);
            }
        }
        int64_t t = esp_timer_get_time();
        /* The sequencers run MIDI_LINK_LEAD_US ahead; the PC plays each message at
         * its time. The beat sends at most one step a call, due at its next_us;
         * the loop gives each message's time in due_us; DEMO is stamped with the
         * tick's time (off by up to a scan). */
        int64_t ahead = t + MIDI_LINK_LEAD_US;
        int64_t beat_due = rhythm.next_us;
        demo_tick(&demo, ahead, send_at, &ahead);
        looper_tick(&looper, ahead, send_at, &looper.due_us);
        rhythm_tick(&rhythm, ahead, send_beat, &beat_due);
        /* The metronome: on the beat's grid while it plays, else its own at pot 6's tempo. */
        if (rhythm.on) {
            const rhythm_style_t *st = rhythm_style(rhythm.style);
            int64_t step = rhythm_step_us(st);
            metro_tick(&metro, ahead, rhythm.next_us - (int64_t)rhythm.step * step, step * st->steps_per_beat,
                       st->steps / st->steps_per_beat, send_at, &metro.due_us);
        } else {
            metro_tick(&metro, ahead, metro.origin_us, metro_beat_us(&metro), METRO_BEATS, send_at,
                       &metro.due_us);
        }
        if (t >= next_pots) {
            next_pots = t + POT_PERIOD_US;
            pots_read(t);
        }
        /* A layer just added keeps the filter and reverb it was played with. */
        static size_t frozen;
        if (looper.n_layers < frozen) frozen = looper.n_layers; /* undone or erased */
        for (; frozen < looper.n_layers; frozen++) mixer_freeze_layer(&mixer, frozen, send_midi, NULL);
        if (t >= next_leds) {
            next_leds = t + LEDS_PERIOD_US;
            leds_update(t);
        }
        show(&matrix);
        vTaskDelay(pdMS_TO_TICKS(SCAN_PERIOD_MS));
    }
}
