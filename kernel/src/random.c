#include <unos/random.h>
#include <unos/debug_log.h>
void kernel_random_fill(void *buffer, size_t length) {
    if (kernel_random_try_fill(buffer,length) != 0) {
        debug_log_puts("rng: entropy unavailable, refusing insecure output\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
}
uint64_t kernel_random_u64(void) {
    uint64_t value;
    kernel_random_fill(&value,sizeof(value));
    return value;
}
