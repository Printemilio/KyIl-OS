#include <unos/smp.h>
#include <unos/gpu.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <unos/debug_log.h>
#include <unos/gpu_driver.h>
#include <unos/hardware_resources.h>
#include <unos/heap.h>
#include <unos/scheduler.h>
#include <unos/process_memory.h>

#define GPU_PCI_CONFIG_ADDRESS 0xCF8u
#define GPU_PCI_CONFIG_DATA    0xCFCu
#define GPU_PCI_CLASS_DISPLAY  0x03u
#define GPU_MAX_QUEUE_COUNT    1u
#define GPU_MAX_DRIVER_COUNT   8u

typedef struct gpu_buffer {
    uint32_t pid;
    uint32_t handle;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t flags;
    uint32_t references;
    size_t size;
    gpu_driver_resource_t resource;
    uint8_t owner_released;
    struct gpu_buffer *next;
} gpu_buffer_t;

typedef struct gpu_queue gpu_queue_t;
typedef struct gpu_queue_waiter gpu_queue_waiter_t;

struct gpu_queue_waiter {
    gpu_queue_t *queue;
    uint64_t serial;
    uint32_t pid;
    uint32_t tid;
    volatile uint8_t woken;
    volatile uint8_t cancelled;
    gpu_queue_waiter_t *next;
};

struct gpu_queue {
    uint32_t pid;
    uint32_t family;
    uint32_t references;
    uint8_t owner_released;
    uint8_t closed;
    uint32_t in_flight;
    uint8_t abandoned;
    uint8_t callback_active;
    uint64_t handle;
    uint64_t submitted_serial;
    uint64_t completed_serial;
    uint64_t failure_serial;
    int32_t failure_status;
    void *driver_queue;
    gpu_queue_waiter_t *waiters;
    gpu_queue_t *next;
};

typedef struct gpu_state {
    struct limine_framebuffer *framebuffer;
    vex_gpu_info_t info;
    gpu_buffer_t *buffers;
    gpu_queue_t *queues;
    const gpu_driver_ops_t *driver;
    void *driver_context;
    uint32_t next_handle;
    uint64_t next_queue_handle;
    uint8_t ready;
} gpu_state_t;

static gpu_state_t gpu_state;
static volatile uint8_t gpu_queue_lock_word;
static const gpu_driver_ops_t *gpu_drivers[GPU_MAX_DRIVER_COUNT];
static uint32_t gpu_driver_count;

static void gpu_queue_lock_acquire(void) {
    while (__atomic_test_and_set(&gpu_queue_lock_word, __ATOMIC_ACQUIRE)) {
        smp_relax();
    }
}

static void gpu_queue_lock_release(void) {
    __atomic_clear(&gpu_queue_lock_word, __ATOMIC_RELEASE);
}

static gpu_queue_t *gpu_queue_find_locked(uint32_t pid, uint64_t handle) {
    for (gpu_queue_t *queue = gpu_state.queues; queue != NULL;
         queue = queue->next) {
        if (queue->pid == pid && queue->handle == handle &&
            queue->owner_released == 0u) return queue;
    }
    return NULL;
}

static gpu_queue_t *gpu_queue_find_any_locked(uint32_t pid, uint64_t handle) {
    for (gpu_queue_t *queue = gpu_state.queues; queue != NULL;
         queue = queue->next) {
        if (queue->pid == pid && queue->handle == handle) return queue;
    }
    return NULL;
}

static void gpu_queue_wake_locked(gpu_queue_t *queue, uint64_t completed,
                                  uint8_t cancel) {
    if (queue == NULL) return;
    for (gpu_queue_waiter_t *waiter = queue->waiters; waiter != NULL;
         waiter = waiter->next) {
        if (cancel != 0u || waiter->serial <= completed) {
            waiter->cancelled = cancel;
            __atomic_store_n(&waiter->woken,1u,__ATOMIC_RELEASE);
            process_wake(waiter->tid);
        }
    }
}

static void gpu_queue_unlink_waiter_locked(gpu_queue_t *queue,
                                           gpu_queue_waiter_t *waiter) {
    gpu_queue_waiter_t **link;
    if (queue == NULL || waiter == NULL) return;
    link = &queue->waiters;
    while (*link != NULL && *link != waiter) link = &(*link)->next;
    if (*link == waiter) {
        *link = waiter->next;
        waiter->next = NULL;
    }
}

