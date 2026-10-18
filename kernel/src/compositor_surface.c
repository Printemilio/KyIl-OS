#include <unos/smp.h>
#include <unos/kernel_guard.h>
#include <unos/compositor_surface.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <unos/gpu.h>
#include <unos/gui.h>
#include <unos/event_counter.h>
#include <unos/heap.h>
#include <unos/random.h>
#include <unos/scheduler.h>
#include <unos/process_memory.h>
#include <unos/memory_diagnostic.h>
#include <unos/debug_log.h>
#include <stdio.h>

#define COMPOSITOR_SURFACE_SLOT_COUNT 256u
#define COMPOSITOR_SURFACE_CAP_COUNT 256u
#define COMPOSITOR_SURFACE_MAX_DIMENSION 4096u
#define COMPOSITOR_SURFACE_MAX_PIXELS (16u * 1024u * 1024u)
#define COMPOSITOR_SURFACE_BACKING_ID_TAG (1ull << 63u)
#define COMPOSITOR_SURFACE_COMPLETION_HISTORY 16u

typedef struct compositor_surface_backing {
    uint64_t identity;
    uint32_t references;
    uint32_t backend_pid;
    uint32_t backend_handle;
    compositor_surface_backing_type_t type;
    uint64_t shared_backing_id;
    size_t size;
    uint8_t imported;
    struct compositor_surface_backing *next;
} compositor_surface_backing_t;

typedef struct compositor_surface_waiter compositor_surface_waiter_t;

typedef struct compositor_surface_slot {
    compositor_surface_identity_t identity;
    compositor_surface_desc_t desc;
    vex_rect_t damage[COMPOSITOR_SURFACE_DAMAGE_MAX];
    compositor_surface_backing_t *backing;
    uint32_t creator_pid;
    uint32_t references;
    uint32_t map_readers;
    uint32_t map_writers;
    uint32_t active_operations;
    uint32_t allocation_width;
    uint32_t allocation_height;
    uint32_t stride;
    uint32_t damage_count;
    uint32_t pending_presents;
    uint64_t submitted_serial;
    uint64_t completed_serial;
    uint64_t completion_serials[COMPOSITOR_SURFACE_COMPLETION_HISTORY];
    int32_t completion_status[COMPOSITOR_SURFACE_COMPLETION_HISTORY];
    compositor_surface_waiter_t *waiters;
    uint8_t destroy_pending;
    uint8_t occupied;
} compositor_surface_slot_t;

typedef struct compositor_surface_capability {
    compositor_surface_handle_t token;
    compositor_surface_slot_t *object;
    uint32_t owner_pid;
    uint32_t rights;
    uint32_t local_refs;
    uint32_t map_readers;
    uint32_t map_writers;
    uint8_t occupied;
} compositor_surface_capability_t;

struct compositor_surface_waiter {
    compositor_surface_waiter_t *next;
    compositor_surface_slot_t *object;
    uint32_t pid;
    uint32_t tid;
    uint64_t serial;
    uint8_t woken;
    uint8_t cancelled;
};

typedef struct compositor_surface_queued_present {
    compositor_surface_slot_t *slot;
    compositor_surface_backing_t *backing;
    compositor_surface_identity_t identity;
    compositor_surface_desc_t desc;
    compositor_layer_t layer;
    uint64_t serial;
    uint64_t image_available_event;
    uint64_t swapchain_available_event;
    struct compositor_surface_queued_present *next;
    int32_t status;
    vex_rect_t damage;
    flow_context_t flow;
} compositor_surface_queued_present_t;

static compositor_surface_slot_t compositor_surfaces[COMPOSITOR_SURFACE_SLOT_COUNT];
static compositor_surface_capability_t compositor_surface_caps[COMPOSITOR_SURFACE_CAP_COUNT];
static compositor_surface_backing_t *compositor_surface_backings;
static uint64_t compositor_surface_next_identity = 1u;
static uint64_t compositor_surface_next_backing_identity = 1u;
static kernel_gate_t compositor_surface_gate=KERNEL_GATE_INIT;
static uint8_t compositor_surface_ready;
static compositor_surface_queued_present_t *compositor_surface_present_head;
static compositor_surface_queued_present_t *compositor_surface_present_tail;
static uint32_t compositor_surface_present_count;

typedef struct surface_guard {kernel_guard_t memory,object;} surface_guard_t;
static surface_guard_t surface_lock(void);
static void surface_unlock(surface_guard_t *guard);
void surface_memory_diagnostic(void) {
    uint64_t surfaces=0,caps=0,backings=0,bytes=0;char line[160];
    surface_guard_t surface_guard=surface_lock();
    /* Called from the syscall diagnostics; do not change refcounts. */
    for(unsigned i=0;i<COMPOSITOR_SURFACE_SLOT_COUNT;i++)surfaces+=compositor_surfaces[i].occupied!=0;
    for(unsigned i=0;i<COMPOSITOR_SURFACE_CAP_COUNT;i++)caps+=compositor_surface_caps[i].occupied!=0;
    for(compositor_surface_backing_t *b=compositor_surface_backings;b;b=b->next){++backings;bytes+=b->size;}
    uint32_t queued=compositor_surface_present_count;
    surface_unlock(&surface_guard);
    snprintf(line,sizeof(line),"memory-category: surface objects=%u caps=%u backings=%u bytes=%u queued=%u\n",
        (unsigned int)surfaces,(unsigned int)caps,(unsigned int)backings,(unsigned int)bytes,queued);
    debug_log_puts(line);
}

static void surface_operation_end(compositor_surface_slot_t *slot);
static void surface_destroy_locked(compositor_surface_slot_t *slot);

static uint64_t surface_new_object_identity(void) {
    if (compositor_surface_next_identity == 0u ||
        (compositor_surface_next_identity & COMPOSITOR_SURFACE_BACKING_ID_TAG) != 0u) {
        return 0u;
    }
    return compositor_surface_next_identity++;
}

static surface_guard_t surface_lock(void) {surface_guard_t g;g.memory=process_memory_begin_update();g.object=kernel_guard_enter_irq_safe(&compositor_surface_gate);return g;}
static void surface_unlock(surface_guard_t *guard) {kernel_guard_leave(&guard->object);process_memory_end_update(&guard->memory);}

static void surface_wake_completed_waiters_locked(compositor_surface_slot_t *slot) {
    if (slot == NULL) {
        return;
    }
    for (compositor_surface_waiter_t *waiter = slot->waiters; waiter != NULL;
         waiter = waiter->next) {
        if (waiter->serial <= slot->completed_serial) {
            __atomic_store_n(&waiter->woken,1u,__ATOMIC_RELEASE);
            process_wake(waiter->tid);
        }
    }
}

static void surface_unlink_waiter_locked(compositor_surface_waiter_t *waiter) {
    compositor_surface_waiter_t **link;
    if (waiter == NULL || waiter->object == NULL) {
        return;
    }
    link = &waiter->object->waiters;
    while (*link != NULL && *link != waiter) {
        link = &(*link)->next;
    }
    if (*link == waiter) {
        *link = waiter->next;
        waiter->next = NULL;
    }
}

static int surface_desc_valid(const compositor_surface_desc_t *desc) {
    size_t pixels;

    if (desc == NULL || desc->width == 0u || desc->height == 0u ||
        desc->width > COMPOSITOR_SURFACE_MAX_DIMENSION ||
        desc->height > COMPOSITOR_SURFACE_MAX_DIMENSION ||
        (size_t)desc->width > SIZE_MAX / (size_t)desc->height) {
        return 0;
    }
    pixels = (size_t)desc->width * (size_t)desc->height;
    if (pixels > COMPOSITOR_SURFACE_MAX_PIXELS) {
        return 0;
    }
    if (desc->format != COMPOSITOR_SURFACE_FORMAT_XRGB8888 &&
        desc->format != COMPOSITOR_SURFACE_FORMAT_ARGB8888) {
        return 0;
    }
    if (desc->alpha_mode > COMPOSITOR_SURFACE_ALPHA_STRAIGHT) {
        return 0;
    }
    if (desc->format == COMPOSITOR_SURFACE_FORMAT_XRGB8888 &&
        desc->alpha_mode != COMPOSITOR_SURFACE_ALPHA_OPAQUE) {
        return 0;
    }
    if ((desc->usage & COMPOSITOR_SURFACE_USAGE_PROTECTED) != 0u &&
        (desc->usage & (COMPOSITOR_SURFACE_USAGE_CPU_READ |
                        COMPOSITOR_SURFACE_USAGE_CPU_WRITE)) != 0u) {
        return 0;
    }
    return 1;
}

static compositor_surface_capability_t *surface_find_cap_locked(
    uint32_t owner_pid, compositor_surface_handle_t token) {
    if (token == 0u || owner_pid == 0u) {
        return NULL;
    }
    for (uint32_t i = 0u; i < COMPOSITOR_SURFACE_CAP_COUNT; ++i) {
        compositor_surface_capability_t *cap = &compositor_surface_caps[i];
        if (cap->occupied && cap->owner_pid == owner_pid && cap->token == token) {
            return cap;
        }
    }
    return NULL;
}

static int surface_cap_allows_locked(uint32_t owner_pid,
                                     compositor_surface_handle_t token,
                                     uint32_t rights,
                                     compositor_surface_slot_t **slot_out,
                                     compositor_surface_capability_t **cap_out) {
    compositor_surface_capability_t *cap = surface_find_cap_locked(owner_pid, token);
    if (cap == NULL || cap->object == NULL || !cap->object->occupied) {
        return 0;
    }
    if ((cap->rights & rights) != rights) {
        return -1;
    }
    if (slot_out != NULL) {
        *slot_out = cap->object;
    }
    if (cap_out != NULL) {
        *cap_out = cap;
    }
    return 1;
}

static compositor_surface_slot_t *surface_find_identity_locked(
    compositor_surface_identity_t identity) {
    if (identity == 0u) {
        return NULL;
    }
    for (uint32_t i = 0u; i < COMPOSITOR_SURFACE_SLOT_COUNT; ++i) {
        if (compositor_surfaces[i].occupied &&
            compositor_surfaces[i].identity == identity) {
            return &compositor_surfaces[i];
        }
    }
    return NULL;
}

static int surface_handle_token_in_use(compositor_surface_handle_t token) {
    for (uint32_t i = 0u; i < COMPOSITOR_SURFACE_CAP_COUNT; ++i) {
        if (compositor_surface_caps[i].occupied &&
            compositor_surface_caps[i].token == token) {
            return 1;
        }
    }
    return 0;
}

static int surface_cap_mint_locked(compositor_surface_slot_t *slot,
                                   uint32_t owner_pid, uint32_t rights,
                                   compositor_surface_handle_t *token_out) {
    compositor_surface_capability_t *cap = NULL;
    compositor_surface_handle_t token;

    if (slot == NULL || owner_pid == 0u || token_out == NULL ||
        rights == 0u || (rights & ~COMPOSITOR_SURFACE_RIGHT_ALL) != 0u ||
        slot->references == UINT32_MAX) {
        return -1;
    }
    for (uint32_t i = 0u; i < COMPOSITOR_SURFACE_CAP_COUNT; ++i) {
        if (!compositor_surface_caps[i].occupied) {
            cap = &compositor_surface_caps[i];
            break;
        }
    }
    if (cap == NULL) {
        return -1;
    }
    do {
        if (kernel_random_try_fill(&token,sizeof(token)) != 0) return -1;
    } while (token == 0u || surface_handle_token_in_use(token));
    memset(cap, 0, sizeof(*cap));
    cap->token = token;
    cap->object = slot;
    cap->owner_pid = owner_pid;
    cap->rights = rights;
    cap->local_refs = 1u;
    cap->occupied = 1u;
    ++slot->references;
    *token_out = token;
    return 0;
}

static uint32_t surface_default_rights(const compositor_surface_slot_t *slot) {
    uint32_t rights = COMPOSITOR_SURFACE_RIGHT_QUERY |
        COMPOSITOR_SURFACE_RIGHT_PRESENT | COMPOSITOR_SURFACE_RIGHT_EXPORT |
        COMPOSITOR_SURFACE_RIGHT_COMPOSE;
    if (slot != NULL && slot->backing != NULL &&
        slot->backing->imported == 0u) {
        rights |= COMPOSITOR_SURFACE_RIGHT_RESIZE;
    }
    if (slot != NULL && slot->backing != NULL &&
        slot->backing->type == COMPOSITOR_SURFACE_BACKING_CPU &&
        slot->backing->shared_backing_id != 0u &&
        (slot->desc.usage & COMPOSITOR_SURFACE_USAGE_PROTECTED) == 0u) {
        if ((slot->desc.usage & COMPOSITOR_SURFACE_USAGE_CPU_READ) != 0u) {
            rights |= COMPOSITOR_SURFACE_RIGHT_MAP_READ;
        }
        if ((slot->desc.usage & COMPOSITOR_SURFACE_USAGE_CPU_WRITE) != 0u) {
            rights |= COMPOSITOR_SURFACE_RIGHT_MAP_WRITE;
        }
    }
    return rights;
}

