#include "strip.h"

#include <string.h>

#include "driver/rmt_tx.h"

#define STRIP_GPIO GPIO_NUM_17
#define RES_HZ     10000000 /* 0.1 us a tick */

static rmt_channel_handle_t chan;
static rmt_encoder_handle_t enc;
static uint8_t buf[LEDS_N * 3];

esp_err_t strip_init(void)
{
    rmt_tx_channel_config_t cfg = {
        .gpio_num = STRIP_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RES_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 2,
    };
    esp_err_t err = rmt_new_tx_channel(&cfg, &chan);
    /* WS2812: a 0 is 0.3 us high + 0.9 low, a 1 is 0.9 high + 0.3 low, MSB first. */
    rmt_bytes_encoder_config_t bits = {
        .bit0 = {.level0 = 1, .duration0 = 3, .level1 = 0, .duration1 = 9},
        .bit1 = {.level0 = 1, .duration0 = 9, .level1 = 0, .duration1 = 3},
        .flags.msb_first = 1,
    };
    if (err == ESP_OK) err = rmt_new_bytes_encoder(&bits, &enc);
    if (err == ESP_OK) err = rmt_enable(chan);
    return err;
}

void strip_show(const leds_t colours)
{
    if (!chan) return;
    /* Waits for the last frame: the strip latches after 50 us low, and the
     * buffer can't change under a transfer. */
    rmt_tx_wait_all_done(chan, 10);
    for (int i = 0; i < LEDS_N; i++) { /* the strip takes G, R, B */
        buf[i * 3] = colours[i][1];
        buf[i * 3 + 1] = colours[i][0];
        buf[i * 3 + 2] = colours[i][2];
    }
    rmt_transmit_config_t tx = {.loop_count = 0};
    rmt_transmit(chan, enc, buf, sizeof(buf), &tx);
}
