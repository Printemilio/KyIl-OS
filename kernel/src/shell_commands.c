#include <unos/shell.h>
#include <unos/kernel_mapping_stats.h>
#include <unos/vmm.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unos/browser_downloads.h>
#include <unos/clipboard.h>
#include <unos/debug_log.h>
#include <unos/disk.h>
#include <unos/dns.h>
#include <unos/dhcp.h>
#include <unos/elf.h>
#include <unos/fd_usage.h>
#include <unos/profiler.h>
#include <unos/ethernet.h>
#include <unos/fs.h>
#include <unos/power.h>
#include <unos/compat.h>
#include <unos/profiler.h>
#include <unos/gpu.h>
#include <unos/gui.h>
#include <unos/heap.h>
#include <unos/inst.h>
#include <unos/ipc.h>
#include <unos/ipv4.h>
#include <unos/netdev.h>
#include <unos/pmm.h>
#include <unos/security.h>
#include <unos/scheduler.h>
#include <unos/smp.h>
#include <unos/spotlight.h>
#include <unos/tls.h>
#include <unos/tty.h>
#include <unos/ui_theme.h>
#include <unos/driver_manager.h>
#include <unos/users.h>
#include <unos/vex_abi.h>
#include <unos/vex_syscall.h>

#include "shell_internal.h"

#define SHELL_PATH_SIZE 128u
#define SHELL_MAX_ARGS 8u
#define SHELL_ARG_SIZE 64u
#define SHELL_RUN_LINE 512u
#define SHELL_RUN_MAX_ARGS 16u
#define SHELL_LOG_BUFFER_SIZE 2048u

static uint8_t shell_ls_show_hidden;
void kernel_mapping_stats_report(int reset) {
    kernel_mapping_stats_t s;
    char line[512];
    vmm_kernel_mapping_stats_snapshot(&s, reset);
    const struct { const char *name; uint64_t value; } fields[] = {
        {"map", s.map_calls}, {"unmap", s.unmap_calls}, {"tasks", s.task_visits},
        {"distinct", s.distinct_spaces}, {"duplicate_cr3", s.duplicate_cr3},
        {"sync", s.sync_calls}, {"checked", s.root_slots_checked},
        {"stores", s.root_stores}, {"changes", s.root_changes},
        {"leaf_map", s.leaf_maps}, {"leaf_unmap", s.leaf_unmaps},
        {"publications", s.root_publications}, {"registered", s.registered_spaces}
    };
    char *cursor = line;
    const char *prefix = reset ? "kmap-stats reset:" : "kmap-stats:";
    while (*prefix) *cursor++ = *prefix++;
    for (size_t i = 0u; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        char digits[20]; size_t count = 0u; uint64_t value = fields[i].value;
        const char *name = fields[i].name;
        *cursor++ = ' ';
        while (*name) *cursor++ = *name++;
        *cursor++ = '=';
        do { digits[count++] = (char)('0' + value % 10u); value /= 10u; } while (value);
        while (count) *cursor++ = digits[--count];
    }
    *cursor++ = '\n'; *cursor = '\0';
    tty_puts(line);
    debug_log_puts(line);
}
static char shell_log_buffer[SHELL_LOG_BUFFER_SIZE];
#define SHELL_TOP_MAX_TASKS 128u
typedef struct shell_top_task_sample {
    uint32_t tid;
    uint32_t process_id;
    uint64_t cpu_ticks;
} shell_top_task_sample_t;
typedef struct shell_top_group_row {
    uint32_t process_id;
    uint32_t parent_pid;
    uint32_t hottest_tid;
    uint32_t thread_count;
    uint64_t cpu_ticks;
    uint64_t hottest_ticks;
    char name[SCHEDULER_TASK_NAME_SIZE];
} shell_top_group_row_t;
static shell_top_task_sample_t shell_top_before[SHELL_TOP_MAX_TASKS];
static shell_top_group_row_t shell_top_groups[SHELL_TOP_MAX_TASKS];

static void halt_forever(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static size_t string_length(const char *str) {
    size_t length = 0;

    while (str[length] != '\0') {
        ++length;
    }

    return length;
}

static int string_equals(const char *left, const char *right) {
    size_t i = 0;

    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) {
            return 0;
        }
        ++i;
    }

    return left[i] == right[i];
}

static int starts_with_word(const char *line, const char *word) {
    const size_t word_len = string_length(word);

    for (size_t i = 0; i < word_len; ++i) {
        if (line[i] != word[i]) {
            return 0;
        }
    }

    return line[word_len] == '\0' || line[word_len] <= ' ';
}

static const char *argument_after_word(const char *line) {
    while (*line != '\0' && *line != ' ') {
        ++line;
    }

    while (*line == ' ') {
        ++line;
    }

    return line;
}

static const char *parse_token(const char *line, char *token, size_t capacity) {
    size_t i = 0;

    while (*line == ' ') {
        ++line;
    }

    while (*line != '\0' && *line != ' ') {
        if (i + 1u < capacity) {
            token[i++] = *line;
        }
        ++line;
    }

    token[i] = '\0';

    while (*line == ' ') {
        ++line;
    }

    return line;
}

static void print_u64(uint64_t value) {
    char digits[21];
    size_t count = 0;

    if (value == 0) {
        tty_putchar('0');
        return;
    }

    while (value > 0 && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (count > 0) {
        tty_putchar(digits[--count]);
    }
}

static int parse_u64(const char *text, uint64_t *out) {
    uint64_t value = 0u;

    if (text == NULL || text[0] == '\0' || out == NULL) {
        return -1;
    }

    for (size_t i = 0; text[i] != '\0'; ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return -1;
        }
        value = value * 10u + (uint64_t)(text[i] - '0');
    }

    *out = value;
    return 0;
}

static void print_kib(uint64_t bytes) {
    print_u64(bytes / 1024u);
    tty_puts(" KiB");
}

static void print_percent(uint64_t numerator, uint64_t denominator) {
    uint64_t whole;
    uint64_t tenths;

    if (denominator == 0u) {
        tty_puts("0.0%");
        return;
    }

    whole = (numerator * 100u) / denominator;
    tenths = ((numerator * 1000u) / denominator) % 10u;
    print_u64(whole);
    tty_putchar('.');
    print_u64(tenths);
    tty_putchar('%');
}

static void print_ipv4(uint32_t ip) {
    print_u64((ip >> 24u) & 0xffu);
    tty_putchar('.');
    print_u64((ip >> 16u) & 0xffu);
    tty_putchar('.');
    print_u64((ip >> 8u) & 0xffu);
    tty_putchar('.');
    print_u64(ip & 0xffu);
}

