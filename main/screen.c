#include "screen.h"

#include <stdio.h>
#include <string.h>

#include "display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define POLL_MS 30 /* a change shows within this */

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static ui_state_t s_next; /* written by the scan loop */

static void draw(canvas_t *c, void *ctx)
{
    ui_render(c, ctx);
}

/* Field by field: memcmp would also compare the padding. The names are
 * pointers to keymap's constant strings, so the same name is the same pointer.
 * A new ui_state_t field goes here too, or it shows only when something else
 * changes (2026-11-06: the metronome's header waited for a key press). */
static bool same(const ui_state_t *a, const ui_state_t *b)
{
    return a->pc == b->pc && a->demo == b->demo && a->sound == b->sound && a->volume == b->volume
           && a->octave == b->octave && a->beat == b->beat && a->beat_bpm == b->beat_bpm && a->loop == b->loop
           && a->loop_layers == b->loop_layers && a->screen == b->screen && a->keys == b->keys
           && a->held == b->held && memcmp(a->notes, b->notes, sizeof(a->notes)) == 0
           && a->g_steps == b->g_steps && a->g_steps_per_beat == b->g_steps_per_beat && a->g_step == b->g_step
           && a->g_lane[0] == b->g_lane[0] && a->g_lane[1] == b->g_lane[1]
           && a->g_lane_name[0] == b->g_lane_name[0] && a->g_lane_name[1] == b->g_lane_name[1]
           && a->g_on_time == b->g_on_time && a->g_off == b->g_off && a->g_has_last == b->g_has_last
           && a->g_last_ms == b->g_last_ms && a->metro_on == b->metro_on && a->metro_bpm == b->metro_bpm
           && a->metro_flash == b->metro_flash && a->fx == b->fx;
}

static void screen_task(void *arg)
{
    (void)arg;
    ui_state_t shown;
    bool drawn = false;
    while (true) {
        ui_state_t now;
        taskENTER_CRITICAL(&s_lock);
        now = s_next;
        taskEXIT_CRITICAL(&s_lock);
        if (!drawn || !same(&now, &shown)) {
            esp_err_t err = display_frame(draw, &now);
            if (err != ESP_OK) printf("screen: %s\n", esp_err_to_name(err));
            shown = now;
            drawn = true;
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

void screen_set(const ui_state_t *s)
{
    taskENTER_CRITICAL(&s_lock);
    s_next = *s;
    taskEXIT_CRITICAL(&s_lock);
}

void screen_init(void)
{
    esp_err_t err = display_init();
    if (err != ESP_OK) {
        printf("screen: %s\n", esp_err_to_name(err));
        return;
    }
    /* Core 1: the scan loop runs on core 0, so a frame (~20-30 ms) can't delay a scan. */
    xTaskCreatePinnedToCore(screen_task, "screen", 4096, NULL, 2, NULL, 1);
}
