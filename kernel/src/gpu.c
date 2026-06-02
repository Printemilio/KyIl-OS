#include <unos/gpu.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/debug_log.h>
#include <unos/heap.h>

#define GPU_PCI_CONFIG_ADDRESS 0xCF8u
#define GPU_PCI_CONFIG_DATA    0xCFCu
#define GPU_PCI_CLASS_DISPLAY  0x03u
#define GPU_MAX_QUEUE_COUNT    1u

typedef struct gpu_buffer {
    uint32_t pid;
    uint32_t handle;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t flags;
    size_t size;
    uint32_t *pixels;
    struct gpu_buffer *next;
} gpu_buffer_t;

typedef struct gpu_state {
    struct limine_framebuffer *framebuffer;
    vex_gpu_info_t info;
    gpu_buffer_t *buffers;
    uint32_t next_handle;
    uint8_t ready;
} gpu_state_t;

static gpu_state_t gpu_state;

static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    const uint32_t address = 0x80000000u
        | ((uint32_t)bus << 16u)
        | ((uint32_t)device << 11u)
        | ((uint32_t)function << 8u)
        | (offset & 0xfcu);
    outl(GPU_PCI_CONFIG_ADDRESS, address);
    return inl(GPU_PCI_CONFIG_DATA);
}

static uint64_t pci_bar(uint8_t bus, uint8_t device, uint8_t function, uint8_t bar) {
    const uint8_t offset = (uint8_t)(0x10u + bar * 4u);
    const uint32_t low = pci_read32(bus, device, function, offset);

    if ((low & 0x1u) != 0u) {
        return (uint64_t)(low & ~0x3u);
    }
    if ((low & 0x6u) == 0x4u) {
        const uint32_t high = pci_read32(bus, device, function, (uint8_t)(offset + 4u));
        return ((uint64_t)high << 32u) | (uint64_t)(low & ~0x0fu);
    }

    return (uint64_t)(low & ~0x0fu);
}

static void gpu_scan_display_device(vex_gpu_info_t *info) {
    if (info == NULL) {
        return;
    }

    info->vendor_id = 0xffffu;
    info->device_id = 0xffffu;
    info->mmio_base = 0u;
    info->mmio_size = 0u;

    for (uint16_t bus = 0u; bus < 256u; ++bus) {
        for (uint8_t device = 0u; device < 32u; ++device) {
            const uint16_t vendor = (uint16_t)pci_read32((uint8_t)bus, device, 0u, 0x00u);
            uint8_t function_count = 1u;

            if (vendor == 0xffffu) {
                continue;
            }

            if ((pci_read32((uint8_t)bus, device, 0u, 0x0cu) & 0x00800000u) != 0u) {
                function_count = 8u;
            }

            for (uint8_t function = 0u; function < function_count; ++function) {
                const uint32_t id_reg = pci_read32((uint8_t)bus, device, function, 0x00u);
                const uint16_t fn_vendor = (uint16_t)id_reg;
                const uint16_t fn_device = (uint16_t)(id_reg >> 16u);
                const uint32_t class_reg = pci_read32((uint8_t)bus, device, function, 0x08u);
                const uint8_t class_code = (uint8_t)(class_reg >> 24u);

                if (fn_vendor == 0xffffu || class_code != GPU_PCI_CLASS_DISPLAY) {
                    continue;
                }

                info->vendor_id = fn_vendor;
                info->device_id = fn_device;
                info->mmio_base = pci_bar((uint8_t)bus, device, function, 0u);
                info->mmio_size = 0x1000u;
                return;
            }
        }
    }
}

static gpu_buffer_t *gpu_find_buffer(uint32_t pid, uint32_t handle) {
    for (gpu_buffer_t *buffer = gpu_state.buffers; buffer != NULL; buffer = buffer->next) {
        if (buffer->pid == pid && buffer->handle == handle) {
            return buffer;
        }
    }

    return NULL;
}

static void gpu_clip_rect(const gpu_buffer_t *buffer, const vex_rect_t *input, uint32_t *x0, uint32_t *y0, uint32_t *x1, uint32_t *y1) {
    uint32_t left = 0u;
    uint32_t top = 0u;
    uint32_t right = buffer->width;
    uint32_t bottom = buffer->height;

    if (input != NULL) {
        if (input->w <= 0 || input->h <= 0) {
            right = left;
            bottom = top;
        } else {
            left = input->x < 0 ? 0u : (uint32_t)input->x;
            top = input->y < 0 ? 0u : (uint32_t)input->y;
            right = (uint64_t)(input->x < 0 ? 0 : input->x) + (uint64_t)input->w > buffer->width
                ? buffer->width
                : (uint32_t)(input->x + input->w);
            bottom = (uint64_t)(input->y < 0 ? 0 : input->y) + (uint64_t)input->h > buffer->height
                ? buffer->height
                : (uint32_t)(input->y + input->h);
        }
    }

    *x0 = left;
    *y0 = top;
    *x1 = right;
    *y1 = bottom;
}