static void print_hex_nibble(uint8_t value) {
    tty_putchar((char)(value < 10u ? '0' + value : 'a' + (value - 10u)));
}

static void print_mac(const uint8_t *mac) {
    for (size_t i = 0; i < 6u; ++i) {
        if (i != 0) {
            tty_putchar(':');
        }
        print_hex_nibble((uint8_t)(mac[i] >> 4u));
        print_hex_nibble((uint8_t)(mac[i] & 0x0fu));
    }
}

static void print_hex_byte(uint8_t value) {
    print_hex_nibble((uint8_t)(value >> 4u));
    print_hex_nibble((uint8_t)(value & 0x0fu));
}

static void print_hex_u32(uint32_t value) {
    for (int shift = 28; shift >= 0; shift -= 4) {
        print_hex_nibble((uint8_t)((value >> (uint32_t)shift) & 0x0fu));
    }
}

static void append_string(char *dst, size_t capacity, size_t *length, const char *src) {
    while (*src != '\0' && *length + 1u < capacity) {
        dst[*length] = *src;
        ++(*length);
        ++src;
    }

    dst[*length] = '\0';
}

static int path_has_hidden_segment(const char *path) {
    for (size_t i = 0; path[i] != '\0'; ++i) {
        if (path[i] == '.' && (i == 0u || path[i - 1u] == '/')) {
            return 1;
        }
    }

    return 0;
}

static void print_fs_entry(const char *path, size_t size) {
    if (!shell_ls_show_hidden && path_has_hidden_segment(path)) {
        return;
    }

    tty_puts(path);
    tty_puts(" ");
    print_u64(size);
    tty_puts(" bytes\n");
}

static uint8_t parse_acl_perms(const char *text, uint8_t *perms_out) {
    uint8_t perms = 0u;

    if (text == NULL || perms_out == NULL || text[0] == '\0') {
        return 0u;
    }
    for (size_t i = 0u; text[i] != '\0'; ++i) {
        if (text[i] == 'r') {
            perms |= FS_ACL_READ;
        } else if (text[i] == 'w') {
            perms |= FS_ACL_WRITE;
        } else if (text[i] == 'x') {
            perms |= FS_ACL_EXEC;
        } else if (text[i] != '-') {
            return 0u;
        }
    }
    *perms_out = perms;
    return 1u;
}

static void print_acl_perms(uint8_t perms) {
    tty_putchar((perms & FS_ACL_READ) != 0u ? 'r' : '-');
    tty_putchar((perms & FS_ACL_WRITE) != 0u ? 'w' : '-');
    tty_putchar((perms & FS_ACL_EXEC) != 0u ? 'x' : '-');
}

/* help: the terminal paints these SGR colours with the system theme
 * (35 accent, 34 blue, 2 secondary, 90 faint), so the page follows the dark
 * and the light palette. Every name below is a real command of this shell. */
#define HELP_ACCENT "\033[35m"
#define HELP_BLUE "\033[34m"
#define HELP_DIM "\033[2m"
#define HELP_FAINT "\033[90m"
#define HELP_RESET "\033[0m"

static void help_row(const char *command, const char *what) {
    size_t width = 0u;
    tty_puts("    " HELP_BLUE);
    tty_puts(command);
    tty_puts(HELP_RESET);
    while (command[width] != '\0') ++width;
    for (; width < 16u; ++width) tty_putchar(' ');
    tty_puts(HELP_DIM);
    tty_puts(what);
    tty_puts(HELP_RESET "\n");
}

static void help_group(const char *name, const char *commands) {
    size_t width = 0u;
    tty_puts("  " HELP_ACCENT);
    tty_puts(name);
    tty_puts(HELP_RESET);
    while (name[width] != '\0') ++width;
    for (; width < 12u; ++width) tty_putchar(' ');
    tty_puts(commands);
    tty_putchar('\n');
}

static void shell_help(void) {
    tty_puts("\n  " HELP_ACCENT "KyIl-OS" HELP_RESET "  " HELP_DIM "help" HELP_RESET "\n");
    tty_puts("  " HELP_FAINT "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
             "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
             "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
             "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
             "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
             "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
             HELP_RESET "\n\n");

    tty_puts("  " HELP_FAINT "START HERE" HELP_RESET "\n");
    help_row("sysinfo", "uptime, memory and display at a glance");
    help_row("top", "processes and what they cost");
    help_row("ls  cat", "list a directory, print a file");
    help_row("theme light", "the whole system in the light palette");
    help_row("launcher", "every application in one window");
    tty_putchar('\n');

    tty_puts("  " HELP_FAINT "ALL COMMANDS" HELP_RESET "\n");
    help_group("system", "sysinfo  top  ps  kill  log  powerinfo  theme  cls  echo");
    help_group("files", "ls  cat  write  rm  chmod  getfacl  setfacl  sync  spotlight");
    help_group("apps", "launcher  gfm  browser  paint  downloads  run  suchu  inst");
    help_group("network", "ifconfig  netstat  resolve  https  nettest");
    help_group("users", "whoami  login  useradd  secaudit  elevatetest");
    help_group("power", "shutdown  reboot  halt");
    help_group("diagnose", "flowstats  fdstats  kmapstats  kmaptest  diskinfo  stress");
    help_group("", "gputest  vktest  mousetest  guibench  guiprof  runtimetest");
    help_group("", "persisttest  persistcheck  vexfsformat  attacktest  attackkill");
    help_group("", "ipc_write  ipc_read");
    tty_putchar('\n');

    tty_puts("  " HELP_FAINT "\xe2\x86\x91 \xe2\x86\x93 history   \xc2\xb7   Ctrl+L clear   \xc2\xb7   "
             "Ctrl+Shift+C copy   \xc2\xb7   PgUp PgDn scroll" HELP_RESET "\n\n");
}

