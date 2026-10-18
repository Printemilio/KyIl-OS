#ifndef UNOS_COMPOSITOR_SURFACE_H
#define UNOS_COMPOSITOR_SURFACE_H

#include <stddef.h>
#include <stdint.h>

#include <unos/message_ipc.h>
#include <unos/vex_abi.h>

/*
 * Compositor-owned surface API.
 *
 * A process-local capability names a logical surface object. The object and
 * its backing have independent identities and references; callers never
 * infer a slot or dereference compositor state from a handle.
 */
typedef uint64_t compositor_surface_handle_t;
typedef uint64_t compositor_surface_identity_t;

#define COMPOSITOR_SURFACE_INVALID 0u
#define COMPOSITOR_SURFACE_DAMAGE_MAX 16u
#define COMPOSITOR_SURFACE_PRESENT_QUEUE_MAX 64u

typedef enum compositor_surface_status {
    COMPOSITOR_SURFACE_OK = 0,
    COMPOSITOR_SURFACE_ERROR_INVALID = -1,
    COMPOSITOR_SURFACE_ERROR_NO_MEMORY = -2,
    COMPOSITOR_SURFACE_ERROR_NOT_FOUND = -3,
    COMPOSITOR_SURFACE_ERROR_ACCESS = -4,
    COMPOSITOR_SURFACE_ERROR_BUSY = -5,
    COMPOSITOR_SURFACE_ERROR_UNSUPPORTED = -6
} compositor_surface_status_t;

typedef enum compositor_surface_format {
    COMPOSITOR_SURFACE_FORMAT_XRGB8888 = VEX_GPU_FORMAT_XRGB8888,
    COMPOSITOR_SURFACE_FORMAT_ARGB8888 = 2u
} compositor_surface_format_t;

typedef enum compositor_surface_alpha {
    COMPOSITOR_SURFACE_ALPHA_OPAQUE = 0u,
    COMPOSITOR_SURFACE_ALPHA_PREMULTIPLIED = 1u,
    COMPOSITOR_SURFACE_ALPHA_STRAIGHT = 2u
} compositor_surface_alpha_t;

typedef enum compositor_surface_filter {
    COMPOSITOR_SURFACE_FILTER_NEAREST = 0u,
    COMPOSITOR_SURFACE_FILTER_LINEAR = 1u
} compositor_surface_filter_t;

typedef enum compositor_surface_transform {
    COMPOSITOR_SURFACE_TRANSFORM_IDENTITY = 0u,
    COMPOSITOR_SURFACE_TRANSFORM_ROTATE_90 = 1u,
    COMPOSITOR_SURFACE_TRANSFORM_ROTATE_180 = 2u,
    COMPOSITOR_SURFACE_TRANSFORM_ROTATE_270 = 3u
} compositor_surface_transform_t;

#define COMPOSITOR_SURFACE_USAGE_CPU_READ      (1u << 0)
#define COMPOSITOR_SURFACE_USAGE_CPU_WRITE     (1u << 1)
#define COMPOSITOR_SURFACE_USAGE_SAMPLED       (1u << 2)
#define COMPOSITOR_SURFACE_USAGE_RENDER_TARGET (1u << 3)
#define COMPOSITOR_SURFACE_USAGE_SCANOUT       (1u << 4)
#define COMPOSITOR_SURFACE_USAGE_PROTECTED     (1u << 5)

#define COMPOSITOR_SURFACE_MAP_READ  (1u << 0)
#define COMPOSITOR_SURFACE_MAP_WRITE (1u << 1)

#define COMPOSITOR_SURFACE_RIGHT_QUERY    (1u << 0)
#define COMPOSITOR_SURFACE_RIGHT_PRESENT  (1u << 1)
#define COMPOSITOR_SURFACE_RIGHT_MAP_READ  (1u << 2)
#define COMPOSITOR_SURFACE_RIGHT_MAP_WRITE (1u << 3)
#define COMPOSITOR_SURFACE_RIGHT_RESIZE   (1u << 4)
#define COMPOSITOR_SURFACE_RIGHT_EXPORT   (1u << 5)
#define COMPOSITOR_SURFACE_RIGHT_COMPOSE  (1u << 6)
#define COMPOSITOR_SURFACE_RIGHT_ALL (COMPOSITOR_SURFACE_RIGHT_QUERY | \
    COMPOSITOR_SURFACE_RIGHT_PRESENT | COMPOSITOR_SURFACE_RIGHT_MAP_READ | \
    COMPOSITOR_SURFACE_RIGHT_MAP_WRITE | COMPOSITOR_SURFACE_RIGHT_RESIZE | \
    COMPOSITOR_SURFACE_RIGHT_EXPORT | COMPOSITOR_SURFACE_RIGHT_COMPOSE)

typedef enum compositor_surface_backing_type {
    COMPOSITOR_SURFACE_BACKING_CPU = 1,
    COMPOSITOR_SURFACE_BACKING_DEVICE = 2
} compositor_surface_backing_type_t;

#define COMPOSITOR_SURFACE_RESIZE_PRESERVE_CONTENTS (1u << 0)
#define COMPOSITOR_SURFACE_PRESENT_DISCARD_OLD      (1u << 0)

typedef struct compositor_surface_desc {
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t alpha_mode;
    uint32_t usage;
    uint32_t reserved;
} compositor_surface_desc_t;