static compositor_surface_backing_t *surface_backing_create(
    uint32_t backend_pid, uint32_t backend_handle,
    compositor_surface_backing_type_t type, uint8_t imported,
    uint64_t shared_backing_id, size_t size) {
    compositor_surface_backing_t *backing = kmalloc(sizeof(*backing));
    if (backing == NULL || compositor_surface_next_backing_identity == 0u ||
        (compositor_surface_next_backing_identity & COMPOSITOR_SURFACE_BACKING_ID_TAG) != 0u) {
        kfree(backing);
        return NULL;
    }
    memset(backing, 0, sizeof(*backing));
    backing->identity = COMPOSITOR_SURFACE_BACKING_ID_TAG |
        compositor_surface_next_backing_identity++;
    backing->references = 1u;
    backing->backend_pid = backend_pid;
    backing->backend_handle = backend_handle;
    backing->type = type;
    backing->imported = imported;
    backing->shared_backing_id = shared_backing_id;
    backing->size = size;
    backing->next = compositor_surface_backings;
    compositor_surface_backings = backing;
    return backing;
}

static compositor_surface_backing_t *surface_backing_find_locked(
    uint32_t backend_pid, uint32_t backend_handle) {
    for (compositor_surface_backing_t *backing = compositor_surface_backings;
         backing != NULL; backing = backing->next) {
        if (backing->backend_pid == backend_pid &&
            backing->backend_handle == backend_handle) {
            return backing;
        }
    }
    return NULL;
}

static void surface_backing_release_locked(compositor_surface_backing_t *backing) {
    compositor_surface_backing_t **link;
    if (backing == NULL || backing->references == 0u) {
        return;
    }
    if (--backing->references != 0u) {
        return;
    }
    for (link = &compositor_surface_backings; *link != NULL;
         link = &(*link)->next) {
        if (*link == backing) {
            *link = backing->next;
            break;
        }
    }
    if (backing->imported != 0u) {
        gpu_buffer_release(backing->backend_pid, backing->backend_handle);
    } else {
        (void)gpu_kernel_buffer_destroy(backing->backend_handle);
    }
    kfree(backing);
}

static int surface_object_release_locked(compositor_surface_slot_t *slot) {
    if (slot == NULL || slot->references == 0u) {
        return -1;
    }
    --slot->references;
    if (slot->references == 0u) {
        if (slot->map_readers != 0u || slot->map_writers != 0u ||
            slot->active_operations != 0u) {
            slot->destroy_pending = 1u;
        } else {
            surface_destroy_locked(slot);
        }
    }
    return 0;
}

static void surface_destroy_locked(compositor_surface_slot_t *slot) {
    if (slot == NULL) {
        return;
    }
    surface_backing_release_locked(slot->backing);
    memset(slot, 0, sizeof(*slot));
}

static int rect_clip_to_surface(const compositor_surface_slot_t *slot,
                                const vex_rect_t *input, vex_rect_t *output) {
    int64_t x0;
    int64_t y0;
    int64_t x1;
    int64_t y1;

    if (slot == NULL || output == NULL) {
        return 0;
    }
    if (input == NULL || input->w <= 0 || input->h <= 0) {
        output->x = 0;
        output->y = 0;
        output->w = (int32_t)slot->desc.width;
        output->h = (int32_t)slot->desc.height;
        return output->w > 0 && output->h > 0;
    }

    x0 = input->x;
    y0 = input->y;
    x1 = x0 + input->w;
    y1 = y0 + input->h;
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > slot->desc.width) {
        x1 = slot->desc.width;
    }
    if (y1 > slot->desc.height) {
        y1 = slot->desc.height;
    }
    if (x1 <= x0 || y1 <= y0) {
        return 0;
    }
    output->x = (int32_t)x0;
    output->y = (int32_t)y0;
    output->w = (int32_t)(x1 - x0);
    output->h = (int32_t)(y1 - y0);
    return 1;
}

static int rects_touch(const vex_rect_t *a, const vex_rect_t *b) {
    return (int64_t)a->x <= (int64_t)b->x + b->w &&
           (int64_t)a->x + a->w >= b->x &&
           (int64_t)a->y <= (int64_t)b->y + b->h &&
           (int64_t)a->y + a->h >= b->y;
}

static void rect_union(vex_rect_t *dst, const vex_rect_t *src) {
    const int32_t x0 = dst->x < src->x ? dst->x : src->x;
    const int32_t y0 = dst->y < src->y ? dst->y : src->y;
    const int64_t dst_x1 = (int64_t)dst->x + dst->w;
    const int64_t dst_y1 = (int64_t)dst->y + dst->h;
    const int64_t src_x1 = (int64_t)src->x + src->w;
    const int64_t src_y1 = (int64_t)src->y + src->h;
    const int64_t x1 = dst_x1 > src_x1 ? dst_x1 : src_x1;
    const int64_t y1 = dst_y1 > src_y1 ? dst_y1 : src_y1;

    dst->x = x0;
    dst->y = y0;
    dst->w = (int32_t)(x1 - x0);
    dst->h = (int32_t)(y1 - y0);
}

static void surface_add_damage_locked(compositor_surface_slot_t *slot,
                                      const vex_rect_t *damage) {
    vex_rect_t clipped;

    if (!rect_clip_to_surface(slot, damage, &clipped)) {
        return;
    }
    for (uint32_t i = 0u; i < slot->damage_count; ++i) {
        if (rects_touch(&slot->damage[i], &clipped)) {
            rect_union(&slot->damage[i], &clipped);
            return;
        }
    }
    if (slot->damage_count < COMPOSITOR_SURFACE_DAMAGE_MAX) {
        slot->damage[slot->damage_count++] = clipped;
        return;
    }
    slot->damage[0].x = 0;
    slot->damage[0].y = 0;
    slot->damage[0].w = (int32_t)slot->desc.width;
    slot->damage[0].h = (int32_t)slot->desc.height;
    slot->damage_count = 1u;
}

static int surface_allocate_backend(uint32_t width, uint32_t height, uint32_t usage,
                                    uint32_t *handle_out, uint32_t **pixels_out,
                                    uint32_t *stride_out) {
    uint32_t gpu_flags = 0u;
    uint32_t handle = 0u;
    uint32_t *pixels;

    if ((usage & (COMPOSITOR_SURFACE_USAGE_CPU_READ |
                  COMPOSITOR_SURFACE_USAGE_CPU_WRITE)) != 0u) {
        gpu_flags |= VEX_GPU_MEMORY_CPU_VISIBLE;
    }
    if ((usage & COMPOSITOR_SURFACE_USAGE_SCANOUT) != 0u) {
        gpu_flags |= VEX_GPU_MEMORY_SCANOUT;
    }
    if (gpu_kernel_buffer_create(width, height, width, gpu_flags, &handle) != 0) {
        return COMPOSITOR_SURFACE_ERROR_NO_MEMORY;
    }
    pixels = gpu_kernel_buffer_map(handle, stride_out, NULL, NULL);
    if ((gpu_flags & VEX_GPU_MEMORY_CPU_VISIBLE) != 0u && pixels == NULL) {
        (void)gpu_kernel_buffer_destroy(handle);
        return COMPOSITOR_SURFACE_ERROR_NO_MEMORY;
    }
    if (handle_out != NULL) {
        *handle_out = handle;
    }
    if (pixels_out != NULL) {
        *pixels_out = pixels;
    }
    return COMPOSITOR_SURFACE_OK;
}

static uint32_t grow_dimension(uint32_t current, uint32_t requested) {
    uint32_t grown;

    if (requested <= current) {
        return current;
    }
    grown = current + current / 2u;
    if (grown < requested || grown < current) {
        grown = requested;
    }
    if (grown > COMPOSITOR_SURFACE_MAX_DIMENSION) {
        grown = requested;
    }
    return grown;
}

void compositor_surface_system_init(void) {
    memset(compositor_surfaces, 0, sizeof(compositor_surfaces));
    memset(compositor_surface_caps, 0, sizeof(compositor_surface_caps));
    compositor_surface_backings = NULL;
    compositor_surface_next_identity = 1u;
    compositor_surface_next_backing_identity = 1u;
    compositor_surface_gate=(kernel_gate_t)KERNEL_GATE_INIT;
    compositor_surface_ready = 1u;
}

int compositor_surface_create(uint32_t owner_pid,
                              const compositor_surface_desc_t *desc,
                              compositor_surface_handle_t *handle_out) {
    surface_guard_t surface_guard;
    compositor_surface_slot_t *slot = NULL;
    gpu_buffer_view_t created_view;
    uint32_t backend_handle = 0u;
    uint32_t stride = 0u;
    compositor_surface_backing_type_t backing_type;

    if (!compositor_surface_ready || !surface_desc_valid(desc) || handle_out == NULL) {
        return COMPOSITOR_SURFACE_ERROR_INVALID;
    }
    if (surface_allocate_backend(desc->width, desc->height, desc->usage,
                                 &backend_handle, NULL, &stride) != COMPOSITOR_SURFACE_OK) {
        return COMPOSITOR_SURFACE_ERROR_NO_MEMORY;
    }
    if (gpu_buffer_view(0u, backend_handle, &created_view) != 0 ||
        (((desc->usage & (COMPOSITOR_SURFACE_USAGE_CPU_READ |
                          COMPOSITOR_SURFACE_USAGE_CPU_WRITE)) != 0u) &&
         (created_view.shared_backing_id == 0u || created_view.pixels == NULL))) {
        (void)gpu_kernel_buffer_destroy(backend_handle);
        return COMPOSITOR_SURFACE_ERROR_UNSUPPORTED;
    }

    backing_type = created_view.shared_backing_id != 0u &&
        (created_view.flags & VEX_GPU_MEMORY_CPU_VISIBLE) != 0u
        ? COMPOSITOR_SURFACE_BACKING_CPU : COMPOSITOR_SURFACE_BACKING_DEVICE;

    surface_guard=surface_lock();
    for (uint32_t i = 0u; i < COMPOSITOR_SURFACE_SLOT_COUNT; ++i) {
        if (!compositor_surfaces[i].occupied) {
            slot = &compositor_surfaces[i];
            break;
        }
    }
    if (slot == NULL || owner_pid == 0u) {
        surface_unlock(&surface_guard);
        (void)gpu_kernel_buffer_destroy(backend_handle);
        return COMPOSITOR_SURFACE_ERROR_NO_MEMORY;
    }
    memset(slot, 0, sizeof(*slot));
    slot->desc = *desc;
    slot->creator_pid = owner_pid;
    slot->backing = surface_backing_create(0u, backend_handle, backing_type, 0u,
        created_view.shared_backing_id, created_view.size);
    if (slot->backing == NULL) {
        surface_unlock(&surface_guard);
        (void)gpu_kernel_buffer_destroy(backend_handle);
        return COMPOSITOR_SURFACE_ERROR_NO_MEMORY;
    }
    slot->identity = surface_new_object_identity();
    if (slot->identity == 0u) {
        surface_backing_release_locked(slot->backing);
        memset(slot, 0, sizeof(*slot));
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_NO_MEMORY;
    }
    slot->allocation_width = desc->width;
    slot->allocation_height = desc->height;
    slot->stride = stride;
    slot->occupied = 1u;
    if (surface_cap_mint_locked(slot, owner_pid, surface_default_rights(slot),
                                handle_out) != 0) {
        surface_destroy_locked(slot);
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_NO_MEMORY;
    }
    surface_unlock(&surface_guard);
    return COMPOSITOR_SURFACE_OK;
}

