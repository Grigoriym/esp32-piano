#include "encoder.h"

#include "driver/gpio.h"
#include "encoder_decode.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/* From mukklet, without its press gestures: nothing here needs them yet. */
#define ENC_CLK_GPIO    GPIO_NUM_18
#define ENC_DT_GPIO     GPIO_NUM_19
#define ENC_SW_GPIO     GPIO_NUM_23
#define BTN_POLL_MS     10
#define BTN_DEBOUNCE_MS 30

static QueueHandle_t events;
static enc_rotation_t rotation;

static uint8_t IRAM_ATTR pin_state(void)
{
    return (uint8_t)((gpio_get_level(ENC_CLK_GPIO) << 1) | gpio_get_level(ENC_DT_GPIO));
}

/* Rotation is decoded in the pin-change interrupt: polling would miss steps on a quick spin. */
static void IRAM_ATTR encoder_isr(void *arg)
{
    int click = enc_rotation_update(&rotation, pin_state());
    if (click == 0) return;
    knob_event_t ev = click > 0 ? KNOB_CW : KNOB_CCW;
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(events, &ev, &woken);
    portYIELD_FROM_ISR(woken);
}

static void button_task(void *arg)
{
    enc_button_t button;
    enc_button_init(&button);
    while (true) {
        if (enc_button_update(&button, gpio_get_level(ENC_SW_GPIO), BTN_POLL_MS, BTN_DEBOUNCE_MS)) {
            knob_event_t ev = KNOB_PRESS;
            xQueueSend(events, &ev, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_MS));
    }
}

esp_err_t encoder_init(void)
{
    events = xQueueCreate(16, sizeof(knob_event_t));
    if (!events) return ESP_ERR_NO_MEM;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << ENC_CLK_GPIO) | (1ULL << ENC_DT_GPIO) | (1ULL << ENC_SW_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, /* SW's pull-up is often missing on the KY-040 */
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;

    enc_rotation_init(&rotation, pin_state());
    gpio_set_intr_type(ENC_CLK_GPIO, GPIO_INTR_ANYEDGE);
    gpio_set_intr_type(ENC_DT_GPIO, GPIO_INTR_ANYEDGE);
    err = gpio_install_isr_service(0);
    if (err != ESP_OK) return err;
    gpio_isr_handler_add(ENC_CLK_GPIO, encoder_isr, NULL);
    gpio_isr_handler_add(ENC_DT_GPIO, encoder_isr, NULL);

    xTaskCreate(button_task, "knob_button", 2048, NULL, 5, NULL);
    return ESP_OK;
}

bool encoder_poll(knob_event_t *ev)
{
    return events && xQueueReceive(events, ev, 0) == pdTRUE;
}
