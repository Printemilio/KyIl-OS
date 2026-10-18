#include <unos/profiler.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/debug_log.h>
#include <unos/heap.h>
#include <unos/irq.h>
#include <unos/kernel_guard.h>
#include <unos/process_memory.h>
#include <unos/scheduler.h>
#include <unos/sched_clock.h>
#include <unos/smp.h>

/* Sampling is aggregated in the kernel: a hash table per CPU counts how often
 * each (mode, thread, instruction bucket) was observed, so a long window costs
 * a fixed amount of memory and the report stays a few thousand lines. */
#define PROF_MAX_CPUS 8u
#define PROF_TABLE_SIZE 8192u          /* entries per CPU, power of two */
#define PROF_PROBE_LIMIT 24u
#define PROF_MAX_SECONDS 240u
#define PROF_TOP_PER_CPU 700u
#define PROF_GATE_SLOTS 512u
#define PROF_LAT_BUCKETS 28u           /* log2 of nanoseconds */
#define PROF_MAX_TASKS 768u

enum prof_mode { PROF_IDLE, PROF_USER, PROF_KERNEL, PROF_SPIN, PROF_MODE_COUNT };

typedef struct prof_entry {
    uint64_t rip16;        /* instruction address >> 4 (0 = empty slot) */
    uint64_t holder;       /* SPIN: where the gate was taken by its holder */
    uint64_t gate;         /* SPIN: which gate */
    uint32_t tid;
    uint32_t count;
    uint8_t mode;
} prof_entry_t;

/* One slot per (gate, call site that took it). */
typedef struct prof_gate_stat {
    uint64_t id;                 /* hash of gate and pc; 0 = free */
    uint64_t gate, pc;
    uint64_t count, total_ns, max_ns;
} prof_gate_stat_t;

typedef struct prof_latency {
    uint64_t count, sum_ns, max_ns;
    uint64_t bucket[PROF_LAT_BUCKETS];
} prof_latency_t;

volatile uint8_t profiler_active;

static prof_entry_t *tables[PROF_MAX_CPUS];
static uint32_t dropped[PROF_MAX_CPUS];
static uint32_t samples_taken[PROF_MAX_CPUS];
static uint32_t series[PROF_MAX_SECONDS][PROF_MAX_CPUS][PROF_MODE_COUNT + 1u];
static prof_gate_stat_t gate_stats[PROF_GATE_SLOTS];
static prof_latency_t latencies[PROF_LAT_KIND_COUNT];
static uint64_t window_start_ns;
static uint64_t window_end_ns;
static volatile uint8_t report_due;

static const char *const mode_names[PROF_MODE_COUNT] = { "idle", "user", "kernel", "spin" };
static const char *const latency_names[PROF_LAT_KIND_COUNT] = {
    "gui_tick", "gui_gap", "mouse_queue", "web_present_gap", "present_cost"
};

/* Gates some CPU was seen spinning on. A kernel sample taken on the CPU that holds
 * one of them is tagged with it, which shows what the holder is doing. */
#define PROF_WATCHED_GATES 8u
static uint64_t watched_gates[PROF_WATCHED_GATES];

static void watch_gate(uint64_t gate) {
    for (uint32_t i = 0u; i < PROF_WATCHED_GATES; ++i) {
        uint64_t expected = 0u;

        if (__atomic_compare_exchange_n(&watched_gates[i], &expected, gate, 0,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED) || expected == gate) {
            return;
        }
    }
}

static uint64_t held_watched_gate(uint32_t cpu_index) {
    for (uint32_t i = 0u; i < PROF_WATCHED_GATES && watched_gates[i] != 0u; ++i) {
        const kernel_gate_t *gate = (const kernel_gate_t *)(uintptr_t)watched_gates[i];

        if (gate->depth != 0u && (uint32_t)gate->owner == cpu_index) {
            return watched_gates[i];
        }
    }
    return 0u;
}

/* ── sampling (timer interrupt context) ──────────────────────────────────── */

