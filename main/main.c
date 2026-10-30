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
 * looper.c (RECORD / REPLAY), rhythm.c (SELECT: drum beats).
 * MIDI goes out as UDP over WiFi to tools/piano_bridge.c (main/midi_link.h).
 *
 * Output on serial (115200):
 *   DOWN  <name>   (<EFx>+<ABy>)
 *   UP    <name>
 *   GHOST? <names>  the pressed set contains a rectangle; one of them may be fake
 *   MIDI  <what>   what was sent (or "(not connected)")
 *   KNOB  CW / CCW / PRESS
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
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "keymap.h"
#include "looper.h"
#include "matrix.h"
#include "midi_link.h"
#include "rhythm.h"
#include "screen.h"
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

static keymap_t keymap;
static demo_t demo;
static looper_t looper;
static rhythm_t rhythm;

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

/* What keymap sends: also what the looper records. */
static void send_key(const uint8_t *msg, size_t len, const char *what, void *ctx)
{
    send_midi(msg, len, what, ctx);
    looper_capture(&looper, esp_timer_get_time(), msg, len, keymap.program);
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
    s.screen = screen_shown;
    for (size_t r = 0; r < 4; r++) {
        s.keys |= (uint32_t)m->sent[r] << (8 * r);
        s.held |= (uint32_t)(m->stable[r] & ~m->sent[r]) << (8 * r);
    }
    s.loop = looper.state != LOOPER_IDLE ? LOOP[looper.state]
             : looper.length_us          ? UI_LOOP_STOPPED
                                         : UI_LOOP_EMPTY;
    screen_set(&s);
}

static void on_cell(size_t r, size_t c, bool down, void *ctx)
{
    int64_t now = esp_timer_get_time();
    switch (keymap_event(&keymap, r, c, down, send_key, NULL)) {
        case KEYMAP_DEMO: demo_toggle(&demo, now, send_midi, NULL); break;
        case KEYMAP_RECORD: looper_record(&looper, now, send_midi, NULL); break;
        case KEYMAP_REPLAY: looper_replay(&looper, now, send_midi, NULL); break;
        case KEYMAP_SELECT: rhythm_toggle(&rhythm, keymap.style, now, send_midi, NULL); break;
        case KEYMAP_STYLE: rhythm_select(&rhythm, keymap.style); break;
        case KEYMAP_RESET:
            if (demo.on) demo_toggle(&demo, now, send_midi, NULL);
            if (rhythm.on) rhythm_toggle(&rhythm, keymap.style, now, send_midi, NULL);
            looper_clear(&looper, send_midi, NULL);
            break;
        case KEYMAP_NONE: break;
    }
}

void app_main(void)
{
    mcp_init();
    ESP_ERROR_CHECK(encoder_init());
    screen_init();
    wifi_connect(WIFI_TIMEOUT_MS);
    midi_link_init("chicco-piano");
    printf("\nesp32-piano key scanner: %u x %u, MIDI over WiFi\n", N_ROWS, N_COLS);

    matrix_t matrix;
    matrix_init(&matrix);
    keymap_init(&keymap);
    looper_init(&looper);
    grid_t now;
    grid_t prev;
    while (true) {
        scan(now);
        if (matrix_feed(&matrix, now, prev)) {
            report(prev, matrix.stable);
            matrix_sync(&matrix, on_cell, NULL);
        }
        knob_event_t knob;
        while (encoder_poll(&knob)) {
            printf("KNOB  %s\n", knob == KNOB_CW ? "CW" : knob == KNOB_CCW ? "CCW" : "PRESS");
            if (knob != KNOB_PRESS) {
                int step = knob == KNOB_CW ? 1 : UI_SCREEN_COUNT - 1;
                screen_shown = (ui_screen_t)((screen_shown + step) % UI_SCREEN_COUNT);
            }
        }
        int64_t t = esp_timer_get_time();
        demo_tick(&demo, t, send_midi, NULL);
        looper_tick(&looper, t, send_midi, NULL);
        rhythm_tick(&rhythm, t, send_midi, NULL);
        show(&matrix);
        vTaskDelay(pdMS_TO_TICKS(SCAN_PERIOD_MS));
    }
}