int compositor_surface_import_gpu_buffer(uint32_t owner_pid, uint32_t gpu_buffer_handle,
                                         uint32_t alpha_mode, uint32_t usage,
                                         compositor_surface_handle_t *handle_out) {
    surface_guard_t surface_guard;
    gpu_buffer_view_t view;
    compositor_surface_desc_t desc;
    compositor_surface_slot_t *slot = NULL;
    compositor_surface_backing_t *backing;

    if (!compositor_surface_ready || owner_pid == 0u || handle_out == NULL ||
        alpha_mode > COMPOSITOR_SURFACE_ALPHA_STRAIGHT) {
        return COMPOSITOR_SURFACE_ERROR_INVALID;
    }
    if (gpu_buffer_acquire(owner_pid, gpu_buffer_handle, &view) != 0) {
        return COMPOSITOR_SURFACE_ERROR_NOT_FOUND;
    }
    memset(&desc, 0, sizeof(desc));
    desc.width = view.width;
    desc.height = view.height;
    desc.format = view.format;
    desc.alpha_mode = alpha_mode;
    desc.usage = usage;
    if (view.shared_backing_id != 0u &&
        (view.flags & VEX_GPU_MEMORY_CPU_VISIBLE) != 0u) {
        desc.usage |= COMPOSITOR_SURFACE_USAGE_CPU_READ |
                      COMPOSITOR_SURFACE_USAGE_CPU_WRITE;
    }
    if (view.format != VEX_GPU_FORMAT_XRGB8888 ||
        (alpha_mode != COMPOSITOR_SURFACE_ALPHA_OPAQUE &&
         view.format == VEX_GPU_FORMAT_XRGB8888) ||
        !surface_desc_valid(&desc)) {
        gpu_buffer_release(owner_pid, gpu_buffer_handle);
        return COMPOSITOR_SURFACE_ERROR_UNSUPPORTED;
    }

    if ((view.flags & VEX_GPU_MEMORY_CPU_VISIBLE) == 0u) {
        desc.usage &= ~(COMPOSITOR_SURFACE_USAGE_CPU_READ |
                        COMPOSITOR_SURFACE_USAGE_CPU_WRITE);
    }
    surface_guard=surface_lock();
    for (uint32_t i = 0u; i < COMPOSITOR_SURFACE_SLOT_COUNT; ++i) {
        if (!compositor_surfaces[i].occupied) {
            slot = &compositor_surfaces[i];
            break;
        }
    }
    if (slot == NULL) {
        surface_unlock(&surface_guard);
        gpu_buffer_release(owner_pid, gpu_buffer_handle);
        return COMPOSITOR_SURFACE_ERROR_NO_MEMORY;
    }

    backing = surface_backing_find_locked(owner_pid, gpu_buffer_handle);
    if (backing != NULL) {
        if (backing->references == UINT32_MAX) {
            surface_unlock(&surface_guard);
            gpu_buffer_release(owner_pid, gpu_buffer_handle);
            return COMPOSITOR_SURFACE_ERROR_BUSY;
        }
        ++backing->references;
        gpu_buffer_release(owner_pid, gpu_buffer_handle);
    } else {
        backing = surface_backing_create(owner_pid, gpu_buffer_handle,
            (view.shared_backing_id != 0u &&
             (view.flags & VEX_GPU_MEMORY_CPU_VISIBLE) != 0u)
                ? COMPOSITOR_SURFACE_BACKING_CPU
                : COMPOSITOR_SURFACE_BACKING_DEVICE, 1u,
            view.shared_backing_id, view.size);
        if (backing == NULL) {
            surface_unlock(&surface_guard);
            gpu_buffer_release(owner_pid, gpu_buffer_handle);
            return COMPOSITOR_SURFACE_ERROR_NO_MEMORY;
        }
    }
    memset(slot, 0, sizeof(*slot));
    slot->desc = desc;
    slot->creator_pid = owner_pid;
    slot->backing = backing;
    slot->identity = surface_new_object_identity();
    if (slot->identity == 0u) {
        surface_backing_release_locked(backing);
        memset(slot, 0, sizeof(*slot));
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_NO_MEMORY;
    }
    slot->allocation_width = view.width;
    slot->allocation_height = view.height;
    slot->stride = view.stride;
    slot->occupied = 1u;
    if (surface_cap_mint_locked(slot, owner_pid, surface_default_rights(slot),
                                handle_out) != 0) {
        surface_destroy_locked(slot);
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_NO_MEMORY;
    }
    surface_unlock(&surface_guard);
    return COMPOSITOR_SURFACE_OK;
}

int compositor_surface_retain(uint32_t owner_pid, compositor_surface_handle_t handle) {
    surface_guard_t surface_guard;
    compositor_surface_slot_t *slot;
    compositor_surface_capability_t *cap;

    surface_guard=surface_lock();
    if (surface_cap_allows_locked(owner_pid, handle, 0u, &slot, &cap) != 1) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_NOT_FOUND;
    }
    if (slot->destroy_pending) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_ACCESS;
    }
    if (slot->pending_presents != 0u) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_BUSY;
    }
    if (cap->local_refs == UINT32_MAX || slot->references == UINT32_MAX) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_BUSY;
    }
    ++cap->local_refs;
    ++slot->references;
    surface_unlock(&surface_guard);
    return COMPOSITOR_SURFACE_OK;
}

int compositor_surface_release(uint32_t owner_pid, compositor_surface_handle_t handle) {
    surface_guard_t surface_guard;
    compositor_surface_capability_t *cap;
    compositor_surface_slot_t *slot;

    surface_guard=surface_lock();
    cap = surface_find_cap_locked(owner_pid, handle);
    if (cap == NULL) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_NOT_FOUND;
    }
    slot = cap->object;
    if (slot == NULL || !slot->occupied) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_NOT_FOUND;
    }
    /* A mapped capability must keep its last local reference, but mappings
       do not forbid returning an additional retained reference. Otherwise
       create -> retain -> map -> release(creator) leaks that creator ref. */
    if (cap->local_refs == 1u &&
        (cap->map_readers != 0u || cap->map_writers != 0u)) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_BUSY;
    }
    if (cap->local_refs > 1u) {
        --cap->local_refs;
    } else {
        memset(cap, 0, sizeof(*cap));
    }
    (void)surface_object_release_locked(slot);
    surface_unlock(&surface_guard);
    return COMPOSITOR_SURFACE_OK;
}

int compositor_surface_export(uint32_t owner_pid, compositor_surface_handle_t handle,
                              uint32_t rights, vex_message_attachment_t *attachment_out) {
    surface_guard_t surface_guard;
    compositor_surface_capability_t *cap;
    compositor_surface_slot_t *slot;

    if (attachment_out == NULL || rights == 0u ||
        (rights & ~COMPOSITOR_SURFACE_RIGHT_ALL) != 0u) {
        return COMPOSITOR_SURFACE_ERROR_INVALID;
    }
    memset(attachment_out, 0, sizeof(*attachment_out));
    surface_guard=surface_lock();
    cap = surface_find_cap_locked(owner_pid, handle);
    if (cap == NULL || cap->object == NULL || !cap->object->occupied) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_NOT_FOUND;
    }
    if ((cap->rights & COMPOSITOR_SURFACE_RIGHT_EXPORT) == 0u ||
        (rights & cap->rights) != rights || cap->object->destroy_pending) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_ACCESS;
    }
    slot = cap->object;
    if (slot->references == UINT32_MAX) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_BUSY;
    }
    ++slot->references; /* the queued/in-flight attachment owns this ref */
    attachment_out->object = slot;
    attachment_out->rights = rights;
    attachment_out->type = VEX_MESSAGE_ATTACHMENT_SURFACE;
    surface_unlock(&surface_guard);
    return COMPOSITOR_SURFACE_OK;
}

int compositor_surface_attachment_valid(const vex_message_attachment_t *attachment) {
    surface_guard_t surface_guard;
    const uintptr_t address = attachment != NULL
        ? (uintptr_t)attachment->object : 0u;
    const uintptr_t start = (uintptr_t)&compositor_surfaces[0];
    const uintptr_t end = (uintptr_t)&compositor_surfaces[COMPOSITOR_SURFACE_SLOT_COUNT];
    compositor_surface_slot_t *slot;
    int valid = 0;

    if (attachment == NULL || attachment->type != VEX_MESSAGE_ATTACHMENT_SURFACE ||
        address < start || address >= end ||
        (address - start) % sizeof(compositor_surfaces[0]) != 0u ||
        attachment->rights == 0u ||
        (attachment->rights & ~COMPOSITOR_SURFACE_RIGHT_ALL) != 0u) {
        return 0;
    }
    slot = (compositor_surface_slot_t *)attachment->object;
    surface_guard=surface_lock();
    valid = slot->occupied && slot->references != 0u && !slot->destroy_pending;
    surface_unlock(&surface_guard);
    return valid;
}

int compositor_surface_import_attachment(uint32_t owner_pid,
                                          const vex_message_attachment_t *attachment,
                                          uint32_t requested_rights,
                                          compositor_surface_handle_t *handle_out) {
    surface_guard_t surface_guard;
    compositor_surface_slot_t *slot;
    const uintptr_t address = attachment != NULL
        ? (uintptr_t)attachment->object : 0u;
    const uintptr_t start = (uintptr_t)&compositor_surfaces[0];
    const uintptr_t end = (uintptr_t)&compositor_surfaces[COMPOSITOR_SURFACE_SLOT_COUNT];

    if (owner_pid == 0u || handle_out == NULL || attachment == NULL ||
        attachment->type != VEX_MESSAGE_ATTACHMENT_SURFACE ||
        requested_rights == 0u ||
        (requested_rights & ~attachment->rights) != 0u ||
        (requested_rights & ~COMPOSITOR_SURFACE_RIGHT_ALL) != 0u ||
        address < start || address >= end ||
        (address - start) % sizeof(compositor_surfaces[0]) != 0u) {
        return COMPOSITOR_SURFACE_ERROR_INVALID;
    }
    slot = (compositor_surface_slot_t *)attachment->object;
    surface_guard=surface_lock();
    if (!slot->occupied || slot->references == 0u || slot->destroy_pending ||
        slot->backing == NULL) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_NOT_FOUND;
    }
    if (surface_cap_mint_locked(slot, owner_pid, requested_rights,
                                handle_out) != 0) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_NO_MEMORY;
    }
    surface_unlock(&surface_guard);
    return COMPOSITOR_SURFACE_OK;
}

void compositor_surface_attachment_retain(void *object) {
    surface_guard_t surface_guard;
    const uintptr_t address = (uintptr_t)object;
    const uintptr_t start = (uintptr_t)&compositor_surfaces[0];
    const uintptr_t end = (uintptr_t)&compositor_surfaces[COMPOSITOR_SURFACE_SLOT_COUNT];
    compositor_surface_slot_t *slot;
    if (address < start || address >= end ||
        (address - start) % sizeof(compositor_surfaces[0]) != 0u) {
        return;
    }
    slot = (compositor_surface_slot_t *)object;
    surface_guard=surface_lock();
    if (slot->occupied && slot->references != 0u &&
        slot->references != UINT32_MAX) {
        ++slot->references;
    }
    surface_unlock(&surface_guard);
}

void compositor_surface_attachment_release(void *object) {
    surface_guard_t surface_guard;
    const uintptr_t address = (uintptr_t)object;
    const uintptr_t start = (uintptr_t)&compositor_surfaces[0];
    const uintptr_t end = (uintptr_t)&compositor_surfaces[COMPOSITOR_SURFACE_SLOT_COUNT];
    compositor_surface_slot_t *slot;
    if (address < start || address >= end ||
        (address - start) % sizeof(compositor_surfaces[0]) != 0u) {
        return;
    }
    slot = (compositor_surface_slot_t *)object;
    surface_guard=surface_lock();
    if (slot->occupied && slot->references != 0u) {
        (void)surface_object_release_locked(slot);
    }
    surface_unlock(&surface_guard);
}

int compositor_surface_pin(uint32_t owner_pid, compositor_surface_handle_t handle,
                           compositor_surface_identity_t *identity_out) {
    surface_guard_t surface_guard;
    compositor_surface_slot_t *slot;
    if (identity_out == NULL) {
        return COMPOSITOR_SURFACE_ERROR_INVALID;
    }
    surface_guard=surface_lock();
    if (surface_cap_allows_locked(owner_pid, handle,
            COMPOSITOR_SURFACE_RIGHT_COMPOSE, &slot, NULL) != 1 ||
        slot->references == UINT32_MAX) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_ACCESS;
    }
    ++slot->references;
    *identity_out = slot->identity;
    surface_unlock(&surface_guard);
    return COMPOSITOR_SURFACE_OK;
}

void compositor_surface_unpin(compositor_surface_identity_t identity) {
    surface_guard_t surface_guard;
    compositor_surface_slot_t *slot;
    surface_guard=surface_lock();
    slot = surface_find_identity_locked(identity);
    if (slot != NULL) {
        (void)surface_object_release_locked(slot);
    }
    surface_unlock(&surface_guard);
}

