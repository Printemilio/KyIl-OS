#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* Diagnostic-only tracing, compiled into the staged EGL/GLES stubs. */
#if defined(VEX_TRACE_EGL)
#define TRACE_PREFIX "egl"
#define TRACE_DUMP vex_egl_trace_dump
#define TRACE_LABEL "egl"
#else
#define TRACE_PREFIX "gl"
#define TRACE_DUMP vex_gles_trace_dump
#define TRACE_LABEL "gles"
#endif

#define TRACE_SLOTS 2048u
#define TRACE_ORDER 4096u
#define TRACE_DETAIL_LIMIT 2048u

typedef struct {
    const void *address;
    volatile uint64_t calls;
} trace_slot_t;

static trace_slot_t slots[TRACE_SLOTS];
static const void *order[TRACE_ORDER];
static volatile uint64_t sequence;
static volatile uint64_t detail_count;

__attribute__((no_instrument_function))
void __cyg_profile_func_enter(void *this_fn, void *call_site)
{
    uintptr_t h = ((uintptr_t)this_fn >> 4) ^ ((uintptr_t)this_fn >> 13);
    unsigned i;
    uint64_t seq;
    (void)call_site;
    seq = __sync_fetch_and_add(&sequence, 1u);
    if (seq < TRACE_ORDER) order[seq] = this_fn;
    for (i = 0; i < 16u; ++i) {
        trace_slot_t *slot = &slots[(h + i) & (TRACE_SLOTS - 1u)];
        const void *seen = slot->address;
        if (seen == this_fn) {
            __sync_fetch_and_add(&slot->calls, 1u);
            return;
        }
        if (!seen && __sync_bool_compare_and_swap(&slot->address, NULL, this_fn)) {
            __sync_fetch_and_add(&slot->calls, 1u);
            return;
        }
    }
}

__attribute__((no_instrument_function))
void __cyg_profile_func_exit(void *this_fn, void *call_site)
{
    (void)this_fn;
    (void)call_site;
}

__attribute__((no_instrument_function))
void vex_gl_trace_detail(const char *format, ...)
{
    uint64_t n = __sync_fetch_and_add(&detail_count, 1u);
    va_list ap;
    if (n >= TRACE_DETAIL_LIMIT) return;
    va_start(ap, format);
    fprintf(stderr, "[GLTRACE][%s][detail %llu] ", TRACE_LABEL,
            (unsigned long long)n);
    vfprintf(stderr, format, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static int trace_name(const void *address, Dl_info *info)
{
    size_t prefix_length = sizeof(TRACE_PREFIX) - 1u;
    if (!dladdr(address, info) || !info->dli_sname) return 0;
    return strncmp(info->dli_sname, TRACE_PREFIX, prefix_length) == 0;
}

__attribute__((visibility("default"), no_instrument_function))
void TRACE_DUMP(void)
{
    uint64_t i, n = sequence;
    unsigned j;
    fprintf(stderr, "[GLTRACE][%s] summary total-entry-calls=%llu\n",
            TRACE_LABEL, (unsigned long long)n);
    for (j = 0; j < TRACE_SLOTS; ++j) {
        Dl_info info;
        if (!slots[j].address || !slots[j].calls || !trace_name(slots[j].address, &info))
            continue;
        fprintf(stderr, "[GLTRACE][%s] count %s=%llu\n", TRACE_LABEL,
                info.dli_sname, (unsigned long long)slots[j].calls);
    }
    if (n > TRACE_ORDER) n = TRACE_ORDER;
    fprintf(stderr, "[GLTRACE][%s] first-call-order begin (%llu captured)\n",
            TRACE_LABEL, (unsigned long long)n);
    for (i = 0; i < n; ++i) {
        Dl_info info;
        if (order[i] && trace_name(order[i], &info))
            fprintf(stderr, "[GLTRACE][%s] #%llu %s\n", TRACE_LABEL,
                    (unsigned long long)i, info.dli_sname);
    }
    fprintf(stderr, "[GLTRACE][%s] first-call-order end\n", TRACE_LABEL);
}
