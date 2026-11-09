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
 * pots: the metronome's volume and tempo on ADC1; on ADS1115 #2 beat, live keys, filter, reverb;
 * on #1 resonance, echo, release, wobble), metronome.c
 * (FROG), leds.c (the LED strip), fireworks.c (the 32x8 dot matrix, shown by dots.c).
 * MIDI goes out as UDP over WiFi to tools/piano_bridge.c (main/midi_link.h).
 *
 * Output on serial (115200):
 *   DOWN  <name>   (<EFx>+<ABy>)
 *   UP    <name>
 *   GHOST? <names>  the pressed set contains a rectangle; one of them may be fake
 *   MIDI  <what>   what was sent (or "(not connected)")
 *   KNOB  CW / CCW / PRESS
 *   POT   beat|click|keys|filter|reverb|metro|resonance|echo|release|wobble <level>   a pot turned (0-127)
 * Changes are reported after DEBOUNCE equal scans in a row.
 *
 * The line-pair finder that mapped the grid is in git history (233c8c3).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "demo.h"
#include "dots.h"
#include "driver/i2c_master.h"
#include "encoder.h"
#include "fx.h"
#include "take.h"
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
#include "stats.h"
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

/* Two ADS1115s on the same bus: #1 ADDR open (0x48), #2 ADDR on VDD (0x49). Each converts one input
 * while the next step comes round: single-shot, +-4.096 V (125 uV a count), 860 samples/s (1.2 ms). */
#define ADS_N      2
#define ADS_ADDR   0x48 /* + the board's index */
#define ADS_CONFIG 0x01
#define ADS_CONV   0x00
#define ADS_FULL   25936 /* counts at a pot's top: 3242 mV on the 3V3 rail (measured 2026-11-09) */

#define WIFI_TIMEOUT_MS 10000 /* boot waits this long, then scans anyway */
#define SETTLE_US       50    /* pull-up (100k) against the toy's wiring */
#define SCAN_PERIOD_MS  2
#define SCAN_STALL_US   20000 /* a scan pass this late is logged */
#define POT_PERIOD_US   10000 /* the pots are read this often */
#define POT_READS       8     /* averaged per pot per read (~10 us each) */
#define ADS_PERIOD_US   2000  /* an ADS1115 step: each board moves to its next input (a pot every ~4 steps) */
#define FX_PERIOD_US    30000 /* the filter and reverb are sent at most this often */
#define LEDS_PERIOD_US  20000 /* the strip is redrawn this often (when it changed) */

/* The metronome's two pots on ADC1 (ADC2 doesn't work with WiFi on); the rest on the ADS1115s
 * (2026-11-09: D32, D34, D35 and VP are free since). */
#define ADC_POTS 2
static const struct {
    mixer_pot_t pot;
    adc_channel_t ch;
} ADC_POT[ADC_POTS] = {
    {MIXER_CLICK, ADC_CHANNEL_5}, /* D33 */
    {MIXER_METRO, ADC_CHANNEL_3}, /* VN (GPIO39) */
};
/* Each ADS1115's A0-A3. */
static const mixer_pot_t ADS_POT[ADS_N][4] = {
    {MIXER_RES, MIXER_ECHO, MIXER_RELEASE, MIXER_WOBBLE},
    {MIXER_BEAT, MIXER_KEYS, MIXER_FILTER, MIXER_REVERB},
};
static const char *const POT_NAME[MIXER_POTS] = {"beat",  "click",     "keys", "filter",  "reverb",
                                                 "metro", "resonance", "echo", "release", "wobble"};

static keymap_t keymap;
static demo_t demo;
static looper_t looper;
static rhythm_t rhythm;
static groove_t groove;
static mixer_t mixer;
static metro_t metro;
static fx_t fx;
static take_t take; /* what the chicks play, as the looper records it */
static adc_oneshot_unit_handle_t adc;

static i2c_master_bus_handle_t i2c_bus;
static i2c_master_dev_handle_t mcp;
static i2c_master_dev_handle_t ads[ADS_N]; /* NULL: not found at boot */

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
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus));
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MCP_ADDR,
        .scl_speed_hz = I2C_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &mcp));
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

