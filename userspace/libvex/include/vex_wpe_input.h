#ifndef VEX_WPE_INPUT_H
#define VEX_WPE_INPUT_H

#include <stdint.h>

/* Vex's native pointer protocol numbers buttons 1..3 compactly (left,
 * right, middle). The staged WebKit 2.50/WPE event factory consumes the
 * WPE button field as the compact WebKit button ordinal 1..3; passing Linux
 * evdev BTN_LEFT (0x110) reaches its invalid-button branch and prevents DOM
 * click synthesis. Keep the conversion at this ABI boundary. */
static inline uint32_t vex_wpe_button_from_native(uint32_t button) {
    switch (button) {
    case 1u: return 1u;
    case 2u: return 2u;
    case 3u: return 3u;
    default: return 0u;
    }
}

#endif
