#ifndef UNOS_GPU_DRIVER_H
#define UNOS_GPU_DRIVER_H

#include <limine.h>
#include <stddef.h>
#include <stdint.h>

#include <unos/vex_abi.h>

#define GPU_DRIVER_ABI_VERSION 2u
#define GPU_DRIVER_NAME_MAX 32u

#define GPU_DRIVER_CAP_CPU_MAP          VEX_GPU_CAP_CPU_MAP
#define GPU_DRIVER_CAP_SCANOUT          VEX_GPU_CAP_SCANOUT
#define GPU_DRIVER_CAP_DAMAGE           VEX_GPU_CAP_DAMAGE
#define GPU_DRIVER_CAP_COMPOSITE        VEX_GPU_CAP_COMPOSITE
#define GPU_DRIVER_CAP_LINEAR_FILTER    VEX_GPU_CAP_LINEAR_FILTER
#define GPU_DRIVER_CAP_TRANSFORMS       VEX_GPU_CAP_TRANSFORMS
#define GPU_DRIVER_CAP_DEVICE_LOCAL     VEX_GPU_CAP_DEVICE_LOCAL
#define GPU_DRIVER_CAP_ASYNC_SUBMIT     VEX_GPU_CAP_ASYNC_SUBMIT
#define GPU_DRIVER_CAP_TIMELINE_FENCES  VEX_GPU_CAP_TIMELINE_FENCES
#define GPU_DRIVER_CAP_VULKAN_1_3       VEX_GPU_CAP_VULKAN_1_3

typedef struct gpu_driver_probe_info {
    struct limine_framebuffer *framebuffer;
    uint16_t vendor_id;
    uint16_t device_id;
    uint64_t mmio_base;
    uint64_t mmio_size;
} gpu_driver_probe_info_t;

typedef struct gpu_driver_buffer_desc {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t flags;
    size_t size;
} gpu_driver_buffer_desc_t;

/*
 * Driver resources are deliberately opaque to the GPU object manager.
 * cpu_mapping is optional: device-local Vulkan images normally leave it NULL.
 */
typedef struct gpu_driver_resource {
    void *cookie;
    uint32_t *cpu_mapping;
    uint32_t stride;
    size_t size;
    /* Refcounted process_memory backing for software/CPU-visible resources;
     * zero means this driver resource cannot be mapped by a process. */
    uint64_t shared_backing_id;
} gpu_driver_resource_t;

/* Queue handles and timeline points are owned by the kernel object manager.
 * Drivers receive an opaque per-queue cookie and validate ordered submission
 * and completion notifications; renderer command interpretation stays in
 * userspace for the software backend. */
typedef struct gpu_driver_timeline_point {
    uint64_t serial;
    int32_t status;
    uint32_t reserved0;
} gpu_driver_timeline_point_t;

typedef struct gpu_driver_composite_req {
    const gpu_driver_resource_t *source;
    gpu_driver_resource_t *destination;
    uint32_t source_width;
    uint32_t source_height;
    uint32_t destination_width;
    uint32_t destination_height;
    vex_rect_t source_rect;
    vex_rect_t destination_rect;
    vex_rect_t clip;
    uint32_t opacity;
    uint32_t alpha_mode;
    uint32_t filter;
    uint32_t transform;
} gpu_driver_composite_req_t;

typedef struct gpu_driver_ops {
    uint32_t abi_version;
    uint32_t backend;
    uint32_t priority;
    uint32_t reserved0;
    uint64_t capabilities;
    const char *name;

    int (*probe)(const gpu_driver_probe_info_t *probe);
    int (*start)(const gpu_driver_probe_info_t *probe, void **context_out);
    void (*stop)(void *context);

    int (*buffer_create)(void *context, const gpu_driver_buffer_desc_t *desc,
                         gpu_driver_resource_t *resource_out);
    void (*buffer_destroy)(void *context, gpu_driver_resource_t *resource);
    int (*buffer_clear)(void *context, gpu_driver_resource_t *resource,
                        uint32_t width, uint32_t height, uint32_t color);
    int (*buffer_fill_rect)(void *context, gpu_driver_resource_t *resource,
                            uint32_t width, uint32_t height,
                            const vex_rect_t *rect, uint32_t color);
    int (*buffer_blit)(void *context, gpu_driver_resource_t *resource,
                       uint32_t width, uint32_t height,
                       const vex_gpu_command_t *command);
    int (*buffer_composite)(void *context,
                            const gpu_driver_composite_req_t *request);
    int (*present)(void *context, const gpu_driver_resource_t *resource,
                   uint32_t width, uint32_t height,
                   const vex_rect_t *rects, size_t rect_count);
    int (*present_cpu)(void *context, const uint32_t *pixels,
                       uint32_t width, uint32_t height, uint32_t stride,
                       const vex_rect_t *rects, size_t rect_count);

    int (*queue_create)(void *context, uint32_t family, void **queue_out);
    void (*queue_destroy)(void *context, void *queue);
    int (*queue_submission_begin)(void *context, void *queue,
                                  uint64_t serial);
    int (*queue_submission_complete)(void *context, void *queue,
                                    const gpu_driver_timeline_point_t *point);
} gpu_driver_ops_t;

/*
 * Built-in drivers register before gpu_init() selects one. A driver is only
 * eligible when probe() and start() both succeed; merely recognizing a PCI ID
 * never makes VexOS falsely advertise hardware Vulkan.
 */
int gpu_driver_register(const gpu_driver_ops_t *driver);
void gpu_driver_software_register(void);

#endif