/* Notes of the keys, DEMO and the loop layers (channels 1-6) go to the dot matrix. */
static void dots_midi(const uint8_t *msg, size_t len, int64_t at_us)
{
    if (len == 3 && (msg[0] & 0xf0) == 0x90 && msg[2] && (msg[0] & 0x0f) < LOOP_CH + LOOP_CHANNELS)
        dots_note(msg[1], at_us);
}

static void send_midi(const uint8_t *msg, size_t len, const char *what, void *ctx)
{
    if (len) {
        midi_link_send(msg, len);
        dots_midi(msg, len, esp_timer_get_time());
    }
    /* The chicks' channels follow the keys' sound, volume, fader, filter, reverb and hold (fx.h). */
    if (len >= 2 && (msg[0] & 0x0f) == KEY_CH && ((msg[0] & 0xf0) == 0xb0 || (msg[0] & 0xf0) == 0xc0)) {
        static const uint8_t COPY[] = {FX_ECHO_CH, FX_WIDE_CH, FX_WIDE2_CH};
        for (size_t i = 0; i < sizeof(COPY); i++) {
            uint8_t m[3] = {(uint8_t)((msg[0] & 0xf0) | COPY[i]), msg[1], len > 2 ? msg[2] : 0};
            midi_link_send(m, len);
        }
    }
    if (what) printf("MIDI  %s%s\n", what, midi_link_connected() ? "" : " (not connected)");
}

/* Sequenced MIDI (beat, DEMO, loop), worked out MIDI_LINK_LEAD_US ahead: ctx
 * points to the time it should sound. */
static void send_at(const uint8_t *msg, size_t len, const char *what, void *ctx)
{
    if (len) {
        midi_link_send_at(msg, len, *(const int64_t *)ctx);
        dots_midi(msg, len, *(const int64_t *)ctx);
    }
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

static void take_out(const uint8_t *msg, size_t len, const char *what, void *ctx)
{
    (void)what;
    (void)ctx;
    looper_capture(&looper, esp_timer_get_time(), msg, len, keymap.program);
}

/* What the chicks' effects send: played, and recorded as the notes they make (take.h). */
static void send_fx(const uint8_t *msg, size_t len, const char *what, void *ctx)
{
    send_midi(msg, len, what, ctx);
    take_msg(&take, msg, len, take_out, NULL);
}

/* What keymap sends: played as is (pot 3 is CC11 on the keys' channel); the keys'
 * notes go through the chicks' effects and are recorded from there. */
static void send_key(const uint8_t *msg, size_t len, const char *what, void *ctx)
{
    int64_t now = esp_timer_get_time();
    if (len == 3 && (msg[0] & 0x0f) == KEY_CH && ((msg[0] & 0xf0) == 0x90 || (msg[0] & 0xf0) == 0x80)) {
        fx_key(&fx, msg, len, now, send_fx, NULL);
        if (what) send_midi(NULL, 0, what, ctx);
        return;
    }
    send_midi(msg, len, what, ctx);
    looper_capture(&looper, now, msg, len, keymap.program); /* drums */
}

static void ads_init(void)
{
    for (int b = 0; b < ADS_N; b++) {
        esp_err_t err = i2c_master_probe(i2c_bus, ADS_ADDR + b, 10);
        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = ADS_ADDR + b, .scl_speed_hz = I2C_HZ};
        if (err == ESP_OK) err = i2c_master_bus_add_device(i2c_bus, &cfg, &ads[b]);
        if (err != ESP_OK) ads[b] = NULL;
        printf("ads1115 at 0x%02x: %s\n", ADS_ADDR + b, esp_err_to_name(err));
    }
}