typedef struct compositor_surface_info {
    compositor_surface_desc_t desc;
    compositor_surface_identity_t object_identity;
    uint64_t backing_identity;
    compositor_surface_backing_type_t backing_type;
    uint64_t shared_backing_id; /* kernel-only identity; never a user handle */
    size_t backing_size;
    uint32_t allocation_width;
    uint32_t allocation_height;
    uint32_t stride;
    uint32_t owner_pid;
    uint32_t capability_rights; /* caller-local capability rights */
    uint64_t submitted_serial;
    uint64_t completed_serial;
} compositor_surface_info_t;

typedef struct compositor_surface_mapping {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t access;
} compositor_surface_mapping_t;

typedef struct compositor_surface_present_info {
    const vex_rect_t *damage;
    uint32_t damage_count;
    uint32_t flags;
    uint64_t wait_serial;
} compositor_surface_present_info_t;

typedef struct compositor_target {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t backend_pid;
    uint32_t backend_handle;
    uint32_t reserved;
} compositor_target_t;

typedef struct compositor_layer {
    vex_rect_t source;
    vex_rect_t destination;
    uint32_t opacity;
    uint32_t filter;
    uint32_t transform;
    uint32_t reserved;
    /* Kernel composition scissor; zero extent means the complete target.
     * Not part of the native userspace Surface ABI. */
    vex_rect_t clip;
} compositor_layer_t;

/* Opaque kernel-only completion token for queued compositor work. */
typedef struct compositor_surface_present_completion {
    void *token;
    vex_rect_t damage;
    int32_t status;
    uint32_t reserved;
} compositor_surface_present_completion_t;

void compositor_surface_system_init(void);
int compositor_surface_create(uint32_t owner_pid,
                              const compositor_surface_desc_t *desc,
                              compositor_surface_handle_t *handle_out);
int compositor_surface_import_gpu_buffer(uint32_t owner_pid, uint32_t gpu_buffer_handle,
                                         uint32_t alpha_mode, uint32_t usage,
                                         compositor_surface_handle_t *handle_out);
int compositor_surface_retain(uint32_t owner_pid, compositor_surface_handle_t handle);
int compositor_surface_release(uint32_t owner_pid, compositor_surface_handle_t handle);
int compositor_surface_export(uint32_t owner_pid, compositor_surface_handle_t handle,
                              uint32_t rights, vex_message_attachment_t *attachment_out);
int compositor_surface_import_attachment(uint32_t owner_pid,
                                          const vex_message_attachment_t *attachment,
                                          uint32_t requested_rights,
                                          compositor_surface_handle_t *handle_out);
int compositor_surface_attachment_valid(const vex_message_attachment_t *attachment);
void compositor_surface_attachment_retain(void *object);
void compositor_surface_attachment_release(void *object);
int compositor_surface_pin(uint32_t owner_pid, compositor_surface_handle_t handle,
                           compositor_surface_identity_t *identity_out);
void compositor_surface_unpin(compositor_surface_identity_t identity);
int compositor_surface_authorize_mapping(uint32_t owner_pid,
                                         compositor_surface_handle_t handle,
                                         uint32_t access, uint64_t offset,
                                         size_t length, uint64_t *shared_id_out,
                                         compositor_surface_identity_t *pin_out);
int compositor_surface_resize(uint32_t owner_pid, compositor_surface_handle_t handle,
                              uint32_t width, uint32_t height, uint32_t flags);
int compositor_surface_map(uint32_t owner_pid, compositor_surface_handle_t handle,
                           uint32_t access, compositor_surface_mapping_t *mapping_out);
int compositor_surface_unmap(uint32_t owner_pid, compositor_surface_handle_t handle,
                             uint32_t access);
int compositor_surface_query(uint32_t owner_pid, compositor_surface_handle_t handle,
                             compositor_surface_info_t *info_out);
int compositor_surface_present(uint32_t owner_pid, compositor_surface_handle_t handle,
                               const compositor_surface_present_info_t *present,
                               uint64_t *serial_out);
int compositor_surface_queue_present(uint32_t owner_pid,
                                     compositor_surface_handle_t handle,
                                     const compositor_surface_present_info_t *present,
                                     const compositor_layer_t *layer,
                                     uint64_t image_available_event,
                                     uint64_t swapchain_available_event,
                                     uint64_t *serial_out);
uint32_t compositor_surface_process_queued(
    const compositor_target_t *target,
    compositor_surface_present_completion_t *completions,
    uint32_t capacity);
void compositor_surface_finish_queued(
    compositor_surface_present_completion_t *completion);
int compositor_surface_consume_damage(uint32_t owner_pid,
                                      compositor_surface_handle_t handle,
                                      vex_rect_t *rects, uint32_t capacity,
                                      uint32_t *count_out);
int compositor_surface_is_complete(uint32_t owner_pid,
                                   compositor_surface_handle_t handle,
                                   uint64_t serial);
int compositor_surface_wait(uint32_t owner_pid, compositor_surface_handle_t handle,
                            uint64_t serial, uint64_t timeout_ticks);
int compositor_surface_complete(compositor_surface_identity_t identity,
                                uint64_t serial);
int compositor_surface_compose(uint32_t owner_pid,
                               compositor_surface_handle_t handle,
                               const compositor_target_t *target,
                               const compositor_layer_t *layer,
                               vex_rect_t *destination_damage_out);
void compositor_surface_reap_pid(uint32_t owner_pid);
void compositor_surface_cancel_pid(uint32_t owner_pid);
void compositor_surface_thread_stopped(uint32_t tid);
int compositor_surface_self_test(void);

#endif
