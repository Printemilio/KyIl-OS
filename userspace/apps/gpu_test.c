#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <unos/vex_legacy_compat.h>

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
    (void)vex_syscall1(VEX_SYS_DEBUG_LOG, (uint64_t)(uintptr_t)text);
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

static int surface_capability_test(uint32_t buffer_handle,
                                   uint64_t surface_handle,
                                   size_t backing_size) {
    int fds[2] = { -1, -1 };
    uint32_t marker = 0xff31a75bu;
    uint32_t received_marker = 0u;
    vex_message_pair_req_t pair_req;
    vex_message_attachment_req_t attachments[1];
    vex_message_io_req_t io;
    vex_surface_map_req_t map_req;
    vex_surface_unmap_req_t unmap_req = {0};
    vex_memory_req_t memory_req;
    uint64_t imported_handle = 0u;
    uint64_t mapped_address = 0u;
    uint64_t mapped_handle = 0u;
    uint32_t mapped_access = 0u;
    size_t aligned_size = (backing_size + 4095u) & ~(size_t)4095u;
    int original_released = 0;
    int buffer_released = 0;
    int imported_released = 0;
    int ok = 0;

    pair_req.fds = fds;
    pair_req.flags = 0u;
    pair_req.reserved = 0u;
    if (vex_message_pair(&pair_req) != 0) {
        goto cleanup;
    }

    memset(&map_req, 0, sizeof(map_req));
    map_req.surface_handle = surface_handle;
    map_req.length = backing_size;
    map_req.access = VEX_SURFACE_MAP_READ | VEX_SURFACE_MAP_WRITE;
    if (vex_surface_map(&map_req) != 0 || map_req.mapped_addr == 0u) {
        goto cleanup;
    }
    mapped_address = map_req.mapped_addr;
    mapped_handle = surface_handle;
    mapped_access = map_req.access;
    *(volatile uint32_t *)(uintptr_t)mapped_address = marker;
    unmap_req.surface_handle = mapped_handle;
    unmap_req.mapped_addr = mapped_address;
    unmap_req.length = backing_size;
    unmap_req.access = mapped_access;
    if (vex_surface_unmap(&unmap_req) != 0) {
        goto cleanup;
    }
    mapped_address = 0u;

    memset(attachments, 0, sizeof(attachments));
    attachments[0].handle = surface_handle;
    attachments[0].type = VEX_MESSAGE_OBJECT_SURFACE;
    attachments[0].rights = VEX_SURFACE_RIGHT_QUERY |
                            VEX_SURFACE_RIGHT_MAP_READ;
    memset(&io, 0, sizeof(io));
    io.fd = fds[0];
    io.buffer = &marker;
    io.length = sizeof(marker);
    io.attachments = attachments;
    io.attachment_count = 1u;
    if (vex_message_send_objects(&io) != (ssize_t)sizeof(marker)) {
        goto cleanup;
    }

    memset(attachments, 0, sizeof(attachments));
    attachments[0].rights = VEX_SURFACE_RIGHT_QUERY |
                            VEX_SURFACE_RIGHT_MAP_READ;
    memset(&io, 0, sizeof(io));
    io.fd = fds[1];
    io.buffer = &received_marker;
    io.length = sizeof(received_marker);
    io.attachments = attachments;
    io.attachment_count = 1u;
    if (vex_message_receive_objects(&io) != (ssize_t)sizeof(received_marker) ||
        received_marker != marker || io.attachment_count != 1u ||
        attachments[0].type != VEX_MESSAGE_OBJECT_SURFACE ||
        attachments[0].handle == 0u ||
        attachments[0].handle == surface_handle ||
        attachments[0].rights != (VEX_SURFACE_RIGHT_QUERY |
                                  VEX_SURFACE_RIGHT_MAP_READ)) {
        goto cleanup;
    }
    imported_handle = attachments[0].handle;

    memset(&map_req, 0, sizeof(map_req));
    map_req.surface_handle = imported_handle;
    map_req.length = backing_size;
    map_req.access = VEX_SURFACE_MAP_WRITE;
    if (vex_surface_map(&map_req) == 0) {
        mapped_address = map_req.mapped_addr;
        mapped_handle = imported_handle;
        mapped_access = map_req.access;
        goto cleanup;
    }

    memset(&map_req, 0, sizeof(map_req));
    map_req.surface_handle = imported_handle;
    map_req.length = backing_size;
    map_req.access = VEX_SURFACE_MAP_READ;
    if (vex_surface_map(&map_req) != 0 || map_req.mapped_addr == 0u ||
        *(volatile uint32_t *)(uintptr_t)map_req.mapped_addr != marker) {
        goto cleanup;
    }
    mapped_address = map_req.mapped_addr;
    mapped_handle = imported_handle;
    mapped_access = map_req.access;

    /* A read-only imported capability must not gain write access through a
       later mprotect, even though another process once had a writable alias. */
    memory_req.addr = (void *)(uintptr_t)mapped_address;
    memory_req.length = aligned_size;
    memory_req.prot_or_advice = PROT_READ | PROT_WRITE;
    if (vex_syscall1(VEX_SYS_MPROTECT,
            (uint64_t)(uintptr_t)&memory_req) == 0u) {
        goto cleanup;
    }

    if (vex_gpu_surface_destroy(surface_handle) != 0) {
        goto cleanup;
    }
    original_released = 1;
    if (vex_gpu_buffer_destroy(buffer_handle) != 0) {
        goto cleanup;
    }
    buffer_released = 1;

    /* The backing survives its original producer capabilities. The last
       mapped local capability must remain until the typed unmap releases
       its mapping/access bookkeeping, as required by the Surface contract. */
    if (vex_gpu_surface_destroy(imported_handle) == 0) {
        imported_released = 1;
        goto cleanup;
    }
    if (*(volatile uint32_t *)(uintptr_t)mapped_address != marker) {
        goto cleanup;
    }
    unmap_req.surface_handle = mapped_handle;
    unmap_req.mapped_addr = mapped_address;
    unmap_req.length = backing_size;
    unmap_req.access = mapped_access;
    if (vex_surface_unmap(&unmap_req) != 0) {
        goto cleanup;
    }
    mapped_address = 0u;
    if (vex_gpu_surface_destroy(imported_handle) != 0) goto cleanup;
    imported_released = 1;

    memset(&map_req, 0, sizeof(map_req));
    map_req.surface_handle = imported_handle;
    map_req.length = backing_size;
    map_req.access = VEX_SURFACE_MAP_READ;
    if (vex_surface_map(&map_req) == 0) {
        mapped_address = map_req.mapped_addr;
        mapped_handle = imported_handle;
        mapped_access = map_req.access;
        goto cleanup;
    }
    ok = 1;

cleanup:
    if (mapped_address != 0u) {
        unmap_req.surface_handle = mapped_handle;
        unmap_req.mapped_addr = mapped_address;
        unmap_req.length = backing_size;
        unmap_req.access = mapped_access;
        (void)vex_surface_unmap(&unmap_req);
    }
    if (!imported_released && imported_handle != 0u) {
        (void)vex_gpu_surface_destroy(imported_handle);
    }
    if (!original_released) {
        (void)vex_gpu_surface_destroy(surface_handle);
    }
    if (!buffer_released) {
        (void)vex_gpu_buffer_destroy(buffer_handle);
    }
    if (fds[0] >= 0) {
        (void)close(fds[0]);
    }
    if (fds[1] >= 0) {
        (void)close(fds[1]);
    }
    return ok;
}