static int gpu_queue_unlink_if_dead_locked(gpu_queue_t *queue) {
    gpu_queue_t **link = &gpu_state.queues;
    if (queue == NULL || queue->references != 0u ||
        queue->owner_released == 0u) return 0;
    while (*link != NULL && *link != queue) link = &(*link)->next;
    if (*link != queue) return 0;
    *link = queue->next;
    queue->next = NULL;
    return 1;
}

static void gpu_queue_free(gpu_queue_t *queue) {
    if (queue == NULL) return;
    if (gpu_state.driver != NULL && queue->driver_queue != NULL) {
        gpu_state.driver->queue_destroy(gpu_state.driver_context,
                                        queue->driver_queue);
    }
    kfree(queue);
}

static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t function,
                           uint8_t offset) {
    return hardware_pci_read32(bus,device,function,offset);
}

static uint64_t pci_bar(uint8_t bus, uint8_t device, uint8_t function,
                        uint8_t bar) {
    const uint8_t offset = (uint8_t)(0x10u + bar * 4u);
    const uint32_t low = pci_read32(bus, device, function, offset);

    if ((low & 0x1u) != 0u) {
        return (uint64_t)(low & ~0x3u);
    }
    if ((low & 0x6u) == 0x4u) {
        const uint32_t high = pci_read32(bus, device, function,
                                         (uint8_t)(offset + 4u));
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
            const uint16_t vendor = (uint16_t)pci_read32(
                (uint8_t)bus, device, 0u, 0x00u);
            uint8_t function_count = 1u;

            if (vendor == 0xffffu) {
                continue;
            }
            if ((pci_read32((uint8_t)bus, device, 0u, 0x0cu) &
                 0x00800000u) != 0u) {
                function_count = 8u;
            }
            for (uint8_t function = 0u;
                 function < function_count; ++function) {
                const uint32_t id_reg = pci_read32(
                    (uint8_t)bus, device, function, 0x00u);
                const uint16_t function_vendor = (uint16_t)id_reg;
                const uint16_t function_device = (uint16_t)(id_reg >> 16u);
                const uint32_t class_reg = pci_read32(
                    (uint8_t)bus, device, function, 0x08u);
                const uint8_t class_code = (uint8_t)(class_reg >> 24u);

                if (function_vendor == 0xffffu ||
                    class_code != GPU_PCI_CLASS_DISPLAY) {
                    continue;
                }
                info->vendor_id = function_vendor;
                info->device_id = function_device;
                info->mmio_base = pci_bar((uint8_t)bus, device,
                                          function, 0u);
                info->mmio_size = 0x1000u;
                return;
            }
        }
    }
}

int gpu_driver_register(const gpu_driver_ops_t *driver) {
    if (driver == NULL ||
        driver->abi_version != GPU_DRIVER_ABI_VERSION ||
        driver->name == NULL || driver->probe == NULL ||
        driver->start == NULL || driver->buffer_create == NULL ||
        driver->buffer_destroy == NULL || driver->buffer_clear == NULL ||
        driver->buffer_fill_rect == NULL || driver->buffer_blit == NULL ||
        driver->buffer_composite == NULL || driver->present == NULL ||
        driver->present_cpu == NULL || driver->queue_create == NULL ||
        driver->queue_destroy == NULL ||
        driver->queue_submission_begin == NULL ||
        driver->queue_submission_complete == NULL) {
        return -1;
    }
    for (uint32_t i = 0u; i < gpu_driver_count; ++i) {
        if (gpu_drivers[i] == driver) {
            return 0;
        }
    }
    if (gpu_driver_count >= GPU_MAX_DRIVER_COUNT) {
        return -1;
    }
    gpu_drivers[gpu_driver_count++] = driver;
    return 0;
}

static int gpu_select_driver(const gpu_driver_probe_info_t *probe) {
    uint32_t attempted = 0u;

    while (attempted != ((1u << gpu_driver_count) - 1u)) {
        const gpu_driver_ops_t *candidate = NULL;
        uint32_t candidate_index = 0u;

        for (uint32_t i = 0u; i < gpu_driver_count; ++i) {
            if ((attempted & (1u << i)) != 0u ||
                !gpu_drivers[i]->probe(probe)) {
                continue;
            }
            if (candidate == NULL ||
                gpu_drivers[i]->priority > candidate->priority) {
                candidate = gpu_drivers[i];
                candidate_index = i;
            }
        }
        if (candidate == NULL) {
            return -1;
        }
        attempted |= 1u << candidate_index;
        if (candidate->start(probe, &gpu_state.driver_context) == 0) {
            gpu_state.driver = candidate;
            return 0;
        }
        gpu_state.driver_context = NULL;
    }
    return -1;
}