/* Board b's input converted since its last step, as 0..MIXER_RAW; starts the next. Returns the input, -1: none. */
static int ads_step(int b, int *raw)
{
    static int ch[ADS_N] = {-1, -1}; /* converting */
    static bool failed[ADS_N];
    if (!ads[b]) return -1;
    int got = -1;
    esp_err_t err = ESP_OK;
    if (ch[b] >= 0) {
        uint8_t reg = ADS_CONV;
        uint8_t in[2];
        err = i2c_master_transmit_receive(ads[b], &reg, 1, in, 2, 10);
        if (err == ESP_OK) {
            int32_t v = (int16_t)((in[0] << 8) | in[1]);
            v = v < 0 ? 0 : v * MIXER_RAW / ADS_FULL;
            *raw = v > MIXER_RAW ? MIXER_RAW : (int)v;
            got = ch[b];
        }
    }
    int next = (ch[b] + 1) % 4;
    if (err == ESP_OK) {
        uint16_t c =
            (uint16_t)((1u << 15) | ((4u + (unsigned)next) << 12) | (1u << 9) | (1u << 8) | (7u << 5) | 3u);
        uint8_t w[3] = {ADS_CONFIG, (uint8_t)(c >> 8), (uint8_t)(c & 0xff)};
        err = i2c_master_transmit(ads[b], w, 3, 10);
    }
    if (err != ESP_OK && !failed[b]) printf("ads1115 0x%02x: %s\n", ADS_ADDR + b, esp_err_to_name(err));
    if (err == ESP_OK && failed[b]) printf("ads1115 0x%02x: back\n", ADS_ADDR + b);
    failed[b] = err != ESP_OK;
    ch[b] = failed[b] ? -1 : next;
    return got;
}

static void pots_init(void)
{
    mixer_init(&mixer);
    ads_init();
    adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
    if (adc_oneshot_new_unit(&unit, &adc) != ESP_OK) {
        printf("pots: no ADC\n");
        adc = NULL;
        return;
    }
    adc_oneshot_chan_cfg_t ch = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12};
    for (size_t i = 0; i < ADC_POTS; i++) adc_oneshot_config_channel(adc, ADC_POT[i].ch, &ch);
}

static bool fx_due[MIXER_POTS];

static void pot_reading(size_t i, int raw, int64_t now)
{
    if (!mixer_feed(&mixer, (mixer_pot_t)i, raw, now)) return;
    printf("POT   %s %u\n", POT_NAME[i], (unsigned)mixer.pot[i]);
    if (i != MIXER_BEAT && i != MIXER_METRO && i != MIXER_ECHO) fx_due[i] = true;
    if (i == MIXER_METRO) metro_set_bpm(&metro, metro_bpm_for(mixer.metro));
}

/* The ADS1115 pots: one step of each board. */
static void ads_read(int64_t now)
{
    for (int b = 0; b < ADS_N; b++) {
        int raw;
        int in = ads_step(b, &raw);
        if (in >= 0) pot_reading(ADS_POT[b][in], raw, now);
    }
}

static void pots_read(int64_t now)
{
    for (size_t i = 0; adc && i < ADC_POTS; i++) {
        /* The mean of POT_READS readings: one oneshot read is noisy. */
        int sum = 0;
        int n = 0;
        for (int r = 0; r < POT_READS; r++) {
            int raw;
            if (adc_oneshot_read(adc, ADC_POT[i].ch, &raw) == ESP_OK) {
                sum += raw;
                n++;
            }
        }
        if (n) pot_reading(ADC_POT[i].pot, sum / n, now);
    }
    /* A PC that just said HELLO (the bridge or FluidSynth restarted) gets the effects again. */
    static bool was_connected;
    bool connected = midi_link_connected();
    if (connected && !was_connected) {
        for (size_t i = 0; i < MIXER_POTS; i++)
            fx_due[i] = i != MIXER_BEAT && i != MIXER_METRO && i != MIXER_ECHO;
        if (metro.on) metro_setup(send_midi, NULL);
        fx_setup(send_midi, NULL);
        uint8_t program[2] = {0xc0 | KEY_CH, keymap.program}; /* copied to the chicks' channels */
        send_midi(program, 2, NULL, NULL);
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
        looper_pot(&looper, now, (int)i, mixer.pot[i]); /* a layer being recorded keeps the move */
    }
}

