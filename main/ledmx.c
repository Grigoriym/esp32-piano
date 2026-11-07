#include "ledmx.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_timer.h"

#define DS_GPIO   25
#define SH_GPIO   26
#define ST_GPIO   27
#define COLUMN_US 1000 /* 8 columns: the whole frame 125 times a second */
#define FLIP_ROWS 0    /* which way is up and left depends on how the matrix sits: */
#define FLIP_COLS 0    /* row 1 = matrix pin 9, column 1 = pin 13 */

static volatile uint8_t frame[8];
static int column;

static inline void pin(int gpio, int level)
{
    gpio_set_level((gpio_num_t)gpio, (uint32_t)level);
}

static uint8_t flip8(uint8_t b)
{
    uint8_t r = 0;
    for (int i = 0; i < 8; i++) r |= (uint8_t)(((b >> i) & 1) << (7 - i));
    return r;
}

/* 16 bits: the rows byte first (it ends up in chip B), MSB first so bit n lands on Qn. */
static void out(uint8_t rows, uint8_t cols)
{
    uint16_t v = (uint16_t)(rows << 8 | cols);
    pin(ST_GPIO, 0);
    for (int i = 15; i >= 0; i--) {
        pin(DS_GPIO, (v >> i) & 1);
        pin(SH_GPIO, 1);
        pin(SH_GPIO, 0);
    }
    pin(ST_GPIO, 1);
}

static void next_column(void *arg)
{
    (void)arg;
    column = (column + 1) & 7;
    int c = FLIP_COLS ? 7 - column : column;
    uint8_t rows = frame[c];
    if (FLIP_ROWS) rows = flip8(rows);
    out(rows, (uint8_t)~(1u << column));
}

esp_err_t ledmx_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << DS_GPIO | 1ULL << SH_GPIO | 1ULL << ST_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;
    out(0, 0xff);
    esp_timer_handle_t timer;
    esp_timer_create_args_t args = {.callback = next_column, .name = "ledmx"};
    err = esp_timer_create(&args, &timer);
    if (err == ESP_OK) err = esp_timer_start_periodic(timer, COLUMN_US);
    return err;
}

void ledmx_show(const uint8_t cols[8])
{
    for (int c = 0; c < 8; c++) frame[c] = cols[c]; /* a torn frame lasts 8 ms: harmless */
}