static void gpu_fill_rect(gpu_buffer_t *buffer, const vex_rect_t *rect, uint32_t color) {
    uint32_t x0;
    uint32_t y0;
    uint32_t x1;
    uint32_t y1;

    gpu_clip_rect(buffer, rect, &x0, &y0, &x1, &y1);
    for (uint32_t y = y0; y < y1; ++y) {
        uint32_t *row = buffer->pixels + (uint64_t)y * buffer->stride;
        for (uint32_t x = x0; x < x1; ++x) {
            row[x] = color;
        }
    }
}

static void gpu_blit_rect(gpu_buffer_t *buffer, const vex_gpu_command_t *command) {
    uint32_t x0;
    uint32_t y0;
    uint32_t x1;
    uint32_t y1;

    if (command == NULL || command->src_pixels == NULL || command->src_stride == 0u) {
        return;
    }

    gpu_clip_rect(buffer, &command->rect, &x0, &y0, &x1, &y1);
    for (uint32_t y = y0; y < y1; ++y) {
        const uint32_t src_y = (uint32_t)(y - y0);
        if (src_y >= command->src_height) {
            break;
        }

        uint32_t *dst = buffer->pixels + (uint64_t)y * buffer->stride;
        const uint32_t *src = command->src_pixels + (uint64_t)src_y * command->src_stride;
        for (uint32_t x = x0; x < x1; ++x) {
            const uint32_t src_x = (uint32_t)(x - x0);
            if (src_x >= command->src_width) {
                break;
            }
            dst[x] = src[src_x];
        }
    }
}

static void gpu_present_pixels(const uint32_t *pixels, uint32_t width, uint32_t stride, const vex_rect_t *rects, size_t rect_count) {
    if (!gpu_state.ready || gpu_state.framebuffer == NULL || gpu_state.framebuffer->bpp != 32 || pixels == NULL || width == 0u) {
        return;
    }

    if (rect_count == 0u || rects == NULL) {
        vex_rect_t full;
        full.x = 0;
        full.y = 0;
        full.w = (int32_t)gpu_state.framebuffer->width;
        full.h = (int32_t)gpu_state.framebuffer->height;
        rects = &full;
        rect_count = 1u;
    }

    for (size_t i = 0; i < rect_count; ++i) {
        const vex_rect_t *rect = &rects[i];
        uint32_t x0 = rect->x < 0 ? 0u : (uint32_t)rect->x;
        uint32_t y0 = rect->y < 0 ? 0u : (uint32_t)rect->y;
        uint32_t x1 = rect->w <= 0 ? x0 : (uint64_t)x0 + (uint64_t)rect->w > gpu_state.framebuffer->width
            ? gpu_state.framebuffer->width
            : x0 + (uint32_t)rect->w;
        uint32_t y1 = rect->h <= 0 ? y0 : (uint64_t)y0 + (uint64_t)rect->h > gpu_state.framebuffer->height
            ? gpu_state.framebuffer->height
            : y0 + (uint32_t)rect->h;

        for (uint32_t y = y0; y < y1; ++y) {
            uint32_t *dst = (uint32_t *)((uint8_t *)gpu_state.framebuffer->address + (uint64_t)y * gpu_state.framebuffer->pitch);
            const uint32_t *src = pixels + (uint64_t)y * stride;
            memcpy(dst + x0, src + x0, (size_t)(x1 - x0) * sizeof(uint32_t));
        }
    }
}