static gpu_buffer_t *gpu_find_buffer_raw(uint32_t pid, uint32_t handle) {
    for (gpu_buffer_t *buffer = gpu_state.buffers;
         buffer != NULL; buffer = buffer->next) {
        if (buffer->pid == pid && buffer->handle == handle) {
            return buffer;
        }
    }
    return NULL;
}

static gpu_buffer_t *gpu_find_buffer(uint32_t pid, uint32_t handle) {
    gpu_buffer_t *buffer = gpu_find_buffer_raw(pid, handle);

    return buffer != NULL && !buffer->owner_released ? buffer : NULL;
}

static void gpu_free_buffer(gpu_buffer_t *buffer) {
    gpu_buffer_t **link;

    if (buffer == NULL) {
        return;
    }
    link = &gpu_state.buffers;
    while (*link != NULL && *link != buffer) {
        link = &(*link)->next;
    }
    if (*link == buffer) {
        *link = buffer->next;
    }
    if (gpu_state.driver != NULL) {
        gpu_state.driver->buffer_destroy(gpu_state.driver_context,
                                         &buffer->resource);
    }
    kfree(buffer);
}

static void gpu_fill_buffer_view(const gpu_buffer_t *buffer,
                                 gpu_buffer_view_t *view) {
    if (buffer == NULL || view == NULL) {
        return;
    }
    view->pixels = buffer->resource.cpu_mapping;
    view->width = buffer->width;
    view->height = buffer->height;
    view->stride = buffer->resource.stride;
    view->format = buffer->format;
    view->flags = buffer->flags;
    view->size = buffer->resource.size;
    view->shared_backing_id = buffer->resource.shared_backing_id;
}

