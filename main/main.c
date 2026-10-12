/*
 * esp32-piano: key matrix scanner (milestone 2).
 *
 * The toy's keys and buttons sit on an 8 x 8 grid of the sound chip's slot
 * lines (docs/MATRIX.md): AB0-AB7 are the rows, EF0-EF7 the columns. Toy
 * batteries out: the only voltage on the lines is the ESP32's 3.3 V.
 *
 * Every line rests as an input with a pull-up. A scan pulls one AB row low at a
 * time and reads the EF columns: a column that follows it low has a pressed
 * key or button at that crossing. Lines are never driven high.
 *
 * No diodes in the toy: three pressed corners of a rectangle make the fourth
 * read as pressed too (ghosting). Such a state is flagged with GHOST?.
 *
 * Output on serial (115200):
 *   DOWN  <name>   (<EFx>+<ABy>)
 *   UP    <name>
 *   GHOST? <names>  the pressed set contains a rectangle; one of them may be fake
 * Changes are reported after DEBOUNCE equal scans in a row.
 *
 * The line-pair finder that mapped the grid is in git history (233c8c3).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define N_ROWS 8
#define N_COLS 8

/* Rows AB0-AB7: pulled low one at a time. */
static const gpio_num_t ROWS[N_ROWS] = {
    GPIO_NUM_21, GPIO_NUM_22, GPIO_NUM_23, GPIO_NUM_25,
    GPIO_NUM_26, GPIO_NUM_27, GPIO_NUM_32, GPIO_NUM_33,
};
/* Columns EF0-EF7: read. */
static const gpio_num_t COLS[N_COLS] = {
    GPIO_NUM_4,  GPIO_NUM_5,  GPIO_NUM_13, GPIO_NUM_14,
    GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18, GPIO_NUM_19,
};
/* CD0 (GPIO34) is wired but carries nothing found so far: not scanned. */

/* NAMES[row][col]; rows AB0-AB3 are the piano keys, key = row * 8 + col + 1. */
static const char *const BUTTONS[4][N_COLS] = {
    /* AB4 */ {"SLOW ROCK", "ROCK", "NEW AGE", "DISCO", "MARCH", "WALTZ", "SAMBA", "BLUES"},
    /* AB5 */ {"TEMPO+", "TEMPO-", "VOLUME+", "VOLUME-", "RESET", "RECORD", "REPLAY", NULL},
    /* AB6 */ {"chick B4", "chick B1", "chick T3", "chick T2", "chick B2", "chick T1", "chick B3", "chick T4"},
    /* AB7 */ {"DOG", "FROG", "RED BIRD", "BLUE BIRD", "SELECT", "DEMO", NULL, NULL},
};
/* chick T1-T4: row next to the style buttons, from the BLUES end; B1-B4: row
 * next to the bottom edge, same direction. */

#define SETTLE_US 50 /* pull-up (~45k) against the toy's wiring */
#define SCAN_PERIOD_MS 10
#define DEBOUNCE 3

typedef uint8_t grid_t[N_ROWS]; /* grid[row] bit col: pressed */

static void cell_name(size_t r, size_t c, char *buf, size_t len) {
    if (r < 4) {
        snprintf(buf, len, "key %u", (unsigned)(r * N_COLS + c + 1));
    } else if (BUTTONS[r - 4][c]) {
        snprintf(buf, len, "%s", BUTTONS[r - 4][c]);
    } else {
        snprintf(buf, len, "?");
    }
}

static void scan(grid_t g) {
    for (size_t r = 0; r < N_ROWS; r++) {
        gpio_set_level(ROWS[r], 0);
        gpio_set_direction(ROWS[r], GPIO_MODE_OUTPUT);
        esp_rom_delay_us(SETTLE_US);
        uint8_t bits = 0;
        for (size_t c = 0; c < N_COLS; c++) {
            if (gpio_get_level(COLS[c]) == 0) {
                bits |= 1u << c;
            }
        }
        g[r] = bits;
        gpio_set_direction(ROWS[r], GPIO_MODE_INPUT);
    }
}

static bool same(const grid_t a, const grid_t b) {
    for (size_t r = 0; r < N_ROWS; r++) {
        if (a[r] != b[r]) {
            return false;
        }
    }
    return true;
}

/* Two rows sharing two or more pressed columns: a rectangle, a possible ghost. */
static bool has_rectangle(const grid_t g) {
    for (size_t a = 0; a < N_ROWS; a++) {
        for (size_t b = a + 1; b < N_ROWS; b++) {
            uint8_t common = g[a] & g[b];
            if (common & (common - 1)) {
                return true;
            }
        }
    }
    return false;
}

static void report(const grid_t old_g, const grid_t new_g) {
    char name[16];
    for (size_t r = 0; r < N_ROWS; r++) {
        for (size_t c = 0; c < N_COLS; c++) {
            bool was = old_g[r] & (1u << c);
            bool is = new_g[r] & (1u << c);
            if (was == is) {
                continue;
            }
            cell_name(r, c, name, sizeof(name));
            if (is) {
                printf("DOWN  %-10s (EF%u+AB%u)\n", name, (unsigned)c, (unsigned)r);
            } else {
                printf("UP    %s\n", name);
            }
        }
    }
    if (has_rectangle(new_g)) {
        printf("GHOST?");
        for (size_t r = 0; r < N_ROWS; r++) {
            for (size_t c = 0; c < N_COLS; c++) {
                if (new_g[r] & (1u << c)) {
                    cell_name(r, c, name, sizeof(name));
                    printf(" [%s]", name);
                }
            }
        }
        printf("\n");
    }
}

void app_main(void) {
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
    printf("\nesp32-piano key scanner: %u x %u\n", N_ROWS, N_COLS);

    grid_t stable = {0};
    grid_t candidate = {0};
    grid_t now;
    int equal_count = 0;

    while (true) {
        scan(now);
        if (same(now, candidate)) {
            equal_count++;
        } else {
            for (size_t r = 0; r < N_ROWS; r++) {
                candidate[r] = now[r];
            }
            equal_count = 1;
        }
        if (equal_count == DEBOUNCE && !same(candidate, stable)) {
            report(stable, candidate);
            for (size_t r = 0; r < N_ROWS; r++) {
                stable[r] = candidate[r];
            }
        }
        vTaskDelay(pdMS_TO_TICKS(SCAN_PERIOD_MS));
    }
}
