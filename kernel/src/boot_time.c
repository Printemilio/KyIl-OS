#include <unos/boot_time.h>
#include <unos/sched_clock.h>

#include <stdint.h>

#define DEFAULT_TSC_MHZ 2500ull

static uint64_t boot_start_tsc;
static uint64_t tsc_mhz;

static uint64_t rdtsc(void) {
    uint32_t low;
    uint32_t high;

    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32u) | low;
}

static void cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile (
        "cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "a"(leaf), "c"(subleaf)
    );
}

static uint64_t detect_tsc_mhz(void) {
    uint32_t max_leaf;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;

    cpuid(0, 0, &max_leaf, &b, &c, &d);

    if (max_leaf >= 0x16u) {
        cpuid(0x16u, 0, &a, &b, &c, &d);
        if (a != 0) {
            return a;
        }
    }

    if (max_leaf >= 0x15u) {
        cpuid(0x15u, 0, &a, &b, &c, &d);
        if (a != 0 && b != 0 && c != 0) {
            return ((uint64_t)c * b) / a / 1000000ull;
        }
    }

    return DEFAULT_TSC_MHZ;
}

void boot_time_init(void) {
    tsc_mhz = detect_tsc_mhz();
    if (tsc_mhz == 0) {
        tsc_mhz = DEFAULT_TSC_MHZ;
    }
    boot_start_tsc = rdtsc();
}

uint64_t boot_time_ms(void) {
    const uint64_t elapsed = rdtsc() - boot_start_tsc;
    uint64_t hz=sched_clock_frequency();
    if(hz)return (elapsed/hz)*1000ull+(elapsed%hz)*1000ull/hz;
    return elapsed / (tsc_mhz * 1000ull);
}