static void shell_top_report(void) {
    const uint64_t total_pages = pmm_total_page_count();
    const uint64_t used_pages = pmm_used_page_count();
    const uint64_t free_pages = total_pages >= used_pages ? total_pages - used_pages : 0u;
    const uint64_t before_total = scheduler_time_ns();
    const uint64_t before_idle = scheduler_idle_ns_now();
    const uint64_t before_count_raw = scheduler_process_count();
    const size_t before_count = before_count_raw < SHELL_TOP_MAX_TASKS
        ? (size_t)before_count_raw : SHELL_TOP_MAX_TASKS;
    uint64_t after_total, after_idle, sample_ticks, idle_ticks, busy_ticks;
    uint64_t accounted_ticks = 0u;
    uint64_t after_count_raw;
    size_t after_count;
    size_t group_count = 0u;
    int truncated = before_count_raw > SHELL_TOP_MAX_TASKS;

    for (size_t i = 0u; i < before_count; ++i) {
        scheduler_process_info_t info;
        if (scheduler_process_info_at(i, &info) != 0) {
            shell_top_before[i] = (shell_top_task_sample_t){0};
            continue;
        }
        shell_top_before[i] = (shell_top_task_sample_t){
            .tid = info.pid, .process_id = info.process_id, .cpu_ticks = scheduler_thread_cpu_ns(info.pid)
        };
    }

    /* One second at the 100 Hz scheduler timer. Sleeping leaves the WebKit
       workload runnable and makes this a measured interval, not a since-boot
       average. */
    process_sleep_ticks(100u);

    after_total = scheduler_time_ns();
    after_idle = scheduler_idle_ns_now();
    sample_ticks = after_total >= before_total ? after_total - before_total : 0u;
    const uint64_t sample_elapsed=sample_ticks;
    const uint32_t sample_cpus=smp_online_count();
    sample_ticks=sample_cpus && sample_ticks>UINT64_MAX/sample_cpus?UINT64_MAX:sample_ticks*sample_cpus;
    idle_ticks = after_idle >= before_idle ? after_idle - before_idle : 0u;
    busy_ticks = sample_ticks >= idle_ticks ? sample_ticks - idle_ticks : 0u;
    after_count_raw = scheduler_process_count();
    after_count = after_count_raw < SHELL_TOP_MAX_TASKS
        ? (size_t)after_count_raw : SHELL_TOP_MAX_TASKS;
    truncated |= after_count_raw > SHELL_TOP_MAX_TASKS;
    memset(shell_top_groups, 0, sizeof(shell_top_groups));

    for (size_t i = 0u; i < after_count; ++i) {
        scheduler_process_info_t info;
        uint64_t previous_ticks = 0u;
        uint64_t delta;
        size_t group_index;
        if (scheduler_process_info_at(i, &info) != 0 || info.pid == 0u) continue;
        for (size_t j = 0u; j < before_count; ++j) {
            if (shell_top_before[j].tid == info.pid) {
                previous_ticks = shell_top_before[j].cpu_ticks;
                break;
            }
        }
        const uint64_t received_ns=scheduler_thread_cpu_ns(info.pid);
        delta = received_ns >= previous_ticks ? received_ns - previous_ticks : 0u;
        for (group_index = 0u; group_index < group_count; ++group_index) {
            if (shell_top_groups[group_index].process_id == info.process_id) break;
        }
        if (group_index == group_count) {
            if (group_count >= SHELL_TOP_MAX_TASKS) { truncated = 1; continue; }
            shell_top_groups[group_index].process_id = info.process_id;
            shell_top_groups[group_index].parent_pid = info.parent_pid;
            memcpy(shell_top_groups[group_index].name, info.name,
                   sizeof(shell_top_groups[group_index].name));
            ++group_count;
        }
        shell_top_groups[group_index].cpu_ticks += delta;
        ++shell_top_groups[group_index].thread_count;
        if (delta > shell_top_groups[group_index].hottest_ticks) {
            shell_top_groups[group_index].hottest_ticks = delta;
            shell_top_groups[group_index].hottest_tid = info.pid;
        }
    }

    for (size_t i = 0u; i < group_count; ++i) {
        size_t best = i;
        for (size_t j = i + 1u; j < group_count; ++j) {
            if (shell_top_groups[j].cpu_ticks > shell_top_groups[best].cpu_ticks) best = j;
        }
        if (best != i) {
            const shell_top_group_row_t swap = shell_top_groups[i];
            shell_top_groups[i] = shell_top_groups[best];
            shell_top_groups[best] = swap;
        }
        accounted_ticks += shell_top_groups[i].cpu_ticks;
    }

    tty_puts("CPU sample: "); print_u64(sample_elapsed/1000000ull); tty_puts(" ms elapsed; ");
    print_u64(sample_cpus);tty_puts(" CPUs (percent of total capacity)\n");
    tty_puts("busy: "); print_percent(busy_ticks, sample_ticks);
    tty_puts("  idle: "); print_percent(idle_ticks, sample_ticks);
    tty_puts("  unattributed kernel/IRQ: ");
    print_percent(busy_ticks > accounted_ticks ? busy_ticks - accounted_ticks : 0u, sample_ticks);
    tty_puts("\nRAM: "); print_kib(used_pages * 4096u); tty_puts(" / ");
    print_kib(total_pages * 4096u); tty_puts(" used; ");
    print_kib(free_pages * 4096u); tty_puts(" free; tasks: ");
    print_u64(after_count_raw); tty_putchar('\n');
    tty_puts("\nPID PPID THREADS CPU% CPU-MS HOT-TID NAME\n");
    for (size_t i = 0u; i < group_count && i < 10u; ++i) {
        const shell_top_group_row_t *row = &shell_top_groups[i];
        if (row->cpu_ticks == 0u) continue;
        print_u64(row->process_id); tty_putchar(' ');
        print_u64(row->parent_pid); tty_putchar(' ');
        print_u64(row->thread_count); tty_putchar(' ');
        print_percent(row->cpu_ticks, sample_ticks); tty_putchar(' ');
        print_u64(row->cpu_ticks/1000000ull); tty_putchar(' ');
        print_u64(row->hottest_tid); tty_putchar(' ');
        tty_puts(row->name); tty_putchar('\n');
    }
    if (group_count == 0u || accounted_ticks == 0u) {
        tty_puts("No user task accumulated a scheduler tick in this interval.\n");
    }
    if (truncated) tty_puts("top: task list clipped to 128 live threads\n");
}

static const char *download_status_name(uint32_t status) {
    switch (status) {
    case VEX_DOWNLOAD_RUNNING: return "running";
    case VEX_DOWNLOAD_FINISHED: return "finished";
    case VEX_DOWNLOAD_FAILED: return "failed";
    case VEX_DOWNLOAD_CANCELLED: return "cancelled";
    default: return "unknown";
    }
}

static void print_downloads(void) {
    static vex_download_state_t list[VEX_BROWSER_MAX_DOWNLOADS];
    const unsigned count = browser_downloads_snapshot(list, VEX_BROWSER_MAX_DOWNLOADS);

    if (count == 0u) {
        tty_puts("no downloads in this browser session\n");
        return;
    }
    tty_puts("ID  STATE      BYTES (received/total)  FILE\n");
    for (unsigned i = 0u; i < count; ++i) {
        const vex_download_state_t *download = &list[i];

        print_u64(download->id);
        tty_puts("   ");
        tty_puts(download_status_name(download->status));
        tty_puts("   ");
        print_u64(download->received_bytes);
        tty_putchar('/');
        if (download->total_bytes == VEX_DOWNLOAD_SIZE_UNKNOWN) {
            tty_putchar('?');
        } else {
            print_u64(download->total_bytes);
        }
        tty_puts("   ");
        /* Only a finished download has a file at its destination. */
        tty_puts(download->status == VEX_DOWNLOAD_FINISHED ? download->destination : download->name);
        if (download->error[0] != '\0') {
            tty_puts("  (");
            tty_puts(download->error);
            tty_putchar(')');
        }
        tty_putchar('\n');
    }
}

