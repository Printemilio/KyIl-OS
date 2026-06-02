#ifndef UNOS_GPU_H
#define UNOS_GPU_H

#include <limine.h>
#include <stddef.h>
#include <stdint.h>
#include <unos/vex_abi.h>

void gpu_init(struct limine_framebuffer *framebuffer);
uint8_t gpu_is_ready(void);
int gpu_query_info(vex_gpu_info_t *out);
int gpu_buffer_create(uint32_t pid, vex_gpu_buffer_create_req_t *req);
int gpu_buffer_destroy(uint32_t pid, uint32_t handle);
int gpu_queue_submit(uint32_t pid, const vex_gpu_submit_req_t *req, const vex_gpu_command_t *commands);
int gpu_present_buffer(uint32_t pid, const vex_gpu_present_req_t *req);
void gpu_reap_pid(uint32_t pid);
int gpu_kernel_buffer_create(uint32_t width, uint32_t height, uint32_t stride, uint32_t flags, uint32_t *handle_out);
int gpu_kernel_buffer_destroy(uint32_t handle);
uint32_t *gpu_kernel_buffer_map(uint32_t handle, uint32_t *stride_out, uint32_t *width_out, uint32_t *height_out);
int gpu_kernel_present(uint32_t handle, const vex_rect_t *rects, size_t rect_count);
void gpu_present_scene(const uint32_t *pixels, uint32_t width, const vex_rect_t *rects, size_t rect_count);

#endif
