#include <stddef.h>
#include <stdint.h>

#include <unos/vex_legacy_compat.h>

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
    ensure_test_dirs();

    /* Seed the victims below the MRAC rate; rewriting them is the attack. */
    for (uint32_t i = 0u; i < 32u; ++i) {
        (void)try_write(i < 20u ? i : 100u + (i - 20u));
        (void)usleep(120000u);
    }

    for (uint32_t i = 0u; i < 20u; ++i) {
        if (try_write(i) != 0) {
            break;
        }
    }

    for (uint32_t i = 0u; i < 12u; ++i) {
        (void)try_write(100u + i);
        (void)usleep(60000u);
    }

    return 1;
}
