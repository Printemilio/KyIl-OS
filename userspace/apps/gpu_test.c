#include <stddef.h>
#include <stdint.h>

#include <unos/vex_legacy_compat.h>

#include "vex_runtime.h"

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
    vex_gpu_info_t info;
    vex_gpu_buffer_create_req_t create_req;
    vex_gpu_present_req_t present_req;
    vex_gpu_submit_req_t submit_req;
    vex_gpu_command_t commands[3];
    char line[160];
    size_t length = 0u;

    write_text("gpu_test: begin\n");
    if (vex_gpu_query_info(&info) != 0) {
        write_text("gpu_test: query failed\n");
        return 1;
    }

    append_text(line, sizeof(line), &length, "gpu_test: backend=");
    append_u32(line, sizeof(line), &length, info.backend);
    append_text(line, sizeof(line), &length, " vendor=");
    append_u32(line, sizeof(line), &length, info.vendor_id);
    append_text(line, sizeof(line), &length, " device=");
    append_u32(line, sizeof(line), &length, info.device_id);
    append_text(line, sizeof(line), &length, "\n");
    write_text(line);

    create_req.width = 320u;
    create_req.height = 180u;
    create_req.stride = 320u;
    create_req.format = VEX_GPU_FORMAT_XRGB8888;
    create_req.flags = VEX_GPU_MEMORY_CPU_VISIBLE;
    create_req.handle = 0u;
    create_req.size = 0u;
    if (vex_gpu_buffer_create(&create_req) != 0) {
        write_text("gpu_test: create failed\n");
        return 1;
    }

    commands[0].op = VEX_GPU_CMD_CLEAR;
    commands[0].reserved0 = 0u;
    commands[0].rect.x = 0;
    commands[0].rect.y = 0;
    commands[0].rect.w = 0;
    commands[0].rect.h = 0;
    commands[0].color = 0xff171b2dU;
    commands[0].src_width = 0u;
    commands[0].src_height = 0u;
    commands[0].src_stride = 0u;
    commands[0].src_pixels = 0;

    commands[1] = commands[0];
    commands[1].op = VEX_GPU_CMD_FILL_RECT;
    commands[1].rect.x = 18;
    commands[1].rect.y = 18;
    commands[1].rect.w = 284;
    commands[1].rect.h = 48;
    commands[1].color = 0xff3f7cffU;

    commands[2] = commands[0];
    commands[2].op = VEX_GPU_CMD_FILL_RECT;
    commands[2].rect.x = 40;
    commands[2].rect.y = 88;
    commands[2].rect.w = 240;
    commands[2].rect.h = 56;
    commands[2].color = 0xfff97316U;

    submit_req.queue_id = 0u;
    submit_req.buffer_handle = create_req.handle;
    submit_req.command_count = 3u;
    submit_req.flags = 0u;
    submit_req.commands = commands;
    if (vex_gpu_queue_submit(&submit_req) != 0) {
        write_text("gpu_test: submit failed\n");
        (void)vex_gpu_buffer_destroy(create_req.handle);
        return 1;
    }

    present_req.buffer_handle = create_req.handle;
    present_req.flags = VEX_GPU_PRESENT_IMMEDIATE;
    present_req.dirty.x = 0;
    present_req.dirty.y = 0;
    present_req.dirty.w = 320;
    present_req.dirty.h = 180;
    present_req.dst_x = 80;
    present_req.dst_y = 80;
    if (vex_gpu_present(&present_req) != 0) {
        write_text("gpu_test: present failed\n");
        (void)vex_gpu_buffer_destroy(create_req.handle);
        return 1;
    }

    (void)vex_gpu_buffer_destroy(create_req.handle);
    write_text("gpu_test: ok\n");
    return 0;
}
