/*
 * esp32-piano: key matrix scanner + MIDI over WiFi (milestone 3, direction A).
 *
 * The toy's keys and buttons sit on an 8 x 8 grid of the sound chip's slot
 * lines (docs/MATRIX.md): AB0-AB7 are the rows, EF0-EF7 the columns. Toy
 * batteries out: the only voltage on the lines is the ESP32's 3.3 V.
 *
 * Every line rests as an input with a pull-up. A scan pulls one AB row low at a
 * time and reads the EF columns: a column that follows it low has a pressed
 * key or button at that crossing. Lines are never driven high.
 *
 * The pure parts, host-tested (tools/test.sh): matrix.c (debounce, ghosts),
 * keymap.c (names, what each key and button plays), demo.c (DEMO button).
 * MIDI goes out as UDP over WiFi to tools/piano_bridge.c (main/midi_link.h).
 *
 * Output on serial (115200):
 *   DOWN  <name>   (<EFx>+<ABy>)
 *   UP    <name>
 *   GHOST? <names>  the pressed set contains a rectangle; one of them may be fake
 *   MIDI  <what>   what was sent (or "(not connected)")
 * Changes are reported after DEBOUNCE equal scans in a row.
 *
 * The line-pair finder that mapped the grid is in git history (233c8c3).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "demo.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "keymap.h"
#include "matrix.h"
#include "midi_link.h"
#include "wifi.h"

/* Rows AB0-AB7: pulled low one at a time. */
static const gpio_num_t ROWS[N_ROWS] = {
    GPIO_NUM_21, GPIO_NUM_22, GPIO_NUM_23, GPIO_NUM_25, GPIO_NUM_26, GPIO_NUM_27, GPIO_NUM_32, GPIO_NUM_33,
};
/* Columns EF0-EF7: read. */
static const gpio_num_t COLS[N_COLS] = {
    GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_13, GPIO_NUM_14, GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18, GPIO_NUM_19,
};
/* CD0 carries no key or button: not wired (GPIO34 is free). */

#define WIFI_TIMEOUT_MS 10000 /* boot waits this long, then scans anyway */
#define SETTLE_US       50    /* pull-up (~45k) against the toy's wiring */
#define SCAN_PERIOD_MS  2

static keymap_t keymap;
static demo_t demo;

static void scan(grid_t g)
{
    for (size_t r = 0; r < N_ROWS; r++) {
        gpio_set_level(ROWS[r], 0);
        gpio_set_direction(ROWS[r], GPIO_MODE_OUTPUT);
        esp_rom_delay_us(SETTLE_US);
        uint8_t bits = 0;
        for (size_t c = 0; c < N_COLS; c++) {
            if (gpio_get_level(COLS[c]) == 0) bits |= 1u << c;
        }
        g[r] = bits;
        gpio_set_direction(ROWS[r], GPIO_MODE_INPUT);
    }
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

static void on_cell(size_t r, size_t c, bool down, void *ctx)
{
    switch (keymap_event(&keymap, r, c, down, send_midi, NULL)) {
        case KEYMAP_DEMO: demo_toggle(&demo, esp_timer_get_time(), send_midi, NULL); break;
        case KEYMAP_RESET:
            if (demo.on) demo_toggle(&demo, esp_timer_get_time(), send_midi, NULL);
            break;
        case KEYMAP_NONE: break;
    }
}

void app_main(void)
{
    for (size_t i = 0; i < N_ROWS; i++) {
        gpio_reset_pin(ROWS[i]);
        gpio_set_direction(ROWS[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(ROWS[i], GPIO_PULLUP_ONLY);
    }
    for (size_t i = 0; i < N_COLS; i++) {
        gpio_reset_pin(COLS[i]);
        gpio_set_direction(COLS[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(COLS[i], GPIO_PULLUP_ONLY);
    }
    wifi_connect(WIFI_TIMEOUT_MS);
    midi_link_init("chicco-piano");
    printf("\nesp32-piano key scanner: %u x %u, MIDI over WiFi\n", N_ROWS, N_COLS);

    matrix_t matrix;
    matrix_init(&matrix);
    keymap_init(&keymap);
    grid_t now;
    grid_t prev;
    while (true) {
        scan(now);
        if (matrix_feed(&matrix, now, prev)) {
            report(prev, matrix.stable);
            matrix_sync(&matrix, on_cell, NULL);
        }
        demo_tick(&demo, esp_timer_get_time(), send_midi, NULL);
        vTaskDelay(pdMS_TO_TICKS(SCAN_PERIOD_MS));
    }
}
