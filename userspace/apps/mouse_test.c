#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>
#include <unistd.h>

#include <unos/vex_abi.h>

#include "vex_runtime.h"
#include "vex_syscall.h"

static size_t string_length(const char *text) {
    size_t length = 0u;
    while (text != 0 && text[length] != '\0') {
        ++length;
    }
    return length;
}

static void write_text(const char *text) {
    (void)write(STDOUT_FILENO, text, string_length(text));
}

static void append_text(char *buffer, size_t capacity, size_t *length, const char *text) {
    size_t i = 0u;
    while (text != 0 && text[i] != '\0' && *length + 1u < capacity) {
        buffer[(*length)++] = text[i++];
    }
    buffer[*length] = '\0';
}

static void append_i32(char *buffer, size_t capacity, size_t *length, int32_t value) {
    uint32_t magnitude;
    char digits[16];
    size_t count = 0u;

    if (value < 0) {
        append_text(buffer, capacity, length, "-");
        magnitude = (uint32_t)(-(int64_t)value);
    } else {
        magnitude = (uint32_t)value;
    }

    if (magnitude == 0u) {
        append_text(buffer, capacity, length, "0");
        return;
    }

    while (magnitude != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (magnitude % 10u));
        magnitude /= 10u;
    }
    while (count > 0u && *length + 1u < capacity) {
        buffer[(*length)++] = digits[--count];
    }
    buffer[*length] = '\0';
}

static void append_u32(char *buffer, size_t capacity, size_t *length, uint32_t value) {
    char digits[16];
    size_t count = 0u;

    if (value == 0u) {
        append_text(buffer, capacity, length, "0");
        return;
    }

    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count > 0u && *length + 1u < capacity) {
        buffer[(*length)++] = digits[--count];
    }
    buffer[*length] = '\0';
}

int main(void) {
    char line[128];
    uint32_t samples = 0u;
    uint64_t start_ms;
    uint64_t last_event_ms;
    uint8_t seen_event = 0u;
    struct timeval tv;

    write_text("mouse_test: begin\n");
    if (gettimeofday(&tv, 0) != 0) {
        write_text("mouse_test: time failed\n");
        return 1;
    }
    start_ms = (uint64_t)tv.tv_sec * 1000u + (uint64_t)(tv.tv_usec / 1000u);
    last_event_ms = start_ms;

    while (samples < 256u) {
        vex_mouse_packet_t packet;
        int result;
        uint64_t now_ms;

        result = vex_mouse_poll(&packet);
        if (result < 0) {
            write_text("mouse_test: poll failed\n");
            return 1;
        }
        if (gettimeofday(&tv, 0) != 0) {
            write_text("mouse_test: time failed\n");
            return 1;
        }
        now_ms = (uint64_t)tv.tv_sec * 1000u + (uint64_t)(tv.tv_usec / 1000u);

        if (result == 0) {
            if ((!seen_event && now_ms - start_ms >= 3000u) ||
                (seen_event && now_ms - last_event_ms >= 1200u)) {
                break;
            }
            (void)vex_syscall0(VEX_SYS_PROCESS_YIELD);
            continue;
        }

        if (packet.dx != 0 || packet.dy != 0 || packet.buttons != 0u) {
            size_t length = 0u;
            seen_event = 1u;
            last_event_ms = now_ms;
            append_text(line, sizeof(line), &length, "mouse_test: dx=");
            append_i32(line, sizeof(line), &length, packet.dx);
            append_text(line, sizeof(line), &length, " dy=");
            append_i32(line, sizeof(line), &length, packet.dy);
            append_text(line, sizeof(line), &length, " buttons=");
            append_u32(line, sizeof(line), &length, packet.buttons);
            append_text(line, sizeof(line), &length, "\n");
            write_text(line);
            ++samples;
        }
    }

    write_text("mouse_test: end\n");
    return 0;
}