static void gpu_present_buffer_region(const gpu_buffer_t *buffer, const vex_rect_t *source_rect, int32_t dst_x, int32_t dst_y) {
    uint32_t src_x0;
    uint32_t src_y0;
    uint32_t src_x1;
    uint32_t src_y1;

    if (!gpu_state.ready || gpu_state.framebuffer == NULL || gpu_state.framebuffer->bpp != 32 || buffer == NULL || buffer->pixels == NULL) {
        return;
    }

    gpu_clip_rect(buffer, source_rect, &src_x0, &src_y0, &src_x1, &src_y1);
    if (src_x1 <= src_x0 || src_y1 <= src_y0) {
        return;
    }

    for (uint32_t src_y = src_y0; src_y < src_y1; ++src_y) {
        const int64_t fb_y = (int64_t)dst_y + (int64_t)(src_y - src_y0);

        if (fb_y < 0 || (uint64_t)fb_y >= gpu_state.framebuffer->height) {
            continue;
        }

        const uint32_t *src_row = buffer->pixels + (uint64_t)src_y * buffer->stride;
        uint32_t *dst_row = (uint32_t *)((uint8_t *)gpu_state.framebuffer->address + (uint64_t)fb_y * gpu_state.framebuffer->pitch);

        /* Compute the x range that falls within the framebuffer */
        int64_t fb_x_start = (int64_t)dst_x;
        uint32_t src_col_start = src_x0;
        if (fb_x_start < 0) {
            src_col_start += (uint32_t)(-fb_x_start);
            fb_x_start = 0;
        }
        int64_t fb_x_end = (int64_t)dst_x + (int64_t)(src_x1 - src_x0);
        if (fb_x_end > (int64_t)gpu_state.framebuffer->width) {
            fb_x_end = (int64_t)gpu_state.framebuffer->width;
        }
        if (fb_x_start >= fb_x_end) {
            continue;
        }

        const size_t copy_pixels = (size_t)(fb_x_end - fb_x_start);
        memcpy(dst_row + fb_x_start, src_row + src_col_start, copy_pixels * sizeof(uint32_t));
    }
}

void gpu_init(struct limine_framebuffer *framebuffer) {
    memset(&gpu_state, 0, sizeof(gpu_state));
    gpu_state.framebuffer = framebuffer;
    gpu_state.next_handle = 1u;
    gpu_state.info.backend = framebuffer != NULL ? VEX_GPU_BACKEND_SOFTWARE : VEX_GPU_BACKEND_NONE;
    gpu_state.info.framebuffer_width = framebuffer == NULL ? 0u : (uint32_t)framebuffer->width;
    gpu_state.info.framebuffer_height = framebuffer == NULL ? 0u : (uint32_t)framebuffer->height;
    gpu_state.info.framebuffer_pitch = framebuffer == NULL ? 0u : (uint32_t)framebuffer->pitch;
    gpu_state.info.framebuffer_bpp = framebuffer == NULL ? 0u : (uint32_t)framebuffer->bpp;
    gpu_state.info.framebuffer_address = framebuffer == NULL ? 0u : (uint64_t)(uintptr_t)framebuffer->address;
    gpu_state.info.queue_count = GPU_MAX_QUEUE_COUNT;
    gpu_scan_display_device(&gpu_state.info);
    gpu_state.ready = framebuffer != NULL ? 1u : 0u;

    if (gpu_state.ready) {
        debug_log_puts("gpu: backend ready\n");
    }
}

uint8_t gpu_is_ready(void) {
    return gpu_state.ready;
}

int gpu_query_info(vex_gpu_info_t *out) {
    if (!gpu_state.ready || out == NULL) {
        return -1;
    }

    *out = gpu_state.info;
    return 0;
}

int gpu_buffer_create(uint32_t pid, vex_gpu_buffer_create_req_t *req) {
    gpu_buffer_t *buffer;
    size_t size;

    if (!gpu_state.ready || req == NULL || req->width == 0u || req->height == 0u) {
        return -1;
    }

    if (req->format == 0u) {
        req->format = VEX_GPU_FORMAT_XRGB8888;
    }
    if (req->format != VEX_GPU_FORMAT_XRGB8888) {
        return -1;
    }
    if (req->stride == 0u) {
        req->stride = req->width;
    }
    if (req->stride < req->width) {
        return -1;
    }

    size = req->size;
    if (size == 0u) {
        size = (size_t)req->stride * (size_t)req->height * sizeof(uint32_t);
    }
    if (size < (size_t)req->stride * (size_t)req->height * sizeof(uint32_t)) {
        return -1;
    }

    buffer = kmalloc(sizeof(*buffer));
    if (buffer == NULL) {
        return -1;
    }
    memset(buffer, 0, sizeof(*buffer));

    buffer->pixels = kmalloc(size);
    if (buffer->pixels == NULL) {
        kfree(buffer);
        return -1;
    }
    memset(buffer->pixels, 0, size);

    buffer->pid = pid;
    buffer->handle = gpu_state.next_handle++;
    buffer->width = req->width;
    buffer->height = req->height;
    buffer->stride = req->stride;
    buffer->format = req->format;
    buffer->flags = req->flags;
    buffer->size = size;
    buffer->next = gpu_state.buffers;
    gpu_state.buffers = buffer;

    req->handle = buffer->handle;
    req->size = size;
    return 0;
}