void gpu_init(struct limine_framebuffer *framebuffer) {
    gpu_driver_probe_info_t probe;

    if (gpu_state.ready && gpu_state.framebuffer == framebuffer) {
        return;
    }
    memset(&gpu_state, 0, sizeof(gpu_state));
    gpu_state.framebuffer = framebuffer;
    gpu_state.next_handle = 1u;
    gpu_state.next_queue_handle = 1u;
    gpu_state.info.backend = VEX_GPU_BACKEND_NONE;
    gpu_state.info.framebuffer_width = framebuffer == NULL
        ? 0u : (uint32_t)framebuffer->width;
    gpu_state.info.framebuffer_height = framebuffer == NULL
        ? 0u : (uint32_t)framebuffer->height;
    gpu_state.info.framebuffer_pitch = framebuffer == NULL
        ? 0u : (uint32_t)framebuffer->pitch;
    gpu_state.info.framebuffer_bpp = framebuffer == NULL
        ? 0u : (uint32_t)framebuffer->bpp;
    gpu_state.info.framebuffer_address = 0u;
    gpu_state.info.queue_count = 0u;
    gpu_scan_display_device(&gpu_state.info);

    gpu_driver_software_register();
    memset(&probe, 0, sizeof(probe));
    probe.framebuffer = framebuffer;
    probe.vendor_id = (uint16_t)gpu_state.info.vendor_id;
    probe.device_id = (uint16_t)gpu_state.info.device_id;
    probe.mmio_base = gpu_state.info.mmio_base;
    probe.mmio_size = gpu_state.info.mmio_size;
    if (gpu_select_driver(&probe) != 0) {
        debug_log_puts("gpu: no usable driver\n");
        return;
    }

    gpu_state.info.backend = gpu_state.driver->backend;
    gpu_state.info.queue_count = GPU_MAX_QUEUE_COUNT;
    gpu_state.ready = 1u;
    debug_log_puts("gpu: driver ");
    debug_log_puts(gpu_state.driver->name);
    debug_log_puts("\n");
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

int gpu_query_caps(vex_gpu_caps_t *out) {
    size_t name_length = 0u;

    if (!gpu_state.ready || gpu_state.driver == NULL || out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->struct_size = sizeof(*out);
    out->abi_version = VEX_GPU_CAPS_ABI_VERSION;
    out->backend = gpu_state.driver->backend;
    out->capabilities = gpu_state.driver->capabilities;
    while (gpu_state.driver->name[name_length] != '\0' &&
           name_length + 1u < sizeof(out->driver_name)) {
        out->driver_name[name_length] = gpu_state.driver->name[name_length];
        ++name_length;
    }
    out->driver_name[name_length] = '\0';
    return 0;
}

const char *gpu_driver_name(void) {
    return gpu_state.driver == NULL ? "none" : gpu_state.driver->name;
}

uint64_t gpu_driver_capabilities(void) {
    return gpu_state.driver == NULL ? 0u : gpu_state.driver->capabilities;
}

int gpu_buffer_create(uint32_t pid, vex_gpu_buffer_create_req_t *req) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    gpu_buffer_t *buffer;
    gpu_driver_buffer_desc_t desc;
    size_t minimum_size;

    if (!gpu_state.ready || gpu_state.driver == NULL || req == NULL ||
        req->width == 0u || req->height == 0u) {
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
    if (req->stride < req->width ||
        (size_t)req->stride > SIZE_MAX / (size_t)req->height ||
        (size_t)req->stride * (size_t)req->height >
            SIZE_MAX / sizeof(uint32_t)) {
        return -1;
    }
    minimum_size = (size_t)req->stride * (size_t)req->height *
                   sizeof(uint32_t);
    if (req->size == 0u) {
        req->size = minimum_size;
    }
    if (req->size < minimum_size) {
        return -1;
    }

    buffer = kmalloc(sizeof(*buffer));
    if (buffer == NULL) {
        return -1;
    }
    memset(buffer, 0, sizeof(*buffer));
    memset(&desc, 0, sizeof(desc));
    desc.width = req->width;
    desc.height = req->height;
    desc.stride = req->stride;
    desc.format = req->format;
    desc.flags = req->flags;
    desc.size = req->size;
    if (gpu_state.driver->buffer_create(gpu_state.driver_context, &desc,
                                        &buffer->resource) != 0) {
        kfree(buffer);
        return -1;
    }
    if (((req->flags & VEX_GPU_MEMORY_CPU_VISIBLE) != 0u &&
         buffer->resource.cpu_mapping == NULL) ||
        (buffer->resource.cpu_mapping != NULL &&
         (buffer->resource.stride < req->width ||
          buffer->resource.size < minimum_size))) {
        gpu_state.driver->buffer_destroy(gpu_state.driver_context,
                                         &buffer->resource);
        kfree(buffer);
        return -1;
    }
    if (buffer->resource.stride == 0u) {
        buffer->resource.stride = req->stride;
    }
    if (buffer->resource.size == 0u) {
        buffer->resource.size = req->size;
    }

    buffer->pid = pid;
    buffer->handle = gpu_state.next_handle++;
    if (buffer->handle == 0u) {
        buffer->handle = gpu_state.next_handle++;
    }
    buffer->width = req->width;
    buffer->height = req->height;
    buffer->stride = buffer->resource.stride;
    buffer->format = req->format;
    buffer->flags = req->flags;
    buffer->references = 1u;
    buffer->size = buffer->resource.size;
    buffer->next = gpu_state.buffers;
    gpu_state.buffers = buffer;

    req->handle = buffer->handle;
    req->stride = buffer->stride;
    req->size = buffer->size;
    return 0;
}

int gpu_buffer_destroy(uint32_t pid, uint32_t handle) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    gpu_buffer_t *buffer = gpu_find_buffer(pid, handle);

    if (buffer == NULL || buffer->references == 0u) {
        return -1;
    }
    buffer->owner_released = 1u;
    --buffer->references;
    if (buffer->references == 0u) {
        gpu_free_buffer(buffer);
    }
    return 0;
}

int gpu_buffer_view(uint32_t pid, uint32_t handle,
                    gpu_buffer_view_t *view_out) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    gpu_buffer_t *buffer = gpu_find_buffer_raw(pid, handle);

    if (buffer == NULL || view_out == NULL) {
        return -1;
    }
    gpu_fill_buffer_view(buffer, view_out);
    return 0;
}

int gpu_buffer_acquire(uint32_t pid, uint32_t handle,
                       gpu_buffer_view_t *view_out) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    gpu_buffer_t *buffer = gpu_find_buffer(pid, handle);

    if (buffer == NULL || buffer->references == UINT32_MAX) {
        return -1;
    }
    ++buffer->references;
    if (view_out != NULL) {
        gpu_fill_buffer_view(buffer, view_out);
    }
    return 0;
}

