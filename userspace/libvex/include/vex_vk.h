#ifndef VEX_VK_H
#define VEX_VK_H

#include <stddef.h>
#include <stdint.h>

#include <unos/vex_abi.h>

typedef struct vexvk_instance vexvk_instance_t;
typedef struct vexvk_device vexvk_device_t;
typedef struct vexvk_swapchain vexvk_swapchain_t;
typedef struct vexvk_cmd_buffer vexvk_cmd_buffer_t;

typedef struct vexvk_physical_device_info {
    uint32_t backend;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint32_t queue_count;
} vexvk_physical_device_info_t;

int vexvk_create_instance(vexvk_instance_t **out_instance);
void vexvk_destroy_instance(vexvk_instance_t *instance);
int vexvk_enumerate_physical_device(vexvk_instance_t *instance, vexvk_physical_device_info_t *out_info);
int vexvk_create_device(vexvk_instance_t *instance, vexvk_device_t **out_device);
void vexvk_destroy_device(vexvk_device_t *device);
int vexvk_create_swapchain(vexvk_device_t *device, uint32_t width, uint32_t height, uint32_t image_count, vexvk_swapchain_t **out_swapchain);
void vexvk_destroy_swapchain(vexvk_swapchain_t *swapchain);
int vexvk_acquire_next_image(vexvk_swapchain_t *swapchain, uint32_t *out_image_index);
int vexvk_create_command_buffer(vexvk_device_t *device, vexvk_cmd_buffer_t **out_cmd);
void vexvk_destroy_command_buffer(vexvk_cmd_buffer_t *cmd);
void vexvk_cmd_reset(vexvk_cmd_buffer_t *cmd);
int vexvk_cmd_clear_color(vexvk_cmd_buffer_t *cmd, uint32_t color);
int vexvk_cmd_fill_rect(vexvk_cmd_buffer_t *cmd, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
int vexvk_cmd_blit(vexvk_cmd_buffer_t *cmd, int32_t x, int32_t y, int32_t w, int32_t h, const uint32_t *pixels, uint32_t src_width, uint32_t src_height, uint32_t src_stride);
int vexvk_queue_submit(vexvk_device_t *device, vexvk_swapchain_t *swapchain, uint32_t image_index, vexvk_cmd_buffer_t *cmd);
int vexvk_queue_present(vexvk_device_t *device, vexvk_swapchain_t *swapchain, uint32_t image_index, int32_t dst_x, int32_t dst_y);

#endif
