#include "stats.h"

#include <stdio.h>

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi.h"

#define STATS_PERIOD_US 1000000  /* the screen's numbers: each covers the last second */
#define STATS_LOG_US    10000000 /* a STATS line this often */

static int64_t s_gap_max, s_work_max, s_work_sum, s_last_start;
static int s_passes;

void stats_scan(int64_t start_us, int64_t work_us)
{
    if (s_last_start && start_us - s_last_start > s_gap_max) s_gap_max = start_us - s_last_start;
    s_last_start = start_us;
    if (work_us > s_work_max) s_work_max = work_us;
    s_work_sum += work_us;
    s_passes++;
}

int stats_update(ui_debug_t *d, int64_t now_us)
{
    static int64_t last_us;
    static int64_t next_log_us;
    static uint32_t last_idle[2];
    if (last_us && now_us - last_us < STATS_PERIOD_US) return 0;
    /* Run-time stats count in esp_timer microseconds (the IDF default clock). */
    for (int core = 0; core < 2; core++) {
        uint32_t idle = ulTaskGetIdleRunTimeCounterForCore(core);
        if (last_us) {
            int64_t busy =
                (now_us - last_us) - (int64_t)(idle - last_idle[core]); /* wraps after ~71 min: unsigned */
            int pct = (int)(busy * 100 / (now_us - last_us));
            d->load[core] = pct < 0 ? 0 : pct > 100 ? 100 : pct;
        }
        last_idle[core] = idle;
    }
    d->heap_kb = (int)(esp_get_free_heap_size() / 1024);
    d->heap_min_kb = (int)(esp_get_minimum_free_heap_size() / 1024);
    wifi_ap_record_t ap;
    d->rssi = wifi_is_connected() && esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
    d->uptime_s = (int)(now_us / 1000000);
    d->scan_gap_ms = (int)(s_gap_max / 1000);
    d->scan_work_us = s_passes ? (int)(s_work_sum / s_passes) : 0;
    d->scan_work_max_us = (int)s_work_max;
    s_gap_max = s_work_max = s_work_sum = 0;
    s_passes = 0;
    if (last_us && now_us >= next_log_us) {
        next_log_us = now_us + STATS_LOG_US;
        printf("STATS core0 %d%% core1 %d%%, heap %d KB (lowest %d), WiFi %d dBm, scan gap %d ms, pass %d us "
               "(worst %d)\n",
               d->load[0], d->load[1], d->heap_kb, d->heap_min_kb, d->rssi, d->scan_gap_ms, d->scan_work_us,
               d->scan_work_max_us);
    }
    last_us = now_us;
    return 1;
}
