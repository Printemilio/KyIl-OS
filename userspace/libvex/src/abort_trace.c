#define _GNU_SOURCE
#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

typedef void (*void_fn)(void);
typedef void (*assert_fn)(const char *, const char *, unsigned, const char *);
static volatile sig_atomic_t trace_active;

static void emit_stack(const char *reason)
{
    void *frames[96];
    int n, i;
    char line[1024];
    long tid = (long)syscall(SYS_gettid);
    int len = snprintf(line, sizeof(line),
        "\n[ABORTTRACE] pid=%ld tid=%ld reason=%s\n", (long)getpid(), tid, reason);
    if (len > 0) (void)write(STDERR_FILENO, line, (size_t)len);
    n = backtrace(frames, (int)(sizeof(frames)/sizeof(frames[0])));
    for (i = 0; i < n; ++i) {
        Dl_info info;
        const char *module = "?", *symbol = "?";
        uintptr_t offset = 0;
        memset(&info, 0, sizeof(info));
        if (dladdr(frames[i], &info)) {
            if (info.dli_fname) module = info.dli_fname;
            if (info.dli_sname) symbol = info.dli_sname;
            if (info.dli_saddr) offset = (uintptr_t)frames[i] - (uintptr_t)info.dli_saddr;
        }
        len = snprintf(line, sizeof(line),
            "[ABORTTRACE] #%02d pc=%p %s!%s+0x%lx base=%p\n", i, frames[i],
            module, symbol, (unsigned long)offset, info.dli_fbase);
        if (len > 0) (void)write(STDERR_FILENO, line, (size_t)len);
    }
    {
        /* libepoxy loads this Vex GLES implementation through libGL.so.1 on
         * this workload; VSL keeps path aliases as distinct ELF instances. */
        void *handle = dlopen("/lib/libGL.so.1", RTLD_NOW | RTLD_LOCAL);
        void_fn dump = handle ? (void_fn)dlsym(handle, "vex_gles_trace_dump") : NULL;
        if (dump) dump();
        handle = dlopen("/lib/libGLESv2.so.2", RTLD_NOW | RTLD_LOCAL);
        dump = handle ? (void_fn)dlsym(handle, "vex_gles_trace_dump") : NULL;
        if (dump) dump();
        handle = dlopen("/lib/libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
        dump = handle ? (void_fn)dlsym(handle, "vex_egl_trace_dump") : NULL;
        if (dump) dump();
    }
}

static void report_once(const char *reason)
{
    if (__sync_bool_compare_and_swap(&trace_active, 0, 1)) emit_stack(reason);
}

__attribute__((noreturn)) void abort(void)
{
    void_fn real_abort;
    report_once("abort()");
    real_abort = (void_fn)dlsym(RTLD_NEXT, "abort");
    if (real_abort) real_abort();
    _exit(134);
}

__attribute__((noreturn)) void __stack_chk_fail(void)
{
    void_fn real_fail;
    report_once("__stack_chk_fail (stack canary mismatch)");
    real_fail = (void_fn)dlsym(RTLD_NEXT, "__stack_chk_fail");
    if (real_fail) real_fail();
    abort();
}

__attribute__((noreturn)) void __assert_fail(const char *assertion,
                                             const char *file,
                                             unsigned int line,
                                             const char *function)
{
    assert_fn real_assert;
    char reason[512];
    (void)snprintf(reason, sizeof(reason), "__assert_fail: %s at %s:%u (%s)",
                   assertion ? assertion : "?", file ? file : "?", line,
                   function ? function : "?");
    report_once(reason);
    real_assert = (assert_fn)dlsym(RTLD_NEXT, "__assert_fail");
    if (real_assert) real_assert(assertion, file, line, function);
    abort();
}