void gpu_buffer_release(uint32_t pid, uint32_t handle) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    gpu_buffer_t *buffer = gpu_find_buffer_raw(pid, handle);

    if (buffer == NULL || buffer->references == 0u) {
        return;
    }
    --buffer->references;
    if (buffer->references == 0u) {
        gpu_free_buffer(buffer);
    }
}

void gpu_reap_pid(uint32_t pid) {
    {kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    gpu_buffer_t *buffer = gpu_state.buffers;

    while (buffer != NULL) {
        gpu_buffer_t *next = buffer->next;

        if (buffer->pid == pid && !buffer->owner_released) {
            buffer->owner_released = 1u;
            if (buffer->references != 0u) {
                --buffer->references;
            }
            if (buffer->references == 0u) {
                gpu_free_buffer(buffer);
            }
        }
        buffer = next;
    }
    }

    {
        gpu_queue_t *garbage = NULL;
        gpu_queue_lock_acquire();
        for (gpu_queue_t *queue = gpu_state.queues; queue != NULL; ) {
            gpu_queue_t *next = queue->next;
            if (queue->pid == pid && queue->owner_released == 0u) {
                queue->owner_released = 1u;
                queue->closed = 1u;
                queue->abandoned = 1u;
                gpu_queue_wake_locked(queue, queue->completed_serial, 1u);
                if (queue->in_flight != 0u) {
                    const uint32_t in_flight = queue->in_flight;
                    queue->in_flight = 0u;
                    if (queue->submitted_serial > queue->completed_serial) {
                        queue->completed_serial = queue->submitted_serial;
                        queue->failure_serial = queue->submitted_serial;
                        queue->failure_status = -1;
                    }
                    if (in_flight <= queue->references)
                        queue->references -= in_flight;
                    else
                        queue->references = 0u;
                }
                if (queue->references != 0u) --queue->references;
                if (gpu_queue_unlink_if_dead_locked(queue)) {
                    queue->next = garbage;
                    garbage = queue;
                }
            }
            queue = next;
        }
        gpu_queue_lock_release();
        while (garbage != NULL) {
            gpu_queue_t *next = garbage->next;
            garbage->next = NULL;
            gpu_queue_free(garbage);
            garbage = next;
        }
    }
}

int gpu_queue_submit(uint32_t pid, const vex_gpu_submit_req_t *req,
                     const vex_gpu_command_t *commands) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    gpu_buffer_t *buffer;

    if (!gpu_state.ready || gpu_state.driver == NULL || req == NULL ||
        commands == NULL || req->queue_id >= GPU_MAX_QUEUE_COUNT) {
        return -1;
    }
    buffer = gpu_find_buffer(pid, req->buffer_handle);
    if (buffer == NULL) {
        return -1;
    }

    for (uint32_t i = 0u; i < req->command_count; ++i) {
        const vex_gpu_command_t *command = &commands[i];
        int result;

        switch (command->op) {
        case VEX_GPU_CMD_CLEAR:
            result = gpu_state.driver->buffer_clear(
                gpu_state.driver_context, &buffer->resource,
                buffer->width, buffer->height, command->color);
            break;
        case VEX_GPU_CMD_FILL_RECT:
            result = gpu_state.driver->buffer_fill_rect(
                gpu_state.driver_context, &buffer->resource,
                buffer->width, buffer->height, &command->rect,
                command->color);
            break;
        case VEX_GPU_CMD_BLIT:
            result = gpu_state.driver->buffer_blit(
                gpu_state.driver_context, &buffer->resource,
                buffer->width, buffer->height, command);
            break;
        default:
            return -1;
        }
        if (result != 0) {
            return -1;
        }
    }
    return 0;
}