static void shell_downloads_command(const char *argument) {
    char verb[SHELL_ARG_SIZE];
    char id_text[SHELL_ARG_SIZE];
    uint64_t id;

    const char *rest = parse_token(argument, verb, sizeof(verb));
    if (verb[0] == '\0') {
        print_downloads();
        return;
    }
    if (string_equals(verb, "cancel")) {
        (void)parse_token(rest, id_text, sizeof(id_text));
        if (parse_u64(id_text, &id) != 0 || id == 0u || id > 0xffffffffu) {
            tty_puts("usage: downloads cancel <id>\n");
            return;
        }
        if (browser_downloads_request_cancel((uint32_t)id) != 0) {
            tty_puts("no such running download\n");
            return;
        }
        tty_puts("cancel requested\n");
        return;
    }
    tty_puts("usage: downloads [cancel <id>]\n");
}

static void print_fd_usage(void) {
    static vex_fd_usage_t usage[64];
    const unsigned count = vex_fd_usage_snapshot(usage, 64u);

    tty_puts("PID OPEN-FDS\n");
    for (unsigned i = 0u; i < count; ++i) {
        print_u64(usage[i].pid);
        tty_puts("   ");
        print_u64(usage[i].open_descriptors);
        tty_putchar('\n');
    }
}

static int shell_launch_desktop_app(const char *path) {
    uint32_t caps=string_equals(path,"/usr/bin/kyil-launcher")?VEX_CAP_ALL:0;
    if(!scheduler_current_has_capability(caps))return -1;
    uint32_t pid=0;if(elf_spawn_pid_caps(path,&pid,caps)!=0 || !pid)return -1;
    scheduler_detach_process(pid);return 0;
}
void shell_execute_command(const char *line) {
    ora_view_t fs_lease_0 __attribute__((cleanup(ora_view_release)))={0};
    while (line[0] != '\0' && line[0] <= ' ') {
        ++line;
    }

    if (line[0] == '\0') {
        return;
    }
    if(string_equals(line,"shutdown") || string_equals(line,"reboot")) {
        if(power_request(string_equals(line,"shutdown")?POWER_OFF:POWER_REBOOT))
            tty_puts("power: denied, unavailable or already in progress\n");
        return;
    }
    if(string_equals(line,"powerinfo")){power_info();return;}
    if(string_equals(line,"guibench")){gui_performance_benchmark();return;}
    if(starts_with_word(line,"vexfsformat")) {
        char part[16],confirmation[16];uint64_t index;
        const char *rest=parse_token(argument_after_word(line),part,sizeof(part));
        parse_token(rest,confirmation,sizeof(confirmation));
        if(parse_u64(part,&index) || index<1 || index>4 || !string_equals(confirmation,"CONFIRM")) {
            tty_puts("DESTRUCTIVE: vexfsformat <MBR partition 1..4> CONFIRM. Only a dedicated type 0xDA partition; existing VexFS refused.\n");return;
        }
        tty_puts(fs_format_partition((unsigned)index)?"format refused/failed; no mount change\n":"partition formatted; reboot to mount. Current RAM files are not copied.\n");return;
    }
    if(string_equals(line,"persisttest") || string_equals(line,"persistcheck")) {
        fs_storage_info_t storage;
        if(fs_get_storage_info(&storage) || !storage.disk_backed){tty_puts("persistence test REFUSED: no durable native volume mounted\n");return;}
        unsigned char *data=kmalloc(65536);int failed=data==NULL;
        if(!failed && string_equals(line,"persisttest")) {
            for(unsigned i=0;i<65536;i++)data[i]=(unsigned char)(i*37u+11u);
            failed=fs_write("/home/root/.persist-test.bin",data,65536)!=0 || fs_sync()!=0;
        }
        ora_view_t view={0};void *read_data=NULL;size_t size=0;
        if(!failed)failed=fs_read_view("/home/root/.persist-test.bin",&read_data,&size,&view)!=0 || size!=65536;
        if(!failed)for(unsigned i=0;i<65536;i++)if(((unsigned char *)read_data)[i]!=(unsigned char)(i*37u+11u)){failed=1;break;}
        ora_view_release(&view);kfree(data);
        tty_puts(failed?"persistence payload FAILED\n":"persistence payload PASS (65536 real bytes verified)\n");
        debug_log_puts(failed?"persistence: FAILED\n":"persistence: PASS 65536 bytes\n");return;
    }
    if(string_equals(line,"guiprof")){if(profiler_start(15))tty_puts("guiprof: already running\n");else tty_puts("guiprof: 15s real GUI profile; move/resize windows\n");return;}
    if(string_equals(line,"runtimetest")) {
        static const char *dirs[]={"/tmp/d","/tmp/d/wpe","/tmp/d/wpe/storage","/tmp/s","/home/root/.config","/tmp/c","/tmp/c/fontconfig","/tmp/c/wpe"};
        unsigned failed=0;
        for(unsigned i=0;i<sizeof(dirs)/sizeof(dirs[0]);i++) {
            struct stat metadata;(void)mkdir(dirs[i],0700);
            if(stat(dirs[i],&metadata) || (metadata.st_mode&0170000)!=0040000){++failed;tty_puts("runtime directory failed: ");tty_puts(dirs[i]);tty_putchar('\n');}
        }
        tty_puts(failed?"runtime directory test FAILED\n":"runtime directory test PASS (real writable directories)\n");
        debug_log_puts(failed?"runtime-directory: FAILED\n":"runtime-directory: PASS\n");return;
    }
    if(string_equals(line,"sync")){tty_puts(vex_fd_sync_all() || fs_sync()?"sync: failed\n":"sync: completed (RAM-only data remains volatile)\n");return;}
    if(starts_with_word(line,"driver")) {driver_manager_command(argument_after_word(line));return;}

    if (string_equals(line, "cls")) {
        tty_clear();
        return;
    }

    if (string_equals(line, "help")) {
        shell_help();
        return;
    }

    if (string_equals(line, "sysinfo")) {
        vex_bulk_result_t bulk;
        vex_bulk_req_t req;

        req.mask = VEX_BULK_TIME | VEX_BULK_CLOCK | VEX_BULK_MEMORY | VEX_BULK_SCHEDULER | VEX_BULK_SYSTEM;
        req.result = &bulk;
        if (vex_syscall_dispatch(VEX_SYS_BULK, (uint64_t)(uintptr_t)&req, 0, 0) != 0u) {
            tty_puts("sysinfo: vex_bulk failed\n");
            return;
        }

        tty_puts("KyIlOS system\n");
        tty_puts("uptime: ");
        print_u64(bulk.monotonic_ms / 1000u);
        tty_puts("s (");
        print_u64(bulk.scheduler_ticks);
        tty_puts(" ticks)\n");
        tty_puts("resolution: ");
        print_u64(gui_width());
        tty_putchar('x');
        print_u64(gui_height());
        tty_putchar('\n');
        tty_puts("processes: ");
        print_u64(bulk.process_count);
        tty_putchar('\n');
        tty_puts("memory total: ");
        print_kib(bulk.total_pages * 4096u);
        tty_putchar('\n');
        tty_puts("memory used:  ");
        print_kib(bulk.used_pages * 4096u);
        tty_putchar('\n');
        tty_puts("memory free:  ");
        print_kib(bulk.free_pages * 4096u);
        tty_putchar('\n');
        return;
    }

    if (string_equals(line, "top")) {
        shell_top_report();
        return;
    }
    /* theme [dark|light|toggle]: the system palette (also on the desktop
       menu). Applications follow through /etc/kyil/theme.conf. */
    if (string_equals(line, "theme") || string_equals(line, "theme dark") ||
        string_equals(line, "theme light") || string_equals(line, "theme toggle")) {
        int mode = ui_theme_mode();
        if (string_equals(line, "theme dark")) mode = UI_THEME_DARK;
        else if (string_equals(line, "theme light")) mode = UI_THEME_LIGHT;
        else if (string_equals(line, "theme toggle")) mode = mode == UI_THEME_DARK ? UI_THEME_LIGHT : UI_THEME_DARK;
        if (!string_equals(line, "theme")) (void)gui_set_theme(mode);
        tty_puts("theme: ");
        tty_puts(ui_theme()->name);
        tty_puts(" (theme dark | theme light | theme toggle)\n");
        return;
    }
    if (string_equals(line, "flowstats")) {
        scheduler_flow_report();scheduler_dump_cpu_distribution(0u);
        tty_puts("FlowSched counters written to serial log\n");return;
    }
    if (string_equals(line, "kmapstats") || string_equals(line, "kmapstats reset")) {
        kernel_mapping_stats_report(string_equals(line, "kmapstats reset"));
        return;
    }
    if (string_equals(line, "kmaptest")) {
        int result = vmm_kernel_mapping_selftest();
        const char *message = result == 0
            ? "kmap-test: PASS (6 threads, 2 CR3s, late root, shared kernel tree, NX/USER, unmap, 32 teardown cycles)\n"
            : "kmap-test: FAILED\n";
        tty_puts(message); debug_log_puts(message);
        return;
    }

#if VEX_BROWSER_DIAGNOSTIC
    if (string_equals(line, "browsertrace")) {
        extern void compat_linux_debug_tasks(void);
        compat_linux_debug_tasks();tty_puts("Browser task snapshot written to serial log.\n");return;
    }
#endif
    if (starts_with_word(line, "prof")) {
        char seconds_text[SHELL_ARG_SIZE];
        uint64_t seconds = 30u;

        (void)parse_token(argument_after_word(line), seconds_text, sizeof(seconds_text));
        if (seconds_text[0] != '\0' && (parse_u64(seconds_text, &seconds) != 0 || seconds == 0u)) {
            tty_puts("usage: prof [seconds]\n");
            return;
        }
        if (profiler_start((uint32_t)seconds) != 0) {
            tty_puts("profiler busy or out of memory\n");
            return;
        }
        tty_puts("profiling; the report goes to the serial log when the window ends\n");
        return;
    }
    if (string_equals(line, "fdstats")) {
        print_fd_usage();
        return;
    }
    if (starts_with_word(line, "downloads")) {
        shell_downloads_command(argument_after_word(line));
        return;
    }
    if (string_equals(line, "ps")) {
        scheduler_process_info_t info;
        const uint64_t count = scheduler_process_count();

        tty_puts("PID PPID STATE     PRI USER UID ADMIN CAPS SLEEP\n");
        for (uint64_t i = 0; i < count; ++i) {
            if (scheduler_process_info_at(i, &info) != 0) {
                continue;
            }
            print_u64(info.pid);
            tty_putchar(' ');
            print_u64(info.parent_pid);
            tty_putchar(' ');
            tty_puts(scheduler_state_name_value(info.state));
            tty_putchar(' ');
            print_u64(info.priority);
            tty_putchar(' ');
            tty_puts(info.user_mode ? "yes" : "no");
            tty_putchar(' ');
            print_u64(info.uid);
            tty_putchar(' ');
            tty_puts(info.is_admin ? "yes" : "no");
            tty_putchar(' ');
            print_hex_u32(info.capabilities);
            tty_putchar(' ');
            print_u64(info.sleep_until_tick);
            tty_putchar('\n');
        }
        return;
    }

    if (starts_with_word(line, "kill")) {
        char pid_text[SHELL_ARG_SIZE];
        uint64_t pid;
        (void)parse_token(argument_after_word(line), pid_text, sizeof(pid_text));

        if (parse_u64(pid_text, &pid) != 0 || pid == 0u || pid > 0xffffffffu) {
            tty_puts("usage: kill <pid>\n");
            return;
        }

        if (process_kill((uint32_t)pid) != 0) {
            tty_puts("kill failed\n");
            return;
        }

        tty_puts("killed ");
        print_u64(pid);
        tty_putchar('\n');
        return;
    }

    if (string_equals(line, "log") || string_equals(line, "syslog")) {
        uint32_t seq = 0u;
        const size_t copied = debug_log_copy_recent(shell_log_buffer, sizeof(shell_log_buffer) - 1u, &seq);
        if (copied == 0u) {
            tty_puts("log empty\n");
            return;
        }
        shell_log_buffer[copied] = '\0';
        tty_puts(shell_log_buffer);
        if (shell_log_buffer[copied - 1u] != '\n') {
            tty_putchar('\n');
        }
        return;
    }

    if (string_equals(line, "secaudit")) {
        security_audit_event_t events[32];
        size_t count;
        if (!user_has_capability(VEX_CAP_AUDIT_ADMIN)) {
            tty_puts("secaudit denied\n");
            return;
        }
        count = security_audit_copy_recent(events, 32u);
        if (count == 0u) {
            tty_puts("secaudit empty\n");
            return;
        }
        for (size_t i = 0u; i < count; ++i) {
            print_u64(events[i].ticks);
            tty_puts(" pid=");
            print_u64(events[i].pid);
            tty_puts(" uid=");
            print_u64(events[i].uid);
            tty_puts(events[i].allowed ? " allow " : " deny ");
            tty_puts(events[i].action);
            tty_puts(" caps=");
            print_hex_u32(events[i].caps);
            tty_puts(" target=");
            tty_puts(events[i].target);
            tty_putchar('\n');
        }
        return;
    }

    if (starts_with_word(line, "log") || starts_with_word(line, "syslog")) {
        const char *message = argument_after_word(line);
        if (message[0] == '\0') {
            tty_puts("usage: log [message]\n");
            return;
        }
        debug_log_puts("shell: ");
        debug_log_puts(message);
        debug_log_puts("\n");
        return;
    }

    if (starts_with_word(line, "echo")) {
        tty_puts(argument_after_word(line));
        tty_putchar('\n');
        return;
    }

    if (string_equals(line, "whoami")) {
        tty_puts(user_current_username());
        tty_putchar('\n');
        return;
    }

    if (starts_with_word(line, "useradd")) {
        char username[32];
        (void)parse_token(argument_after_word(line), username, sizeof(username));
        if (username[0] == '\0') {
            tty_puts("usage: useradd <username>\n");
            return;
        }

        if (user_create(username, 0) != 0) {
            tty_puts("useradd failed\n");
            return;
        }
        return;
    }

    if (starts_with_word(line, "login")) {
        char username[32];
        (void)parse_token(argument_after_word(line), username, sizeof(username));
        if (username[0] == '\0') {
            tty_puts("usage: login <username>\n");
            return;
        }

        if (user_login(username) != 0) {
            tty_puts("login failed\n");
            return;
        }
        return;
    }

    if (string_equals(line, "diskinfo")) {
        disk_init();
        tty_puts("Disk: ");
        tty_puts(disk_type());
        tty_putchar('\n');
        tty_puts("Size: ");
        print_u64((disk_size() * 512ull) / (1024ull * 1024ull));
        tty_puts(" MB\n");
        return;
    }
    if (string_equals(line,"diskselftest")) {
        tty_puts(disk_fixture_self_test()==0 ? "Tagged ATA fixture: PASS\n" : "Tagged ATA fixture: refused/FAILED\n");
        return;
    }

    if (string_equals(line, "ifconfig")) {
        tty_puts("IP: ");
        print_ipv4(ipv4_get_local_ip());
        tty_putchar('\n');
        tty_puts("Mask: ");
        print_ipv4(dhcp_netmask());
        tty_putchar('\n');
        tty_puts("MAC: ");
        print_mac(ethernet_mac());
        tty_putchar('\n');
        return;
    }

    if (string_equals(line, "netstat")) {
        netdev_stats_t stats;
        netdev_stats(&stats);

        tty_puts("Driver: ");
        tty_puts(netdev_type());
        tty_putchar('\n');
        tty_puts("Link: ");
        tty_puts(stats.link_up != 0 ? "up\n" : "down\n");
        tty_puts("IP: ");
        print_ipv4(ipv4_get_local_ip());
        tty_putchar('\n');
        tty_puts("TX packets: ");
        print_u64(stats.tx_packets);
        tty_putchar('\n');
        tty_puts("RX packets: ");
        print_u64(stats.rx_packets);
        tty_putchar('\n');
        tty_puts("RX broadcast: ");
        print_u64(stats.rx_broadcast);
        tty_putchar('\n');
        tty_puts("RX multicast: ");
        print_u64(stats.rx_multicast);
        tty_putchar('\n');
        tty_puts("RX no buffer: ");
        print_u64(stats.rx_no_buffer);
        tty_putchar('\n');
        tty_puts("Missed packets: ");
        print_u64(stats.missed_packets);
        tty_putchar('\n');
        tty_puts("RX head: ");
        print_u64(stats.rx_head);
        tty_putchar('\n');
        tty_puts("RX tail: ");
        print_u64(stats.rx_tail);
        tty_putchar('\n');
        tty_puts("RX control: ");
        print_u64(stats.rx_control);
        tty_putchar('\n');
        tty_puts("RX desc status: ");
        for (size_t i = 0; i < 4u; ++i) {
            if (i != 0) {
                tty_putchar(' ');
            }
            print_hex_byte(stats.rx_descriptor_status[i]);
        }
        tty_putchar('\n');
        return;
    }

    if (starts_with_word(line, "resolve")) {
        char hostname[SHELL_PATH_SIZE];
        uint32_t ip = 0;
        (void)parse_token(argument_after_word(line), hostname, sizeof(hostname));
        if (hostname[0] == '\0') {
            tty_puts("usage: resolve <hostname>\n");
            return;
        }

        if (dns_resolve(hostname, &ip) != 0) {
            tty_puts("resolve failed\n");
            return;
        }

        print_ipv4(ip);
        tty_putchar('\n');
        return;
    }

    if (starts_with_word(line, "https")) {
        char hostname[SHELL_PATH_SIZE];
        char path[SHELL_PATH_SIZE];
        const char *rest = parse_token(argument_after_word(line), hostname, sizeof(hostname));
        (void)parse_token(rest, path, sizeof(path));

        if (hostname[0] == '\0' || path[0] == '\0') {
            tty_puts("usage: https <hostname> <path>\n");
            return;
        }

        {
            uint32_t ip = 0;
            if (dns_resolve(hostname, &ip) != 0) {
                tty_puts("resolve failed\n");
                return;
            }

            {
                int sock = tls_connect(ip, 443, hostname);
                if (sock < 0) {
                    tty_puts("tls connect failed\n");
                    return;
                }

                {
                    char request[384];
                    size_t request_len = 0;
                    append_string(request, sizeof(request), &request_len, "GET ");
                    append_string(request, sizeof(request), &request_len, path);
                    append_string(request, sizeof(request), &request_len, " HTTP/1.1\r\nHost: ");
                    append_string(request, sizeof(request), &request_len, hostname);
                    append_string(request, sizeof(request), &request_len, "\r\nConnection: close\r\n\r\n");

                    if (tls_send(sock, request, request_len) != 0) {
                        tty_puts("https send failed\n");
                        tls_close(sock);
                        return;
                    }
                }

                {
                    char response[501];
                    size_t response_size = 500u;
                    if (tls_recv(sock, response, &response_size) != 0) {
                        tty_puts("https recv failed\n");
                        tls_close(sock);
                        return;
                    }

                    for (size_t i = 0; i < response_size && i < 500u; ++i) {
                        if (response[i] != '\r') {
                            tty_putchar(response[i]);
                        }
                    }
                    tty_putchar('\n');
                }

                tls_close(sock);
            }
        }
        return;
    }

    if (starts_with_word(line, "spotlight")) {
        spotlight_search(argument_after_word(line));
        return;
    }

    if (string_equals(line, "ls") || string_equals(line, "ls -a")) {
        shell_ls_show_hidden = string_equals(line, "ls -a");
        fs_list(print_fs_entry);
        shell_ls_show_hidden = 0u;
        return;
    }

    if (starts_with_word(line, "cat")) {
        char path[SHELL_PATH_SIZE];
        (void)parse_token(argument_after_word(line), path, sizeof(path));
        if (path[0] == '\0') {
            tty_puts("usage: cat <path>\n");
            return;
        }

        {
            void *data = NULL;
            size_t size = 0;
            if (fs_read_view(path,&data,&size, &fs_lease_0) != 0) {
                tty_puts("not found: ");
                tty_puts(path);
                tty_putchar('\n');
                return;
            }

            {
                const char *bytes = data;
                for (size_t i = 0; i < size; ++i) {
                    tty_putchar(bytes[i]);
                }
                tty_putchar('\n');
            }
        }
        return;
    }

    if (starts_with_word(line, "write")) {
        char path[SHELL_PATH_SIZE];
        const char *content = parse_token(argument_after_word(line), path, sizeof(path));
        if (path[0] == '\0') {
            tty_puts("usage: write <path> <content>\n");
            return;
        }

        if (fs_write(path, (void *)content, string_length(content)) != 0) {
            tty_puts("write failed\n");
            return;
        }
        return;
    }

    if (starts_with_word(line, "rm")) {
        char first[SHELL_PATH_SIZE];
        char second[SHELL_PATH_SIZE];
        const char *rest = parse_token(argument_after_word(line), first, sizeof(first));

        if (first[0] == '\0') {
            tty_puts("usage: rm [-r] <path>\n");
            return;
        }

        if (string_equals(first, "-r")) {
            (void)parse_token(rest, second, sizeof(second));
            if (second[0] == '\0') {
                tty_puts("usage: rm -r <path>\n");
                return;
            }

            {
                const int deleted = fs_delete_prefix(second);
                if (deleted < 0) {
                    tty_puts("rm failed\n");
                    return;
                }
                print_u64((uint64_t)deleted);
                tty_puts(" removed\n");
            }
            return;
        }

        if (fs_delete(first) != 0) {
            tty_puts("rm failed\n");
            return;
        }
        inst_cleanup_for_path(first);
        return;
    }

    if (starts_with_word(line, "chmod")) {
        char mode_text[SHELL_ARG_SIZE];
        char path[SHELL_PATH_SIZE];
        uint64_t mode_value = 0u;
        const char *rest = parse_token(argument_after_word(line), mode_text, sizeof(mode_text));
        (void)parse_token(rest, path, sizeof(path));

        if (mode_text[0] == '\0' || path[0] == '\0' || parse_u64(mode_text, &mode_value) != 0 || mode_value > 0777u) {
            tty_puts("usage: chmod <mode> <path>\n");
            return;
        }

        if (fs_set_mode(path, (uint32_t)mode_value) != 0) {
            tty_puts("chmod failed\n");
        }
        return;
    }

    if (starts_with_word(line, "getfacl")) {
        char path[SHELL_PATH_SIZE];
        fs_metadata_t metadata;
        fs_acl_entry_t entries[FS_ACL_MAX_ENTRIES];
        size_t acl_count = 0u;
        (void)parse_token(argument_after_word(line), path, sizeof(path));

        if (path[0] == '\0') {
            tty_puts("usage: getfacl <path>\n");
            return;
        }
        if (fs_get_metadata(path, &metadata, 1u) != 0) {
            tty_puts("getfacl failed\n");
            return;
        }

        tty_puts("owner=");
        tty_puts(user_name_for_uid(metadata.uid));
        tty_puts(" uid=");
        print_u64(metadata.uid);
        tty_puts(" mode=");
        print_u64(metadata.mode);
        tty_putchar('\n');

        if (fs_acl_copy(path, entries, FS_ACL_MAX_ENTRIES, &acl_count) != 0 || acl_count == 0u) {
            tty_puts("acl empty\n");
            return;
        }

        for (size_t i = 0u; i < acl_count; ++i) {
            tty_puts("user:");
            tty_puts(user_name_for_uid(entries[i].uid));
            tty_puts(" ");
            print_acl_perms(entries[i].perms);
            tty_putchar('\n');
        }
        return;
    }

    if (starts_with_word(line, "setfacl")) {
        char path[SHELL_PATH_SIZE];
        char username[SHELL_ARG_SIZE];
        char perms_text[SHELL_ARG_SIZE];
        uint32_t uid = 0u;
        uint8_t perms = 0u;
        const char *rest = parse_token(argument_after_word(line), path, sizeof(path));
        rest = parse_token(rest, username, sizeof(username));
        (void)parse_token(rest, perms_text, sizeof(perms_text));

        if (path[0] == '\0' || username[0] == '\0' || perms_text[0] == '\0') {
            tty_puts("usage: setfacl <path> <user> <rwx|clear>\n");
            return;
        }
        if (user_lookup_uid(username, &uid) != 0) {
            tty_puts("unknown user\n");
            return;
        }
        if (string_equals(perms_text, "clear")) {
            if (fs_acl_clear(path, uid) != 0) {
                tty_puts("setfacl failed\n");
            }
            return;
        }
        if (!parse_acl_perms(perms_text, &perms)) {
            tty_puts("usage: setfacl <path> <user> <rwx|clear>\n");
            return;
        }
        if (fs_acl_set(path, uid, perms) != 0) {
            tty_puts("setfacl failed\n");
        }
        return;
    }

    if (starts_with_word(line, "ipc_write")) {
        char key[SHELL_PATH_SIZE];
        const char *value = parse_token(argument_after_word(line), key, sizeof(key));
        if (key[0] == '\0' || value[0] == '\0') {
            tty_puts("usage: ipc_write <key> <value>\n");
            return;
        }

        {
            if (ipc_store(key,value,string_length(value)+1u)!=0) {
                tty_puts("ipc_write failed\n");
            }
        }
        return;
    }

    if (starts_with_word(line, "ipc_read")) {
        char key[SHELL_PATH_SIZE];
        (void)parse_token(argument_after_word(line), key, sizeof(key));
        if (key[0] == '\0') {
            tty_puts("usage: ipc_read <key>\n");
            return;
        }

        {
            ora_view_t view __attribute__((cleanup(ora_view_release)))={0};
            if (ipc_acquire(key,&view)!=0) {
                tty_puts("not found\n");
                return;
            }

            tty_puts(ora_view_data(&view));
            tty_putchar('\n');
        }
        return;
    }

    if (starts_with_word(line, "inst")) {
        char args[SHELL_MAX_ARGS][SHELL_ARG_SIZE];
        char *argv[SHELL_MAX_ARGS];
        int argc = 0;
        const char *rest = argument_after_word(line);

        while (*rest != '\0' && argc < (int)SHELL_MAX_ARGS) {
            rest = parse_token(rest, args[argc], sizeof(args[argc]));
            if (args[argc][0] != '\0') {
                argv[argc] = args[argc];
                ++argc;
            }
        }

        (void)inst_execute(argc, argv);
        return;
    }

    if (starts_with_word(line, "browser") || starts_with_word(line, "webkit")) {
        gui_browser_launch();
        return;
    }

    if (string_equals(line, "gfm") || string_equals(line, "files")) {
        if (shell_launch_desktop_app("/usr/bin/kyil-files") != 0) {
            tty_puts("gfm launch failed\n");
        }
        return;
    }

    if (string_equals(line, "paint")) {
        if (shell_launch_desktop_app("/usr/bin/kyil-paint") != 0) {
            tty_puts("paint launch failed\n");
        }
        return;
    }

    if (string_equals(line, "launcher")) {
        if (shell_launch_desktop_app("/usr/bin/kyil-launcher") != 0) {
            tty_puts("launcher launch failed\n");
        }
        return;
    }

    if (string_equals(line, "stress")) {
        if (elf_spawn("/home/root/bin/alloc_stress") != 0) {
            tty_puts("stress launch failed\n");
        }
        return;
    }

    if (string_equals(line, "nettest")) {
        if (elf_spawn("/home/root/bin/net_test") != 0) {
            tty_puts("nettest launch failed\n");
        }
        return;
    }

    if (string_equals(line, "gpuinfo")) {
        vex_gpu_info_t info;
        vex_gpu_caps_t caps;
        if (gpu_query_info(&info) != 0) {
            tty_puts("gpu unavailable\n");
            return;
        }
        if (gpu_query_caps(&caps) != 0) {
            tty_puts("gpu capabilities unavailable\n");
            return;
        }
        tty_puts("backend: ");
        print_u64(info.backend);
        tty_puts(" (");
        tty_puts(caps.driver_name);
        tty_puts(")");
        tty_puts("\nvendor: ");
        print_u64(info.vendor_id);
        tty_puts("\ndevice: ");
        print_u64(info.device_id);
        tty_puts("\nfb: ");
        print_u64(info.framebuffer_width);
        tty_puts("x");
        print_u64(info.framebuffer_height);
        tty_puts(" pitch=");
        print_u64(info.framebuffer_pitch);
        tty_puts("\nqueues: ");
        print_u64(info.queue_count);
        tty_puts("\ncapabilities: ");
        print_u64(caps.capabilities);
        tty_putchar('\n');
        return;
    }

    if (string_equals(line, "gputest")) {
        if (elf_spawn("/home/root/bin/gpu_test") != 0) {
            tty_puts("gputest launch failed\n");
        }
        return;
    }

    if (string_equals(line, "vktest")) {
        if (elf_spawn("/home/root/bin/vk_test") != 0) {
            tty_puts("vktest launch failed\n");
        }
        return;
    }

    if (string_equals(line, "mousetest")) {
        if (elf_spawn("/home/root/bin/mouse_test") != 0) {
            tty_puts("mousetest launch failed\n");
        }
        return;
    }

    if (string_equals(line, "elevatetest")) {
        if (elf_spawn("/home/root/bin/elevate_test") != 0) {
            tty_puts("elevatetest launch failed\n");
        }
        return;
    }

    if (string_equals(line, "attacktest")) {
        if (elf_spawn("/home/root/bin/attack_test") != 0) {
            tty_puts("attacktest launch failed\n");
        }
        return;
    }

    if (string_equals(line, "attackkill")) {
        uint32_t payload_pid = 0u;
        if (elf_spawn_pid("/home/root/bin/attack_kill_payload", &payload_pid) != 0) {
            tty_puts("attackkill payload launch failed\n");
        } else {
            scheduler_detach_process(payload_pid);
            tty_puts("attackkill payload launched\n");
        }
        return;
    }

    /* run <path> [args...]       a native program, waited for
       suchu <args...>            shorthand for run /usr/bin/suchu <args...>
       Arguments are separated by spaces; "double quotes" group one. The
       program gets PATH, TMPDIR and HOME so that suchu comp can find
       ocamlopt, as and ld. */
    if (starts_with_word(line, "run") || starts_with_word(line, "suchu")) {
        static char words[SHELL_RUN_LINE];
        const char *argv[SHELL_RUN_MAX_ARGS + 1];
        static const char *const envp[] = {
            "PATH=/usr/bin:/bin", "TMPDIR=/tmp", "HOME=/home/root", "TERM=dumb",
        };
        size_t argc = 0u, used = 0u;
        const char *cursor = line;
        int status = 0;

        if (starts_with_word(line, "suchu")) {
            argv[argc++] = "/usr/bin/suchu";
        }
        cursor = argument_after_word(line);
        while (*cursor != '\0' && argc < SHELL_RUN_MAX_ARGS) {
            char quote = 0;
            while (*cursor == ' ') ++cursor;
            if (*cursor == '\0') break;
            argv[argc++] = &words[used];
            if (*cursor == '"') quote = *cursor++;
            while (*cursor != '\0' && (quote ? *cursor != quote : *cursor != ' ') &&
                   used + 1u < sizeof(words)) {
                words[used++] = *cursor++;
            }
            if (quote && *cursor == quote) ++cursor;
            if (used + 1u >= sizeof(words)) break;
            words[used++] = '\0';
        }
        argv[argc] = NULL;
        if (argc == 0u) {
            tty_puts("usage: run <path> [args...]   |   suchu run|comp|build <file.suchu> ...\n");
            return;
        }
        if (elf_load_args(argv[0], argv, argc, envp, sizeof(envp) / sizeof(envp[0]), &status) != 0) {
            tty_puts("run failed: ");
            tty_puts(argv[0]);
            tty_putchar('\n');
            return;
        }
        if (status != 0) {
            char line_out[48];
            snprintf(line_out, sizeof(line_out), "[exit status %d]\n",
                     (status & 0x7f) == 0 ? (status >> 8) & 0xff : 128 + (status & 0x7f));
            tty_puts(line_out);
        }
        return;
    }

    if (string_equals(line, "halt")) {
        tty_puts("halting\n");
        halt_forever();
    }

    tty_puts("unknown command: ");
    tty_puts(line);
    tty_putchar('\n');
}
