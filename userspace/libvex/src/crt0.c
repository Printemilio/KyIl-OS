#include <stdint.h>

#include <unos/vex_legacy_compat.h>

extern int main(int argc, char** argv);

extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);

static void run_init_array(void) {
    for (void (**ctor)(void) = __init_array_start; ctor < __init_array_end; ++ctor) {
        if (*ctor != 0) {
            (*ctor)();
        }
    }
}

void _start(void) {
    run_init_array();
    exit(main(0, (char**)0));
    for (;;) {
    }
}
