/*
 * esp32-piano: line-pair finder (milestone 0/2).
 *
 * The toy's sound chip is removed; its 24 slot pads are labelled on the main
 * board (docs/MATRIX.md). 17 of them are wired to the ESP32 below, VSS to GND.
 * Toy batteries out: the only voltage on the lines is the ESP32's 3.3 V.
 *
 * Every line rests as an input with a pull-up. A scan pulls one line low at a
 * time and reads the others: a line that follows it low is connected to it
 * (a pressed key or button, or something on the main board). A line that reads
 * low with nothing driven is connected to GND/VSS. Lines are never driven high.
 *
 * Output on serial (115200):
 *   REST  <a>-<b>   connections already there at boot, nothing pressed
 *   DOWN  <a>-<b>   a new connection (press)
 *   UP    <a>-<b>   it went away (release)
 * Changes are reported after DEBOUNCE equal scans in a row.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    const char *name;
    gpio_num_t gpio;
    bool input_only; /* GPIO34-39: no output, no internal pull-up (10k external) */
} line_t;

static const line_t LINES[] = {
    {"EF0", GPIO_NUM_4, false},  {"EF1", GPIO_NUM_5, false},  {"EF2", GPIO_NUM_13, false},
    {"EF3", GPIO_NUM_14, false}, {"EF4", GPIO_NUM_16, false}, {"EF5", GPIO_NUM_17, false},
    {"EF6", GPIO_NUM_18, false}, {"EF7", GPIO_NUM_19, false}, {"AB0", GPIO_NUM_21, false},
    {"AB1", GPIO_NUM_22, false}, {"AB2", GPIO_NUM_23, false}, {"AB3", GPIO_NUM_25, false},
    {"AB4", GPIO_NUM_26, false}, {"AB5", GPIO_NUM_27, false}, {"AB6", GPIO_NUM_32, false},
    {"AB7", GPIO_NUM_33, false}, {"CD0", GPIO_NUM_34, true},
};
#define N_LINES (sizeof(LINES) / sizeof(LINES[0]))
#define GND N_LINES /* extra "line": connection to VSS */
#define N_NODES (N_LINES + 1)

#define SETTLE_US 50 /* pull-up (~45k) against the toy's wiring */
#define SCAN_PERIOD_MS 10
#define DEBOUNCE 3

/* conn[i] bit j: node i connected to node j (kept symmetric). */
typedef uint32_t conn_t[N_NODES];

static void line_release(size_t i) {
    gpio_set_direction(LINES[i].gpio, GPIO_MODE_INPUT);
    if (!LINES[i].input_only) {
        gpio_set_pull_mode(LINES[i].gpio, GPIO_PULLUP_ONLY);
    }
}

static void link(conn_t c, size_t a, size_t b) {
    c[a] |= 1u << b;
    c[b] |= 1u << a;
}

static void scan(conn_t c) {
    for (size_t i = 0; i < N_NODES; i++) {
        c[i] = 0;
    }
    /* Nothing driven: anything low is tied to GND. */
    esp_rom_delay_us(SETTLE_US);
    uint32_t at_rest_low = 0;
    for (size_t j = 0; j < N_LINES; j++) {
        if (gpio_get_level(LINES[j].gpio) == 0) {
            at_rest_low |= 1u << j;
            link(c, j, GND);
        }
    }
    for (size_t i = 0; i < N_LINES; i++) {
        if (LINES[i].input_only || (at_rest_low & (1u << i))) {
            continue; /* can't drive it, or it is already low anyway */
        }
        gpio_set_level(LINES[i].gpio, 0);
        gpio_set_direction(LINES[i].gpio, GPIO_MODE_OUTPUT);
        esp_rom_delay_us(SETTLE_US);
        for (size_t j = 0; j < N_LINES; j++) {
            if (j != i && !(at_rest_low & (1u << j)) && gpio_get_level(LINES[j].gpio) == 0) {
                link(c, i, j);
            }
        }
        line_release(i);
    }
}

static const char *node_name(size_t i) {
    return i == GND ? "VSS" : LINES[i].name;
}

static void report(const char *what, const conn_t old_c, const conn_t new_c) {
    for (size_t a = 0; a < N_NODES; a++) {
        for (size_t b = a + 1; b < N_NODES; b++) {
            bool was = old_c[a] & (1u << b);
            bool is = new_c[a] & (1u << b);
            if (is && !was) {
                printf("%s  %s-%s\n", what, node_name(a), node_name(b));
            } else if (was && !is) {
                printf("UP    %s-%s\n", node_name(a), node_name(b));
            }
        }
    }
}

static bool same(const conn_t a, const conn_t b) {
    for (size_t i = 0; i < N_NODES; i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

void app_main(void) {
    for (size_t i = 0; i < N_LINES; i++) {
        gpio_reset_pin(LINES[i].gpio);
        line_release(i);
    }
    printf("\nesp32-piano line-pair finder: %u lines + VSS\n", (unsigned)N_LINES);

    conn_t stable = {0};
    conn_t candidate = {0};
    conn_t now;
    int equal_count = 0;
    bool first = true;

    while (true) {
        scan(now);
        if (same(now, candidate)) {
            equal_count++;
        } else {
            for (size_t i = 0; i < N_NODES; i++) {
                candidate[i] = now[i];
            }
            equal_count = 1;
        }
        if (equal_count == DEBOUNCE && !same(candidate, stable)) {
            report(first ? "REST " : "DOWN ", stable, candidate);
            for (size_t i = 0; i < N_NODES; i++) {
                stable[i] = candidate[i];
            }
        }
        if (equal_count == DEBOUNCE && first) {
            first = false;
            printf("ready: press one key or button at a time\n");
        }
        vTaskDelay(pdMS_TO_TICKS(SCAN_PERIOD_MS));
    }
}
