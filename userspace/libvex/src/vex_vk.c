#include "vex_vk.h"

#include "vex_syscall.h"
#include "vex_runtime.h"

static void *vexvk_heap_alloc(size_t size) {
    return (void *)(uintptr_t)vex_syscall1(VEX_SYS_HEAP_ALLOC, (uint64_t)size);
}

static void vexvk_heap_free(void *ptr) {
    vex_heap_req_t req;

    if (ptr != 0) {
        req.ptr = ptr;
        req.size = 0u;
        (void)vex_syscall1(VEX_SYS_HEAP_FREE, (uint64_t)(uintptr_t)&req);
    }
}

static void *vexvk_heap_realloc(void *ptr, size_t size) {
    vex_heap_req_t req;

    req.ptr = ptr;
    req.size = size;
    return (void *)(uintptr_t)vex_syscall1(VEX_SYS_HEAP_REALLOC, (uint64_t)(uintptr_t)&req);
}

struct vexvk_instance {
    vex_gpu_info_t info;
};

struct vexvk_device {
    vexvk_instance_t *instance;
};

struct vexvk_swapchain {
    vexvk_device_t *device;
    uint32_t width;
    uint32_t height;
    uint32_t image_count;
    uint32_t next_image;
    uint32_t *handles;
};

struct vexvk_cmd_buffer {
    vex_gpu_command_t *commands;
    uint32_t count;
    uint32_t capacity;
};

static int vexvk_reserve(vexvk_cmd_buffer_t *cmd, uint32_t extra) {
    uint32_t required;
    vex_gpu_command_t *grown;

    if (cmd == 0) {
        return -1;
    }
    required = cmd->count + extra;
    if (required <= cmd->capacity) {
        return 0;
    }

    cmd->capacity = cmd->capacity == 0u ? 8u : cmd->capacity * 2u;
    if (cmd->capacity < required) {
        cmd->capacity = required;
    }
    grown = (vex_gpu_command_t *)vexvk_heap_realloc(cmd->commands, (size_t)cmd->capacity * sizeof(vex_gpu_command_t));
    if (grown == 0) {
        return -1;
    }
    cmd->commands = grown;
    return 0;
}

int vexvk_create_instance(vexvk_instance_t **out_instance) {
    vexvk_instance_t *instance;

    if (out_instance == 0) {
        return -1;
    }
    instance = (vexvk_instance_t *)vexvk_heap_alloc(sizeof(*instance));
    if (instance == 0) {
        return -1;
    }
    instance->info.backend = 0u;
    if (vex_gpu_query_info(&instance->info) != 0) {
        vexvk_heap_free(instance);
        return -1;
    }
    *out_instance = instance;
    return 0;
}

void vexvk_destroy_instance(vexvk_instance_t *instance) {
    vexvk_heap_free(instance);
}

int vexvk_enumerate_physical_device(vexvk_instance_t *instance, vexvk_physical_device_info_t *out_info) {
    if (instance == 0 || out_info == 0) {
        return -1;
    }
    out_info->backend = instance->info.backend;
    out_info->vendor_id = instance->info.vendor_id;
    out_info->device_id = instance->info.device_id;
    out_info->framebuffer_width = instance->info.framebuffer_width;
    out_info->framebuffer_height = instance->info.framebuffer_height;
    out_info->framebuffer_pitch = instance->info.framebuffer_pitch;
    out_info->queue_count = instance->info.queue_count;
    return 0;
}

int vexvk_create_device(vexvk_instance_t *instance, vexvk_device_t **out_device) {
    vexvk_device_t *device;

    if (instance == 0 || out_device == 0) {
        return -1;
    }
    device = (vexvk_device_t *)vexvk_heap_alloc(sizeof(*device));
    if (device == 0) {
        return -1;
    }
    device->instance = instance;
    *out_device = device;
    return 0;
}

void vexvk_destroy_device(vexvk_device_t *device) {
    vexvk_heap_free(device);
}

int vexvk_create_swapchain(vexvk_device_t *device, uint32_t width, uint32_t height, uint32_t image_count, vexvk_swapchain_t **out_swapchain) {
    vexvk_swapchain_t *swapchain;

    if (device == 0 || out_swapchain == 0 || width == 0u || height == 0u) {
        return -1;
    }
    if (image_count < 2u) {
        image_count = 2u;
    }

    swapchain = (vexvk_swapchain_t *)vexvk_heap_alloc(sizeof(*swapchain));
    if (swapchain == 0) {
        return -1;
    }
    swapchain->handles = (uint32_t *)vexvk_heap_alloc((size_t)image_count * sizeof(uint32_t));
    if (swapchain->handles == 0) {
        vexvk_heap_free(swapchain);
        return -1;
    }
    for (uint32_t i = 0u; i < image_count; ++i) {
        swapchain->handles[i] = 0u;
    }

    swapchain->device = device;
    swapchain->width = width;
    swapchain->height = height;
    swapchain->image_count = image_count;
    swapchain->next_image = 0u;
    for (uint32_t i = 0u; i < image_count; ++i) {
        vex_gpu_buffer_create_req_t req;
        req.width = width;
        req.height = height;
        req.stride = width;
        req.format = VEX_GPU_FORMAT_XRGB8888;
        req.flags = VEX_GPU_MEMORY_CPU_VISIBLE | VEX_GPU_MEMORY_SCANOUT;
        req.handle = 0u;
        req.size = 0u;
        if (vex_gpu_buffer_create(&req) != 0) {
            for (uint32_t j = 0u; j < i; ++j) {
                (void)vex_gpu_buffer_destroy(swapchain->handles[j]);
            }
            vexvk_heap_free(swapchain->handles);
            vexvk_heap_free(swapchain);
            return -1;
        }
        swapchain->handles[i] = req.handle;
    }

    *out_swapchain = swapchain;
    return 0;
}