static void record(uint32_t cpu_index, uint8_t mode, uint32_t tid, uint64_t rip,
                   uint64_t holder, uint64_t gate) {
    prof_entry_t *table = tables[cpu_index];
    const uint64_t key = rip >> 4;
    uint64_t hash = (key * 0x9e3779b97f4a7c15ull) ^ ((uint64_t)tid * 0xc2b2ae3d27d4eb4full) ^
                    ((uint64_t)mode << 57) ^ (gate >> 3);
    uint32_t slot = (uint32_t)(hash >> 40) & (PROF_TABLE_SIZE - 1u);

    for (uint32_t probe = 0u; probe < PROF_PROBE_LIMIT; ++probe) {
        prof_entry_t *entry = &table[(slot + probe) & (PROF_TABLE_SIZE - 1u)];

        if (entry->rip16 == 0u) {
            entry->rip16 = key | (1ull << 63);   /* never zero, even for RIP 0 */
            entry->mode = mode;
            entry->tid = tid;
            entry->holder = holder;
            entry->gate = gate;
            entry->count = 1u;
            return;
        }
        if (entry->rip16 == (key | (1ull << 63)) && entry->mode == mode &&
            entry->tid == tid && entry->gate == gate) {
            ++entry->count;
            return;
        }
    }
    ++dropped[cpu_index];
}

void profiler_sample(const interrupt_frame_t *frame) {
    vex_cpu_t *cpu;
    uint32_t index;
    uint8_t mode;
    uint64_t holder = 0u;
    uint64_t gate = 0u;
    uint64_t now;
    uint32_t second;

    if (!profiler_active || frame == NULL) {
        return;
    }
    cpu = smp_current_cpu();
    index = cpu != NULL ? cpu->index : 0u;
    if (index >= PROF_MAX_CPUS || tables[index] == NULL) {
        return;
    }
    now = sched_clock_ns();
    if (now > window_end_ns) {
        if (index == 0u) {
            report_due = 1u;
        }
        return;
    }
    if (cpu != NULL && cpu->spin_gate != 0u) {
        mode = PROF_SPIN;
        gate = cpu->spin_gate;
        holder = ((const kernel_gate_t *)(uintptr_t)gate)->holder_pc;
        watch_gate(gate);
    } else if (scheduler_cpu_is_idle()) {
        mode = PROF_IDLE;
    } else {
        mode = (frame->cs & 3u) == 3u ? PROF_USER : PROF_KERNEL;
        if (mode == PROF_KERNEL) {
            gate = held_watched_gate(index);   /* this CPU is inside a gate others wait for */
        }
    }
    record(index, mode, scheduler_current_tid(), frame->rip, holder, gate);
    ++samples_taken[index];

    second = (uint32_t)((now - window_start_ns) / 1000000000ull);
    if (second < PROF_MAX_SECONDS) {
        ++series[second][index][mode];
        series[second][index][PROF_MODE_COUNT] += scheduler_cpu_queue_depth();
    }
}

/* ── gate hold times and latencies ───────────────────────────────────────── */

