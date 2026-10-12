/*
 * esp32-piano: key matrix scanner + BLE-MIDI (milestone 3, direction A).
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
 * read as pressed too (ghosting). Such a state is flagged with GHOST?, and a
 * press that may be the fake corner is held back from MIDI until the
 * rectangle is gone.
 *
 * MIDI (BLE, device "Chicco Piano"): keys play notes on channel 1 (key 1 = F3,
 * TEMPO+/- shift by an octave), the 8 style buttons pick an instrument, the
 * animals and chicks play drums on channel 10, VOLUME+/- send CC7, RESET sends
 * all notes off and restores the defaults. SELECT, RECORD, REPLAY, DEMO: none yet.
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

#include "ble_midi.h"
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

#define KEY_CH 0
#define DRUM_CH 9
#define BASE_NOTE 53 /* key 1 = F3: the black keys start with a group of three */
#define VELOCITY 100
#define VOLUME_STEP 16
#define DEFAULT_VOLUME 100

/* General MIDI programs for the style buttons, AB4 EF0-EF7 order. */
static const uint8_t STYLE_PROGRAM[N_COLS] = {
    19, /* SLOW ROCK: church organ */
    30, /* ROCK: distortion guitar */
    88, /* NEW AGE: new age pad */
    81, /* DISCO: saw lead */
    56, /* MARCH: trumpet */
    21, /* WALTZ: accordion */
    12, /* SAMBA: marimba */
    4,  /* BLUES: electric piano */
};
/* General MIDI drum notes for the animals and chicks, AB6/AB7 x EF0-EF7; 0 = none. */
static const uint8_t DRUM_NOTE[2][N_COLS] = {
    /* AB6 chicks */ {75, 38, 51, 49, 39, 42, 54, 56},
    /* AB7 */        {36, 45, 48, 46, 0, 0, 0, 0}, /* dog, frog, red bird, blue bird */
};

#define SETTLE_US 50 /* pull-up (~45k) against the toy's wiring */
#define SCAN_PERIOD_MS 2
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

/* (r, c) is pressed together with the other three corners of some rectangle:
 * it may be the fake one. */
static bool ambiguous(const grid_t g, size_t r, size_t c) {
    for (size_t r2 = 0; r2 < N_ROWS; r2++) {
        if (r2 == r || !(g[r2] & (1u << c))) {
            continue;
        }
        if ((g[r] & g[r2]) & ~(1u << c)) {
            return true;
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

/* ---- MIDI ---- */

static int octave;                    /* -2..+2 */
static int volume = DEFAULT_VOLUME;
static uint8_t key_note[4 * N_COLS];  /* note sent for each held key */

static void midi(uint8_t status, uint8_t d1, uint8_t d2, size_t len, const char *what) {
    uint8_t msg[3] = {status, d1, d2};
    ble_midi_send(msg, len);
    printf("MIDI  %s%s\n", what, ble_midi_connected() ? "" : " (not connected)");
}

static void send_volume(void) {
    char what[32];
    snprintf(what, sizeof(what), "volume %d", volume);
    midi(0xb0 | KEY_CH, 7, volume, 3, what);
    midi(0xb0 | DRUM_CH, 7, volume, 3, what);
}

static void reset_all(void) {
    midi(0xb0 | KEY_CH, 123, 0, 3, "all notes off");
    midi(0xb0 | DRUM_CH, 123, 0, 3, "all notes off (drums)");
    octave = 0;
    volume = DEFAULT_VOLUME;
    send_volume();
    midi(0xc0 | KEY_CH, 0, 0, 2, "program 0");
}

static void cell_event(size_t r, size_t c, bool down) {
    char what[32];
    if (r < 4) {
        size_t key = r * N_COLS + c;
        if (down) {
            key_note[key] = BASE_NOTE + 12 * octave + key;
            snprintf(what, sizeof(what), "note on %u", key_note[key]);
            midi(0x90 | KEY_CH, key_note[key], VELOCITY, 3, what);
        } else {
            snprintf(what, sizeof(what), "note off %u", key_note[key]);
            midi(0x80 | KEY_CH, key_note[key], 0, 3, what);
        }
        return;
    }
    if (r >= 6) {
        uint8_t note = DRUM_NOTE[r - 6][c];
        if (note) {
            snprintf(what, sizeof(what), "drum %s %u", down ? "on" : "off", note);
            midi((down ? 0x90 : 0x80) | DRUM_CH, note, down ? VELOCITY : 0, 3, what);
            return;
        }
    }
    if (!down) {
        return;
    }
    if (r == 4) {
        snprintf(what, sizeof(what), "program %u", STYLE_PROGRAM[c]);
        midi(0xc0 | KEY_CH, STYLE_PROGRAM[c], 0, 2, what);
    } else if (r == 5 && c <= 1) {
        octave += c == 0 ? 1 : -1; /* TEMPO+ / TEMPO- */
        octave = octave > 2 ? 2 : octave < -2 ? -2 : octave;
        printf("MIDI  octave %+d (next notes)\n", octave);
    } else if (r == 5 && c <= 3) {
        volume += c == 2 ? VOLUME_STEP : -VOLUME_STEP; /* VOLUME+ / VOLUME- */
        volume = volume > 127 ? 127 : volume < 0 ? 0 : volume;
        send_volume();
    } else if (r == 5 && c == 4) {
        reset_all();
    }
}

/* Bring what MIDI has heard in line with the debounced grid, holding back
 * presses that may be ghosts. */
static void sync_midi(const grid_t g, grid_t sent) {
    for (size_t r = 0; r < N_ROWS; r++) {
        for (size_t c = 0; c < N_COLS; c++) {
            uint8_t bit = 1u << c;
            bool is = g[r] & bit;
            bool was = sent[r] & bit;
            if (was && !is) {
                sent[r] &= ~bit;
                cell_event(r, c, false);
            } else if (is && !was && !ambiguous(g, r, c)) {
                sent[r] |= bit;
                cell_event(r, c, true);
            }
        }
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
    ble_midi_init("Chicco Piano");
    printf("\nesp32-piano key scanner: %u x %u, BLE-MIDI\n", N_ROWS, N_COLS);

    grid_t stable = {0};
    grid_t candidate = {0};
    grid_t sent = {0};
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
            sync_midi(stable, sent);
        }
        vTaskDelay(pdMS_TO_TICKS(SCAN_PERIOD_MS));
    }
}