/* The dot matrix's beat: the drum beat's, else the metronome's. */
static void dots_run(int64_t now)
{
    static int last = -1;
    int beat = -1;
    int beats = 0;
    if (rhythm.on) {
        const rhythm_style_t *st = rhythm_style(rhythm.style);
        int step = groove_step(&groove, now);
        if (step >= 0) {
            beat = step / st->steps_per_beat;
            beats = st->steps / st->steps_per_beat;
        }
    } else if (metro.on && now >= metro.origin_us && metro_beat_us(&metro) > 0) {
        beat = (int)((now - metro.origin_us) / metro_beat_us(&metro) % METRO_BEATS);
        beats = METRO_BEATS;
    }
    if (beat >= 0 && beat != last) dots_beat(beat, beats);
    last = beat;
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
        .muted = looper.muted,
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
    if (s.pot >= 0) s.level = mixer.pot[s.pot];
    leds_t c;
    leds_draw(&s, c);
    if (drawn && memcmp(c, last, sizeof(c)) == 0) return;
    memcpy(last, c, sizeof(c));
    drawn = true;
    strip_show(c);
}

static ui_screen_t screen_shown;
static int layer_cursor;       /* the Layers screen's row */
static ui_debug_t debug_stats; /* stats.c, once a second */

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
    s.metro_on = metro.on;
    s.metro_bpm = beat ? beat->bpm : metro.bpm > 0 ? metro.bpm : 120;
    s.metro_flash = metro_flash(&metro, esp_timer_get_time());
    s.screen = screen_shown;
    s.dbg = debug_stats;
    for (size_t k = 0; k < FX_COUNT; k++) {
        if (fx.on[k]) s.fx |= (uint8_t)(1u << k);
    }
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
    if (layer_cursor >= (int)looper.n_layers)
        layer_cursor = looper.n_layers ? (int)looper.n_layers - 1 : 0; /* undone */
    s.layer_cursor = layer_cursor;
    s.layer_muted = looper.muted;
    s.layer_pending = looper.mute_flip;
    for (int i = 0; i < s.loop_layers && i < 4; i++) {
        /* The layer being added takes the keys' sound when its first note comes. */
        s.layer_sound[i] = keymap_program_name(
            i < (int)looper.n_layers || looper.dub_events ? looper.program[i] : keymap.program);
    }
    screen_set(&s);
}

/* Marks for tools/loop_check.py on channel 16 (FluidSynth ignores these CCs): what the
 * looper did, sent at once like the keys, and each pass's start, at its own time. */
#define MARK_STATUS  (0xb0 | 15)
#define MARK_TAKE_CC 118
#define MARK_PASS_CC 119
enum { MARK_ARMED = 1, MARK_CLOSED, MARK_DUB, MARK_DUB_CLOSED, MARK_STOPPED, MARK_DROPPED };

typedef struct {
    looper_state_t state;
    bool dubbing;
    size_t layers;
} loop_snap_t;

static loop_snap_t loop_snap(void)
{
    return (loop_snap_t){looper.state, looper.dubbing, looper.n_layers};
}

static void mark(uint8_t cc, uint8_t value, const int64_t *at)
{
    uint8_t m[3] = {MARK_STATUS, cc, value};
    if (at) {
        int64_t when = *at;
        send_at(m, 3, NULL, &when);
    } else {
        send_midi(m, 3, NULL, NULL);
    }
}

/* A take mark: what happened * 8 + the layers in the loop after it. */
static void take_mark(int what)
{
    mark(MARK_TAKE_CC, (uint8_t)(what * 8 + (int)looper.n_layers), NULL);
}

