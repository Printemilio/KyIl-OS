#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../libvex/include/vex_wpe_input.h"

int main(void) {
    assert(vex_wpe_button_from_native(1u) == 1u);
    assert(vex_wpe_button_from_native(2u) == 2u);
    assert(vex_wpe_button_from_native(3u) == 3u);
    assert(vex_wpe_button_from_native(0u) == 0u);
    assert(vex_wpe_button_from_native(4u) == 0u);
    puts("Vex-to-WPE pointer button translation PASS");
    return 0;
}