int compositor_surface_authorize_mapping(uint32_t owner_pid,
                                         compositor_surface_handle_t handle,
                                         uint32_t access, uint64_t offset,
                                         size_t length, uint64_t *shared_id_out,
                                         compositor_surface_identity_t *pin_out) {
    surface_guard_t surface_guard;
    compositor_surface_slot_t *slot;
    uint32_t required_rights = 0u;
    int cap_result;

    if (shared_id_out == NULL || pin_out == NULL || access == 0u || length == 0u ||
        (access & ~(COMPOSITOR_SURFACE_MAP_READ |
                    COMPOSITOR_SURFACE_MAP_WRITE)) != 0u ||
        (offset & 0xfffu) != 0u ||
        (uint64_t)length > UINT64_MAX - offset) {
        return COMPOSITOR_SURFACE_ERROR_INVALID;
    }
    if ((access & COMPOSITOR_SURFACE_MAP_READ) != 0u) {
        required_rights |= COMPOSITOR_SURFACE_RIGHT_MAP_READ;
    }
    if ((access & COMPOSITOR_SURFACE_MAP_WRITE) != 0u) {
        required_rights |= COMPOSITOR_SURFACE_RIGHT_MAP_WRITE;
    }
    surface_guard=surface_lock();
    cap_result = surface_cap_allows_locked(owner_pid, handle, required_rights,
                                            &slot, NULL);
    if (cap_result != 1) {
        surface_unlock(&surface_guard);
        return cap_result == 0 ? COMPOSITOR_SURFACE_ERROR_NOT_FOUND
                               : COMPOSITOR_SURFACE_ERROR_ACCESS;
    }
    if (slot->destroy_pending || slot->backing == NULL ||
        slot->backing->type != COMPOSITOR_SURFACE_BACKING_CPU ||
        slot->backing->shared_backing_id == 0u ||
        (slot->desc.usage & COMPOSITOR_SURFACE_USAGE_PROTECTED) != 0u ||
        ((access & COMPOSITOR_SURFACE_MAP_READ) != 0u &&
         (slot->desc.usage & COMPOSITOR_SURFACE_USAGE_CPU_READ) == 0u) ||
        ((access & COMPOSITOR_SURFACE_MAP_WRITE) != 0u &&
         (slot->desc.usage & COMPOSITOR_SURFACE_USAGE_CPU_WRITE) == 0u) ||
        offset > slot->backing->size ||
        (uint64_t)length > (uint64_t)slot->backing->size - offset) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_ACCESS;
    }
    if (slot->references == UINT32_MAX) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_BUSY;
    }
    ++slot->references; /* pin through process_memory's backing retain */
    *shared_id_out = slot->backing->shared_backing_id;
    *pin_out = slot->identity;
    surface_unlock(&surface_guard);
    return COMPOSITOR_SURFACE_OK;
}

int compositor_surface_resize(uint32_t owner_pid, compositor_surface_handle_t handle,
                              uint32_t width, uint32_t height, uint32_t flags) {
    surface_guard_t surface_guard;
    compositor_surface_slot_t *slot;
    compositor_surface_capability_t *cap;
    compositor_surface_backing_t *old_backing;
    compositor_surface_backing_t *new_backing;
    compositor_surface_desc_t resized;
    uint32_t old_backend;
    uint32_t new_backend = 0u;
    uint32_t new_stride = 0u;
    uint32_t allocation_width;
    uint32_t allocation_height;
    uint32_t old_width;
    uint32_t old_height;
    uint32_t old_stride;
    uint32_t *old_pixels;
    uint32_t *new_pixels = NULL;
    gpu_buffer_view_t new_view;

    surface_guard=surface_lock();
    {
        const int access = surface_cap_allows_locked(owner_pid, handle,
            COMPOSITOR_SURFACE_RIGHT_RESIZE, &slot, &cap);
        if (access != 1) {
            surface_unlock(&surface_guard);
            return access == 0 ? COMPOSITOR_SURFACE_ERROR_NOT_FOUND
                               : COMPOSITOR_SURFACE_ERROR_ACCESS;
        }
    }
    if (slot->destroy_pending || slot->backing == NULL ||
        slot->backing->imported != 0u) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_ACCESS;
    }
    resized = slot->desc;
    resized.width = width;
    resized.height = height;
    if (!surface_desc_valid(&resized)) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_INVALID;
    }
    if (slot->map_readers != 0u || slot->map_writers != 0u) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_BUSY;
    }
    if (width <= slot->allocation_width && height <= slot->allocation_height) {
        slot->desc.width = width;
        slot->desc.height = height;
        slot->damage_count = 0u;
        surface_add_damage_locked(slot, NULL);
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_OK;
    }

    allocation_width = grow_dimension(slot->allocation_width, width);
    allocation_height = grow_dimension(slot->allocation_height, height);
    if ((size_t)allocation_width * allocation_height > COMPOSITOR_SURFACE_MAX_PIXELS) {
        allocation_width = width;
        allocation_height = height;
    }
    old_backing = slot->backing;
    old_backend = old_backing->backend_handle;
    old_width = slot->desc.width;
    old_height = slot->desc.height;
    old_stride = slot->stride;
    if (old_backing->references == UINT32_MAX) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_BUSY;
    }
    ++old_backing->references; /* keep the old storage alive during reallocation */
    ++slot->active_operations;
    surface_unlock(&surface_guard);

    if (surface_allocate_backend(allocation_width, allocation_height, resized.usage,
                                 &new_backend, &new_pixels, &new_stride) != COMPOSITOR_SURFACE_OK) {
        surface_guard=surface_lock();
        surface_backing_release_locked(old_backing);
        surface_unlock(&surface_guard);
        surface_operation_end(slot);
        return COMPOSITOR_SURFACE_ERROR_NO_MEMORY;
    }
    if ((flags & COMPOSITOR_SURFACE_RESIZE_PRESERVE_CONTENTS) != 0u &&
        new_pixels != NULL) {
        old_pixels = gpu_kernel_buffer_map(old_backend, NULL, NULL, NULL);
        if (old_pixels != NULL) {
            const uint32_t copy_width = old_width < width ? old_width : width;
            const uint32_t copy_height = old_height < height ? old_height : height;

            for (uint32_t y = 0u; y < copy_height; ++y) {
                memcpy(new_pixels + (size_t)y * new_stride,
                       old_pixels + (size_t)y * old_stride,
                       (size_t)copy_width * sizeof(uint32_t));
            }
        }
    }
    if (gpu_buffer_view(0u, new_backend, &new_view) != 0) {
        (void)gpu_kernel_buffer_destroy(new_backend);
        surface_guard=surface_lock();
        surface_backing_release_locked(old_backing);
        surface_unlock(&surface_guard);
        surface_operation_end(slot);
        return COMPOSITOR_SURFACE_ERROR_NO_MEMORY;
    }

    surface_guard=surface_lock();
    if (!slot->occupied || slot->destroy_pending || slot->backing != old_backing ||
        surface_find_cap_locked(owner_pid, handle) != cap ||
        slot->map_readers != 0u || slot->map_writers != 0u) {
        surface_unlock(&surface_guard);
        (void)gpu_kernel_buffer_destroy(new_backend);
        surface_guard=surface_lock();
        surface_backing_release_locked(old_backing);
        surface_unlock(&surface_guard);
        surface_operation_end(slot);
        return COMPOSITOR_SURFACE_ERROR_BUSY;
    }
    new_backing = surface_backing_create(0u, new_backend,
        new_view.shared_backing_id != 0u &&
        (new_view.flags & VEX_GPU_MEMORY_CPU_VISIBLE) != 0u
            ? COMPOSITOR_SURFACE_BACKING_CPU
            : COMPOSITOR_SURFACE_BACKING_DEVICE, 0u,
        new_view.shared_backing_id, new_view.size);
    if (new_backing == NULL) {
        surface_unlock(&surface_guard);
        (void)gpu_kernel_buffer_destroy(new_backend);
        surface_guard=surface_lock();
        surface_backing_release_locked(old_backing);
        surface_unlock(&surface_guard);
        surface_operation_end(slot);
        return COMPOSITOR_SURFACE_ERROR_NO_MEMORY;
    }
    slot->backing = new_backing;
    slot->allocation_width = allocation_width;
    slot->allocation_height = allocation_height;
    slot->stride = new_stride;
    slot->desc.width = width;
    slot->desc.height = height;
    slot->damage_count = 0u;
    surface_add_damage_locked(slot, NULL);
    /* Drop the surface's old backing ref and the temporary resize ref. */
    surface_backing_release_locked(old_backing);
    surface_backing_release_locked(old_backing);
    --slot->active_operations;
    if (slot->destroy_pending && slot->references == 0u &&
        slot->map_readers == 0u && slot->map_writers == 0u &&
        slot->active_operations == 0u) {
        surface_destroy_locked(slot);
    }
    surface_unlock(&surface_guard);
    return COMPOSITOR_SURFACE_OK;
}

int compositor_surface_map(uint32_t owner_pid, compositor_surface_handle_t handle,
                           uint32_t access, compositor_surface_mapping_t *mapping_out) {
    surface_guard_t surface_guard;
    compositor_surface_slot_t *slot;
    compositor_surface_capability_t *cap;
    gpu_buffer_view_t view;
    uint32_t backend_pid;
    uint32_t backend_handle;
    uint32_t required_rights = 0u;

    if (mapping_out == NULL || access == 0u ||
        (access & ~(COMPOSITOR_SURFACE_MAP_READ |
                    COMPOSITOR_SURFACE_MAP_WRITE)) != 0u) {
        return COMPOSITOR_SURFACE_ERROR_INVALID;
    }
    if ((access & COMPOSITOR_SURFACE_MAP_READ) != 0u) {
        required_rights |= COMPOSITOR_SURFACE_RIGHT_MAP_READ;
    }
    if ((access & COMPOSITOR_SURFACE_MAP_WRITE) != 0u) {
        required_rights |= COMPOSITOR_SURFACE_RIGHT_MAP_WRITE;
    }
    surface_guard=surface_lock();
    {
        const int cap_result = surface_cap_allows_locked(owner_pid, handle,
            required_rights, &slot, &cap);
        if (cap_result != 1) {
            surface_unlock(&surface_guard);
            return cap_result == 0 ? COMPOSITOR_SURFACE_ERROR_NOT_FOUND
                                   : COMPOSITOR_SURFACE_ERROR_ACCESS;
        }
    }
    if (slot->destroy_pending || slot->backing == NULL ||
        slot->backing->type != COMPOSITOR_SURFACE_BACKING_CPU ||
        slot->backing->shared_backing_id == 0u ||
        (slot->desc.usage & COMPOSITOR_SURFACE_USAGE_PROTECTED) != 0u ||
        ((access & COMPOSITOR_SURFACE_MAP_READ) != 0u &&
         (slot->desc.usage & COMPOSITOR_SURFACE_USAGE_CPU_READ) == 0u) ||
        ((access & COMPOSITOR_SURFACE_MAP_WRITE) != 0u &&
         (slot->desc.usage & COMPOSITOR_SURFACE_USAGE_CPU_WRITE) == 0u)) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_ACCESS;
    }
    if (cap->map_readers == UINT32_MAX || cap->map_writers == UINT32_MAX ||
        slot->map_readers == UINT32_MAX || slot->map_writers == UINT32_MAX) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_BUSY;
    }
    backend_pid = slot->backing->backend_pid;
    backend_handle = slot->backing->backend_handle;
    if ((access & COMPOSITOR_SURFACE_MAP_READ) != 0u) {
        ++slot->map_readers;
        ++cap->map_readers;
    }
    if ((access & COMPOSITOR_SURFACE_MAP_WRITE) != 0u) {
        ++slot->map_writers;
        ++cap->map_writers;
    }
    mapping_out->width = slot->desc.width;
    mapping_out->height = slot->desc.height;
    mapping_out->stride = slot->stride;
    mapping_out->format = slot->desc.format;
    mapping_out->access = access;
    surface_unlock(&surface_guard);

    if (gpu_buffer_view(backend_pid, backend_handle, &view) != 0 || view.pixels == NULL) {
        (void)compositor_surface_unmap(owner_pid, handle, access);
        return COMPOSITOR_SURFACE_ERROR_UNSUPPORTED;
    }
    mapping_out->pixels = view.pixels;
    return COMPOSITOR_SURFACE_OK;
}

int compositor_surface_unmap(uint32_t owner_pid, compositor_surface_handle_t handle,
                             uint32_t access) {
    surface_guard_t surface_guard;
    compositor_surface_slot_t *slot;
    compositor_surface_capability_t *cap;

    if (access == 0u ||
        (access & ~(COMPOSITOR_SURFACE_MAP_READ |
                    COMPOSITOR_SURFACE_MAP_WRITE)) != 0u) {
        return COMPOSITOR_SURFACE_ERROR_INVALID;
    }
    surface_guard=surface_lock();
    if (surface_cap_allows_locked(owner_pid, handle, 0u, &slot, &cap) != 1) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_NOT_FOUND;
    }
    if (((access & COMPOSITOR_SURFACE_MAP_READ) != 0u &&
         (slot->map_readers == 0u || cap->map_readers == 0u)) ||
        ((access & COMPOSITOR_SURFACE_MAP_WRITE) != 0u &&
         (slot->map_writers == 0u || cap->map_writers == 0u))) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_ACCESS;
    }
    if ((access & COMPOSITOR_SURFACE_MAP_READ) != 0u) {
        --slot->map_readers;
        --cap->map_readers;
    }
    if ((access & COMPOSITOR_SURFACE_MAP_WRITE) != 0u) {
        --slot->map_writers;
        --cap->map_writers;
    }
    if (slot->destroy_pending && slot->references == 0u &&
        slot->map_readers == 0u && slot->map_writers == 0u &&
        slot->active_operations == 0u) {
        surface_destroy_locked(slot);
    }
    surface_unlock(&surface_guard);
    return COMPOSITOR_SURFACE_OK;
}

