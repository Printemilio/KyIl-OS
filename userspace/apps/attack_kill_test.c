#include <stddef.h>
#include <stdint.h>

#include <unos/vex_abi.h>
#include <unos/vex_legacy_compat.h>
#include <vex_syscall.h>

static size_t string_length(const char *text) {
    size_t length = 0u;
    while (text != NULL && text[length] != '\0') {
        ++length;
    }
    return length;
}

static void write_line(const char *text) {
    static const char clear_tail[] = "                                        \n";
    (void)write(STDOUT_FILENO, text, string_length(text));
    (void)write(STDOUT_FILENO, clear_tail, sizeof(clear_tail) - 1u);
}

static void append_text(char *buffer, size_t capacity, size_t *length, const char *text) {
    size_t i = 0u;
    while (text != NULL && text[i] != '\0' && *length + 1u < capacity) {
        buffer[(*length)++] = text[i++];
    }
    buffer[*length] = '\0';
}

static void append_u32(char *buffer, size_t capacity, size_t *length, uint32_t value) {
    char digits[16];
    size_t count = 0u;

    if (*length + 1u >= capacity) {
        return;
    }
    if (value == 0u) {
        buffer[(*length)++] = '0';
        buffer[*length] = '\0';
        return;
    }

    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count != 0u && *length + 1u < capacity) {
        buffer[(*length)++] = digits[--count];
    }
    buffer[*length] = '\0';
}

static void ensure_test_dirs(void) {
    (void)mkdir("/home/root/redteam", 0755u);
    (void)mkdir("/home/root/redteam/kill", 0755u);
}

static int try_write(uint32_t index) {
    static const char payload[] = "mrac persistent attack";
    char path[128];
    size_t length = 0u;
    int fd;
    ssize_t written;
    int close_result;

    append_text(path, sizeof(path), &length, "/home/root/redteam/kill/file_");
    append_u32(path, sizeof(path), &length, index);
    append_text(path, sizeof(path), &length, ".txt");

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644u);
    if (fd < 0) {
        return -1;
    }

    written = write(fd, payload, sizeof(payload) - 1u);
    close_result = close(fd);
    if (written < 0 || close_result != 0) {
        return -1;
    }
    return 0;
}

int main(void) {
    vex_process_spawn_req_t request;
    char line[160];
    size_t length = 0u;

    request.path = "/home/root/bin/attack_kill_payload";
    request.flags = 0u;
    request.pid = 0u;

    write_line("attack_kill_test: launching detached payload");
    if ((int64_t)vex_syscall1(VEX_SYS_PROCESS_SPAWN, (uint64_t)(uintptr_t)&request) != 0) {
        write_line("attack_kill_test: spawn failed");
        return 1;
    }

    append_text(line, sizeof(line), &length, "attack_kill_test: payload pid=");
    append_u32(line, sizeof(line), &length, request.pid);
    write_line(line);
    write_line("attack_kill_test: launcher exiting; use `secaudit` to confirm mrac-kill");
    return 0;
}
