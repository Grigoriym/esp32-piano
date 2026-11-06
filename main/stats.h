/*
 * Debug numbers for the Debug screen and the log (2026-11-06, the user felt the
 * chip warm and wanted to see how busy it is): each core's load from its idle
 * task's run time (FreeRTOS run-time stats), free memory, the WiFi signal, the
 * scan loop's timing. ESP-IDF side, not host-tested.
 */
#pragma once

#include <stdint.h>

#include "ui.h"

/* The scan loop, once a pass: when the pass started and how long its work took. */
void stats_scan(int64_t start_us, int64_t work_us);

/* Every STATS_PERIOD_US: new numbers into d; returns 1 when it measured. Every
 * STATS_LOG_US it also prints a STATS line. */
int stats_update(ui_debug_t *d, int64_t now_us);
