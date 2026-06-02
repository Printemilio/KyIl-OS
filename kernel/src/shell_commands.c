#include <unos/shell.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/clipboard.h>
#include <unos/debug_log.h>
#include <unos/disk.h>
#include <unos/dns.h>
#include <unos/dhcp.h>
#include <unos/elf.h>
#include <unos/ethernet.h>
#include <unos/fs.h>
#include <unos/gpu.h>
#include <unos/gui.h>
#include <unos/heap.h>
#include <unos/inst.h>
#include <unos/ipc.h>
#include <unos/ipv4.h>
#include <unos/netdev.h>
#include <unos/scheduler.h>
#include <unos/spotlight.h>
#include <unos/tls.h>
#include <unos/tty.h>
#include <unos/users.h>
#include <unos/vex_abi.h>
#include <unos/vex_syscall.h>

#include "shell_internal.h"

#define SHELL_PATH_SIZE 128u
#define SHELL_MAX_ARGS 8u
#define SHELL_ARG_SIZE 64u
#define SHELL_LOG_BUFFER_SIZE 2048u

static uint8_t shell_ls_show_hidden;
static char shell_log_buffer[SHELL_LOG_BUFFER_SIZE];

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

    return line[word_len] == '\0' || line[word_len] == ' ';
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

void shell_execute_command(const char *line) {
    if (line[0] == '\0') {
        return;
    }

    if (string_equals(line, "cls")) {
        tty_clear();
        return;
    }

    if (string_equals(line, "help")) {
        tty_puts("commands: cls help sysinfo ps kill log echo whoami useradd login diskinfo ifconfig netstat resolve https spotlight ls cat write rm ipc_write ipc_read inst(info/translate/install/remove) run halt\n");
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

        tty_puts("VexOS system\n");
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

    if (string_equals(line, "ps")) {
        scheduler_process_info_t info;
        const uint64_t count = scheduler_process_count();

        tty_puts("PID PPID STATE     PRI USER UID ADMIN SLEEP\n");
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
            if (fs_read(path, &data, &size) != 0) {
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

    if (starts_with_word(line, "ipc_write")) {
        char key[SHELL_PATH_SIZE];
        const char *value = parse_token(argument_after_word(line), key, sizeof(key));
        if (key[0] == '\0' || value[0] == '\0') {
            tty_puts("usage: ipc_write <key> <value>\n");
            return;
        }

        {
            void *existing = ipc_get(key);
            void *stored = NULL;
            if (existing != NULL) {
                ipc_release(key);
                stored = ipc_update(key, (void *)value, string_length(value) + 1u);
            } else {
                stored = ipc_share(key, (void *)value, string_length(value) + 1u);
            }

            if (stored == NULL) {
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
            const char *value = ipc_get(key);
            if (value == NULL) {
                tty_puts("not found\n");
                return;
            }

            tty_puts(value);
            tty_putchar('\n');
            ipc_release(key);
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

    if (string_equals(line, "browser") || string_equals(line, "webkit")) {
        if (elf_spawn("/home/root/bin/webkit") != 0) {
            tty_puts("browser launch failed\n");
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
        if (gpu_query_info(&info) != 0) {
            tty_puts("gpu unavailable\n");
            return;
        }
        tty_puts("backend: ");
        print_u64(info.backend);
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

    if (starts_with_word(line, "run")) {
        char path[SHELL_PATH_SIZE];
        (void)parse_token(argument_after_word(line), path, sizeof(path));
        if (path[0] == '\0') {
            tty_puts("usage: run <path>\n");
            return;
        }

        if (elf_load(path) != 0) {
            tty_puts("run failed\n");
            return;
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
