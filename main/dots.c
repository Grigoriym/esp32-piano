#include "dots.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_random.h"
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
#define SETUP_EVERY    250     /* frames: the setup is sent again (a module that missed it gets another go) */
#define SPI_HZ         2000000 /* the MAX7219 takes 10 MHz; slow for 3.3 V data into a 5 V chip */

typedef struct {
    int64_t at_us; /* a note; 0: a beat */
    uint8_t note;
    int8_t beat, beats;
} dots_event_t;

static QueueHandle_t events;
static spi_device_handle_t spi;

/* One register on every module, in one SPI transaction (CS low around it): the last module's
 * word goes out first. The SPI hardware shifts it (2026-11-09: bit-banged with 1 us delays it
 * kept core 1 35-45% busy). */
static void write_all(uint8_t reg, const uint8_t data[MODULES])
{
    uint8_t buf[MODULES * 2];
    for (int m = 0; m < MODULES; m++) {
        buf[2 * (MODULES - 1 - m)] = reg;
        buf[2 * (MODULES - 1 - m) + 1] = data[m];
    }
    spi_transaction_t t = {.length = sizeof(buf) * 8, .tx_buffer = buf};
    spi_device_transmit(spi, &t);
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
                fw_beat(&fw, e.beat, e.beats, now);
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
    /* SPI3 (VSPI): the screen has SPI2. Mode 0: the MAX7219 samples on the rising edge. */
    spi_bus_config_t bus = {.mosi_io_num = DIN,
                            .miso_io_num = -1,
                            .sclk_io_num = CLK,
                            .quadwp_io_num = -1,
                            .quadhd_io_num = -1,
                            .max_transfer_sz = MODULES * 2};
    esp_err_t err = spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK) return err;
    spi_device_interface_config_t dev = {
        .clock_speed_hz = SPI_HZ, .mode = 0, .spics_io_num = CS, .queue_size = 1};
    err = spi_bus_add_device(SPI3_HOST, &dev, &spi);
    if (err != ESP_OK) return err;
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

void dots_beat(int beat, int beats)
{
    if (!events) return;
    dots_event_t e = {.at_us = 0, .beat = (int8_t)beat, .beats = (int8_t)beats};
    xQueueSend(events, &e, 0);
}