int compositor_surface_query(uint32_t owner_pid, compositor_surface_handle_t handle,
                             compositor_surface_info_t *info_out) {
    surface_guard_t surface_guard;
    compositor_surface_slot_t *slot;
    compositor_surface_capability_t *cap;

    if (info_out == NULL) {
        return COMPOSITOR_SURFACE_ERROR_INVALID;
    }
    surface_guard=surface_lock();
    {
        const int access = surface_cap_allows_locked(owner_pid, handle,
            COMPOSITOR_SURFACE_RIGHT_QUERY, &slot, &cap);
        if (access != 1) {
            surface_unlock(&surface_guard);
            return access == 0 ? COMPOSITOR_SURFACE_ERROR_NOT_FOUND
                               : COMPOSITOR_SURFACE_ERROR_ACCESS;
        }
    }
    if (slot->backing == NULL) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_ACCESS;
    }
    info_out->desc = slot->desc;
    info_out->object_identity = slot->identity;
    info_out->backing_identity = slot->backing->identity;
    info_out->backing_type = slot->backing->type;
    info_out->shared_backing_id = slot->backing->shared_backing_id;
    info_out->backing_size = slot->backing->size;
    info_out->allocation_width = slot->allocation_width;
    info_out->allocation_height = slot->allocation_height;
    info_out->stride = slot->stride;
    info_out->owner_pid = slot->creator_pid;
    info_out->capability_rights = cap->rights;
    info_out->submitted_serial = slot->submitted_serial;
    info_out->completed_serial = slot->completed_serial;
    surface_unlock(&surface_guard);
    return COMPOSITOR_SURFACE_OK;
}

int compositor_surface_present(uint32_t owner_pid, compositor_surface_handle_t handle,
                               const compositor_surface_present_info_t *present,
                               uint64_t *serial_out) {
    surface_guard_t surface_guard;
    compositor_surface_slot_t *slot;

    if (present != NULL && present->wait_serial != 0u) {
        const int wait_result = compositor_surface_wait(owner_pid, handle,
            present->wait_serial, 0u);
        if (wait_result != COMPOSITOR_SURFACE_OK) {
            return wait_result;
        }
    }

    surface_guard=surface_lock();
    {
        const int access = surface_cap_allows_locked(owner_pid, handle,
            COMPOSITOR_SURFACE_RIGHT_PRESENT, &slot, NULL);
        if (access != 1) {
            surface_unlock(&surface_guard);
            return access == 0 ? COMPOSITOR_SURFACE_ERROR_NOT_FOUND
                               : COMPOSITOR_SURFACE_ERROR_ACCESS;
        }
    }
    if (slot->destroy_pending) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_ACCESS;
    }
    if (present != NULL && present->wait_serial > slot->completed_serial) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_BUSY;
    }
    if (slot->submitted_serial == UINT64_MAX) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_BUSY;
    }
    if (present == NULL || present->damage == NULL || present->damage_count == 0u) {
        surface_add_damage_locked(slot, NULL);
    } else {
        for (uint32_t i = 0u; i < present->damage_count; ++i) {
            surface_add_damage_locked(slot, &present->damage[i]);
        }
    }
    ++slot->submitted_serial;
    /* Submission and completion are separate. The producer/compositor signals
     * completion only after the submitted content is actually consumed. */
    if (serial_out != NULL) {
        *serial_out = slot->submitted_serial;
    }
    surface_unlock(&surface_guard);
    return COMPOSITOR_SURFACE_OK;
}

static int surface_compose_snapshot(const compositor_surface_desc_t *desc,
                                    const compositor_surface_backing_t *backing,
                                    const compositor_target_t *target,
                                    const compositor_layer_t *layer,
                                    vex_rect_t *damage_out) {
    gpu_composite_request_t request;
    vex_rect_t source;
    vex_rect_t destination;
    int64_t x0;
    int64_t y0;
    int64_t x1;
    int64_t y1;

    if (desc == NULL || backing == NULL || target == NULL || layer == NULL ||
        target->format != COMPOSITOR_SURFACE_FORMAT_XRGB8888 ||
        target->stride < target->width || target->backend_handle == 0u ||
        layer->transform != COMPOSITOR_SURFACE_TRANSFORM_IDENTITY ||
        layer->filter > COMPOSITOR_SURFACE_FILTER_LINEAR ||
        layer->opacity > 255u) return COMPOSITOR_SURFACE_ERROR_INVALID;
    source = layer->source;
    if (source.w <= 0 || source.h <= 0) {
        source.x = 0;
        source.y = 0;
        source.w = (int32_t)desc->width;
        source.h = (int32_t)desc->height;
    }
    if (source.x < 0 || source.y < 0 || source.w <= 0 || source.h <= 0 ||
        (uint64_t)(uint32_t)source.x + (uint32_t)source.w > desc->width ||
        (uint64_t)(uint32_t)source.y + (uint32_t)source.h > desc->height ||
        layer->destination.w <= 0 || layer->destination.h <= 0) {
        return COMPOSITOR_SURFACE_ERROR_INVALID;
    }
    destination = layer->destination;
    x0 = destination.x < 0 ? 0 : destination.x;
    y0 = destination.y < 0 ? 0 : destination.y;
    x1 = (int64_t)destination.x + destination.w;
    y1 = (int64_t)destination.y + destination.h;
    if (x1 > target->width) x1 = target->width;
    if (y1 > target->height) y1 = target->height;
    if (x1 <= x0 || y1 <= y0 || layer->opacity == 0u) {
        if (damage_out != NULL) memset(damage_out, 0, sizeof(*damage_out));
        return COMPOSITOR_SURFACE_OK;
    }

    memset(&request, 0, sizeof(request));
    request.source = source;
    request.destination = destination;
    request.opacity = layer->opacity;
    request.alpha_mode = desc->alpha_mode;
    request.filter = layer->filter;
    request.transform = layer->transform;
    if (gpu_buffer_composite(target->backend_pid, target->backend_handle,
            backing->backend_pid, backing->backend_handle, &request) != 0) {
        return COMPOSITOR_SURFACE_ERROR_UNSUPPORTED;
    }
    if (damage_out != NULL) {
        damage_out->x = (int32_t)x0;
        damage_out->y = (int32_t)y0;
        damage_out->w = (int32_t)(x1 - x0);
        damage_out->h = (int32_t)(y1 - y0);
    }
    return COMPOSITOR_SURFACE_OK;
}

int compositor_surface_queue_present(uint32_t owner_pid,
                                     compositor_surface_handle_t handle,
                                     const compositor_surface_present_info_t *present,
                                     const compositor_layer_t *layer,
                                     uint64_t image_available_event,
                                     uint64_t swapchain_available_event,
                                     uint64_t *serial_out) {
    surface_guard_t surface_guard;
    compositor_surface_queued_present_t *job;
    compositor_surface_slot_t *slot;
    compositor_surface_backing_t *backing;
    int wait_result;
    int access;

    if (owner_pid == 0u || present == NULL || layer == NULL || serial_out == NULL ||
        image_available_event == 0u || swapchain_available_event == 0u ||
        image_available_event == swapchain_available_event ||
        present->damage_count > COMPOSITOR_SURFACE_DAMAGE_MAX ||
        (present->damage_count != 0u && present->damage == NULL)) {
        return COMPOSITOR_SURFACE_ERROR_INVALID;
    }
    if (present->wait_serial != 0u) {
        wait_result = compositor_surface_wait(owner_pid, handle,
            present->wait_serial, 0u);
        if (wait_result != COMPOSITOR_SURFACE_OK) return wait_result;
    }
    job = (compositor_surface_queued_present_t *)kmalloc(sizeof(*job));
    if (job == NULL) return COMPOSITOR_SURFACE_ERROR_NO_MEMORY;
    memset(job, 0, sizeof(*job));
    if (vex_event_counter_retain(image_available_event) != 0) {
        kfree(job);
        return COMPOSITOR_SURFACE_ERROR_NOT_FOUND;
    }
    if (vex_event_counter_retain(swapchain_available_event) != 0) {
        (void)vex_event_counter_release(image_available_event);
        kfree(job);
        return COMPOSITOR_SURFACE_ERROR_NOT_FOUND;
    }

    surface_guard=surface_lock();
    access = surface_cap_allows_locked(owner_pid, handle,
        COMPOSITOR_SURFACE_RIGHT_PRESENT | COMPOSITOR_SURFACE_RIGHT_COMPOSE,
        &slot, NULL);
    if (access != 1 || slot->destroy_pending || slot->backing == NULL) {
        surface_unlock(&surface_guard);
        (void)vex_event_counter_release(swapchain_available_event);
        (void)vex_event_counter_release(image_available_event);
        kfree(job);
        return access == 0 ? COMPOSITOR_SURFACE_ERROR_NOT_FOUND
                           : COMPOSITOR_SURFACE_ERROR_ACCESS;
    }
    backing = slot->backing;
    {
        vex_rect_t source = layer->source;
        if (layer->transform != COMPOSITOR_SURFACE_TRANSFORM_IDENTITY ||
            layer->filter > COMPOSITOR_SURFACE_FILTER_LINEAR ||
            layer->opacity > 255u || layer->destination.w <= 0 ||
            layer->destination.h <= 0) {
            surface_unlock(&surface_guard);
            (void)vex_event_counter_release(swapchain_available_event);
            (void)vex_event_counter_release(image_available_event);
            kfree(job);
            return COMPOSITOR_SURFACE_ERROR_INVALID;
        }
        if (source.w <= 0 || source.h <= 0) {
            source.x = 0;
            source.y = 0;
            source.w = (int32_t)slot->desc.width;
            source.h = (int32_t)slot->desc.height;
        }
        if (source.x < 0 || source.y < 0 || source.w <= 0 || source.h <= 0 ||
            (uint64_t)(uint32_t)source.x + (uint32_t)source.w > slot->desc.width ||
            (uint64_t)(uint32_t)source.y + (uint32_t)source.h > slot->desc.height) {
            surface_unlock(&surface_guard);
            (void)vex_event_counter_release(swapchain_available_event);
            (void)vex_event_counter_release(image_available_event);
            kfree(job);
            return COMPOSITOR_SURFACE_ERROR_INVALID;
        }
    }
    if (compositor_surface_present_count >= COMPOSITOR_SURFACE_PRESENT_QUEUE_MAX ||
        slot->submitted_serial == UINT64_MAX || slot->references == UINT32_MAX ||
        slot->active_operations == UINT32_MAX || backing->references == UINT32_MAX ||
        slot->pending_presents == UINT32_MAX) {
        surface_unlock(&surface_guard);
        (void)vex_event_counter_release(swapchain_available_event);
        (void)vex_event_counter_release(image_available_event);
        kfree(job);
        return COMPOSITOR_SURFACE_ERROR_BUSY;
    }
    if (present->wait_serial > slot->completed_serial) {
        surface_unlock(&surface_guard);
        (void)vex_event_counter_release(swapchain_available_event);
        (void)vex_event_counter_release(image_available_event);
        kfree(job);
        return COMPOSITOR_SURFACE_ERROR_BUSY;
    }
    if (present->damage_count == 0u) {
        surface_add_damage_locked(slot, NULL);
    } else {
        for (uint32_t i = 0u; i < present->damage_count; ++i) {
            surface_add_damage_locked(slot, &present->damage[i]);
        }
    }
    ++slot->submitted_serial;
    ++slot->references;
    ++slot->active_operations;
    ++slot->pending_presents;
    ++backing->references;
    job->slot = slot;
    job->backing = backing;
    job->identity = slot->identity;
    job->desc = slot->desc;
    job->layer = *layer;
    job->serial = slot->submitted_serial;
    job->image_available_event = image_available_event;
    job->swapchain_available_event = swapchain_available_event;
    job->flow=scheduler_flow_current();
    if (compositor_surface_present_tail != NULL)
        compositor_surface_present_tail->next = job;
    else
        compositor_surface_present_head = job;
    compositor_surface_present_tail = job;
    ++compositor_surface_present_count;
    *serial_out = job->serial;
    surface_unlock(&surface_guard);
    gui_wake();
    return COMPOSITOR_SURFACE_OK;
}