int gpu_queue_create(uint32_t pid, uint32_t family, uint64_t *handle_out) {
    gpu_queue_t *queue;
    void *driver_queue = NULL;
    if (!gpu_state.ready || gpu_state.driver == NULL || pid == 0u ||
        family >= gpu_state.info.queue_count || handle_out == NULL) return -1;
    *handle_out = 0u;
    queue = (gpu_queue_t *)kmalloc(sizeof(*queue));
    if (queue == NULL) return -1;
    memset(queue, 0, sizeof(*queue));
    if (gpu_state.driver->queue_create(gpu_state.driver_context, family,
                                       &driver_queue) != 0 ||
        driver_queue == NULL) {
        kfree(queue);
        return -1;
    }
    queue->pid = pid;
    queue->family = family;
    queue->references = 1u;
    queue->driver_queue = driver_queue;
    gpu_queue_lock_acquire();
    do {
        queue->handle = gpu_state.next_queue_handle++;
        if (queue->handle == 0u) queue->handle = gpu_state.next_queue_handle++;
    } while (gpu_queue_find_any_locked(pid, queue->handle) != NULL);
    queue->next = gpu_state.queues;
    gpu_state.queues = queue;
    gpu_queue_lock_release();
    *handle_out = queue->handle;
    return 0;
}

int gpu_queue_destroy(uint32_t pid, uint64_t handle) {
    gpu_queue_t *queue;
    int free_now;
    if (pid == 0u || handle == 0u) return -1;
    gpu_queue_lock_acquire();
    queue = gpu_queue_find_locked(pid, handle);
    if (queue == NULL || queue->references == 0u) {
        gpu_queue_lock_release();
        return -1;
    }
    queue->owner_released = 1u;
    queue->closed = 1u;
    gpu_queue_wake_locked(queue, queue->completed_serial, 1u);
    --queue->references;
    free_now = gpu_queue_unlink_if_dead_locked(queue);
    gpu_queue_lock_release();
    if (free_now) gpu_queue_free(queue);
    return 0;
}

int gpu_queue_submit_begin(uint32_t pid, uint64_t handle,
                           uint64_t *serial_out) {
    gpu_queue_t *queue;
    uint64_t serial;
    int callback_result;
    if (pid == 0u || handle == 0u || serial_out == NULL) return -1;
    *serial_out = 0u;
    gpu_queue_lock_acquire();
    queue = gpu_queue_find_locked(pid, handle);
    if (queue == NULL || queue->closed != 0u || queue->callback_active != 0u ||
        queue->in_flight == UINT32_MAX ||
        queue->failure_status != 0 || queue->submitted_serial == UINT64_MAX ||
        queue->references > UINT32_MAX - 2u) {
        gpu_queue_lock_release();
        return -1;
    }
    serial = queue->submitted_serial + 1u;
    queue->callback_active = 1u;
    ++queue->in_flight;
    queue->submitted_serial = serial;
    queue->references += 2u; /* in-flight point plus syscall callback pin */
    gpu_queue_lock_release();

    callback_result = gpu_state.driver->queue_submission_begin(
        gpu_state.driver_context, queue->driver_queue, serial);
    gpu_queue_lock_acquire();
    if (callback_result != 0 || queue->closed != 0u ||
        queue->abandoned != 0u) {
        int free_now;
        if (queue->abandoned == 0u) {
            if (queue->submitted_serial == serial &&
                queue->completed_serial < serial)
                queue->submitted_serial = serial - 1u;
            if (queue->in_flight != 0u) {
                --queue->in_flight;
                if (queue->references != 0u) --queue->references;
            }
        }
        queue->callback_active = 0u;
        if (queue->references != 0u) --queue->references; /* callback pin */
        free_now = gpu_queue_unlink_if_dead_locked(queue);
        gpu_queue_lock_release();
        if (free_now) gpu_queue_free(queue);
        return -1;
    }
    queue->callback_active = 0u;
    if (queue->references != 0u) --queue->references; /* callback pin */
    gpu_queue_lock_release();
    *serial_out = serial;
    return 0;
}

