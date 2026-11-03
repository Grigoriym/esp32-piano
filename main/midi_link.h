#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* MIDI over UDP to the PC bridge (tools/piano_bridge.c), protocol:
 *   PC -> toy, port MIDI_LINK_PORT: "HELLO", once a second. The toy sends
 *     to the address and port the last HELLO came from.
 *   toy -> PC: 2-byte sequence number, 4-byte send time (the toy's clock, us,
 *     wraps), 2-byte delay (100 us units: play this long after the send time;
 *     0 = at once), all big endian, + one MIDI message. Sent twice; the PC
 *     drops the copy. Covers a lost packet, which as a lost note off would
 *     leave a note hanging.
 *   The PC maps the toy's clock to its own from the fastest recent packets and
 *   holds a delayed message until its time: WiFi jitter (15-25 ms seen,
 *   2026-11-03) then doesn't reach the beat, which is sent MIDI_LINK_LEAD_US
 *   early.
 * The toy is found as <hostname>.local (mDNS). */
#define MIDI_LINK_PORT 21928
#define MIDI_LINK_LEAD_US \
    150000 /* how early sequenced MIDI (beat, DEMO, loop) is sent; 60 ms let ~1% arrive late */

/* Starts mDNS and the HELLO listener; call after WiFi is up (or retrying). */
void midi_link_init(const char *hostname);

/* True while a HELLO came in the last 3 s. */
bool midi_link_connected(void);

/* Send one MIDI message (status byte + data bytes) to sound at once; dropped
 * when not connected. */
void midi_link_send(const uint8_t *msg, size_t len);

/* The same, to sound at at_us (esp_timer time); a time already past: at once. */
void midi_link_send_at(const uint8_t *msg, size_t len, int64_t at_us);