void profiler_gate_release(const void *gate, uint64_t held_ns, uint64_t holder_pc) {
    const uint64_t key = (uint64_t)(uintptr_t)gate;
    const uint64_t id = (((key >> 3) * 0x9e3779b97f4a7c15ull) ^ (holder_pc * 0xc2b2ae3d27d4eb4full)) | 1u;
    const uint32_t slot = (uint32_t)(id >> 40) % PROF_GATE_SLOTS;

    for (uint32_t probe = 0u; probe < PROF_GATE_SLOTS; ++probe) {
        prof_gate_stat_t *stat = &gate_stats[(slot + probe) % PROF_GATE_SLOTS];
        uint64_t expected = 0u;

        if (__atomic_compare_exchange_n(&stat->id, &expected, id, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            stat->gate = key;
            stat->pc = holder_pc;
        } else if (expected != id) {
            continue;
        }
        __atomic_fetch_add(&stat->count, 1u, __ATOMIC_RELAXED);
        __atomic_fetch_add(&stat->total_ns, held_ns, __ATOMIC_RELAXED);
        if (held_ns > __atomic_load_n(&stat->max_ns, __ATOMIC_RELAXED)) {
            __atomic_store_n(&stat->max_ns, held_ns, __ATOMIC_RELAXED);
        }
        return;
    }
}

void profiler_latency(enum profiler_latency_kind kind, uint64_t ns) {
    prof_latency_t *latency;
    unsigned bucket = 0u;

    if (!profiler_active || (unsigned)kind >= PROF_LAT_KIND_COUNT) {
        return;
    }
    latency = &latencies[kind];
    while (bucket + 1u < PROF_LAT_BUCKETS && (ns >> (bucket + 1u)) != 0u) {
        ++bucket;
    }
    __atomic_fetch_add(&latency->count, 1u, __ATOMIC_RELAXED);
    __atomic_fetch_add(&latency->sum_ns, ns, __ATOMIC_RELAXED);
    __atomic_fetch_add(&latency->bucket[bucket], 1u, __ATOMIC_RELAXED);
    if (ns > __atomic_load_n(&latency->max_ns, __ATOMIC_RELAXED)) {
        __atomic_store_n(&latency->max_ns, ns, __ATOMIC_RELAXED);
    }
}

/* One slot per system call (personality and number). */
typedef struct prof_syscall_stat {
    uint32_t key;                /* 0 = free */
    uint64_t count, total_ns, max_ns;
} prof_syscall_stat_t;
#define PROF_SYSCALL_SLOTS 512u
static prof_syscall_stat_t syscall_stats[PROF_SYSCALL_SLOTS];

void profiler_syscall(uint32_t key, uint64_t ns) {
    const uint32_t slot = (key * 2654435761u) % PROF_SYSCALL_SLOTS;

    key |= 0x80000000u;
    for (uint32_t probe = 0u; probe < PROF_SYSCALL_SLOTS; ++probe) {
        prof_syscall_stat_t *stat = &syscall_stats[(slot + probe) % PROF_SYSCALL_SLOTS];
        uint32_t expected = 0u;

        if (!__atomic_compare_exchange_n(&stat->key, &expected, key, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) &&
            expected != key) {
            continue;
        }
        __atomic_fetch_add(&stat->count, 1u, __ATOMIC_RELAXED);
        __atomic_fetch_add(&stat->total_ns, ns, __ATOMIC_RELAXED);
        if (ns > __atomic_load_n(&stat->max_ns, __ATOMIC_RELAXED)) {
            __atomic_store_n(&stat->max_ns, ns, __ATOMIC_RELAXED);
        }
        return;
    }
}

/* ── control ─────────────────────────────────────────────────────────────── */

int profiler_start(uint32_t seconds) {
    uint32_t cpus = smp_cpu_count();

    if (profiler_active || report_due) {
        return -1;
    }
    if (cpus > PROF_MAX_CPUS) {
        cpus = PROF_MAX_CPUS;
    }
    if (seconds == 0u || seconds > PROF_MAX_SECONDS) {
        seconds = PROF_MAX_SECONDS;
    }
    for (uint32_t i = 0u; i < cpus; ++i) {
        if (tables[i] == NULL) {
            tables[i] = kmalloc(sizeof(prof_entry_t) * PROF_TABLE_SIZE);
            if (tables[i] == NULL) {
                return -1;
            }
        }
        memset(tables[i], 0, sizeof(prof_entry_t) * PROF_TABLE_SIZE);
        dropped[i] = 0u;
        samples_taken[i] = 0u;
    }
    memset(series, 0, sizeof(series));
    memset(gate_stats, 0, sizeof(gate_stats));
    memset(watched_gates, 0, sizeof(watched_gates));
    memset(syscall_stats, 0, sizeof(syscall_stats));
    memset(latencies, 0, sizeof(latencies));
    window_start_ns = sched_clock_ns();
    window_end_ns = window_start_ns + (uint64_t)seconds * 1000000000ull;
    __atomic_store_n(&profiler_active, 1u, __ATOMIC_RELEASE);
    return 0;
}

void profiler_poll(void) {
    if (report_due) {
        profiler_report();
    }
}

/* ── report ──────────────────────────────────────────────────────────────── */

/* The kernel's vsnprintf has no 64-bit conversions, so the report formats its own:
 * %u %d %x %s %c, with an optional "ll" before u, d and x. */
static size_t format_line(char *out, size_t size, const char *format, va_list args) {
    size_t n = 0u;

#define EMIT(ch) do { if (n + 1u < size) out[n] = (char)(ch); ++n; } while (0)
    for (; *format != '\0'; ++format) {
        if (*format != '%') {
            EMIT(*format);
            continue;
        }
        ++format;
        int wide = 0;
        if (format[0] == 'l' && format[1] == 'l') {
            wide = 1;
            format += 2;
        }
        if (*format == 's') {
            const char *text = va_arg(args, const char *);
            while (text != NULL && *text != '\0') {
                EMIT(*text++);
            }
        } else if (*format == 'c') {
            EMIT(va_arg(args, int));
        } else if (*format == 'u' || *format == 'd' || *format == 'x') {
            const unsigned base = *format == 'x' ? 16u : 10u;
            uint64_t value = wide ? va_arg(args, unsigned long long) : va_arg(args, unsigned int);
            char digits[20];
            unsigned count = 0u;

            if (*format == 'd' && !wide && (int)value < 0) {
                EMIT('-');
                value = (uint64_t)(-(int64_t)(int)value);
            }
            do {
                digits[count++] = "0123456789abcdef"[value % base];
                value /= base;
            } while (value != 0u);
            while (count != 0u) {
                EMIT(digits[--count]);
            }
        } else {
            EMIT('%');
            EMIT(*format);
        }
    }
#undef EMIT
    if (size != 0u) {
        out[n < size ? n : size - 1u] = '\0';
    }
    return n;
}

static size_t format_to(char *out, size_t size, const char *format, ...) {
    va_list args;

    va_start(args, format);
    const size_t length = format_line(out, size, format, args);
    va_end(args);
    return length;
}

static void put(const char *format, ...) {
    char line[256];
    va_list args;

    va_start(args, format);
    format_line(line, sizeof(line), format, args);
    va_end(args);
    debug_log_puts(line);
}

typedef struct prof_task {
    uint32_t tid, owner;
    uint8_t state;
    char name[SCHEDULER_TASK_NAME_SIZE];
} prof_task_t;

static uint32_t owner_of(const prof_task_t *tasks, uint32_t count, uint32_t tid) {
    for (uint32_t i = 0u; i < count; ++i) {
        if (tasks[i].tid == tid) {
            return tasks[i].owner;
        }
    }
    return 0u;
}

static void report_cpu_samples(uint32_t cpu, const prof_task_t *tasks, uint32_t task_count) {
    prof_entry_t *table = tables[cpu];
    static prof_entry_t top[PROF_TOP_PER_CPU];
    uint32_t top_count = 0u;

    for (uint32_t i = 0u; i < PROF_TABLE_SIZE; ++i) {
        prof_entry_t *entry = &table[i];
        uint32_t position;

        if (entry->rip16 == 0u) {
            continue;
        }
        if (top_count == PROF_TOP_PER_CPU && entry->count <= top[top_count - 1u].count) {
            continue;
        }
        position = top_count < PROF_TOP_PER_CPU ? top_count++ : top_count - 1u;
        while (position > 0u && top[position - 1u].count < entry->count) {
            top[position] = top[position - 1u];
            --position;
        }
        top[position] = *entry;
    }
    for (uint32_t i = 0u; i < top_count; ++i) {
        const prof_entry_t *entry = &top[i];
        const uint64_t rip = (entry->rip16 & ~(1ull << 63)) << 4;
        const uint32_t owner = owner_of(tasks, task_count, entry->tid);

        if (entry->mode == PROF_USER && owner != 0u) {
            uint64_t base = 0u, offset = 0u;
            uint32_t kind = 0u;
            char path[64];

            path[0] = '\0';
            (void)process_memory_describe_address(owner, rip, &base, &kind, &offset, path, sizeof(path));
            put("PROF-SAMPLE cpu=%u mode=user tid=%u pid=%u n=%u rip=%llx base=%llx off=%llx kind=%x path=%s\n",
                cpu, entry->tid, owner, entry->count, (unsigned long long)rip,
                (unsigned long long)base, (unsigned long long)offset, kind, path);
        } else {
            put("PROF-SAMPLE cpu=%u mode=%s tid=%u pid=%u n=%u rip=%llx holder=%llx gate=%llx\n",
                cpu, mode_names[entry->mode], entry->tid, owner, entry->count,
                (unsigned long long)rip, (unsigned long long)entry->holder,
                (unsigned long long)entry->gate);
        }
    }
}

void profiler_report(void) {
    static prof_task_t tasks[PROF_MAX_TASKS];
    uint32_t task_count = 0u;
    uint32_t cpus = smp_cpu_count();
    uint32_t seconds;
    scheduler_process_info_t info;

    __atomic_store_n(&profiler_active, 0u, __ATOMIC_RELEASE);
    report_due = 0u;
    if (cpus > PROF_MAX_CPUS) {
        cpus = PROF_MAX_CPUS;
    }
    seconds = (uint32_t)((window_end_ns - window_start_ns) / 1000000000ull);
    if (seconds > PROF_MAX_SECONDS) {
        seconds = PROF_MAX_SECONDS;
    }

    for (uint64_t i = 0u; task_count < PROF_MAX_TASKS &&
         scheduler_process_info_at(i, &info) == 0; ++i) {
        tasks[task_count].tid = info.pid;
        tasks[task_count].owner = info.process_id;
        tasks[task_count].state = info.state;
        memcpy(tasks[task_count].name, info.name, sizeof(tasks[task_count].name));
        tasks[task_count].name[sizeof(tasks[task_count].name) - 1u] = '\0';
        ++task_count;
    }

    put("PROF-BEGIN seconds=%u cpus=%u hz=1000\n", seconds, cpus);
    for (uint32_t i = 0u; i < task_count; ++i) {
        put("PROF-TASK tid=%u pid=%u state=%u name=%s\n", tasks[i].tid, tasks[i].owner,
            tasks[i].state, tasks[i].name);
    }
    for (uint32_t second = 0u; second < seconds; ++second) {
        for (uint32_t cpu = 0u; cpu < cpus; ++cpu) {
            put("PROF-SERIES sec=%u cpu=%u idle=%u user=%u kernel=%u spin=%u queue=%u\n",
                second, cpu, series[second][cpu][PROF_IDLE], series[second][cpu][PROF_USER],
                series[second][cpu][PROF_KERNEL], series[second][cpu][PROF_SPIN],
                series[second][cpu][PROF_MODE_COUNT]);
        }
    }
    for (uint32_t cpu = 0u; cpu < cpus; ++cpu) {
        put("PROF-CPU cpu=%u samples=%u dropped=%u\n", cpu, samples_taken[cpu], dropped[cpu]);
    }
    for (unsigned kind = 0u; kind < PROF_LAT_KIND_COUNT; ++kind) {
        const prof_latency_t *latency = &latencies[kind];
        char buckets[PROF_LAT_BUCKETS * 12u];
        size_t used = 0u;

        buckets[0] = '\0';
        for (unsigned b = 0u; b < PROF_LAT_BUCKETS; ++b) {
            if (latency->bucket[b] != 0u) {
                used += format_to(buckets + used, sizeof(buckets) - used, "%u:%llu ",
                                  b, (unsigned long long)latency->bucket[b]);
            }
        }
        put("PROF-LAT kind=%s count=%llu sum_ns=%llu max_ns=%llu log2buckets=%s\n",
            latency_names[kind], (unsigned long long)latency->count,
            (unsigned long long)latency->sum_ns, (unsigned long long)latency->max_ns, buckets);
    }
    for (uint32_t i = 0u; i < PROF_GATE_SLOTS; ++i) {
        const prof_gate_stat_t *stat = &gate_stats[i];

        if (stat->id != 0u && stat->count != 0u) {
            put("PROF-GATE gate=%llx count=%llu total_ns=%llu max_ns=%llu holder=%llx\n",
                (unsigned long long)stat->gate, (unsigned long long)stat->count,
                (unsigned long long)stat->total_ns, (unsigned long long)stat->max_ns,
                (unsigned long long)stat->pc);
        }
    }
    for (uint32_t i = 0u; i < PROF_SYSCALL_SLOTS; ++i) {
        const prof_syscall_stat_t *stat = &syscall_stats[i];

        if (stat->key != 0u && stat->count != 0u) {
            put("PROF-SYSCALL key=%x count=%llu total_ns=%llu max_ns=%llu\n", stat->key & 0x7fffffffu,
                (unsigned long long)stat->count, (unsigned long long)stat->total_ns,
                (unsigned long long)stat->max_ns);
        }
    }
    for (uint32_t cpu = 0u; cpu < cpus; ++cpu) {
        report_cpu_samples(cpu, tasks, task_count);
    }
    put("PROF-END\n");
}
