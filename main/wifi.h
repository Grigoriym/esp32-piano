#pragma once

#include <stdbool.h>

#include "esp_err.h"

/* WiFi station (credentials in wifi_secrets.h, gitignored; copy
 * wifi_secrets.h.example). Waits up to timeout_ms for an IP and keeps
 * retrying in the background either way, also after a later disconnect.
 * Power save is off: it would hold packets for up to a beacon interval. */
esp_err_t wifi_connect(int timeout_ms);

/* True while the station is connected and has an IP. */
bool wifi_is_connected(void);