void vexvk_destroy_swapchain(vexvk_swapchain_t *swapchain) {
    if (swapchain == 0) {
        return;
    }
    if (swapchain->handles != 0) {
        for (uint32_t i = 0u; i < swapchain->image_count; ++i) {
            if (swapchain->handles[i] != 0u) {
                (void)vex_gpu_buffer_destroy(swapchain->handles[i]);
            }
        }
        vexvk_heap_free(swapchain->handles);
    }
    vexvk_heap_free(swapchain);
}

int vexvk_acquire_next_image(vexvk_swapchain_t *swapchain, uint32_t *out_image_index) {
    if (swapchain == 0 || out_image_index == 0 || swapchain->image_count == 0u) {
        return -1;
    }
    *out_image_index = swapchain->next_image;
    swapchain->next_image = (swapchain->next_image + 1u) % swapchain->image_count;
    return 0;
}

int vexvk_create_command_buffer(vexvk_device_t *device, vexvk_cmd_buffer_t **out_cmd) {
    vexvk_cmd_buffer_t *cmd;

    (void)device;
    if (out_cmd == 0) {
        return -1;
    }
    cmd = (vexvk_cmd_buffer_t *)vexvk_heap_alloc(sizeof(*cmd));
    if (cmd == 0) {
        return -1;
    }
    cmd->commands = 0;
    cmd->count = 0u;
    cmd->capacity = 0u;
    *out_cmd = cmd;
    return 0;
}

void vexvk_destroy_command_buffer(vexvk_cmd_buffer_t *cmd) {
    if (cmd == 0) {
        return;
    }
    vexvk_heap_free(cmd->commands);
    vexvk_heap_free(cmd);
}

void vexvk_cmd_reset(vexvk_cmd_buffer_t *cmd) {
    if (cmd != 0) {
        cmd->count = 0u;
    }
}

int vexvk_cmd_clear_color(vexvk_cmd_buffer_t *cmd, uint32_t color) {
    vex_gpu_command_t *command;

    if (vexvk_reserve(cmd, 1u) != 0) {
        return -1;
    }
    command = &cmd->commands[cmd->count++];
    command->op = VEX_GPU_CMD_CLEAR;
    command->reserved0 = 0u;
    command->rect.x = 0;
    command->rect.y = 0;
    command->rect.w = 0;
    command->rect.h = 0;
    command->color = color;
    command->src_width = 0u;
    command->src_height = 0u;
    command->src_stride = 0u;
    command->src_pixels = 0;
    return 0;
}

int vexvk_cmd_fill_rect(vexvk_cmd_buffer_t *cmd, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    vex_gpu_command_t *command;

    if (vexvk_reserve(cmd, 1u) != 0) {
        return -1;
    }
    command = &cmd->commands[cmd->count++];
    command->op = VEX_GPU_CMD_FILL_RECT;
    command->reserved0 = 0u;
    command->rect.x = x;
    command->rect.y = y;
    command->rect.w = w;
    command->rect.h = h;
    command->color = color;
    command->src_width = 0u;
    command->src_height = 0u;
    command->src_stride = 0u;
    command->src_pixels = 0;
    return 0;
}

int vexvk_cmd_blit(vexvk_cmd_buffer_t *cmd, int32_t x, int32_t y, int32_t w, int32_t h, const uint32_t *pixels, uint32_t src_width, uint32_t src_height, uint32_t src_stride) {
    vex_gpu_command_t *command;

    if (pixels == 0 || src_width == 0u || src_height == 0u || src_stride < src_width) {
        return -1;
    }
    if (vexvk_reserve(cmd, 1u) != 0) {
        return -1;
    }
    command = &cmd->commands[cmd->count++];
    command->op = VEX_GPU_CMD_BLIT;
    command->reserved0 = 0u;
    command->rect.x = x;
    command->rect.y = y;
    command->rect.w = w;
    command->rect.h = h;
    command->color = 0u;
    command->src_width = src_width;
    command->src_height = src_height;
    command->src_stride = src_stride;
    command->src_pixels = pixels;
    return 0;
}

int vexvk_queue_submit(vexvk_device_t *device, vexvk_swapchain_t *swapchain, uint32_t image_index, vexvk_cmd_buffer_t *cmd) {
    vex_gpu_submit_req_t req;

    (void)device;
    if (swapchain == 0 || cmd == 0 || image_index >= swapchain->image_count || cmd->count == 0u) {
        return -1;
    }
    req.queue_id = 0u;
    req.buffer_handle = swapchain->handles[image_index];
    req.command_count = cmd->count;
    req.flags = 0u;
    req.commands = cmd->commands;
    return vex_gpu_queue_submit(&req);
}

int vexvk_queue_present(vexvk_device_t *device, vexvk_swapchain_t *swapchain, uint32_t image_index, int32_t dst_x, int32_t dst_y) {
    vex_gpu_present_req_t req;

    (void)device;
    if (swapchain == 0 || image_index >= swapchain->image_count) {
        return -1;
    }
    req.buffer_handle = swapchain->handles[image_index];
    req.flags = VEX_GPU_PRESENT_IMMEDIATE;
    req.dirty.x = 0;
    req.dirty.y = 0;
    req.dirty.w = (int32_t)swapchain->width;
    req.dirty.h = (int32_t)swapchain->height;
    req.dst_x = dst_x;
    req.dst_y = dst_y;
    return vex_gpu_present(&req);
}