int gpu_queue_submit_complete(uint32_t pid, uint64_t handle,
                              uint64_t serial, int32_t status) {
    gpu_queue_t *queue;
    gpu_driver_timeline_point_t point;
    int callback_result;
    int free_now;
    if (pid == 0u || handle == 0u || serial == 0u) return -1;
    gpu_queue_lock_acquire();
    queue = gpu_queue_find_any_locked(pid, handle);
    if (queue == NULL || queue->in_flight == 0u ||
        queue->completed_serial == UINT64_MAX ||
        queue->completed_serial + 1u != serial ||
        serial > queue->submitted_serial || queue->callback_active != 0u ||
        queue->abandoned != 0u || queue->references == UINT32_MAX) {
        gpu_queue_lock_release();
        return -1;
    }
    queue->callback_active = 1u;
    ++queue->references;
    gpu_queue_lock_release();

    point.serial = serial;
    point.status = status;
    point.reserved0 = 0u;
    callback_result = gpu_state.driver->queue_submission_complete(
        gpu_state.driver_context, queue->driver_queue, &point);

    gpu_queue_lock_acquire();
    if (queue->callback_active != 0u) queue->callback_active = 0u;
    if (queue->completed_serial == UINT64_MAX ||
        queue->completed_serial + 1u != serial || queue->in_flight == 0u ||
        queue->abandoned != 0u) {
        if (queue->references != 0u) --queue->references; /* callback pin */
        free_now = gpu_queue_unlink_if_dead_locked(queue);
        gpu_queue_lock_release();
        if (free_now) gpu_queue_free(queue);
        return -1;
    }
    queue->completed_serial = serial;
    --queue->in_flight;
    if (status != 0 || callback_result != 0) {
        queue->failure_serial = serial;
        queue->failure_status = status != 0 ? status : -1;
    }
    gpu_queue_wake_locked(queue, serial, 0u);
    if (queue->references >= 2u) queue->references -= 2u;
    else queue->references = 0u;
    free_now = gpu_queue_unlink_if_dead_locked(queue);
    gpu_queue_lock_release();
    if (free_now) gpu_queue_free(queue);
    return callback_result == 0 ? 0 : -1;
}

/* Timeline points may be submitted ahead of completion. The kernel owns the
   ordered serial/lifetime contract; the software and future hardware drivers
   are notified at begin and completion in queue order. */

int gpu_queue_fence_query(uint32_t pid, uint64_t handle, uint64_t serial) {
    gpu_queue_t *queue;
    int result;
    gpu_queue_lock_acquire();
    queue = gpu_queue_find_locked(pid, handle);
    if (queue == NULL || serial == 0u || serial > queue->submitted_serial) {
        gpu_queue_lock_release();
        return -1;
    }
    if (serial > queue->completed_serial) result = 1;
    else if (queue->failure_status != 0 && serial >= queue->failure_serial)
        result = -2;
    else result = 0;
    gpu_queue_lock_release();
    return result;
}

void gpu_thread_stopped(uint32_t tid) {
    gpu_queue_waiter_t *waiters=NULL;gpu_queue_t *garbage=NULL;
    gpu_queue_lock_acquire();
    for(gpu_queue_t *queue=gpu_state.queues;queue;) {
        gpu_queue_t *next=queue->next;
        gpu_queue_waiter_t **link=&queue->waiters;
        while(*link){gpu_queue_waiter_t *w=*link;
            if(w->tid!=tid){link=&w->next;continue;}
            *link=w->next;w->next=waiters;waiters=w;
            if(queue->references)--queue->references;
        }
        if(gpu_queue_unlink_if_dead_locked(queue)){queue->next=garbage;garbage=queue;}
        queue=next;
    }
    gpu_queue_lock_release();
    while(waiters){gpu_queue_waiter_t *next=waiters->next;kfree(waiters);waiters=next;}
    while(garbage){gpu_queue_t *next=garbage->next;gpu_queue_free(garbage);garbage=next;}
}
int gpu_queue_fence_wait(uint32_t pid, uint64_t handle, uint64_t serial,
                         uint64_t timeout_ticks) {
    gpu_queue_waiter_t *waiter;
    gpu_queue_t *queue;
    uint64_t start_tick;
    int result = 0;
    if (pid == 0u || serial == 0u) return -1;
    waiter = (gpu_queue_waiter_t *)kmalloc(sizeof(*waiter));
    if (waiter == NULL) return -1;
    memset(waiter, 0, sizeof(*waiter));
    gpu_queue_lock_acquire();
    queue = gpu_queue_find_locked(pid, handle);
    if (queue == NULL || serial > queue->submitted_serial) {
        gpu_queue_lock_release();
        kfree(waiter);
        return -1;
    }
    if (serial <= queue->completed_serial) {
        result = queue->failure_status != 0 && serial >= queue->failure_serial
            ? -2 : 0;
        gpu_queue_lock_release();
        kfree(waiter);
        return result;
    }
    if (queue->references == UINT32_MAX) {
        gpu_queue_lock_release();
        kfree(waiter);
        return -1;
    }
    ++queue->references;
    waiter->queue = queue;
    waiter->serial = serial;
    waiter->pid = scheduler_current_pid();
    waiter->tid = scheduler_current_tid();
    waiter->next = queue->waiters;
    queue->waiters = waiter;
    start_tick = scheduler_ticks_now();
    gpu_queue_lock_release();

    while (waiter->woken == 0u && waiter->cancelled == 0u &&
           scheduler_process_alive(waiter->pid)) {
        if (timeout_ticks == 0u) {
            process_wait_event(&waiter->woken);
        } else {
            const uint64_t now = scheduler_ticks_now();
            const uint64_t elapsed = now - start_tick;
            if (elapsed >= timeout_ticks) {
                result = -3;
                break;
            }
            process_wait_event_timeout(&waiter->woken, timeout_ticks - elapsed);
        }
    }

    gpu_queue_lock_acquire();
    gpu_queue_unlink_waiter_locked(queue, waiter);
    if (waiter->cancelled != 0u || queue->closed != 0u ||
        !scheduler_process_alive(waiter->pid)) {
        result = -1;
    } else if (serial <= queue->completed_serial) {
        result = queue->failure_status != 0 && serial >= queue->failure_serial
            ? -2 : 0;
    } else if (result == 0) {
        result = -3;
    }
    if (queue->references != 0u) --queue->references;
    {
        const int free_now = gpu_queue_unlink_if_dead_locked(queue);
        gpu_queue_lock_release();
        kfree(waiter);
        if (free_now) gpu_queue_free(queue);
    }
    return result;
}