uint32_t compositor_surface_process_queued(
    const compositor_target_t *target,
    compositor_surface_present_completion_t *completions,
    uint32_t capacity) {
    surface_guard_t surface_guard;
    uint32_t count = 0u;
    if (target == NULL || completions == NULL || capacity == 0u) return 0u;
    while (count < capacity) {
        compositor_surface_queued_present_t *job;
        surface_guard=surface_lock();
        job = compositor_surface_present_head;
        if (job != NULL) {
            compositor_surface_present_head = job->next;
            if (compositor_surface_present_head == NULL)
                compositor_surface_present_tail = NULL;
            job->next = NULL;
            if (compositor_surface_present_count != 0u)
                --compositor_surface_present_count;
        }
        surface_unlock(&surface_guard);
        if (job == NULL) break;
        scheduler_flow_adopt(job->flow);
        job->status = surface_compose_snapshot(&job->desc, job->backing,
            target, &job->layer, &job->damage);
        completions[count].token = job;
        completions[count].damage = job->damage;
        completions[count].status = job->status;
        completions[count].reserved = 0u;
        ++count;
    }
    return count;
}

static int surface_complete_status(compositor_surface_identity_t identity,
                                   uint64_t serial, int32_t status) {
    surface_guard_t surface_guard;
    compositor_surface_slot_t *slot;
    if (identity == 0u || serial == 0u) return COMPOSITOR_SURFACE_ERROR_INVALID;
    surface_guard=surface_lock();
    slot = surface_find_identity_locked(identity);
    if (slot == NULL) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_NOT_FOUND;
    }
    if (serial > slot->submitted_serial || serial < slot->completed_serial) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_INVALID;
    }
    slot->completion_serials[serial % COMPOSITOR_SURFACE_COMPLETION_HISTORY] = serial;
    slot->completion_status[serial % COMPOSITOR_SURFACE_COMPLETION_HISTORY] = status;
    slot->completed_serial = serial;
    surface_wake_completed_waiters_locked(slot);
    surface_unlock(&surface_guard);
    return COMPOSITOR_SURFACE_OK;
}

static int surface_completed_status_locked(const compositor_surface_slot_t *slot,
                                           uint64_t serial) {
    const uint32_t index = (uint32_t)(serial % COMPOSITOR_SURFACE_COMPLETION_HISTORY);
    if (slot != NULL && slot->completion_serials[index] == serial &&
        slot->completion_status[index] != 0) {
        return COMPOSITOR_SURFACE_ERROR_UNSUPPORTED;
    }
    return COMPOSITOR_SURFACE_OK;
}

void compositor_surface_finish_queued(
    compositor_surface_present_completion_t *completion) {
    surface_guard_t surface_guard;
    compositor_surface_queued_present_t *job;
    if (completion == NULL || completion->token == NULL) return;
    job = (compositor_surface_queued_present_t *)completion->token;
    completion->token = NULL;
    if (vex_event_counter_signal(job->image_available_event, 1u) != 0 ||
        vex_event_counter_signal(job->swapchain_available_event, 1u) != 0) {
        job->status = COMPOSITOR_SURFACE_ERROR_NOT_FOUND;
    }
    surface_guard=surface_lock();
    if (job->slot != NULL && job->slot->occupied &&
        job->slot->pending_presents != 0u) {
        --job->slot->pending_presents;
    }
    surface_unlock(&surface_guard);
    (void)surface_complete_status(job->identity, job->serial, job->status);
    (void)vex_event_counter_release(job->swapchain_available_event);
    (void)vex_event_counter_release(job->image_available_event);
    surface_guard=surface_lock();
    surface_backing_release_locked(job->backing);
    if (job->slot != NULL && job->slot->occupied) {
        if (job->slot->active_operations != 0u) --job->slot->active_operations;
        (void)surface_object_release_locked(job->slot);
        if (job->slot->occupied && job->slot->destroy_pending &&
            job->slot->references == 0u && job->slot->map_readers == 0u &&
            job->slot->map_writers == 0u && job->slot->active_operations == 0u) {
            surface_destroy_locked(job->slot);
        }
    }
    surface_unlock(&surface_guard);
    kfree(job);
}

int compositor_surface_consume_damage(uint32_t owner_pid,
                                      compositor_surface_handle_t handle,
                                      vex_rect_t *rects, uint32_t capacity,
                                      uint32_t *count_out) {
    surface_guard_t surface_guard;
    compositor_surface_slot_t *slot;
    uint32_t count;

    if (count_out == NULL || (capacity != 0u && rects == NULL)) {
        return COMPOSITOR_SURFACE_ERROR_INVALID;
    }
    surface_guard=surface_lock();
    {
        const int access = surface_cap_allows_locked(owner_pid, handle,
            COMPOSITOR_SURFACE_RIGHT_COMPOSE, &slot, NULL);
        if (access != 1) {
            surface_unlock(&surface_guard);
            return access == 0 ? COMPOSITOR_SURFACE_ERROR_NOT_FOUND
                               : COMPOSITOR_SURFACE_ERROR_ACCESS;
        }
    }
    count = slot->damage_count;
    *count_out = count;
    if (rects == NULL && capacity == 0u) {
        slot->damage_count = 0u;
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_OK;
    }
    if (capacity < count) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_BUSY;
    }
    if (count != 0u) {
        memcpy(rects, slot->damage, (size_t)count * sizeof(*rects));
    }
    slot->damage_count = 0u;
    surface_unlock(&surface_guard);
    return COMPOSITOR_SURFACE_OK;
}

int compositor_surface_is_complete(uint32_t owner_pid,
                                   compositor_surface_handle_t handle,
                                   uint64_t serial) {
    surface_guard_t surface_guard;
    compositor_surface_slot_t *slot;
    int complete;

    surface_guard=surface_lock();
    {
        const int access = surface_cap_allows_locked(owner_pid, handle,
            COMPOSITOR_SURFACE_RIGHT_QUERY, &slot, NULL);
        if (access != 1) {
            surface_unlock(&surface_guard);
            return access == 0 ? COMPOSITOR_SURFACE_ERROR_NOT_FOUND
                               : COMPOSITOR_SURFACE_ERROR_ACCESS;
        }
    }
    complete = serial <= slot->completed_serial ? 1 : 0;
    surface_unlock(&surface_guard);
    return complete;
}

int compositor_surface_wait(uint32_t owner_pid, compositor_surface_handle_t handle,
                            uint64_t serial, uint64_t timeout_ticks) {
    surface_guard_t surface_guard;
    compositor_surface_waiter_t waiter;
    compositor_surface_slot_t *slot;
    uint64_t start_tick;
    int result = COMPOSITOR_SURFACE_OK;

    if (serial == 0u || owner_pid == 0u) {
        return COMPOSITOR_SURFACE_ERROR_INVALID;
    }
    memset(&waiter, 0, sizeof(waiter));
    surface_guard=surface_lock();
    {
        const int access = surface_cap_allows_locked(owner_pid, handle,
            COMPOSITOR_SURFACE_RIGHT_QUERY, &slot, NULL);
        if (access != 1) {
            surface_unlock(&surface_guard);
            return access == 0 ? COMPOSITOR_SURFACE_ERROR_NOT_FOUND
                               : COMPOSITOR_SURFACE_ERROR_ACCESS;
        }
    }
    if (serial > slot->submitted_serial) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_INVALID;
    }
    if (serial <= slot->completed_serial) {
        result = surface_completed_status_locked(slot, serial);
        surface_unlock(&surface_guard);
        return result;
    }
    if (slot->references == UINT32_MAX) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_BUSY;
    }
    ++slot->references; /* the waiter pins the object until it unregisters */
    waiter.object = slot;
    waiter.pid = scheduler_current_pid();
    waiter.tid = scheduler_current_tid();
    waiter.serial = serial;
    waiter.next = slot->waiters;
    slot->waiters = &waiter;
    start_tick = scheduler_ticks_now();
    surface_unlock(&surface_guard);
    /* These serials are completed by the GUI compositor service, irrespective
       of the Surface creator or whichever process holds its capability. */
    (void)scheduler_wait_dependency(gui_service_tid());

    while (waiter.woken == 0u && waiter.cancelled == 0u &&
           scheduler_process_alive(waiter.tid)) {
        if (timeout_ticks == 0u) {
            process_wait_event(&waiter.woken);
        } else {
            const uint64_t now = scheduler_ticks_now();
            const uint64_t elapsed = now - start_tick;
            if (elapsed >= timeout_ticks) {
                result = COMPOSITOR_SURFACE_ERROR_BUSY;
                break;
            }
            process_wait_event_timeout(&waiter.woken, timeout_ticks - elapsed);
        }
    }

    scheduler_wait_dependency_clear();
    surface_guard=surface_lock();
    if (waiter.object != NULL) {
        slot = waiter.object;
        surface_unlink_waiter_locked(&waiter);
        waiter.object = NULL;
        if (waiter.cancelled != 0u || !scheduler_process_alive(waiter.tid)) {
            result = COMPOSITOR_SURFACE_ERROR_NOT_FOUND;
        } else if (slot->completed_serial >= serial) {
            /* Completion may race the scheduler timeout after the last wait. */
            result = surface_completed_status_locked(slot, serial);
        } else if (result == COMPOSITOR_SURFACE_OK) {
            result = COMPOSITOR_SURFACE_ERROR_BUSY;
        }
        (void)surface_object_release_locked(slot);
    }
    surface_unlock(&surface_guard);
    return result;
}

int compositor_surface_complete(compositor_surface_identity_t identity,
                                uint64_t serial) {
    return surface_complete_status(identity, serial, 0);
}

static void surface_operation_end(compositor_surface_slot_t *slot) {
    surface_guard_t surface_guard;
    surface_guard=surface_lock();
    if (slot != NULL && slot->occupied && slot->active_operations != 0u) {
        --slot->active_operations;
        if (slot->destroy_pending && slot->references == 0u &&
            slot->map_readers == 0u && slot->map_writers == 0u &&
            slot->active_operations == 0u) {
            surface_destroy_locked(slot);
        }
    }
    surface_unlock(&surface_guard);
}

int compositor_surface_compose(uint32_t owner_pid,
                               compositor_surface_handle_t handle,
                               const compositor_target_t *target,
                               const compositor_layer_t *layer,
                               vex_rect_t *destination_damage_out) {
    surface_guard_t surface_guard;
    compositor_surface_slot_t *slot;
    compositor_surface_desc_t desc;
    uint32_t backend_pid;
    uint32_t backend_handle;
    vex_rect_t source;
    vex_rect_t destination;
    int64_t clip_x0;
    int64_t clip_y0;
    int64_t clip_x1;
    int64_t clip_y1;
    gpu_composite_request_t request;
    int result;
    compositor_surface_backing_t *backing;

    if (destination_damage_out) memset(destination_damage_out,0,sizeof(*destination_damage_out));

    if (target == NULL || layer == NULL ||
        target->format != COMPOSITOR_SURFACE_FORMAT_XRGB8888 ||
        target->stride < target->width ||
        target->backend_handle == 0u) {
        return COMPOSITOR_SURFACE_ERROR_UNSUPPORTED;
    }
    surface_guard=surface_lock();
    if (surface_cap_allows_locked(owner_pid, handle,
            COMPOSITOR_SURFACE_RIGHT_COMPOSE, &slot, NULL) != 1 ||
        slot->destroy_pending || slot->backing == NULL) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_NOT_FOUND;
    }
    if (slot->active_operations == UINT32_MAX) {
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_BUSY;
    }
    ++slot->active_operations;
    desc = slot->desc;
    backing = slot->backing;
    if (backing->references == UINT32_MAX) {
        --slot->active_operations;
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_BUSY;
    }
    ++backing->references;
    backend_pid = backing->backend_pid;
    backend_handle = backing->backend_handle;
    surface_unlock(&surface_guard);

    source = layer->source;
    if (source.w <= 0 || source.h <= 0) {
        source.x = 0;
        source.y = 0;
        source.w = (int32_t)desc.width;
        source.h = (int32_t)desc.height;
    }
    if (source.x < 0 || source.y < 0 || source.w <= 0 || source.h <= 0 ||
        (uint64_t)(uint32_t)source.x + (uint32_t)source.w > desc.width ||
        (uint64_t)(uint32_t)source.y + (uint32_t)source.h > desc.height) {
        surface_operation_end(slot);
        surface_guard=surface_lock();
        surface_backing_release_locked(backing);
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_INVALID;
    }
    destination = layer->destination;
    if (destination.w <= 0 || destination.h <= 0) {
        surface_operation_end(slot);
        surface_guard=surface_lock();
        surface_backing_release_locked(backing);
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_INVALID;
    }

    clip_x0 = destination.x < 0 ? 0 : destination.x;
    clip_y0 = destination.y < 0 ? 0 : destination.y;
    clip_x1 = (int64_t)destination.x + destination.w;
    clip_y1 = (int64_t)destination.y + destination.h;
    if (clip_x1 > target->width) {
        clip_x1 = target->width;
    }
    if (clip_y1 > target->height) {
        clip_y1 = target->height;
    }
    if (layer->clip.w > 0 && layer->clip.h > 0) {
        if (clip_x0 < layer->clip.x) clip_x0 = layer->clip.x;
        if (clip_y0 < layer->clip.y) clip_y0 = layer->clip.y;
        const int64_t right=(int64_t)layer->clip.x+layer->clip.w;
        const int64_t bottom=(int64_t)layer->clip.y+layer->clip.h;
        if (clip_x1 > right) clip_x1=right;
        if (clip_y1 > bottom) clip_y1=bottom;
    }
    if (clip_x1 <= clip_x0 || clip_y1 <= clip_y0) {
        surface_operation_end(slot);
        surface_guard=surface_lock();
        surface_backing_release_locked(backing);
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_OK;
    }

    if (layer->opacity == 0u) {
        surface_operation_end(slot);
        surface_guard=surface_lock();
        surface_backing_release_locked(backing);
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_OK;
    }

    memset(&request, 0, sizeof(request));
    request.source = source;
    request.destination = destination;
    request.clip = layer->clip;
    request.opacity = layer->opacity > 255u ? 255u : layer->opacity;
    request.alpha_mode = desc.alpha_mode;
    request.filter = layer->filter;
    request.transform = layer->transform;
    result = gpu_buffer_composite(target->backend_pid,
                                  target->backend_handle,
                                  backend_pid, backend_handle,
                                  &request);
    if (result != 0) {
        surface_operation_end(slot);
        surface_guard=surface_lock();
        surface_backing_release_locked(backing);
        surface_unlock(&surface_guard);
        return COMPOSITOR_SURFACE_ERROR_UNSUPPORTED;
    }

    if (destination_damage_out != NULL) {
        destination_damage_out->x = (int32_t)clip_x0;
        destination_damage_out->y = (int32_t)clip_y0;
        destination_damage_out->w = (int32_t)(clip_x1 - clip_x0);
        destination_damage_out->h = (int32_t)(clip_y1 - clip_y0);
    }
    surface_operation_end(slot);
    surface_guard=surface_lock();
    surface_backing_release_locked(backing);
    surface_unlock(&surface_guard);
    return COMPOSITOR_SURFACE_OK;
}