/* What changed in the looper since was: each step as a mark. */
static void loop_marks(loop_snap_t was)
{
    loop_snap_t now = loop_snap();
    bool taking = was.state == LOOPER_ARMED || was.state == LOOPER_RECORDING;
    if ((was.dubbing && !now.dubbing && now.layers == was.layers) || now.layers < was.layers
        || (taking && now.state == LOOPER_IDLE))
        take_mark(MARK_DROPPED); /* first: RECORD on a stopped loop forgets it, then arms */
    if (now.state == LOOPER_ARMED && was.state != LOOPER_ARMED) take_mark(MARK_ARMED);
    if (taking && now.state == LOOPER_PLAYING) take_mark(MARK_CLOSED);
    if (!was.dubbing && now.dubbing) take_mark(MARK_DUB);
    if (was.dubbing && !now.dubbing && now.layers > was.layers) take_mark(MARK_DUB_CLOSED);
    if (was.state == LOOPER_PLAYING && now.state == LOOPER_IDLE && now.layers) take_mark(MARK_STOPPED);
}

/* A pass that starts now or later gets its mark at its own time (a pass already under
 * way when the loop closed has none: it would come late). */
static void pass_mark(int64_t now, int64_t ahead)
{
    static int64_t marked;
    if (looper.state != LOOPER_PLAYING || looper.start_us == marked || ahead < looper.start_us) return;
    marked = looper.start_us;
    if (marked >= now) mark(MARK_PASS_CC, (uint8_t)looper.n_layers, &marked);
}

static void on_cell(size_t r, size_t c, bool down, void *ctx)
{
    int64_t now = esp_timer_get_time();
    if (r < 4 && down) groove_press(&groove, now); /* the Groove screen's marks */
    /* What the sequencers send from here (their note offs on a stop) goes after
     * what they already sent ahead, so it can't arrive before a note on. */
    int64_t at = now + MIDI_LINK_LEAD_US;
    keymap.beat_on = rhythm.on;
    loop_snap_t was = loop_snap();
    switch (keymap_event(&keymap, r, c, down, send_key, NULL)) {
        case KEYMAP_DEMO: demo_toggle(&demo, now, send_at, &at); break;
        case KEYMAP_RECORD: looper_record(&looper, now, send_at, &at); break;
        case KEYMAP_REPLAY: looper_replay(&looper, now, send_at, &at); break;
        case KEYMAP_SELECT:
            if (!rhythm.on) { /* the last style's kit: a style button doesn't send it while a beat plays */
                uint8_t kit[2] = {0xc0 | DRUM_CH, keymap_kit(keymap.style)};
                send_midi(kit, 2, "drum kit", NULL);
            }
            rhythm_toggle(&rhythm, keymap.style, now, send_at, &at);
            break;
        case KEYMAP_STYLE: break; /* the keys' sound only: a playing beat stays as it is */
        case KEYMAP_UNDO: looper_undo(&looper, send_at, &at); break;
        case KEYMAP_METRO:
            metro_toggle(&metro, at, send_at, &metro.due_us);
            /* On a playing loop: its bars from the loop's start, not from the press. */
            if (metro.on && looper.state == LOOPER_PLAYING) metro_align(&metro, looper.start_us, at);
            if (metro.on) mixer_send_fx(&mixer, MIXER_CLICK, send_midi, NULL);
            break;
        case KEYMAP_FX: fx_button(&fx, (fx_kind_t)keymap.fx, keymap.fx_down, now, send_fx, NULL); break;
        case KEYMAP_RESET:
            fx_reset(&fx, send_fx, NULL);
            take_init(&take);
            if (demo.on) demo_toggle(&demo, now, send_at, &at);
            if (rhythm.on) rhythm_toggle(&rhythm, keymap.style, now, send_at, &at);
            looper_clear(&looper, send_at, &at);
            break;
        case KEYMAP_NONE: break;
    }
    loop_marks(was);
}

/* The chicks' effects: live, sent as it happens like the keys. */
static void fx_run(int64_t now)
{
    fx.filter = mixer.filter;
    fx.echo = mixer.echo;
    fx_tick(&fx, now, send_fx, NULL);
    if (fx.restore) { /* a Rise or Tape ended: the pots' filter again, and each layer's */
        fx.restore = false;
        mixer_send_fx(&mixer, MIXER_FILTER, send_midi, NULL);
        for (size_t l = 0; l < looper.n_layers; l++) mixer_send_layer_fx(&mixer, l, send_midi, NULL);
    }
}

