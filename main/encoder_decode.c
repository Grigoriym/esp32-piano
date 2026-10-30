#include "encoder_decode.h"

// Called from the GPIO interrupt, so it has to stay in IRAM on the ESP32.
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define ENC_ISR_CODE IRAM_ATTR
#define ENC_ISR_DATA DRAM_ATTR
#else
#define ENC_ISR_CODE
#define ENC_ISR_DATA
#endif

#define ENC_REST 0x3 // CLK=1 DT=1: where the KY-040 sits between detents

// Quadrature decode, index = (prev_state << 2) | new_state. Valid Gray-code
// steps give +1/-1, bounce and skipped states give 0, so contact bounce
// cancels itself out.
static const ENC_ISR_DATA int8_t ENC_STEP[16] = {
    0, -1, +1, 0, +1, 0, 0, -1, -1, 0, 0, +1, 0, +1, -1, 0,
};

void enc_rotation_init(enc_rotation_t *r, uint8_t pin_state)
{
    r->state = pin_state;
    r->steps = 0;
}

int ENC_ISR_CODE enc_rotation_update(enc_rotation_t *r, uint8_t pin_state)
{
    if (pin_state == r->state) return 0;
    r->steps += ENC_STEP[(r->state << 2) | pin_state];
    r->state = pin_state;
    // 4 steps per detent (confirmed at bring-up): count one click each time
    // it gets back to rest, in whichever direction the steps add up to.
    if (pin_state != ENC_REST || r->steps == 0) return 0;
    int click = r->steps > 0 ? +1 : -1;
    r->steps = 0;
    return click;
}

void enc_button_init(enc_button_t *b)
{
    b->stable = 1;
    b->last_raw = 1;
    b->same_ms = 0;
}

bool enc_button_update(enc_button_t *b, int raw, int poll_ms, int debounce_ms)
{
    if (raw != b->last_raw) {
        b->last_raw = raw;
        b->same_ms = 0;
        return false;
    }
    if (raw == b->stable || (b->same_ms += poll_ms) < debounce_ms) return false;
    b->stable = raw;
    return !b->stable;
}