int gpu_buffer_destroy(uint32_t pid, uint32_t handle) {
    gpu_buffer_t **link = &gpu_state.buffers;

    while (*link != NULL) {
        gpu_buffer_t *buffer = *link;
        if (buffer->pid == pid && buffer->handle == handle) {
            *link = buffer->next;
            kfree(buffer->pixels);
            kfree(buffer);
            return 0;
        }
        link = &buffer->next;
    }

    return -1;
}

void gpu_reap_pid(uint32_t pid) {
    gpu_buffer_t **link = &gpu_state.buffers;

    while (*link != NULL) {
        gpu_buffer_t *buffer = *link;
        if (buffer->pid == pid) {
            *link = buffer->next;
            kfree(buffer->pixels);
            kfree(buffer);
            continue;
        }
        link = &buffer->next;
    }
}

int gpu_queue_submit(uint32_t pid, const vex_gpu_submit_req_t *req, const vex_gpu_command_t *commands) {
    gpu_buffer_t *buffer;

    if (!gpu_state.ready || req == NULL || commands == NULL || req->queue_id >= GPU_MAX_QUEUE_COUNT) {
        return -1;
    }

    buffer = gpu_find_buffer(pid, req->buffer_handle);
    if (buffer == NULL) {
        return -1;
    }

    for (uint32_t i = 0u; i < req->command_count; ++i) {
        const vex_gpu_command_t *command = &commands[i];
        switch (command->op) {
        case VEX_GPU_CMD_CLEAR: {
            vex_rect_t full = { 0, 0, (int32_t)buffer->width, (int32_t)buffer->height };
            gpu_fill_rect(buffer, &full, command->color);
            break;
        }
        case VEX_GPU_CMD_FILL_RECT:
            gpu_fill_rect(buffer, &command->rect, command->color);
            break;
        case VEX_GPU_CMD_BLIT:
            gpu_blit_rect(buffer, command);
            break;
        default:
            return -1;
        }
    }

    return 0;
}

int gpu_present_buffer(uint32_t pid, const vex_gpu_present_req_t *req) {
    gpu_buffer_t *buffer;
    vex_rect_t rect;

    if (!gpu_state.ready || req == NULL) {
        return -1;
    }

    buffer = gpu_find_buffer(pid, req->buffer_handle);
    if (buffer == NULL) {
        return -1;
    }

    rect = req->dirty;
    if (rect.w <= 0 || rect.h <= 0) {
        rect.x = 0;
        rect.y = 0;
        rect.w = (int32_t)buffer->width;
        rect.h = (int32_t)buffer->height;
    }
    gpu_present_buffer_region(buffer, &rect, req->dst_x, req->dst_y);
    return 0;
}

int gpu_kernel_buffer_create(uint32_t width, uint32_t height, uint32_t stride, uint32_t flags, uint32_t *handle_out) {
    vex_gpu_buffer_create_req_t req;

    if (handle_out == NULL) {
        return -1;
    }

    memset(&req, 0, sizeof(req));
    req.width = width;
    req.height = height;
    req.stride = stride;
    req.format = VEX_GPU_FORMAT_XRGB8888;
    req.flags = flags;
    if (gpu_buffer_create(0u, &req) != 0) {
        return -1;
    }

    *handle_out = req.handle;
    return 0;
}

int gpu_kernel_buffer_destroy(uint32_t handle) {
    return gpu_buffer_destroy(0u, handle);
}

uint32_t *gpu_kernel_buffer_map(uint32_t handle, uint32_t *stride_out, uint32_t *width_out, uint32_t *height_out) {
    gpu_buffer_t *buffer = gpu_find_buffer(0u, handle);

    if (buffer == NULL) {
        return NULL;
    }
    if (stride_out != NULL) {
        *stride_out = buffer->stride;
    }
    if (width_out != NULL) {
        *width_out = buffer->width;
    }
    if (height_out != NULL) {
        *height_out = buffer->height;
    }
    return buffer->pixels;
}

int gpu_kernel_present(uint32_t handle, const vex_rect_t *rects, size_t rect_count) {
    gpu_buffer_t *buffer = gpu_find_buffer(0u, handle);

    if (buffer == NULL) {
        return -1;
    }

    if (rects == NULL || rect_count == 0u) {
        vex_rect_t full;
        full.x = 0;
        full.y = 0;
        full.w = (int32_t)buffer->width;
        full.h = (int32_t)buffer->height;
        gpu_present_buffer_region(buffer, &full, 0, 0);
        return 0;
    }

    for (size_t i = 0; i < rect_count; ++i) {
        gpu_present_buffer_region(buffer, &rects[i], rects[i].x, rects[i].y);
    }

    return 0;
}

void gpu_present_scene(const uint32_t *pixels, uint32_t width, const vex_rect_t *rects, size_t rect_count) {
    gpu_present_pixels(pixels, width, width, rects, rect_count);
}
