#pragma once

#include <stddef.h>
#include <stdint.h>

/* Where the pure modules send MIDI: one message (status byte + data bytes)
 * and, for the serial log, what it is (NULL: not logged). len 0 sends
 * nothing and only logs what. */
typedef void (*midi_send_fn)(const uint8_t *msg, size_t len, const char *what, void *ctx);