int main(void) {
    vex_gpu_info_t info;
    vex_gpu_caps_t caps;
    vex_gpu_buffer_create_req_t create_req;
    vex_gpu_surface_create_req_t surface_req;
    vex_gpu_surface_present_req_t present_req={0};
    vex_gpu_surface_wait_req_t wait_req;
    vex_gpu_submit_req_t submit_req;
    vex_gpu_command_t commands[3];
    vex_rect_t damage;
    char line[160];
    size_t length = 0u;

    write_text("gpu_test: begin\n");
    if (vex_gpu_query_info(&info) != 0) {
        write_text("gpu_test: query failed\n");
        return 1;
    }
    caps.struct_size = sizeof(caps);
    if (vex_gpu_query_caps(&caps) != 0 ||
        caps.backend != info.backend ||
        caps.driver_name[0] == '\0') {
        write_text("gpu_test: caps query failed\n");
        return 1;
    }

    append_text(line, sizeof(line), &length, "gpu_test: backend=");
    append_u32(line, sizeof(line), &length, info.backend);
    append_text(line, sizeof(line), &length, " vendor=");
    append_u32(line, sizeof(line), &length, info.vendor_id);
    append_text(line, sizeof(line), &length, " device=");
    append_u32(line, sizeof(line), &length, info.device_id);
    append_text(line, sizeof(line), &length, " driver=");
    append_text(line, sizeof(line), &length, caps.driver_name);
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

    surface_req.buffer_handle = create_req.handle;
    surface_req.alpha_mode = VEX_GPU_SURFACE_ALPHA_OPAQUE;
    surface_req.usage = VEX_GPU_SURFACE_USAGE_SAMPLED;
    surface_req.surface_handle = 0u;
    if (vex_gpu_surface_create(&surface_req) != 0) {
        write_text("gpu_test: surface create failed\n");
        (void)vex_gpu_buffer_destroy(create_req.handle);
        return 1;
    }

    damage.x = 0;
    damage.y = 0;
    damage.w = 320;
    damage.h = 180;
    present_req.surface_handle = surface_req.surface_handle;
    /* Surface flags are distinct from the legacy buffer-present flags. */
    present_req.flags = 0u;
    present_req.source = damage;
    present_req.destination.x = 80;
    present_req.destination.y = 80;
    present_req.destination.w = 320;
    present_req.destination.h = 180;
    present_req.damage = &damage;
    present_req.damage_count = 1u;
    present_req.opacity = 255u;
    present_req.filter = VEX_GPU_SURFACE_FILTER_NEAREST;
    present_req.transform = VEX_GPU_SURFACE_TRANSFORM_IDENTITY;
    present_req.reserved0 = 0u;
    present_req.wait_serial = 0u;
    present_req.signal_serial = 0u;
    if (vex_gpu_surface_present(&present_req) != 0 ||
        present_req.signal_serial == 0u) {
        write_text("gpu_test: surface present failed\n");
        (void)vex_gpu_surface_destroy(surface_req.surface_handle);
        (void)vex_gpu_buffer_destroy(create_req.handle);
        return 1;
    }
    wait_req.surface_handle = surface_req.surface_handle;
    wait_req.serial = present_req.signal_serial;
    wait_req.timeout_ticks = 0u;
    if (vex_gpu_surface_wait(&wait_req) != 0) {
        write_text("gpu_test: surface fence wait failed\n");
        (void)vex_gpu_surface_destroy(surface_req.surface_handle);
        (void)vex_gpu_buffer_destroy(create_req.handle);
        return 1;
    }

    if (!surface_capability_test(create_req.handle, surface_req.surface_handle,
                                 (size_t)create_req.size)) {
        write_text("gpu_test: surface capability/mapping failed\n");
        return 1;
    }
    write_text("gpu_test: surface capability transfer/lifetime ok\n");
    write_text("gpu_test: ok\n");
    return 0;
}
