#pragma once

#include <stddef.h>
#include <stdint.h>

/* Names what the held keys play. Pure: no ESP-IDF (host-tested). */

/* keys: bit 0 = key 1 (an F, as at any octave setting), bit 31 = key 32. Writes
 * "" with no keys, the note's name for one pitch, the chord's name ("Am", "C7"),
 * or "?" when it's none of the known shapes. Any inversion gives the same name. */
void chord_name(uint32_t keys, char *out, size_t size);