void compositor_surface_reap_pid(uint32_t owner_pid) {
    surface_guard_t surface_guard;
    if (owner_pid == 0u) {
        return;
    }
    surface_guard=surface_lock();
    for (uint32_t i = 0u; i < COMPOSITOR_SURFACE_CAP_COUNT; ++i) {
        compositor_surface_capability_t *cap = &compositor_surface_caps[i];
        compositor_surface_slot_t *slot;
        uint32_t refs;

        if (!cap->occupied || cap->owner_pid != owner_pid) {
            continue;
        }
        slot = cap->object;
        if (slot == NULL || !slot->occupied) {
            memset(cap, 0, sizeof(*cap));
            continue;
        }
        if (cap->map_readers <= slot->map_readers) {
            slot->map_readers -= cap->map_readers;
        } else {
            slot->map_readers = 0u;
        }
        if (cap->map_writers <= slot->map_writers) {
            slot->map_writers -= cap->map_writers;
        } else {
            slot->map_writers = 0u;
        }
        refs = cap->local_refs;
        memset(cap, 0, sizeof(*cap));
        while (refs-- != 0u && slot->occupied) {
            (void)surface_object_release_locked(slot);
        }
    }
    surface_unlock(&surface_guard);
}

static void surface_cancel_waiters(uint32_t owner_pid,int exact_thread) {
    surface_guard_t surface_guard;
    if (owner_pid == 0u) {
        return;
    }
    surface_guard=surface_lock();
    for (uint32_t i = 0u; i < COMPOSITOR_SURFACE_SLOT_COUNT; ++i) {
        compositor_surface_slot_t *slot = &compositor_surfaces[i];
        compositor_surface_waiter_t **link;
        if (!slot->occupied) {
            continue;
        }
        link = &slot->waiters;
        while (*link != NULL) {
            compositor_surface_waiter_t *waiter = *link;
            if (waiter->tid == owner_pid || (!exact_thread && waiter->pid == owner_pid)) {
                if(!exact_thread) {
                    waiter->cancelled=1u;
                    __atomic_store_n(&waiter->woken,1u,__ATOMIC_RELEASE);
                    process_wake(waiter->tid);link=&waiter->next;continue;
                }
                *link = waiter->next;
                waiter->next = NULL;
                waiter->cancelled = 1u;
                __atomic_store_n(&waiter->woken,1u,__ATOMIC_RELEASE);
                waiter->object = NULL;
                process_wake(waiter->tid);
                (void)surface_object_release_locked(slot);
                continue;
            }
            link = &waiter->next;
        }
    }
    surface_unlock(&surface_guard);
}

void compositor_surface_cancel_pid(uint32_t owner_pid) {surface_cancel_waiters(owner_pid,0);}
void compositor_surface_thread_stopped(uint32_t tid) {surface_cancel_waiters(tid,1);}

static size_t surface_test_backing_count(void) {
    surface_guard_t surface_guard;
    size_t count = 0u;
    surface_guard=surface_lock();
    for (compositor_surface_backing_t *backing = compositor_surface_backings;
         backing != NULL; backing = backing->next) {
        ++count;
    }
    surface_unlock(&surface_guard);
    return count;
}

