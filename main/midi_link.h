#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* MIDI over UDP to the PC bridge (tools/piano_bridge.c), protocol:
 *   PC -> toy, port MIDI_LINK_PORT: "HELLO", once a second. The toy sends
 *     to the address and port the last HELLO came from.
 *   toy -> PC: 2-byte sequence number (big endian) + one MIDI message, sent
 *     twice; the PC drops the copy. Covers a lost packet, which as a lost
 *     note off would leave a note hanging.
 * The toy is found as <hostname>.local (mDNS). */
#define MIDI_LINK_PORT 21928

/* Starts mDNS and the HELLO listener; call after WiFi is up (or retrying). */
void midi_link_init(const char *hostname);

/* True while a HELLO came in the last 3 s. */
bool midi_link_connected(void);

/* Send one MIDI message (status byte + data bytes); dropped when not connected. */
void midi_link_send(const uint8_t *msg, size_t len);