/* The knob: turning walks the screens (and the Layers screen's rows), its press on the
 * Layers screen mutes or brings back a layer. */
static void knob_events(void)
{
    knob_event_t knob;
    while (encoder_poll(&knob)) {
        printf("KNOB  %s\n", knob == KNOB_CW ? "CW" : knob == KNOB_CCW ? "CCW" : "PRESS");
        int layers = (int)looper.n_layers; /* a layer being added can't be muted yet */
        if (knob == KNOB_PRESS) {
            /* The Layers screen: mute or bring back the row's layer (BLUE BIRD undoes since 2026-11-08). */
            if (screen_shown == UI_SCREEN_LAYERS) {
                int64_t at = esp_timer_get_time() + MIDI_LINK_LEAD_US;
                looper_mute(&looper, (size_t)layer_cursor, at, send_at, &at);
            }
        } else {
            ui_knob_turn(&screen_shown, &layer_cursor, knob == KNOB_CW ? 1 : -1, layers);
        }
    }
}

void app_main(void)
{
    mcp_init();
    ESP_ERROR_CHECK(encoder_init());
    pots_init();
    esp_err_t strip_err = strip_init();
    if (strip_err != ESP_OK) printf("LED strip: %s\n", esp_err_to_name(strip_err));
    esp_err_t dots_err = dots_init();
    if (dots_err != ESP_OK) printf("dot matrix: %s\n", esp_err_to_name(dots_err));
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
    fx_init(&fx);
    take_init(&take);
    fx_setup(send_midi, NULL);
    looper_init(&looper);
    groove_init(&groove);
    grid_t now;
    grid_t prev;
    int64_t last_pass = esp_timer_get_time();
    int64_t next_pots = 0;
    int64_t next_ads = 0;
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
            fx_set_grid(&fx, origin, step);
        } else {
            /* No beat: the metronome's bars, if it clicks (the user heard a loop drift off it, 2026-11-06). */
            looper_set_grid(&looper, metro.origin_us, metro.on ? METRO_BEATS * metro_beat_us(&metro) : 0);
            groove_set_grid(&groove, 0, 0, 0);
            fx_set_grid(&fx, metro.origin_us,
                        metro_beat_us(&metro) / 4); /* the metronome's tempo, on or off */
        }
        if (matrix_feed(&matrix, now, prev)) {
            report(prev, matrix.stable);
            matrix_sync(&matrix, on_cell, NULL);
        }
        knob_events();
        int64_t t = esp_timer_get_time();
        /* The sequencers run MIDI_LINK_LEAD_US ahead; the PC plays each message at
         * its time. The beat sends at most one step a call, due at its next_us;
         * the loop gives each message's time in due_us; DEMO is stamped with the
         * tick's time (off by up to a scan). */
        int64_t ahead = t + MIDI_LINK_LEAD_US;
        int64_t beat_due = rhythm.next_us;
        demo_tick(&demo, ahead, send_at, &ahead);
        fx_run(t);
        looper_tick(&looper, ahead, send_at, &looper.due_us);
        pass_mark(t, ahead);
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
        if (t >= next_ads) {
            next_ads = t + ADS_PERIOD_US;
            ads_read(t);
        }
        /* A layer just added keeps the filter and reverb it was played with. */
        static size_t frozen;
        if (looper.n_layers < frozen) frozen = looper.n_layers; /* undone or erased */
        for (; frozen < looper.n_layers; frozen++) mixer_freeze_layer(&mixer, frozen, send_midi, NULL);
        dots_run(t);
        if (t >= next_leds) {
            next_leds = t + LEDS_PERIOD_US;
            leds_update(t);
        }
        stats_update(&debug_stats, t);
        show(&matrix);
        stats_scan(pass, esp_timer_get_time() - pass);
        vTaskDelay(pdMS_TO_TICKS(SCAN_PERIOD_MS));
    }
}