int compositor_surface_self_test(void) {
    surface_guard_t surface_guard;
    static const uint32_t producer_pid = 0xffffe101u;
    static const uint32_t receiver_pid = 0xffffe102u;
    static const uint32_t pinned_pid = 0xffffe103u;
    compositor_surface_desc_t desc;
    compositor_surface_info_t before;
    compositor_surface_info_t after;
    compositor_surface_handle_t producer_handle = 0u;
    compositor_surface_handle_t receiver_handle = 0u;
    compositor_surface_handle_t queued_handle = 0u;
    compositor_surface_handle_t pinned_handle = 0u;
    compositor_surface_handle_t imported_handle = 0u;
    compositor_surface_handle_t async_handle = 0u;
    compositor_surface_identity_t pin = 0u;
    uint64_t map_shared_id = 0u;
    uint64_t serial = 0u;
    uint32_t payload = 0x56455853u;
    uint32_t payload_out = 0u;
    vex_gpu_buffer_create_req_t gpu_buffer_req;
    compositor_surface_mapping_t async_mapping;
    compositor_surface_present_info_t async_present;
    compositor_surface_present_completion_t async_completion;
    compositor_target_t async_target;
    compositor_layer_t async_layer;
    uint32_t async_target_handle = 0u;
    uint32_t async_target_stride = 0u;
    uint32_t async_target_width = 0u;
    uint32_t async_target_height = 0u;
    uint32_t *async_target_pixels = NULL;
    uint64_t async_image_event = 0u;
    uint64_t async_available_event = 0u;
    uint64_t async_shared_id = 0u;
    uint64_t async_serial = 0u;
    const uint32_t async_color = 0xffa15c27u;
    vex_message_attachment_t outgoing;
    vex_message_attachment_t incoming;
    vex_message_endpoint_t *left = NULL;
    vex_message_endpoint_t *right = NULL;
    vex_message_endpoint_t *queued_left = NULL;
    vex_message_endpoint_t *queued_right = NULL;
    size_t attachment_count = 1u;
    const size_t initial_backings = surface_test_backing_count();
    int ok = 0;

    memset(&desc, 0, sizeof(desc));
    desc.width = 8u;
    desc.height = 8u;
    desc.format = COMPOSITOR_SURFACE_FORMAT_XRGB8888;
    desc.alpha_mode = COMPOSITOR_SURFACE_ALPHA_OPAQUE;
    desc.usage = COMPOSITOR_SURFACE_USAGE_CPU_READ |
                 COMPOSITOR_SURFACE_USAGE_CPU_WRITE |
                 COMPOSITOR_SURFACE_USAGE_SAMPLED;
    memset(&outgoing, 0, sizeof(outgoing));
    memset(&incoming, 0, sizeof(incoming));
    memset(&async_completion, 0, sizeof(async_completion));

    if (compositor_surface_create(producer_pid, &desc, &producer_handle) !=
            COMPOSITOR_SURFACE_OK ||
        compositor_surface_query(producer_pid, producer_handle, &before) !=
            COMPOSITOR_SURFACE_OK ||
        before.object_identity == 0u || before.backing_identity == 0u ||
        before.object_identity == before.backing_identity ||
        before.shared_backing_id == 0u ||
        process_memory_shared_size(before.shared_backing_id) < before.backing_size ||
        vex_message_pair_create(0u, &left, &right) != 0 ||
        compositor_surface_export(producer_pid, producer_handle,
            COMPOSITOR_SURFACE_RIGHT_QUERY |
            COMPOSITOR_SURFACE_RIGHT_MAP_READ, &outgoing) !=
            COMPOSITOR_SURFACE_OK ||
        vex_message_send_attached(left, &payload, sizeof(payload),
            &outgoing, 1u, 0u) != (ssize_t)sizeof(payload)) {
        goto cleanup;
    }

    /* The queued typed attachment, not the creator PID, now owns the object. */
    compositor_surface_reap_pid(producer_pid);
    attachment_count = 1u;
    if (vex_message_receive_attached(right, &payload_out, sizeof(payload_out),
            &incoming, &attachment_count, 0u) != (ssize_t)sizeof(payload_out) ||
        payload_out != payload || attachment_count != 1u ||
        incoming.type != VEX_MESSAGE_ATTACHMENT_SURFACE ||
        compositor_surface_import_attachment(receiver_pid, &incoming,
            incoming.rights, &receiver_handle) != COMPOSITOR_SURFACE_OK) {
        goto cleanup;
    }
    vex_message_attachment_release(&incoming);
    if (compositor_surface_query(receiver_pid, receiver_handle, &after) !=
            COMPOSITOR_SURFACE_OK ||
        after.object_identity != before.object_identity ||
        after.backing_identity != before.backing_identity ||
        compositor_surface_export(receiver_pid, receiver_handle,
            COMPOSITOR_SURFACE_RIGHT_QUERY |
            COMPOSITOR_SURFACE_RIGHT_MAP_READ, &incoming) !=
            COMPOSITOR_SURFACE_ERROR_ACCESS ||
        compositor_surface_authorize_mapping(receiver_pid, receiver_handle,
            COMPOSITOR_SURFACE_MAP_WRITE, 0u, sizeof(uint32_t),
            &map_shared_id, &pin) != COMPOSITOR_SURFACE_ERROR_ACCESS ||
        compositor_surface_authorize_mapping(receiver_pid, receiver_handle,
            COMPOSITOR_SURFACE_MAP_READ, 0u, sizeof(uint32_t),
            &map_shared_id, &pin) != COMPOSITOR_SURFACE_OK ||
        map_shared_id != before.shared_backing_id || pin != before.object_identity) {
        goto cleanup;
    }
    compositor_surface_unpin(pin);
    pin = 0u;
    if (compositor_surface_release(receiver_pid, receiver_handle) !=
            COMPOSITOR_SURFACE_OK ||
        compositor_surface_query(receiver_pid, receiver_handle, &after) !=
            COMPOSITOR_SURFACE_ERROR_NOT_FOUND ||
        process_memory_shared_size(before.shared_backing_id) != 0u ||
        surface_test_backing_count() != initial_backings) {
        goto cleanup;
    }
    receiver_handle = 0u;
    (void)vex_message_endpoint_close(left);
    (void)vex_message_endpoint_close(right);
    left = NULL;
    right = NULL;

    /* Imported GPU storage has a distinct capability contract: its backing
       may be shared/mapped, but this wrapper cannot resize the GPU allocation. */
    memset(&gpu_buffer_req, 0, sizeof(gpu_buffer_req));
    gpu_buffer_req.width = 8u;
    gpu_buffer_req.height = 8u;
    gpu_buffer_req.stride = 8u;
    gpu_buffer_req.format = VEX_GPU_FORMAT_XRGB8888;
    gpu_buffer_req.flags = VEX_GPU_MEMORY_CPU_VISIBLE;
    if (gpu_buffer_create(producer_pid, &gpu_buffer_req) != 0 ||
        compositor_surface_import_gpu_buffer(producer_pid, gpu_buffer_req.handle,
            COMPOSITOR_SURFACE_ALPHA_OPAQUE,
            COMPOSITOR_SURFACE_USAGE_CPU_READ |
            COMPOSITOR_SURFACE_USAGE_CPU_WRITE |
            COMPOSITOR_SURFACE_USAGE_SAMPLED, &imported_handle) !=
            COMPOSITOR_SURFACE_OK ||
        compositor_surface_export(producer_pid, imported_handle,
            COMPOSITOR_SURFACE_RIGHT_RESIZE, &outgoing) !=
            COMPOSITOR_SURFACE_ERROR_ACCESS ||
        compositor_surface_release(producer_pid, imported_handle) !=
            COMPOSITOR_SURFACE_OK ||
        gpu_buffer_destroy(producer_pid, gpu_buffer_req.handle) != 0) {
        goto cleanup;
    }
    imported_handle = 0u;
    gpu_buffer_req.handle = 0u;
    if (surface_test_backing_count() != initial_backings) {
        goto cleanup;
    }

    /* Protected/device-only surfaces must never acquire CPU map authority. */
    desc.usage = COMPOSITOR_SURFACE_USAGE_PROTECTED |
                 COMPOSITOR_SURFACE_USAGE_SAMPLED;
    if (compositor_surface_create(producer_pid, &desc, &producer_handle) !=
            COMPOSITOR_SURFACE_OK ||
        compositor_surface_query(producer_pid, producer_handle, &before) !=
            COMPOSITOR_SURFACE_OK ||
        before.backing_type != COMPOSITOR_SURFACE_BACKING_DEVICE ||
        compositor_surface_authorize_mapping(producer_pid, producer_handle,
            COMPOSITOR_SURFACE_MAP_READ, 0u, sizeof(uint32_t),
            &map_shared_id, &pin) != COMPOSITOR_SURFACE_ERROR_ACCESS ||
        compositor_surface_release(producer_pid, producer_handle) !=
            COMPOSITOR_SURFACE_OK) {
        goto cleanup;
    }
    producer_handle = 0u;
    desc.usage = COMPOSITOR_SURFACE_USAGE_CPU_READ |
                 COMPOSITOR_SURFACE_USAGE_CPU_WRITE |
                 COMPOSITOR_SURFACE_USAGE_SAMPLED;
    if (surface_test_backing_count() != initial_backings) {
        goto cleanup;
    }

    /* Closing the sender first leaves the queued capability alive; closing
       the final endpoint drops that attachment and destroys the last ref. */
    if (compositor_surface_create(producer_pid, &desc, &queued_handle) !=
            COMPOSITOR_SURFACE_OK ||
        compositor_surface_query(producer_pid, queued_handle, &before) !=
            COMPOSITOR_SURFACE_OK ||
        vex_message_pair_create(0u, &queued_left, &queued_right) != 0 ||
        compositor_surface_export(producer_pid, queued_handle,
            COMPOSITOR_SURFACE_RIGHT_QUERY, &outgoing) !=
            COMPOSITOR_SURFACE_OK ||
        vex_message_send_attached(queued_left, &payload, sizeof(payload),
            &outgoing, 1u, 0u) != (ssize_t)sizeof(payload)) {
        goto cleanup;
    }
    (void)vex_message_endpoint_close(queued_left);
    queued_left = NULL;
    compositor_surface_reap_pid(producer_pid);
    if (surface_test_backing_count() != initial_backings + 1u) {
        goto cleanup;
    }
    (void)vex_message_endpoint_close(queued_right);
    queued_right = NULL;
    queued_handle = 0u; /* creator's capability was reaped above */
    if (process_memory_shared_size(before.shared_backing_id) != 0u ||
        surface_test_backing_count() != initial_backings) {
        goto cleanup;
    }

    /* A compositor pin, unlike a creator-PID association, keeps the backing
       alive until its own explicit release. */
    if (compositor_surface_create(pinned_pid, &desc, &pinned_handle) !=
            COMPOSITOR_SURFACE_OK ||
        compositor_surface_query(pinned_pid, pinned_handle, &before) !=
            COMPOSITOR_SURFACE_OK ||
        compositor_surface_pin(pinned_pid, pinned_handle, &pin) !=
            COMPOSITOR_SURFACE_OK ||
        compositor_surface_present(pinned_pid, pinned_handle, NULL,
            &serial) != COMPOSITOR_SURFACE_OK || serial == 0u ||
        compositor_surface_complete(pin, serial) != COMPOSITOR_SURFACE_OK ||
        compositor_surface_wait(pinned_pid, pinned_handle, serial, 1u) !=
            COMPOSITOR_SURFACE_OK ||
        compositor_surface_wait(pinned_pid, pinned_handle, serial + 1u, 1u) !=
            COMPOSITOR_SURFACE_ERROR_INVALID) {
        goto cleanup;
    }
    compositor_surface_reap_pid(pinned_pid);
    surface_guard=surface_lock();
    const int pin_kept_object = surface_find_identity_locked(pin) != NULL;
    surface_unlock(&surface_guard);
    if (!pin_kept_object ||
        process_memory_shared_size(before.shared_backing_id) == 0u) {
        goto cleanup;
    }
    compositor_surface_unpin(pin);
    pin = 0u;
    pinned_handle = 0u;
    if (process_memory_shared_size(before.shared_backing_id) != 0u ||
        surface_test_backing_count() != initial_backings) {
        goto cleanup;
    }

    /* A queued WSI frame owns the producer backing and completion
       capabilities after the producer releases its local Surface handle. */
    if (compositor_surface_create(producer_pid, &desc, &async_handle) !=
            COMPOSITOR_SURFACE_OK ||
        compositor_surface_query(producer_pid, async_handle, &before) !=
            COMPOSITOR_SURFACE_OK ||
        compositor_surface_map(producer_pid, async_handle,
            COMPOSITOR_SURFACE_MAP_WRITE, &async_mapping) !=
            COMPOSITOR_SURFACE_OK) {
        goto cleanup;
    }
    async_shared_id = before.shared_backing_id;
    async_mapping.pixels[0] = async_color;
    if (compositor_surface_unmap(producer_pid, async_handle,
            COMPOSITOR_SURFACE_MAP_WRITE) != COMPOSITOR_SURFACE_OK ||
        gpu_kernel_buffer_create(8u, 8u, 8u, VEX_GPU_MEMORY_CPU_VISIBLE,
            &async_target_handle) != 0 ||
        (async_target_pixels = gpu_kernel_buffer_map(async_target_handle,
            &async_target_stride, &async_target_width,
            &async_target_height)) == NULL ||
        async_target_width != 8u || async_target_height != 8u ||
        async_target_stride < 8u ||
        vex_event_counter_create(0u, VEX_EVENT_COUNTER_SEMAPHORE,
            &async_image_event) != 0 ||
        vex_event_counter_create(0u, VEX_EVENT_COUNTER_SEMAPHORE,
            &async_available_event) != 0) {
        goto cleanup;
    }
    memset(&async_target, 0, sizeof(async_target));
    async_target.pixels = async_target_pixels;
    async_target.width = async_target_width;
    async_target.height = async_target_height;
    async_target.stride = async_target_stride;
    async_target.format = COMPOSITOR_SURFACE_FORMAT_XRGB8888;
    async_target.backend_pid = 0u;
    async_target.backend_handle = async_target_handle;
    memset(&async_present, 0, sizeof(async_present));
    memset(&async_layer, 0, sizeof(async_layer));
    async_layer.source.w = 8;
    async_layer.source.h = 8;
    async_layer.destination.w = 8;
    async_layer.destination.h = 8;
    async_layer.opacity = 255u;
    async_layer.filter = COMPOSITOR_SURFACE_FILTER_NEAREST;
    if (compositor_surface_queue_present(producer_pid, async_handle,
            &async_present, &async_layer, async_image_event,
            async_available_event, &async_serial) != COMPOSITOR_SURFACE_OK ||
        async_serial == 0u ||
        compositor_surface_release(producer_pid, async_handle) !=
            COMPOSITOR_SURFACE_OK) {
        goto cleanup;
    }
    async_handle = 0u;
    if (process_memory_shared_size(async_shared_id) == 0u ||
        compositor_surface_process_queued(&async_target, &async_completion, 1u) != 1u ||
        async_completion.token == NULL ||
        async_completion.status != COMPOSITOR_SURFACE_OK ||
        async_target_pixels[0] != async_color ||
        process_memory_shared_size(async_shared_id) == 0u) {
        goto cleanup;
    }
    compositor_surface_finish_queued(&async_completion);
    async_serial = 0u;
    {
        uint64_t event_value = 0u;
        if (vex_event_counter_read(async_image_event, &event_value,
                VEX_EVENT_COUNTER_NONBLOCK) != 0 || event_value != 1u ||
            vex_event_counter_read(async_available_event, &event_value,
                VEX_EVENT_COUNTER_NONBLOCK) != 0 || event_value != 1u ||
            process_memory_shared_size(async_shared_id) != 0u) {
            goto cleanup;
        }
    }
    (void)vex_event_counter_release(async_image_event);
    (void)vex_event_counter_release(async_available_event);
    async_image_event = 0u;
    async_available_event = 0u;
    (void)gpu_kernel_buffer_destroy(async_target_handle);
    async_target_handle = 0u;

    for (uint32_t cycle = 0u; cycle < 16u; ++cycle) {
        if (compositor_surface_create(producer_pid, &desc, &producer_handle) !=
                COMPOSITOR_SURFACE_OK ||
            compositor_surface_query(producer_pid, producer_handle, &before) !=
                COMPOSITOR_SURFACE_OK ||
            vex_message_pair_create(0u, &left, &right) != 0 ||
            compositor_surface_export(producer_pid, producer_handle,
                COMPOSITOR_SURFACE_RIGHT_QUERY, &outgoing) !=
                COMPOSITOR_SURFACE_OK ||
            vex_message_send_attached(left, &payload, sizeof(payload),
                &outgoing, 1u, 0u) != (ssize_t)sizeof(payload)) {
            goto cleanup;
        }
        compositor_surface_reap_pid(producer_pid);
        producer_handle = 0u;
        attachment_count = 1u;
        if (vex_message_receive_attached(right, &payload_out,
                sizeof(payload_out), &incoming, &attachment_count, 0u) !=
                (ssize_t)sizeof(payload_out) || attachment_count != 1u ||
            compositor_surface_import_attachment(receiver_pid, &incoming,
                incoming.rights, &receiver_handle) != COMPOSITOR_SURFACE_OK) {
            goto cleanup;
        }
        vex_message_attachment_release(&incoming);
        (void)vex_message_endpoint_close(left);
        (void)vex_message_endpoint_close(right);
        left = NULL;
        right = NULL;
        if (compositor_surface_release(receiver_pid, receiver_handle) !=
                COMPOSITOR_SURFACE_OK) {
            goto cleanup;
        }
        receiver_handle = 0u;
        if (process_memory_shared_size(before.shared_backing_id) != 0u ||
            surface_test_backing_count() != initial_backings) {
            goto cleanup;
        }
    }
    ok = 1;

cleanup:
    if (async_serial != 0u && async_completion.token == NULL &&
        async_target.backend_handle != 0u &&
        compositor_surface_process_queued(&async_target, &async_completion, 1u) == 1u) {
        compositor_surface_finish_queued(&async_completion);
    }
    if (async_completion.token != NULL)
        compositor_surface_finish_queued(&async_completion);
    if (async_handle != 0u)
        (void)compositor_surface_release(producer_pid, async_handle);
    if (async_image_event != 0u)
        (void)vex_event_counter_release(async_image_event);
    if (async_available_event != 0u)
        (void)vex_event_counter_release(async_available_event);
    if (async_target_handle != 0u)
        (void)gpu_kernel_buffer_destroy(async_target_handle);
    if (imported_handle != 0u) {
        (void)compositor_surface_release(producer_pid, imported_handle);
    }
    if (gpu_buffer_req.handle != 0u) {
        (void)gpu_buffer_destroy(producer_pid, gpu_buffer_req.handle);
    }
    if (pin != 0u) {
        compositor_surface_unpin(pin);
    }
    if (incoming.object != NULL) {
        vex_message_attachment_release(&incoming);
    }
    if (outgoing.object != NULL) {
        vex_message_attachment_release(&outgoing);
    }
    if (left != NULL) {
        (void)vex_message_endpoint_close(left);
    }
    if (right != NULL) {
        (void)vex_message_endpoint_close(right);
    }
    if (queued_left != NULL) {
        (void)vex_message_endpoint_close(queued_left);
    }
    if (queued_right != NULL) {
        (void)vex_message_endpoint_close(queued_right);
    }
    if (producer_handle != 0u) {
        (void)compositor_surface_release(producer_pid, producer_handle);
    }
    if (receiver_handle != 0u) {
        (void)compositor_surface_release(receiver_pid, receiver_handle);
    }
    if (queued_handle != 0u) {
        (void)compositor_surface_release(producer_pid, queued_handle);
    }
    if (pinned_handle != 0u) {
        (void)compositor_surface_release(pinned_pid, pinned_handle);
    }
    if (!ok) {
        compositor_surface_reap_pid(producer_pid);
        compositor_surface_reap_pid(receiver_pid);
        compositor_surface_reap_pid(pinned_pid);
    }
    return ok ? 0 : -1;
}

