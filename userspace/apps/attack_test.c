#include <stddef.h>
#include <stdint.h>

#include <unos/vex_legacy_compat.h>

static size_t string_length(const char *text) {
    size_t length = 0u;
    while (text != NULL && text[length] != '\0') {
        ++length;
    }
    return length;
}

static void write_text(const char *text) {
    (void)write(STDOUT_FILENO, text, string_length(text));
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

static int ensure_test_dirs(void) {
    (void)mkdir("/home/root/redteam", 0755u);
    (void)mkdir("/home/root/redteam/burst", 0755u);
    return 0;
}

static int try_access_path(const char *path) {
    static const char payload[] = "redteam probe";
    const int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644u);
    int close_result = -1;
    ssize_t written = -1;

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

static void report_access_result(const char *label, int result, int expect_success) {
    char line[160];
    size_t length = 0u;

    append_text(line, sizeof(line), &length, "attack_test: ");
    append_text(line, sizeof(line), &length, label);
    append_text(line, sizeof(line), &length, " ");
    if ((result == 0) == (expect_success != 0)) {
        append_text(line, sizeof(line), &length, "ok");
    } else if (result == 0) {
        append_text(line, sizeof(line), &length, "unexpected-success");
    } else {
        append_text(line, sizeof(line), &length, "denied");
    }
    write_line(line);
}

static int try_burst_write(uint32_t index) {
    static const char payload[] = "mrac attack sample";
    char path[128];
    size_t length = 0u;
    int fd;
    ssize_t written;
    int close_result;

    append_text(path, sizeof(path), &length, "/home/root/redteam/burst/file_");
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
    uint32_t success_count = 0u;
    uint32_t denied_index = 0xffffffffu;

    write_line("attack_test: begin");
    (void)ensure_test_dirs();

    report_access_result("direct-sys-write", try_access_path("/sys/redteam_probe"), 0);
    report_access_result("traversal-sys-write", try_access_path("/home/root/../../sys/redteam_probe"), 0);

    /* MRAC reacts to mass rewriting of existing files, so first create the
       victims slowly enough (a few per MRAC window) to look like normal use. */
    write_line("attack_test: seeding victim files");
    for (uint32_t i = 0u; i < 23u; ++i) {
        (void)try_burst_write(i < 20u ? i : 100u + (i - 20u));
        (void)usleep(120000u);
    }

    write_line("attack_test: triggering mrac burst");
    for (uint32_t i = 0u; i < 20u; ++i) {
        if (try_burst_write(i) == 0) {
            ++success_count;
            continue;
        }
        denied_index = i;
        break;
    }

    if (denied_index == 0xffffffffu) {
        write_line("attack_test: mrac quarantine not reached");
        write_line("attack_test: end");
        return 1;
    }

    {
        char line[160];
        size_t length = 0u;

        append_text(line, sizeof(line), &length, "attack_test: burst stopped at file ");
        append_u32(line, sizeof(line), &length, denied_index);
        append_text(line, sizeof(line), &length, " after ");
        append_u32(line, sizeof(line), &length, success_count);
        append_text(line, sizeof(line), &length, " writes");
        write_line(line);
    }

    report_access_result("post-quarantine-write-1", try_burst_write(100u), 0);
    report_access_result("post-quarantine-write-2", try_burst_write(101u), 0);
    report_access_result("post-quarantine-write-3", try_burst_write(102u), 0);

    write_text("attack_test: check `secaudit` for mrac-alert, mrac-quarantine and shadow writes\n");
    write_line("attack_test: end");
    return 0;
}