int gpu_kernel_buffer_create(uint32_t width, uint32_t height, uint32_t stride,
                             uint32_t flags, uint32_t *handle_out) {
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

uint32_t *gpu_kernel_buffer_map(uint32_t handle, uint32_t *stride_out,
                                uint32_t *width_out, uint32_t *height_out) {
    gpu_buffer_t *buffer = gpu_find_buffer(0u, handle);

    if (buffer == NULL) {
        return NULL;
    }
    if (stride_out != NULL) {
        *stride_out = buffer->resource.stride;
    }
    if (width_out != NULL) {
        *width_out = buffer->width;
    }
    if (height_out != NULL) {
        *height_out = buffer->height;
    }
    return buffer->resource.cpu_mapping;
}

int gpu_buffer_composite(uint32_t destination_pid,
                         uint32_t destination_handle,
                         uint32_t source_pid, uint32_t source_handle,
                         const gpu_composite_request_t *request) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    gpu_buffer_t *destination;
    gpu_buffer_t *source;
    gpu_driver_composite_req_t driver_request;

    if (!gpu_state.ready || gpu_state.driver == NULL || request == NULL) {
        return -1;
    }
    destination = gpu_find_buffer_raw(destination_pid, destination_handle);
    source = gpu_find_buffer_raw(source_pid, source_handle);
    if (destination == NULL || source == NULL) {
        return -1;
    }
    memset(&driver_request, 0, sizeof(driver_request));
    driver_request.source = &source->resource;
    driver_request.destination = &destination->resource;
    driver_request.source_width = source->width;
    driver_request.source_height = source->height;
    driver_request.destination_width = destination->width;
    driver_request.destination_height = destination->height;
    driver_request.source_rect = request->source;
    driver_request.destination_rect = request->destination;
    driver_request.clip = request->clip;
    driver_request.opacity = request->opacity;
    driver_request.alpha_mode = request->alpha_mode;
    driver_request.filter = request->filter;
    driver_request.transform = request->transform;
    return gpu_state.driver->buffer_composite(gpu_state.driver_context,
                                               &driver_request);
}

int gpu_kernel_present(uint32_t handle, const vex_rect_t *rects,
                       size_t rect_count) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    gpu_buffer_t *buffer = gpu_find_buffer(0u, handle);

    if (buffer == NULL || gpu_state.driver == NULL) {
        return -1;
    }
    return gpu_state.driver->present(gpu_state.driver_context,
                                     &buffer->resource,
                                     buffer->width, buffer->height,
                                     rects, rect_count);
}

void gpu_present_scene(const uint32_t *pixels, uint32_t width,
                       const vex_rect_t *rects, size_t rect_count) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    if (!gpu_state.ready || gpu_state.driver == NULL ||
        gpu_state.framebuffer == NULL) {
        return;
    }
    (void)gpu_state.driver->present_cpu(
        gpu_state.driver_context, pixels, width,
        (uint32_t)gpu_state.framebuffer->height, width,
        rects, rect_count);
}

