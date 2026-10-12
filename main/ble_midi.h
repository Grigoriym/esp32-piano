#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* BLE-MIDI peripheral (Apple/MMA BLE-MIDI spec) on NimBLE. */
void ble_midi_init(const char *name);

/* True while a central is connected and subscribed to the MIDI characteristic. */
bool ble_midi_connected(void);

/* Send one MIDI message (status byte + data bytes); dropped when not connected. */
void ble_midi_send(const uint8_t *msg, size_t len);
