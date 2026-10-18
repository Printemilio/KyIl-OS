#ifndef UNOS_GPU_H
#define UNOS_GPU_H

#include <limine.h>
#include <stddef.h>
#include <stdint.h>
#include <unos/vex_abi.h>

typedef struct gpu_buffer_view {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t flags;
    size_t size;
    uint64_t shared_backing_id;
} gpu_buffer_view_t;

typedef struct gpu_composite_request {
    vex_rect_t source;
    vex_rect_t destination;
    uint32_t opacity;
    uint32_t alpha_mode;
    uint32_t filter;
    uint32_t transform;
    vex_rect_t clip;
} gpu_composite_request_t;

void gpu_init(struct limine_framebuffer *framebuffer);
uint8_t gpu_is_ready(void);
int gpu_query_info(vex_gpu_info_t *out);
int gpu_query_caps(vex_gpu_caps_t *out);
const char *gpu_driver_name(void);
uint64_t gpu_driver_capabilities(void);
int gpu_buffer_create(uint32_t pid, vex_gpu_buffer_create_req_t *req);
int gpu_buffer_destroy(uint32_t pid, uint32_t handle);
int gpu_buffer_view(uint32_t pid, uint32_t handle, gpu_buffer_view_t *view_out);
int gpu_buffer_acquire(uint32_t pid, uint32_t handle, gpu_buffer_view_t *view_out);
void gpu_buffer_release(uint32_t pid, uint32_t handle);
int gpu_queue_submit(uint32_t pid, const vex_gpu_submit_req_t *req, const vex_gpu_command_t *commands);
int gpu_queue_create(uint32_t pid, uint32_t family, uint64_t *handle_out);
int gpu_queue_destroy(uint32_t pid, uint64_t handle);
int gpu_queue_submit_begin(uint32_t pid, uint64_t handle, uint64_t *serial_out);
int gpu_queue_submit_complete(uint32_t pid, uint64_t handle,
                              uint64_t serial, int32_t status);
int gpu_queue_fence_query(uint32_t pid, uint64_t handle, uint64_t serial);
int gpu_queue_fence_wait(uint32_t pid, uint64_t handle, uint64_t serial,
                         uint64_t timeout_ticks);
void gpu_reap_pid(uint32_t pid);
void gpu_thread_stopped(uint32_t tid);
int gpu_kernel_buffer_create(uint32_t width, uint32_t height, uint32_t stride, uint32_t flags, uint32_t *handle_out);
int gpu_kernel_buffer_destroy(uint32_t handle);
uint32_t *gpu_kernel_buffer_map(uint32_t handle, uint32_t *stride_out, uint32_t *width_out, uint32_t *height_out);
int gpu_buffer_composite(uint32_t destination_pid, uint32_t destination_handle,
                         uint32_t source_pid, uint32_t source_handle,
                         const gpu_composite_request_t *request);
int gpu_kernel_present(uint32_t handle, const vex_rect_t *rects, size_t rect_count);
void gpu_present_scene(const uint32_t *pixels, uint32_t width, const vex_rect_t *rects, size_t rect_count);

#endif
