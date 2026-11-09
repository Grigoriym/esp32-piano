#include "dots.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_random.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "fireworks.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define DIN            GPIO_NUM_25
#define CLK            GPIO_NUM_26
#define CS             GPIO_NUM_27
#define MODULES        4
#define BRIGHTNESS     1 /* of 15: 256 LEDs at 15 is ~1 A, too much for USB */
#define DOTS_IN_AT_TOP 0 /* 1: the IN header's end is up */
#define DOTS_MIRROR    0 /* 1: left and right swapped */
#define FRAME_MS       20
#define SETUP_EVERY    250 /* frames: the setup is sent again (a module that missed it gets another go) */

typedef struct {
    int64_t at_us; /* a note; 0: a beat */
    uint8_t note;
    bool bar_line;
} dots_event_t;

static QueueHandle_t events;

/* One register on every module: the last module's word goes out first. */
static void write_all(uint8_t reg, const uint8_t data[MODULES])
{
    gpio_set_level(CS, 0);
    for (int m = MODULES - 1; m >= 0; m--) {
        uint16_t v = (uint16_t)(reg << 8 | data[m]);
        for (int i = 15; i >= 0; i--) {
            gpio_set_level(DIN, (v >> i) & 1);
            esp_rom_delay_us(1);
            gpio_set_level(CLK, 1); /* sampled on the rising edge */
            esp_rom_delay_us(1);
            gpio_set_level(CLK, 0);
        }
    }
    gpio_set_level(CS, 1);
    esp_rom_delay_us(1);
}

static void write_same(uint8_t reg, uint8_t v)
{
    uint8_t d[MODULES];
    memset(d, v, sizeof(d));
    write_all(reg, d);
}

static void setup(void)
{
    write_same(0x0F, 0);          /* display test off */
    write_same(0x09, 0);          /* no BCD decode */
    write_same(0x0B, 7);          /* all 8 digits */
    write_same(0x0A, BRIGHTNESS); /* intensity */
    write_same(0x0C, 1);          /* out of shutdown */
}

/* The upright picture (row y from the top, bit x from the left) onto the modules: along
 * the chain from the IN header (module 1), one LED a step is 0x80 >> c (check 4's walk);
 * across, digit register r + 1. */
static void show(const uint8_t pic[FW_H])
{
    uint8_t fb[MODULES][8] = {{0}};
    for (int y = 0; y < FW_H; y++) {
        int l = DOTS_IN_AT_TOP ? y : FW_H - 1 - y;
        for (int x = 0; x < FW_W; x++) {
            if (!((pic[y] >> x) & 1)) continue;
            int r = DOTS_MIRROR ? FW_W - 1 - x : x;
            fb[l / 8][r] |= (uint8_t)(0x80 >> (l % 8));
        }
    }
    for (int r = 0; r < 8; r++) {
        uint8_t d[MODULES];
        for (int m = 0; m < MODULES; m++) d[m] = fb[m][r];
        write_all((uint8_t)(r + 1), d);
    }
}

static void dots_task(void *arg)
{
    static fw_t fw;
    fw_init(&fw, esp_random());
    uint8_t last[FW_H];
    memset(last, 0xFF, sizeof(last)); /* the first frame always goes out */
    for (int frame = 0;; frame++) {
        if (frame % SETUP_EVERY == 0) setup();
        int64_t now = esp_timer_get_time();
        dots_event_t e;
        while (xQueueReceive(events, &e, 0)) {
            if (e.at_us) {
                fw_note(&fw, e.note, e.at_us);
            } else {
                fw_beat(&fw, e.bar_line, now);
            }
        }
        fw_tick(&fw, now);
        uint8_t pic[FW_H];
        fw_draw(&fw, pic);
        if (memcmp(pic, last, sizeof(pic)) != 0 || frame % SETUP_EVERY == 0) {
            show(pic);
            memcpy(last, pic, sizeof(pic));
        }
        vTaskDelay(pdMS_TO_TICKS(FRAME_MS));
    }
}

esp_err_t dots_init(void)
{
    gpio_config_t io = {.pin_bit_mask = 1ULL << DIN | 1ULL << CLK | 1ULL << CS, .mode = GPIO_MODE_OUTPUT};
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;
    gpio_set_level(CS, 1);
    gpio_set_level(CLK, 0);
    events = xQueueCreate(64, sizeof(dots_event_t));
    if (!events) return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(dots_task, "dots", 4096, NULL, 2, NULL, 1) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

void dots_note(uint8_t note, int64_t at_us)
{
    if (!events) return;
    dots_event_t e = {.at_us = at_us ? at_us : 1, .note = note};
    xQueueSend(events, &e, 0);
}

void dots_beat(bool bar_line)
{
    if (!events) return;
    dots_event_t e = {.at_us = 0, .bar_line = bar_line};
    xQueueSend(events, &e, 0);
}
