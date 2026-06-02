#include <unos/timezone.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/fs.h>

#define TIMEZONE_PATH "/sys/conf/timezone"

static int8_t offset_hours;

static int parse_offset(const char *text, size_t size, int8_t *out) {
    size_t i = 0;
    int sign = 1;
    int value = 0;

    if (text == 0 || size == 0 || out == 0) {
        return -1;
    }

    while (i < size && (text[i] == ' ' || text[i] == '\n' || text[i] == '\r' || text[i] == '\t')) {
        ++i;
    }

    if (i < size && text[i] == '-') {
        sign = -1;
        ++i;
    } else if (i < size && text[i] == '+') {
        ++i;
    }

    if (i >= size || text[i] < '0' || text[i] > '9') {
        return -1;
    }

    while (i < size && text[i] >= '0' && text[i] <= '9') {
        value = value * 10 + (text[i] - '0');
        ++i;
    }

    value *= sign;
    if (value < -12 || value > 14) {
        return -1;
    }

    *out = (int8_t)value;
    return 0;
}

void timezone_init(void) {
    void *data = 0;
    size_t size = 0;
    int8_t parsed = 0;

    offset_hours = 0;
    if (fs_read(TIMEZONE_PATH, &data, &size) == 0 && parse_offset(data, size, &parsed) == 0) {
        offset_hours = parsed;
    }
}

int8_t timezone_offset_hours(void) {
    return offset_hours;
}
