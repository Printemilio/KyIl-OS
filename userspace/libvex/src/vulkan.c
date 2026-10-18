#include <vulkan/vulkan_vex.h>
#include <string.h>
#include <unos/vex_abi.h>
#include <vex_runtime.h>

#include "vex_syscall.h"
#include "vex_render_types.h"
#include "vex_render_declarations.h"
#define VEX_RENDER_DIAGNOSTIC(msg) ((void)vex_syscall1(VEX_SYS_DEBUG_LOG,(uint64_t)(uintptr_t)(msg)))

#define VK_VULKAN_MAX_VERTEX_INPUT_ATTRIBUTES 8u
#define VK_VULKAN_MAX_REFLECTED_DESCRIPTORS 16u
#define VK_VEX_SURFACE_MAGIC 0x5658535552464143ull
#define VK_VEX_SCHEDULER_TICK_NS 10000000ull
static const uint8_t vk_cache_uuid[VK_UUID_SIZE]={'V','e','x','s','o','f','t','v','k','3','7','0','0','0','0','4'};












static int string_equals_local(const char *left, const char *right);
static int vk_clip_float_is_finite(float value);
static void vk_queue_worker_entry(void *opaque);
static void vk_queue_job_release_payload(vk_queue_job_t *job);



















#define VK_VEX_MAX_IMAGE_LAYERS 256u
#define VK_VEX_MAX_IMAGE_MIPS 13u

/* Layer-major mip chains. All currently supported formats have one aspect
   and four bytes per texel. Layout state belongs to this entry, never to the
   whole image. The allocation/backing and its lifetime remain image-owned. */


/* Borrowed execution view; no ownership or mutable global image state. */














/* All consumed dynamic state is snapshotted in a draw record. It cannot
   mutate a shared pipeline while another submission uses that pipeline. */






static int vk_ref_release_count(uint32_t *references);
static void vk_heap_free(void *ptr);


static void vk_event_release(vk_event_impl_t *e){
    if(e&&vk_ref_release_count(&e->references)){vex_event_counter_release(e->wake_counter);vk_heap_free(e);}
}











static void vk_heap_free(void *ptr);
static void vk_layout_release(vk_descriptor_set_layout_impl_t *layout);
static void vk_pipeline_layout_release(vk_pipeline_layout_impl_t *layout);
static void vk_image_view_release(vk_image_view_impl_t *view);
static void vk_render_pass_release(vk_render_pass_impl_t *render_pass);
static void vk_framebuffer_release(vk_framebuffer_impl_t *framebuffer);
static int vk_layout_compatible(vk_descriptor_set_layout_impl_t *left,
                                vk_descriptor_set_layout_impl_t *right);

/* Queue completion runs concurrently with host destruction. Keep every
   object refcount transition atomic so a handle dropped after submit cannot
   race the worker's final release into a use-after-free/double-free. */
static int vk_ref_retain_count(uint32_t *references) {
    uint32_t value;
    if (references == 0) return 0;
    value = __atomic_load_n(references, __ATOMIC_ACQUIRE);
    for (;;) {
        if (value == 0u || value == UINT32_MAX) return 0;
        if (__atomic_compare_exchange_n(references, &value, value + 1u, 0,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return 1;
    }
}

static int vk_ref_release_count(uint32_t *references) {
    uint32_t value;
    if (references == 0) return 0;
    value = __atomic_load_n(references, __ATOMIC_ACQUIRE);
    for (;;) {
        if (value == 0u) return 0;
        if (__atomic_compare_exchange_n(references, &value, value - 1u, 0,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return value == 1u;
    }
}

static int vk_shader_module_retain(vk_shader_module_impl_t *shader) {
    return shader != 0 && vk_ref_retain_count(&shader->references);
}

static void vk_shader_module_release(vk_shader_module_impl_t *shader) {
    if (shader == 0) return;
    if (vk_ref_release_count(&shader->references)) {
        vk_heap_free(shader->code);
        vk_heap_free(shader);
    }
}

static int vk_pipeline_retain(vk_pipeline_impl_t *pipeline) {
    return pipeline != 0 && vk_ref_retain_count(&pipeline->references);
}

static void vk_pipeline_release(vk_pipeline_impl_t *pipeline) {
    if (pipeline == 0) return;
    if (vk_ref_release_count(&pipeline->references)) {
        vk_pipeline_layout_release(pipeline->layout);
        vk_render_pass_release(pipeline->render_pass);
        vk_shader_module_release(pipeline->vertex_shader);
        vk_shader_module_release(pipeline->fragment_shader);
        vk_shader_module_release(pipeline->compute_shader);
        vk_heap_free(pipeline->compute_profile);
        vk_heap_free(pipeline->shader_workspace[0]);
        vk_heap_free(pipeline->shader_workspace[1]);
        vk_heap_free(pipeline->vertex_profile);
        vk_heap_free(pipeline->fragment_profile);
        vk_heap_free(pipeline);
    }
}

static int vk_render_pass_retain(vk_render_pass_impl_t *render_pass) {
    return render_pass != 0 && vk_ref_retain_count(&render_pass->references);
}

static void vk_render_pass_release(vk_render_pass_impl_t *render_pass) {
    if (render_pass != 0 && vk_ref_release_count(&render_pass->references))
        vk_heap_free(render_pass);
}

static int vk_framebuffer_retain(vk_framebuffer_impl_t *framebuffer) {
    return framebuffer != 0 && vk_ref_retain_count(&framebuffer->references);
}

static void vk_framebuffer_release(vk_framebuffer_impl_t *framebuffer) {
    if (framebuffer != 0 && vk_ref_release_count(&framebuffer->references)) {
        vk_image_view_release(framebuffer->attachment0);
        vk_image_view_release(framebuffer->depth_attachment);
        vk_image_view_release(framebuffer->resolve_attachment);
        vk_render_pass_release(framebuffer->render_pass);
        vk_heap_free(framebuffer);
    }
}

static int vk_memory_retain(vk_memory_impl_t *memory) {
    return memory != 0 && vk_ref_retain_count(&memory->references);
}

static void vk_memory_release(vk_memory_impl_t *memory) {
    if (memory != 0 && vk_ref_release_count(&memory->references)) {
        if (memory->mapped_addr != 0u && memory->surface_capability != 0u) {
            vex_surface_unmap_req_t unmap = {0};
            unmap.surface_handle = memory->surface_capability;
            unmap.mapped_addr = memory->mapped_addr;
            unmap.length = memory->backing_size;
            unmap.access = VEX_SURFACE_MAP_READ | VEX_SURFACE_MAP_WRITE;
            (void)vex_surface_unmap(&unmap);
        }
        if (memory->surface_capability != 0u) {
            (void)vex_gpu_surface_destroy(memory->surface_capability);
        }
        vk_heap_free(memory);
    }
}

static int vk_surface_valid(const vk_surface_impl_t *surface) {
    return surface != 0 && surface->magic == VK_VEX_SURFACE_MAGIC &&
           surface->capability != 0u && surface->pixels != 0;
}

static int vk_surface_retain(vk_surface_impl_t *surface) {
    return vk_surface_valid(surface) && surface->destroy_requested == 0u &&
        vk_ref_retain_count(&surface->references);
}

static void vk_surface_release(vk_surface_impl_t *surface) {
    if (surface != 0 && surface->magic == VK_VEX_SURFACE_MAGIC &&
        vk_ref_release_count(&surface->references)) {
        if (surface->mapped_addr != 0u) {
            vex_surface_unmap_req_t unmap = {0};
            unmap.surface_handle = surface->capability;
            unmap.mapped_addr = surface->mapped_addr;
            unmap.length = surface->mapped_length;
            unmap.access = VEX_SURFACE_MAP_READ | VEX_SURFACE_MAP_WRITE;
            (void)vex_surface_unmap(&unmap);
        }
        if (surface->capability != 0u)
            (void)vex_gpu_surface_destroy(surface->capability);
        surface->magic = 0u;
        vk_heap_free(surface);
    }
}

static int vk_memory_attach_surface(vk_memory_impl_t *memory,
                                   uint64_t surface_capability,
                                   VkDeviceSize requested_size) {
    vex_surface_query_req_t query = {0};
    vex_surface_map_req_t map = {0};
    uint64_t map_bytes;

    if (memory == 0 || surface_capability == 0u || requested_size == 0u ||
        vex_surface_retain(surface_capability) != 0) return -1;
    query.surface_handle = surface_capability;
    if (vex_surface_query(&query) != 0 ||
        query.backing_type != VEX_SURFACE_BACKING_CPU ||
        (query.capability_rights & (VEX_SURFACE_RIGHT_MAP_READ |
                                    VEX_SURFACE_RIGHT_MAP_WRITE)) !=
            (VEX_SURFACE_RIGHT_MAP_READ | VEX_SURFACE_RIGHT_MAP_WRITE) ||
        query.width == 0u || query.height == 0u || query.stride < query.width ||
        query.backing_size == 0u || query.backing_size > SIZE_MAX ||
        requested_size > query.backing_size ||
        (uint64_t)query.stride > UINT64_MAX / query.height / sizeof(uint32_t)) {
        (void)vex_gpu_surface_destroy(surface_capability);
        return -1;
    }
    map_bytes = (uint64_t)query.stride * query.height * sizeof(uint32_t);
    if (map_bytes < requested_size || map_bytes > query.backing_size) {
        (void)vex_gpu_surface_destroy(surface_capability);
        return -1;
    }
    map.surface_handle = surface_capability;
    map.length = query.backing_size;
    map.access = VEX_SURFACE_MAP_READ | VEX_SURFACE_MAP_WRITE;
    if (vex_surface_map(&map) != 0 || map.mapped_addr == 0u) {
        (void)vex_gpu_surface_destroy(surface_capability);
        return -1;
    }
    memory->bytes = (uint8_t *)(uintptr_t)map.mapped_addr;
    memory->size = requested_size;
    memory->backing_size = query.backing_size;
    memory->backing_identity = query.backing_identity;
    memory->surface_capability = surface_capability;
    memory->mapped_addr = map.mapped_addr;
    return 0;
}

static int vk_memory_create_vex_backing(vk_memory_impl_t *memory,
                                        VkDeviceSize requested_size) {
    vex_gpu_buffer_create_req_t buffer = {0};
    vex_gpu_surface_create_req_t surface = {0};
    uint64_t pixels;

    if (memory == 0 || requested_size == 0u ||
        requested_size > (uint64_t)4096u * 4096u * sizeof(uint32_t)) return -1;
    pixels = (requested_size + sizeof(uint32_t) - 1u) / sizeof(uint32_t);
    buffer.width = pixels > 4096u ? 4096u : (uint32_t)pixels;
    if (buffer.width == 0u) buffer.width = 1u;
    buffer.height = (uint32_t)((pixels + buffer.width - 1u) / buffer.width);
    if (buffer.height == 0u || buffer.height > 4096u) return -1;
    buffer.stride = buffer.width;
    buffer.format = VEX_GPU_FORMAT_XRGB8888;
    buffer.flags = VEX_GPU_MEMORY_CPU_VISIBLE;
    buffer.size = (size_t)buffer.width * buffer.height * sizeof(uint32_t);
    if (vex_gpu_buffer_create(&buffer) != 0 || buffer.handle == 0u) return -1;

    surface.buffer_handle = buffer.handle;
    surface.alpha_mode = VEX_GPU_SURFACE_ALPHA_OPAQUE;
    surface.usage = VEX_GPU_SURFACE_USAGE_SAMPLED;
    if (vex_gpu_surface_create(&surface) != 0 || surface.surface_handle == 0u) {
        (void)vex_gpu_buffer_destroy(buffer.handle);
        return -1;
    }
    /* The Surface backing retained the GPU resource; release its creator handle. */
    if (vex_gpu_buffer_destroy(buffer.handle) != 0) {
        (void)vex_gpu_surface_destroy(surface.surface_handle);
        return -1;
    }
    if (vk_memory_attach_surface(memory, surface.surface_handle, requested_size) != 0) {
        (void)vex_gpu_surface_destroy(surface.surface_handle);
        return -1;
    }
    /* vk_memory_attach_surface retained one local reference for VkDeviceMemory. */
    (void)vex_gpu_surface_destroy(surface.surface_handle);
    return 0;
}

static int vk_buffer_retain(vk_buffer_impl_t *buffer) {
    return buffer != 0 && vk_ref_retain_count(&buffer->references);
}

static void vk_buffer_release(vk_buffer_impl_t *buffer) {
    if (buffer != 0 && vk_ref_release_count(&buffer->references)) {
        vk_memory_release(buffer->memory);
        vk_heap_free(buffer);
    }
}

static int vk_image_retain(vk_image_impl_t *image) {
    return image != 0 && vk_ref_retain_count(&image->references);
}

static void vk_image_release(vk_image_impl_t *image) {
    if (image != 0 && vk_ref_release_count(&image->references)) {
        vk_memory_release(image->memory);
        if (image->owns_pixels != 0u) vk_heap_free(image->owned_pixels);
        if (image->available_event != 0u) {
            (void)vex_event_counter_release(image->available_event);
        }
        if (image->owns_surface != 0u && image->presentation_surface != 0u) {
            if (image->mapped_addr != 0u) {
                vex_surface_unmap_req_t unmap = {0};
                unmap.surface_handle = image->presentation_surface;
                unmap.mapped_addr = image->mapped_addr;
                unmap.length = image->mapped_length;
                unmap.access = VEX_SURFACE_MAP_READ | VEX_SURFACE_MAP_WRITE;
                (void)vex_surface_unmap(&unmap);
            }
            (void)vex_gpu_surface_destroy(image->presentation_surface);
        }
        vk_heap_free(image->subresources);
        vk_heap_free(image);
    }
}

typedef struct vk_fence_impl {
    vk_device_impl_t *device;
    VkBool32 signaled;
    uint32_t references;
    uint8_t destroy_requested;
    uint8_t pending;
    uint16_t reserved_lifetime;
    uint64_t kernel_queue;
    uint64_t serial;
} vk_fence_impl_t;

typedef struct vk_semaphore_impl {
    vk_device_impl_t *device;
    uint64_t event_capability;
    VkBool32 signaled;
    uint32_t references;
    uint32_t pending_external_waits;
    uint8_t destroy_requested;
} vk_semaphore_impl_t;

static int vk_fence_retain(vk_fence_impl_t *fence) {
    return fence != 0 && !fence->destroy_requested &&
        vk_ref_retain_count(&fence->references);
}

static void vk_fence_release(vk_fence_impl_t *fence) {
    if (fence != 0 && vk_ref_release_count(&fence->references))
        vk_heap_free(fence);
}

static VkResult vk_fence_refresh(vk_fence_impl_t *fence);

/* Binary Vulkan semaphores use Vex's scheduler-backed event-counter
   semaphore mode. The local bit is validation/accounting only; the token
   itself is kernel-owned and a wait blocks through the scheduler, never by
   polling in userspace. Vulkan requires callers to externally synchronize
   host access to queue operations and semaphore reuse. */
static VkResult vk_binary_semaphore_signal(vk_semaphore_impl_t *semaphore) {
    if (semaphore == 0 || semaphore->event_capability == 0u)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (vex_event_counter_signal(semaphore->event_capability, 1u) != 0)
        return VK_ERROR_DEVICE_LOST;
    return VK_SUCCESS;
}

static VkResult vk_binary_semaphore_wait(vk_semaphore_impl_t *semaphore) {
    uint64_t token = 0u;
    int read_result;
    if (semaphore == 0 || semaphore->event_capability == 0u)
        return VK_ERROR_INITIALIZATION_FAILED;
    read_result = vex_event_counter_read(semaphore->event_capability, &token,
                                          0u, 0u);
    if (read_result != 0) {
        static const char message[] =
            "vulkan queue: semaphore event wait failed\n";
        (void)vex_syscall1(VEX_SYS_DEBUG_LOG,
                           (uint64_t)(uintptr_t)message);
        return VK_ERROR_DEVICE_LOST;
    }
    if (token != 1u) {
        static const char message[] =
            "vulkan queue: semaphore event returned non-binary token\n";
        (void)vex_syscall1(VEX_SYS_DEBUG_LOG,
                           (uint64_t)(uintptr_t)message);
        return VK_ERROR_DEVICE_LOST;
    }
    return VK_SUCCESS;
}

static int vk_semaphore_retain(vk_semaphore_impl_t *semaphore) {
    uint32_t references;
    if (semaphore == 0 || semaphore->event_capability == 0u ||
        semaphore->destroy_requested != 0u) return 0;
    references = __atomic_load_n(&semaphore->references, __ATOMIC_ACQUIRE);
    for (;;) {
        if (references == 0u || references == UINT32_MAX) return 0;
        if (__atomic_compare_exchange_n(&semaphore->references, &references,
                references + 1u, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return 1;
    }
}

static void vk_semaphore_release(vk_semaphore_impl_t *semaphore) {
    if (semaphore == 0) return;
    if (__atomic_sub_fetch(&semaphore->references, 1u, __ATOMIC_ACQ_REL) == 0u) {
        const uint64_t capability = semaphore->event_capability;
        semaphore->event_capability = 0u;
        if (capability != 0u) (void)vex_event_counter_release(capability);
        vk_heap_free(semaphore);
    }
}

static void vk_semaphore_note_external_signal(vk_semaphore_impl_t *semaphore) {
    uint32_t pending;
    if (semaphore == 0) return;
    pending = __atomic_load_n(&semaphore->pending_external_waits,
                              __ATOMIC_ACQUIRE);
    while (pending != 0u) {
        if (__atomic_compare_exchange_n(&semaphore->pending_external_waits,
                &pending, pending - 1u, 0, __ATOMIC_ACQ_REL,
                __ATOMIC_ACQUIRE)) return;
    }
    __atomic_store_n(&semaphore->signaled, VK_TRUE, __ATOMIC_RELEASE);
}







#include "vulkan_texel.h"









static void vk_descriptor_set_release(vk_descriptor_set_impl_t *set);

static int vk_image_view_retain(vk_image_view_impl_t *view) {
    return view != 0 && vk_ref_retain_count(&view->references);
}

static void vk_image_view_release(vk_image_view_impl_t *view) {
    if (view != 0 && vk_ref_release_count(&view->references)) {
        vk_image_release(view->image);
        vk_heap_free(view);
    }
}

static int vk_sampler_retain(vk_sampler_impl_t *sampler) {
    return sampler != 0 && vk_ref_retain_count(&sampler->references);
}

static void vk_sampler_release(vk_sampler_impl_t *sampler) {
    if (sampler != 0 && vk_ref_release_count(&sampler->references))
        vk_heap_free(sampler);
}



static void vk_query_release(vk_query_pool_impl_t *p){
    if(p&&vk_ref_release_count(&p->references)){vex_event_counter_release(p->wake_counter);vk_heap_free(p->slots);vk_heap_free(p);}
}

static void *vk_heap_alloc(size_t size) {
    /* HEAP_FREE removes a page range, not an unknown-sized allocation. Keep
       its exact native size outside the public Vulkan object's storage. */
    if (size == 0u || size > SIZE_MAX - 16u - 4095u) return 0;
    uint64_t bytes = ((uint64_t)size + 16u + 4095u) & ~4095ull;
    uint64_t *base = (void *)(uintptr_t)vex_syscall1(VEX_SYS_HEAP_ALLOC, bytes);
    if (base == 0) return 0;
    base[0] = bytes;
    base[1] = 0x564b48454150414cull;
    return base + 2u;
}

static void vk_heap_free(void *ptr) {
    vex_heap_req_t req;
    if (ptr == 0) {
        return;
    }
    uint64_t *base = (uint64_t *)ptr - 2u;
    req.ptr = base;
    req.size = base[0];
    (void)vex_syscall1(VEX_SYS_HEAP_FREE, (uint64_t)(uintptr_t)&req);
}

static void vk_command_buffer_release(vk_command_buffer_impl_t *cmd);
static void vk_clear_records(vk_command_buffer_impl_t *cmd) {
    ++cmd->generation;
    vk_recorded_command_t *record = cmd->first_record;
    while (record != 0) {
        vk_recorded_command_t *next = record->next;
        switch (record->kind) {
            case VK_RECORD_CLEAR_ATTACHMENTS:vk_framebuffer_release(record->args.clear_attachments.framebuffer);break;
            case VK_RECORD_CLEAR_COLOR:
                vk_image_release(record->args.clear.image);
                break;
            case VK_RECORD_CLEAR_DEPTH_STENCIL:
                vk_image_release(record->args.clear_depth_stencil.image);
                break;
            case VK_RECORD_QUERY_RESET:case VK_RECORD_QUERY_BEGIN:case VK_RECORD_QUERY_END:case VK_RECORD_QUERY_COPY:
                vk_query_release(record->args.query.pool);vk_buffer_release(record->args.query.buffer);break;
            case VK_RECORD_WRITE_BUFFER:
                vk_buffer_release(record->args.write_buffer.buffer);vk_heap_free(record->args.write_buffer.data);break;
            case VK_RECORD_MEMORY_BARRIER:vk_buffer_release(record->args.memory_barrier.buffer);break;
            case VK_RECORD_EVENT_SET:case VK_RECORD_EVENT_RESET:case VK_RECORD_EVENT_WAIT:vk_event_release(record->args.event.event);break;
            case VK_RECORD_COPY_BUFFER:
                vk_buffer_release(record->args.copy_buffer.src);
                vk_buffer_release(record->args.copy_buffer.dst);
                break;
            case VK_RECORD_IMAGE_BARRIER:
                vk_image_release(record->args.image_barrier.image);
                break;
            case VK_RECORD_COPY_BUFFER_TO_IMAGE:
                vk_buffer_release(record->args.copy_buffer_to_image.src);
                vk_image_release(record->args.copy_buffer_to_image.dst);
                break;
            case VK_RECORD_COPY_IMAGE_TO_BUFFER:
                vk_image_release(record->args.copy_image_to_buffer.src);
                vk_buffer_release(record->args.copy_image_to_buffer.dst);
                break;
            case VK_RECORD_RESOLVE_IMAGE:
            case VK_RECORD_COPY_IMAGE:
                vk_image_release(record->args.copy_image.src);
                vk_image_release(record->args.copy_image.dst);
                break;
            case VK_RECORD_BLIT_IMAGE:
                vk_image_release(record->args.blit_image.src);
                vk_image_release(record->args.blit_image.dst);
                break;
            case VK_RECORD_BIND_DESCRIPTOR_SET:
                vk_descriptor_set_release(record->args.bind_descriptor_set.set);
                break;
            case VK_RECORD_BEGIN_RENDER_PASS:
                vk_framebuffer_release(record->args.begin_render_pass.framebuffer);
                break;
            case VK_RECORD_END_RENDER_PASS:
                vk_image_release(record->args.end_render_pass.image);
                vk_image_release(record->args.end_render_pass.depth_image);
                vk_framebuffer_release(record->args.end_render_pass.resolve_framebuffer);
                break;
            case VK_RECORD_EXECUTE_SECONDARY:
                vk_command_buffer_release(record->args.secondary.command);break;
            case VK_RECORD_DISPATCH:
                vk_pipeline_release(record->args.dispatch.pipeline);
                vk_buffer_release(record->args.dispatch.indirect);
                for(uint32_t i=0u;i<8u;++i)vk_descriptor_set_release(record->args.dispatch.sets[i]);
                break;
            case VK_RECORD_DRAW:
                vk_query_release(record->args.draw.query_pool);
                vk_pipeline_release(record->args.draw.pipeline);
                vk_framebuffer_release(record->args.draw.framebuffer);
                vk_buffer_release(record->args.draw.vertex_buffer);
                vk_buffer_release(record->args.draw.index_buffer);
                for (uint32_t i = 0u; i < 8u; ++i)
                    vk_descriptor_set_release(record->args.draw.descriptor_sets[i]);
                break;
        }
        vk_heap_free(record);
        record = next;
    }
    cmd->active_query=0;
    cmd->first_record = 0;
    cmd->last_record = 0;
    cmd->record_error = VK_SUCCESS;
    cmd->recording = 0u;
    cmd->executable = 0u;
    cmd->usage_flags = 0u;
    cmd->active_framebuffer = 0;
    cmd->active_render_pass = 0u;
    memset(&cmd->active_render_area, 0, sizeof(cmd->active_render_area));
    memset(cmd->bound_descriptor_sets, 0, sizeof(cmd->bound_descriptor_sets));
    vk_pipeline_release(cmd->pipeline);
    cmd->pipeline = 0;
    vk_pipeline_release(cmd->compute_pipeline);cmd->compute_pipeline=0;
    memset(cmd->compute_descriptor_sets,0,sizeof(cmd->compute_descriptor_sets));
    vk_buffer_release(cmd->vertex_buffer);
    cmd->vertex_buffer = 0;
    cmd->vertex_buffer_offset = 0u;
    vk_buffer_release(cmd->index_buffer);
    cmd->index_buffer = 0;
    cmd->index_buffer_offset = 0u;
    cmd->index_type = VK_INDEX_TYPE_UINT32;
    memset(cmd->push_constants, 0, sizeof(cmd->push_constants));
    memset(cmd->push_constant_initialized, 0, sizeof(cmd->push_constant_initialized));
}

static int vk_command_buffer_retain(vk_command_buffer_impl_t *cmd) {
    return cmd != 0 && vk_ref_retain_count(&cmd->references);
}

static void vk_command_buffer_release(vk_command_buffer_impl_t *cmd) {
    if (cmd != 0 && vk_ref_release_count(&cmd->references)) {
        vk_clear_records(cmd);
        vk_heap_free(cmd);
    }
}

static vk_recorded_command_t *vk_append_record(vk_command_buffer_impl_t *cmd, vk_record_kind_t kind) {
    vk_recorded_command_t *record;
    if (cmd == 0 || cmd->recording == 0u || cmd->record_error != VK_SUCCESS) {
        return 0;
    }
    record = (vk_recorded_command_t *)vk_heap_alloc(sizeof(*record));
    if (record == 0) {
        cmd->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
        return 0;
    }
    memset(record, 0, sizeof(*record));
    record->kind = kind;
    if (cmd->last_record != 0) {
        cmd->last_record->next = record;
    } else {
        cmd->first_record = record;
    }
    cmd->last_record = record;
    return record;
}

static void vk_record_unsupported(vk_command_buffer_impl_t *cmd) {
    if (cmd != 0 && cmd->recording != 0u && cmd->record_error == VK_SUCCESS) {
        cmd->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
    }
}

VkResult vkEnumerateInstanceExtensionProperties(const char *pLayerName,
                                                uint32_t *pPropertyCount,
                                                VkExtensionProperties *pProperties) {
    static const struct { const char *name; uint32_t version; } supported[] = {
        { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_SURFACE_SPEC_VERSION },
        { VK_VEX_SURFACE_EXTENSION_NAME, VK_VEX_SURFACE_SPEC_VERSION }
    };
    if (pPropertyCount == 0) return VK_ERROR_INITIALIZATION_FAILED;
    if (pLayerName != 0) return VK_ERROR_LAYER_NOT_PRESENT;
    if (pProperties == 0) {
        *pPropertyCount = (uint32_t)(sizeof(supported) / sizeof(supported[0]));
        return VK_SUCCESS;
    }
    {
        const uint32_t capacity = *pPropertyCount;
        const uint32_t total = (uint32_t)(sizeof(supported) / sizeof(supported[0]));
        const uint32_t count = capacity < total ? capacity : total;
        for (uint32_t i = 0u; i < count; ++i) {
            memset(&pProperties[i], 0, sizeof(pProperties[i]));
            memcpy(pProperties[i].extensionName, supported[i].name,
                   strlen(supported[i].name) + 1u);
            pProperties[i].specVersion = supported[i].version;
        }
        *pPropertyCount = count;
        return capacity < total ? VK_INCOMPLETE : VK_SUCCESS;
    }
}

VkResult vkEnumerateInstanceLayerProperties(uint32_t *pPropertyCount,
                                            VkLayerProperties *pProperties) {
    (void)pProperties;
    if (pPropertyCount == 0) return VK_ERROR_INITIALIZATION_FAILED;
    *pPropertyCount = 0u;
    return VK_SUCCESS;
}

VkResult vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice,
                                              const char *pLayerName,
                                              uint32_t *pPropertyCount,
                                              VkExtensionProperties *pProperties) {
    static const struct { const char *name; uint32_t version; } supported[] = {
        { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SWAPCHAIN_SPEC_VERSION }
    };
    if (physicalDevice == VK_NULL_HANDLE || pPropertyCount == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (pLayerName != 0) return VK_ERROR_LAYER_NOT_PRESENT;
    if (pProperties == 0) {
        *pPropertyCount = (uint32_t)(sizeof(supported) / sizeof(supported[0]));
        return VK_SUCCESS;
    }
    if (*pPropertyCount == 0u) return VK_INCOMPLETE;
    memset(&pProperties[0], 0, sizeof(pProperties[0]));
    memcpy(pProperties[0].extensionName, supported[0].name,
           strlen(supported[0].name) + 1u);
    pProperties[0].specVersion = supported[0].version;
    *pPropertyCount = 1u;
    return VK_SUCCESS;
}

VkResult vkEnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice,
                                          uint32_t *pPropertyCount,
                                          VkLayerProperties *pProperties) {
    (void)pProperties;
    if (physicalDevice == VK_NULL_HANDLE || pPropertyCount == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *pPropertyCount = 0u;
    return VK_SUCCESS;
}



static int vk_blend_factor_supported(VkBlendFactor factor, int alpha_channel) {
    switch (factor) {
        case VK_BLEND_FACTOR_ZERO:
        case VK_BLEND_FACTOR_ONE:
        case VK_BLEND_FACTOR_SRC_COLOR:
        case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
        case VK_BLEND_FACTOR_DST_COLOR:
        case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR:
        case VK_BLEND_FACTOR_SRC_ALPHA:
        case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
        case VK_BLEND_FACTOR_DST_ALPHA:
        case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:
        case VK_BLEND_FACTOR_CONSTANT_COLOR:
        case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:
        case VK_BLEND_FACTOR_CONSTANT_ALPHA:
        case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
            return 1;
        case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE:
            return !alpha_channel;
        default:
            return 0;
    }
}

static int vk_blend_op_supported(VkBlendOp op) {
    return op == VK_BLEND_OP_ADD || op == VK_BLEND_OP_SUBTRACT ||
           op == VK_BLEND_OP_REVERSE_SUBTRACT || op == VK_BLEND_OP_MIN ||
           op == VK_BLEND_OP_MAX;
}













static int string_equals_local(const char *left, const char *right) {
    size_t index = 0u;
    if (left == 0 || right == 0) {
        return 0;
    }
    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return 0;
        }
        ++index;
    }
    return left[index] == right[index];
}

static uint64_t vk_timeout_ns_to_ticks(uint64_t timeout_ns) {
    uint64_t ticks;
    if (timeout_ns == UINT64_MAX) return 0u; /* native zero means infinite */
    ticks = timeout_ns / VK_VEX_SCHEDULER_TICK_NS;
    if (timeout_ns % VK_VEX_SCHEDULER_TICK_NS != 0u) ++ticks;
    return ticks == 0u ? 1u : ticks;
}



/* Raster/shader colors use the canonical 0xAARRGGBB word. RGBA8 storage on
   little-endian Vex is 0xAABBGGRR, so swap only the red and blue bytes at the
   format boundary. */




static int vk_sample_count_supported(uint32_t n) {return n==1u||n==2u||n==4u||n==8u;}




static int vk_format_is_ds(uint32_t f) {return vk_format_has_depth(f)||vk_format_has_stencil(f);}

static uint32_t vk_aspect_bytes(VkFormat f,VkImageAspectFlags a) {
    return a==VK_IMAGE_ASPECT_STENCIL_BIT?1u:vk_format_has_depth(f)?4u:vk_format_texel_bytes(f);
}


static int vk_image_init_subresources(vk_image_impl_t *image,
                                      uint32_t mips, uint32_t layers,
                                      VkImageLayout initial) {
    uint64_t chain = 0u;
    uint32_t width = image->width, height = image->height;
    if (mips == 0u || mips > VK_VEX_MAX_IMAGE_MIPS ||
        layers == 0u || layers > VK_VEX_MAX_IMAGE_LAYERS) return 0;
    image->subresources = (vk_image_subresource_impl_t *)vk_heap_alloc(
        (size_t)mips * layers * sizeof(*image->subresources));
    if (image->subresources == 0) return 0;
    image->mip_levels = mips;
    image->array_layers = layers;
    if (image->samples == 0u) image->samples = 1u; /* WSI is single-sampled. */
    image->aspects = vk_format_is_ds(image->format) ? ((vk_format_has_depth(image->format)?VK_IMAGE_ASPECT_DEPTH_BIT:0u)|(vk_format_has_stencil(image->format)?VK_IMAGE_ASPECT_STENCIL_BIT:0u)) : VK_IMAGE_ASPECT_COLOR_BIT;
    for (uint32_t mip = 0u; mip < mips; ++mip) {
        vk_image_subresource_impl_t *sub = &image->subresources[mip];
        sub->offset = chain;
        sub->width = width;
        sub->height = height;
        sub->row_pitch = (uint64_t)width * vk_format_texel_bytes(image->format) * image->samples;
        sub->size = sub->row_pitch * height;
        sub->layout = sub->scheduled_layout = initial;
        chain += sub->size;
        width = width > 1u ? width / 2u : 1u;
        height = height > 1u ? height / 2u : 1u;
    }
    image->layer_pitch = chain;
    if (chain > SIZE_MAX / layers) {
        vk_heap_free(image->subresources);
        image->subresources = 0;
        return 0;
    }
    image->allocation_size = chain * layers;
    for (uint32_t layer = 1u; layer < layers; ++layer) {
        for (uint32_t mip = 0u; mip < mips; ++mip) {
            vk_image_subresource_impl_t *sub =
                &image->subresources[layer * mips + mip];
            *sub = image->subresources[mip];
            sub->offset += chain * layer;
        }
    }
    return 1;
}



static int vk_image_range(const vk_image_impl_t *image,
                          const VkImageSubresourceRange *input,
                          VkImageSubresourceRange *range) {
    if (image == 0 || input == 0 || range == 0 ||
        input->aspectMask == 0u || (input->aspectMask & ~image->aspects) != 0u ||
        input->baseMipLevel >= image->mip_levels ||
        input->baseArrayLayer >= image->array_layers) return 0;
    *range = *input;
    if (range->levelCount == VK_REMAINING_MIP_LEVELS)
        range->levelCount = image->mip_levels - range->baseMipLevel;
    if (range->layerCount == VK_REMAINING_ARRAY_LAYERS)
        range->layerCount = image->array_layers - range->baseArrayLayer;
    return range->levelCount != 0u && range->layerCount != 0u &&
        range->levelCount <= image->mip_levels - range->baseMipLevel &&
        range->layerCount <= image->array_layers - range->baseArrayLayer;
}

static int vk_image_layers(const vk_image_impl_t *image,
                           const VkImageSubresourceLayers *layers,
                           const VkOffset3D *offset, const VkExtent3D *extent) {
    vk_image_subresource_impl_t *sub;
    if (image == 0 || layers == 0 || offset == 0 || extent == 0 ||
        layers->aspectMask == 0u || (layers->aspectMask & ~image->aspects) != 0u ||
        (layers->aspectMask & (layers->aspectMask-1u)) != 0u || layers->layerCount == 0u ||
        layers->baseArrayLayer >= image->array_layers ||
        layers->layerCount > image->array_layers - layers->baseArrayLayer ||
        (sub = vk_image_subresource(image, layers->mipLevel,
                                    layers->baseArrayLayer)) == 0 ||
        offset->x < 0 || offset->y < 0 || offset->z != 0 ||
        extent->width == 0u || extent->height == 0u || extent->depth != 1u ||
        (uint32_t)offset->x > sub->width || (uint32_t)offset->y > sub->height)
        return 0;
    return extent->width <= sub->width - (uint32_t)offset->x &&
        extent->height <= sub->height - (uint32_t)offset->y;
}

static int vk_image_layout_supported(const vk_image_impl_t *image,
                                      VkImageLayout layout, int old) {
    switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED: return old;
    case VK_IMAGE_LAYOUT_GENERAL: return 1;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: return (image->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0u;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: return (image->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0u;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: return (image->usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0u;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: return image->aspects == VK_IMAGE_ASPECT_COLOR_BIT &&
        (image->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0u;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
        return (image->aspects & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0u &&
            (image->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0u;
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: return image->swapchain != 0;
    default: return 0;
    }
}







static uint32_t vk_view_subresource(vk_image_view_impl_t *view) {
    return view->range.baseArrayLayer * view->image->mip_levels +
        view->range.baseMipLevel;
}











VkResult vkCreateInstance(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkInstance *pInstance) {
    vk_instance_impl_t *instance;
    uint8_t khr_surface_enabled = 0u;
    uint8_t vex_surface_enabled = 0u;

    if (pInstance == 0 || pCreateInfo == 0 ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *pInstance = VK_NULL_HANDLE;
    if (pAllocator != 0 || pCreateInfo->pNext != 0 || pCreateInfo->flags != 0u) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (pCreateInfo->enabledLayerCount != 0u) {
        return VK_ERROR_LAYER_NOT_PRESENT;
    }
    if (pCreateInfo->enabledExtensionCount != 0u &&
        pCreateInfo->ppEnabledExtensionNames == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (uint32_t i = 0u; i < pCreateInfo->enabledExtensionCount; ++i) {
        const char *name = pCreateInfo->ppEnabledExtensionNames[i];
        if (string_equals_local(name, VK_KHR_SURFACE_EXTENSION_NAME)) {
            if (khr_surface_enabled != 0u) return VK_ERROR_INITIALIZATION_FAILED;
            khr_surface_enabled = 1u;
        } else if (string_equals_local(name, VK_VEX_SURFACE_EXTENSION_NAME)) {
            if (vex_surface_enabled != 0u) return VK_ERROR_INITIALIZATION_FAILED;
            vex_surface_enabled = 1u;
        } else {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }
    if (vex_surface_enabled != 0u && khr_surface_enabled == 0u) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    if (pCreateInfo->pApplicationInfo != 0) {
        const VkApplicationInfo *app = pCreateInfo->pApplicationInfo;
        if (app->sType != VK_STRUCTURE_TYPE_APPLICATION_INFO) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        if (app->pNext != 0 ||
            (app->apiVersion != 0u && app->apiVersion > VK_API_VERSION_1_0)) {
            return VK_ERROR_INCOMPATIBLE_DRIVER;
        }
    }
    instance = (vk_instance_impl_t *)vk_heap_alloc(sizeof(*instance));
    if (instance == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    instance->khr_surface_enabled = khr_surface_enabled;
    instance->vex_surface_enabled = vex_surface_enabled;
    *pInstance = (VkInstance)(uintptr_t)instance;
    return VK_SUCCESS;
}

void vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator) {
    vk_instance_impl_t *impl = (vk_instance_impl_t *)(uintptr_t)instance;
    (void)pAllocator;
    if (impl == 0) {
        return;
    }
    vk_heap_free(impl);
}

VkResult vkCreateVexSurfaceVEX(VkInstance instance, const VkVexSurfaceCreateInfoVEX *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface) {
    vk_instance_impl_t *instance_impl = (vk_instance_impl_t *)(uintptr_t)instance;
    vk_surface_impl_t *surface;
    vex_surface_query_req_t query = {0};
    vex_surface_map_req_t map = {0};
    uint64_t visible_bytes;
    if (instance_impl == 0 || pCreateInfo == 0 || pSurface == 0 ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_VEX_SURFACE_CREATE_INFO_EXPERIMENTAL ||
        pCreateInfo->pNext != 0 || pCreateInfo->flags != 0u ||
        pCreateInfo->surfaceCapability == 0u) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *pSurface = VK_NULL_HANDLE;
    if (pAllocator != 0 || !instance_impl->khr_surface_enabled ||
        !instance_impl->vex_surface_enabled) return VK_ERROR_EXTENSION_NOT_PRESENT;
    if (vex_surface_retain(pCreateInfo->surfaceCapability) != 0)
        return VK_ERROR_SURFACE_LOST_KHR;
    query.surface_handle = pCreateInfo->surfaceCapability;
    if (vex_surface_query(&query) != 0 ||
        query.backing_type != VEX_SURFACE_BACKING_CPU ||
        query.format != VEX_GPU_FORMAT_XRGB8888 ||
        (query.capability_rights & (VEX_SURFACE_RIGHT_PRESENT |
            VEX_SURFACE_RIGHT_MAP_READ | VEX_SURFACE_RIGHT_MAP_WRITE)) !=
            (VEX_SURFACE_RIGHT_PRESENT | VEX_SURFACE_RIGHT_MAP_READ |
             VEX_SURFACE_RIGHT_MAP_WRITE) ||
        query.width == 0u || query.height == 0u || query.width > 4096u ||
        query.height > 4096u || query.stride != query.width ||
        query.backing_size == 0u || query.backing_size > SIZE_MAX ||
        (uint64_t)query.stride > UINT64_MAX / query.height / sizeof(uint32_t)) {
        (void)vex_gpu_surface_destroy(pCreateInfo->surfaceCapability);
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    visible_bytes = (uint64_t)query.stride * query.height * sizeof(uint32_t);
    if (visible_bytes > query.backing_size) {
        (void)vex_gpu_surface_destroy(pCreateInfo->surfaceCapability);
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    surface = (vk_surface_impl_t *)vk_heap_alloc(sizeof(*surface));
    if (surface == 0) {
        (void)vex_gpu_surface_destroy(pCreateInfo->surfaceCapability);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memset(surface, 0, sizeof(*surface));
    map.surface_handle = pCreateInfo->surfaceCapability;
    map.length = query.backing_size;
    map.access = VEX_SURFACE_MAP_READ | VEX_SURFACE_MAP_WRITE;
    if (vex_surface_map(&map) != 0 || map.mapped_addr == 0u) {
        (void)vex_gpu_surface_destroy(pCreateInfo->surfaceCapability);
        vk_heap_free(surface);
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    surface->magic = VK_VEX_SURFACE_MAGIC;
    surface->instance = instance_impl;
    surface->capability = pCreateInfo->surfaceCapability;
    surface->mapped_addr = map.mapped_addr;
    surface->mapped_length = query.backing_size;
    surface->pixels = (uint32_t *)(uintptr_t)map.mapped_addr;
    surface->info = query;
    surface->references = 1u;
    surface->width = query.width;
    surface->height = query.height;
    *pSurface = (VkSurfaceKHR)(uintptr_t)surface;
    return VK_SUCCESS;
}

void vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks *pAllocator) {
    vk_instance_impl_t *instance_impl = (vk_instance_impl_t *)(uintptr_t)instance;
    vk_surface_impl_t *surface_impl = (vk_surface_impl_t *)(uintptr_t)surface;
    (void)pAllocator;
    if (surface_impl == 0 || surface_impl->magic != VK_VEX_SURFACE_MAGIC ||
        surface_impl->instance != instance_impl ||
        surface_impl->destroy_requested != 0u) return;
    surface_impl->destroy_requested = 1u;
    vk_surface_release(surface_impl);
}

VkResult vkEnumeratePhysicalDevices(VkInstance instance, uint32_t *pPhysicalDeviceCount, VkPhysicalDevice *pPhysicalDevices) {
    vk_instance_impl_t *impl = (vk_instance_impl_t *)(uintptr_t)instance;

    if (impl == 0 || pPhysicalDeviceCount == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (pPhysicalDevices == 0) {
        *pPhysicalDeviceCount = 1u;
        return VK_SUCCESS;
    }
    if (*pPhysicalDeviceCount == 0u) {
        return VK_INCOMPLETE;
    }
    pPhysicalDevices[0] = (VkPhysicalDevice)(uintptr_t)impl;
    *pPhysicalDeviceCount = 1u;
    return VK_SUCCESS;
}

void vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties *pProperties) {
    vk_instance_impl_t *impl = (vk_instance_impl_t *)(uintptr_t)physicalDevice;
    const char *name = "VexOS experimental software Vulkan";
    uint32_t i = 0u;

    if (impl == 0 || pProperties == 0) {
        return;
    }
    memset(pProperties, 0, sizeof(*pProperties));
    pProperties->apiVersion = VK_API_VERSION_1_0;
    pProperties->driverVersion = 1u;
    pProperties->vendorID = 0u;
    pProperties->deviceID = 0u;
    pProperties->deviceType = VK_PHYSICAL_DEVICE_TYPE_CPU;
    memcpy(pProperties->pipelineCacheUUID,vk_cache_uuid,VK_UUID_SIZE);
    pProperties->limits.maxImageDimension2D = 4096u;
    pProperties->limits.maxImageArrayLayers = VK_VEX_MAX_IMAGE_LAYERS;
    pProperties->limits.maxVertexInputAttributes = VK_VULKAN_MAX_VERTEX_INPUT_ATTRIBUTES;
    pProperties->limits.maxVertexInputBindings = 1u;
    pProperties->limits.maxVertexInputAttributeOffset = 2047u;
    pProperties->limits.maxVertexInputBindingStride = 2048u;
    pProperties->limits.maxVertexOutputComponents = 36u;
    pProperties->limits.maxFragmentInputComponents = 32u;
    pProperties->limits.maxFragmentOutputAttachments = 1u;
    pProperties->limits.maxFramebufferWidth = 4096u;
    pProperties->limits.maxFramebufferHeight = 4096u;
    pProperties->limits.maxFramebufferLayers = 1u;
    pProperties->limits.framebufferColorSampleCounts = 15u;
    pProperties->limits.framebufferDepthSampleCounts = 15u;
    pProperties->limits.framebufferStencilSampleCounts = 15u;
    pProperties->limits.standardSampleLocations = VK_TRUE;
    pProperties->limits.framebufferNoAttachmentsSampleCounts = 0u;
    pProperties->limits.sampledImageColorSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    pProperties->limits.maxColorAttachments = 1u;
    pProperties->limits.maxViewports = 1u;
    pProperties->limits.maxViewportDimensions[0] = 4096u;
    pProperties->limits.maxViewportDimensions[1] = 4096u;
    pProperties->limits.maxPushConstantsSize = 128u;
    pProperties->limits.maxBoundDescriptorSets = 8u;
    /* Reflection bounds shader-visible resources to 16 descriptors per stage.
       Descriptor layouts can contain 32 bindings of 256 elements in each of
       8 sets; fixed-size descriptor arrays share the per-stage resource limit. */
    pProperties->limits.maxUniformBufferRange = 65536u;
    pProperties->limits.maxComputeSharedMemorySize=16384u;
    pProperties->limits.maxComputeWorkGroupInvocations=128u;
    for(uint32_t axis=0u;axis<3u;++axis){
        pProperties->limits.maxComputeWorkGroupCount[axis]=65535u;
        pProperties->limits.maxComputeWorkGroupSize[axis]=axis==2u?64u:128u;
    }
    pProperties->limits.maxStorageBufferRange = 65536u;
    pProperties->limits.maxTexelBufferElements=65536u;
    pProperties->limits.minTexelBufferOffsetAlignment=4u;
    pProperties->limits.maxPerStageDescriptorSamplers = 16u;
    pProperties->limits.maxPerStageDescriptorUniformBuffers = 16u;
    pProperties->limits.maxPerStageDescriptorStorageBuffers = 16u;
    pProperties->limits.maxPerStageDescriptorStorageImages=16u;
    pProperties->limits.maxDescriptorSetStorageImages=65536u;
    pProperties->limits.maxPerStageDescriptorSampledImages = 16u;
    pProperties->limits.maxPerStageResources = VK_VULKAN_MAX_REFLECTED_DESCRIPTORS;
    pProperties->limits.maxDescriptorSetSamplers = 65536u;
    pProperties->limits.maxDescriptorSetUniformBuffers = 65536u;
    pProperties->limits.maxDescriptorSetStorageBuffers = 65536u;
    pProperties->limits.maxDescriptorSetSampledImages = 65536u;
    pProperties->limits.minUniformBufferOffsetAlignment = 16u;
    pProperties->limits.minStorageBufferOffsetAlignment = 16u;
    pProperties->limits.minTexelBufferOffsetAlignment=4u;
    pProperties->limits.subPixelPrecisionBits = 8u;
    pProperties->limits.minMemoryMapAlignment=4096u;
    pProperties->limits.nonCoherentAtomSize=1u;
    pProperties->limits.optimalBufferCopyOffsetAlignment=4u;
    pProperties->limits.optimalBufferCopyRowPitchAlignment=1u;
    pProperties->limits.viewportBoundsRange[0]=0.0f;pProperties->limits.viewportBoundsRange[1]=4096.0f;
    pProperties->limits.pointSizeRange[0]=pProperties->limits.pointSizeRange[1]=1.0f;
    pProperties->limits.lineWidthRange[0]=pProperties->limits.lineWidthRange[1]=1.0f;
    pProperties->limits.strictLines=VK_FALSE;
    while (name[i] != '\0' && i + 1u < sizeof(pProperties->deviceName)) {
        pProperties->deviceName[i] = name[i];
        ++i;
    }
    pProperties->deviceName[i] = '\0';
}

void vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties *pMemoryProperties) {
    vk_instance_impl_t *impl = (vk_instance_impl_t *)(uintptr_t)physicalDevice;

    if (impl == 0 || pMemoryProperties == 0) {
        return;
    }

    memset(pMemoryProperties, 0, sizeof(*pMemoryProperties));
    pMemoryProperties->memoryTypeCount = 1u;
    pMemoryProperties->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    pMemoryProperties->memoryTypes[0].heapIndex = 0u;
    pMemoryProperties->memoryHeapCount = 1u;
    {
        vex_bulk_result_t info;
        vex_bulk_req_t req;
        memset(&info, 0, sizeof(info));
        req.mask = VEX_BULK_MEMORY;
        req.result = &info;
        if ((int64_t)vex_syscall1(VEX_SYS_BULK, (uint64_t)(uintptr_t)&req) == 0 &&
            (info.valid_mask & VEX_BULK_MEMORY) != 0u &&
            info.total_pages <= UINT64_MAX / 4096u) {
            pMemoryProperties->memoryHeaps[0].size = info.total_pages * 4096u;
        }
    }
    pMemoryProperties->memoryHeaps[0].flags = 0u;
}

static int vk_vertex_format_info(VkFormat format, uint32_t *components_out,
                                 uint32_t *size_out, uint8_t *encoding_out,
                                 uint8_t *bgra_out);

void vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, uint32_t format, VkFormatProperties *pFormatProperties) {
    (void)physicalDevice;
    if (pFormatProperties == 0) {
        return;
    }
    memset(pFormatProperties, 0, sizeof(*pFormatProperties));
    if (vk_vertex_format_info((VkFormat)format, 0, 0, 0, 0)) {
        pFormatProperties->bufferFeatures = VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT;
    }
    if(vk_texel_format((VkFormat)format,0,0))pFormatProperties->bufferFeatures|=VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT|VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT;
    if(vk_texel_format((VkFormat)format,0,0))pFormatProperties->bufferFeatures|=VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT|VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT;
    if (vk_format_is_color_unorm(format)) {
        pFormatProperties->linearTilingFeatures = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
                                                  VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
                                                  VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                                                  VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                                  VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        pFormatProperties->optimalTilingFeatures = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
                                                   VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
                                                   VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                                                   VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                                   VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if(format==VK_FORMAT_R8G8B8A8_UNORM){pFormatProperties->linearTilingFeatures|=VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;pFormatProperties->optimalTilingFeatures|=VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;}
    } else if (vk_format_is_ds(format)) {
        pFormatProperties->optimalTilingFeatures =
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
            VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    }
}

void vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, VkQueueFamilyProperties *pQueueFamilyProperties) {
    (void)physicalDevice;
    if (pQueueFamilyPropertyCount == 0) {
        return;
    }
    if (pQueueFamilyProperties == 0) {
        *pQueueFamilyPropertyCount = 1u;
        return;
    }
    if (*pQueueFamilyPropertyCount == 0u) {
        return;
    }
    memset(&pQueueFamilyProperties[0], 0, sizeof(pQueueFamilyProperties[0]));
    pQueueFamilyProperties[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    pQueueFamilyProperties[0].queueCount = 1u;
    pQueueFamilyProperties[0].timestampValidBits = 0u;
    pQueueFamilyProperties[0].minImageTransferGranularity.width = 1u;
    pQueueFamilyProperties[0].minImageTransferGranularity.height = 1u;
    pQueueFamilyProperties[0].minImageTransferGranularity.depth = 1u;
    *pQueueFamilyPropertyCount = 1u;
}

VkResult vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32 *pSupported) {
    vk_instance_impl_t *instance = (vk_instance_impl_t *)(uintptr_t)physicalDevice;
    vk_surface_impl_t *surface_impl = (vk_surface_impl_t *)(uintptr_t)surface;
    if (pSupported == 0) return VK_ERROR_INITIALIZATION_FAILED;
    *pSupported = VK_FALSE;
    if (instance == 0 || !instance->khr_surface_enabled ||
        !vk_surface_valid(surface_impl) || surface_impl->instance != instance ||
        surface_impl->destroy_requested != 0u) return VK_ERROR_SURFACE_LOST_KHR;
    if (queueFamilyIndex != 0u) return VK_ERROR_INITIALIZATION_FAILED;
    *pSupported = VK_TRUE;
    return VK_SUCCESS;
}

VkResult vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR *pSurfaceCapabilities) {
    vk_instance_impl_t *instance = (vk_instance_impl_t *)(uintptr_t)physicalDevice;
    vk_surface_impl_t *surface_impl = (vk_surface_impl_t *)(uintptr_t)surface;
    if (pSurfaceCapabilities == 0) return VK_ERROR_INITIALIZATION_FAILED;
    memset(pSurfaceCapabilities, 0, sizeof(*pSurfaceCapabilities));
    if (instance == 0 || !instance->khr_surface_enabled ||
        !vk_surface_valid(surface_impl) || surface_impl->instance != instance ||
        surface_impl->destroy_requested != 0u) return VK_ERROR_SURFACE_LOST_KHR;
    pSurfaceCapabilities->minImageCount = 2u;
    pSurfaceCapabilities->maxImageCount = 4u;
    pSurfaceCapabilities->currentExtent.width = surface_impl->width;
    pSurfaceCapabilities->currentExtent.height = surface_impl->height;
    pSurfaceCapabilities->minImageExtent = pSurfaceCapabilities->currentExtent;
    pSurfaceCapabilities->maxImageExtent = pSurfaceCapabilities->currentExtent;
    pSurfaceCapabilities->maxImageArrayLayers = 1u;
    pSurfaceCapabilities->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pSurfaceCapabilities->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pSurfaceCapabilities->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    pSurfaceCapabilities->supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return VK_SUCCESS;
}

VkResult vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pSurfaceFormatCount, VkSurfaceFormatKHR *pSurfaceFormats) {
    vk_instance_impl_t *instance = (vk_instance_impl_t *)(uintptr_t)physicalDevice;
    vk_surface_impl_t *surface_impl = (vk_surface_impl_t *)(uintptr_t)surface;
    if (pSurfaceFormatCount == 0) return VK_ERROR_INITIALIZATION_FAILED;
    if (instance == 0 || !instance->khr_surface_enabled ||
        !vk_surface_valid(surface_impl) || surface_impl->instance != instance ||
        surface_impl->destroy_requested != 0u) return VK_ERROR_SURFACE_LOST_KHR;
    if (pSurfaceFormats == 0) {
        *pSurfaceFormatCount = 1u;
        return VK_SUCCESS;
    }
    if (*pSurfaceFormatCount == 0u) return VK_INCOMPLETE;
    pSurfaceFormats[0].format = VK_FORMAT_B8G8R8A8_UNORM;
    pSurfaceFormats[0].colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    *pSurfaceFormatCount = 1u;
    return VK_SUCCESS;
}

VkResult vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pPresentModeCount, VkPresentModeKHR *pPresentModes) {
    vk_instance_impl_t *instance = (vk_instance_impl_t *)(uintptr_t)physicalDevice;
    vk_surface_impl_t *surface_impl = (vk_surface_impl_t *)(uintptr_t)surface;
    if (pPresentModeCount == 0) return VK_ERROR_INITIALIZATION_FAILED;
    if (instance == 0 || !instance->khr_surface_enabled ||
        !vk_surface_valid(surface_impl) || surface_impl->instance != instance ||
        surface_impl->destroy_requested != 0u) return VK_ERROR_SURFACE_LOST_KHR;
    if (pPresentModes == 0) {
        *pPresentModeCount = 1u;
        return VK_SUCCESS;
    }
    if (*pPresentModeCount == 0u) return VK_INCOMPLETE;
    pPresentModes[0] = VK_PRESENT_MODE_FIFO_KHR;
    *pPresentModeCount = 1u;
    return VK_SUCCESS;
}
VkResult vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDevice *pDevice) {
    vk_instance_impl_t *instance = (vk_instance_impl_t *)(uintptr_t)physicalDevice;
    vk_device_impl_t *device;
    uint8_t swapchain_enabled = 0u;

    const VkDeviceQueueCreateInfo *queue_info;
    if (instance == 0 || pDevice == 0 || pCreateInfo == 0 ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *pDevice = VK_NULL_HANDLE;
    if (pAllocator != 0 || pCreateInfo->pNext != 0 || pCreateInfo->flags != 0u) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    /* Compare every requested public feature with the actual implementation. */
    if (pCreateInfo->pEnabledFeatures != 0) {
        VkPhysicalDeviceFeatures supported;
        vkGetPhysicalDeviceFeatures(physicalDevice, &supported);
        const VkBool32 *requested = (const VkBool32 *)pCreateInfo->pEnabledFeatures;
        const VkBool32 *available = (const VkBool32 *)&supported;
        for (size_t i = 0u; i < sizeof(supported) / sizeof(VkBool32); ++i)
            if (requested[i] != VK_FALSE && available[i] == VK_FALSE)
                return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (pCreateInfo->enabledLayerCount != 0u) {
        return VK_ERROR_LAYER_NOT_PRESENT;
    }
    if (pCreateInfo->enabledExtensionCount != 0u &&
        pCreateInfo->ppEnabledExtensionNames == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (uint32_t i = 0u; i < pCreateInfo->enabledExtensionCount; ++i) {
        const char *name = pCreateInfo->ppEnabledExtensionNames[i];
        if (string_equals_local(name, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            if (swapchain_enabled != 0u) return VK_ERROR_INITIALIZATION_FAILED;
            swapchain_enabled = 1u;
        } else {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }
    if (swapchain_enabled != 0u && instance->khr_surface_enabled == 0u) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    if (pCreateInfo->queueCreateInfoCount != 1u || pCreateInfo->pQueueCreateInfos == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    queue_info = pCreateInfo->pQueueCreateInfos;
    if (queue_info->sType != VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO ||
        queue_info->pNext != 0 || queue_info->flags != 0u ||
        queue_info->queueFamilyIndex != 0u || queue_info->queueCount != 1u ||
        queue_info->pQueuePriorities == 0 || queue_info->pQueuePriorities[0] < 0.0f ||
        queue_info->pQueuePriorities[0] > 1.0f) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    device = (vk_device_impl_t *)vk_heap_alloc(sizeof(*device));
    if (device == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memset(device, 0, sizeof(*device));
    device->instance = instance;
    device->swapchain_enabled = swapchain_enabled;
    if (pCreateInfo->pEnabledFeatures != 0) {
        device->uniform_array_dynamic = pCreateInfo->pEnabledFeatures->shaderUniformBufferArrayDynamicIndexing != VK_FALSE;
        device->sampled_array_dynamic = pCreateInfo->pEnabledFeatures->shaderSampledImageArrayDynamicIndexing != VK_FALSE;
        device->storage_buffer_array_dynamic=pCreateInfo->pEnabledFeatures->shaderStorageBufferArrayDynamicIndexing!=VK_FALSE;
        device->storage_image_array_dynamic=pCreateInfo->pEnabledFeatures->shaderStorageImageArrayDynamicIndexing!=VK_FALSE;
    device->occlusion_precise=pCreateInfo->pEnabledFeatures?pCreateInfo->pEnabledFeatures->occlusionQueryPrecise:VK_FALSE;
    device->logic_op=pCreateInfo->pEnabledFeatures?pCreateInfo->pEnabledFeatures->logicOp:VK_FALSE;
    }
    *pDevice = (VkDevice)(uintptr_t)device;
    return VK_SUCCESS;
}

void vkDestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator) {
    vk_device_impl_t *impl = (vk_device_impl_t *)(uintptr_t)device;
    (void)pAllocator;
    if (impl == 0) {
        return;
    }
    (void)vkDeviceWaitIdle(device);
    while (impl->queues != 0) {
        vk_queue_impl_t *queue = impl->queues;
        impl->queues = queue->next;
        __atomic_store_n(&queue->stop_worker, 1u, __ATOMIC_RELEASE);
        if (queue->work_event != 0u)
            (void)vex_event_counter_signal(queue->work_event, 1u);
        if (queue->worker_tid != 0u)
            (void)vex_thread_join(queue->worker_tid, 0);
        while (queue->pending_present_head != 0) {
            vk_pending_present_t *pending = queue->pending_present_head;
            queue->pending_present_head = pending->next;
            (void)vex_gpu_surface_destroy(pending->surface_capability);
            vk_heap_free(pending);
        }
        /* The current head is the MPSC queue's final dummy. Normally every
           preceding node was reclaimed by the worker; retain a defensive
           drain for a worker that reported device loss during teardown. */
        {
            vk_queue_job_t *node = queue->job_head;
            while (node != 0) {
                vk_queue_job_t *next = node->next;
                if (node != queue->initial_job &&
                    node->payload_released == 0u) {
                    vk_queue_job_release_payload(node);
                    node->payload_released = 1u;
                }
                if (node != queue->initial_job) vk_heap_free(node);
                node = next;
            }
            if (queue->initial_job != 0) vk_heap_free(queue->initial_job);
        }
        if (queue->work_event != 0u)
            (void)vex_event_counter_release(queue->work_event);
        (void)vex_gpu_queue_destroy(queue->kernel_queue);
        vk_heap_free(queue);
    }
    vk_heap_free(impl);
}

void vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue) {
    vk_device_impl_t *device_impl = (vk_device_impl_t *)(uintptr_t)device;
    vk_queue_impl_t *queue;
    if (pQueue == 0 || device_impl == 0) {
        return;
    }
    *pQueue = VK_NULL_HANDLE;
    if (queueFamilyIndex != 0u || queueIndex != 0u) return;
    for (queue = device_impl->queues; queue != 0; queue = queue->next) {
        if (queue->family == queueFamilyIndex && queue->index == queueIndex) {
            *pQueue = (VkQueue)(uintptr_t)queue;
            return;
        }
    }
    queue = (vk_queue_impl_t *)vk_heap_alloc(sizeof(*queue));
    if (queue == 0) {
        return;
    }
    memset(queue, 0, sizeof(*queue));
    queue->device = device_impl;
    queue->kernel_queue = vex_gpu_queue_create(queueFamilyIndex);
    if ((int64_t)queue->kernel_queue < 0 || queue->kernel_queue == 0u) {
        vk_heap_free(queue);
        return;
    }
    queue->work_event = vex_event_counter_create(0u, 0u);
    queue->initial_job = (vk_queue_job_t *)vk_heap_alloc(sizeof(*queue->initial_job));
    if (queue->work_event == 0u || (int64_t)queue->work_event < 0 ||
        queue->initial_job == 0) {
        if (queue->work_event != 0u && (int64_t)queue->work_event >= 0)
            (void)vex_event_counter_release(queue->work_event);
        if (queue->initial_job != 0) vk_heap_free(queue->initial_job);
        (void)vex_gpu_queue_destroy(queue->kernel_queue);
        vk_heap_free(queue);
        return;
    }
    memset(queue->initial_job, 0, sizeof(*queue->initial_job));
    queue->job_head = queue->initial_job;
    queue->job_tail = queue->initial_job;
    if (vex_thread_create(vk_queue_worker_entry, queue, 0u,
                          &queue->worker_tid) != 0 || queue->worker_tid == 0u) {
        (void)vex_event_counter_release(queue->work_event);
        vk_heap_free(queue->initial_job);
        (void)vex_gpu_queue_destroy(queue->kernel_queue);
        vk_heap_free(queue);
        return;
    }
    queue->family = queueFamilyIndex;
    queue->index = queueIndex;
    queue->next = device_impl->queues;
    device_impl->queues = queue;
    *pQueue = (VkQueue)(uintptr_t)queue;
}

static VkResult vk_swapchain_image_create_backing(vk_image_impl_t *image) {
    vex_gpu_buffer_create_req_t buffer = {0};
    vex_gpu_surface_create_req_t create_surface = {0};
    vex_surface_query_req_t query = {0};
    vex_surface_map_req_t map = {0};
    uint64_t pixel_count;
    uint64_t byte_count;

    if (image == 0 || image->width == 0u || image->height == 0u ||
        image->width > UINT32_MAX / image->height) return VK_ERROR_INITIALIZATION_FAILED;
    pixel_count = (uint64_t)image->width * image->height;
    if (pixel_count > SIZE_MAX / sizeof(uint32_t)) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    byte_count = pixel_count * sizeof(uint32_t);
    buffer.width = image->width;
    buffer.height = image->height;
    buffer.stride = image->width;
    buffer.format = VEX_GPU_FORMAT_XRGB8888;
    buffer.flags = VEX_GPU_MEMORY_CPU_VISIBLE;
    buffer.size = (size_t)byte_count;
    if (vex_gpu_buffer_create(&buffer) != 0 || buffer.handle == 0u)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    create_surface.buffer_handle = buffer.handle;
    create_surface.alpha_mode = VEX_GPU_SURFACE_ALPHA_OPAQUE;
    create_surface.usage = VEX_GPU_SURFACE_USAGE_SAMPLED;
    if (vex_gpu_surface_create(&create_surface) != 0 ||
        create_surface.surface_handle == 0u) {
        (void)vex_gpu_buffer_destroy(buffer.handle);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    if (vex_gpu_buffer_destroy(buffer.handle) != 0) {
        (void)vex_gpu_surface_destroy(create_surface.surface_handle);
        return VK_ERROR_DEVICE_LOST;
    }
    query.surface_handle = create_surface.surface_handle;
    if (vex_surface_query(&query) != 0 ||
        query.width != image->width || query.height != image->height ||
        query.stride < query.width || query.backing_size < byte_count ||
        (query.capability_rights & (VEX_SURFACE_RIGHT_PRESENT |
            VEX_SURFACE_RIGHT_MAP_READ | VEX_SURFACE_RIGHT_MAP_WRITE)) !=
            (VEX_SURFACE_RIGHT_PRESENT | VEX_SURFACE_RIGHT_MAP_READ |
             VEX_SURFACE_RIGHT_MAP_WRITE)) {
        (void)vex_gpu_surface_destroy(create_surface.surface_handle);
        return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    }
    map.surface_handle = create_surface.surface_handle;
    map.length = query.backing_size;
    map.access = VEX_SURFACE_MAP_READ | VEX_SURFACE_MAP_WRITE;
    if (vex_surface_map(&map) != 0 || map.mapped_addr == 0u) {
        (void)vex_gpu_surface_destroy(create_surface.surface_handle);
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    image->presentation_surface = create_surface.surface_handle;
    image->mapped_addr = map.mapped_addr;
    image->mapped_length = query.backing_size;
    image->backing_identity = query.backing_identity;
    image->owned_pixels = (uint32_t *)(uintptr_t)map.mapped_addr;
    image->owned_stride = query.stride;
    image->owns_surface = 1u;
    return VK_SUCCESS;
}

VkResult vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain) {
    vk_device_impl_t *device_impl = (vk_device_impl_t *)(uintptr_t)device;
    vk_surface_impl_t *surface = pCreateInfo != 0
        ? (vk_surface_impl_t *)(uintptr_t)pCreateInfo->surface : 0;
    vk_swapchain_impl_t *old_swapchain = pCreateInfo != 0 &&
        pCreateInfo->oldSwapchain != VK_NULL_HANDLE
        ? (vk_swapchain_impl_t *)(uintptr_t)pCreateInfo->oldSwapchain : 0;
    vk_swapchain_impl_t *swapchain;
    uint32_t image_count;
    uint32_t first_private_image;
    VkResult result = VK_SUCCESS;

    if (device_impl == 0 || pCreateInfo == 0 || pSwapchain == 0 ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR)
        return VK_ERROR_INITIALIZATION_FAILED;
    *pSwapchain = VK_NULL_HANDLE;
    if (pAllocator != 0 || !device_impl->swapchain_enabled ||
        pCreateInfo->pNext != 0 || pCreateInfo->flags != 0u ||
        !vk_surface_valid(surface) || surface->instance != device_impl->instance ||
        surface->destroy_requested != 0u) return VK_ERROR_EXTENSION_NOT_PRESENT;
    if (pCreateInfo->minImageCount < 2u || pCreateInfo->minImageCount > 4u ||
        pCreateInfo->imageArrayLayers != 1u ||
        pCreateInfo->imageFormat != VK_FORMAT_B8G8R8A8_UNORM ||
        pCreateInfo->imageColorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ||
        pCreateInfo->imageExtent.width != surface->width ||
        pCreateInfo->imageExtent.height != surface->height ||
        pCreateInfo->imageExtent.width == 0u || pCreateInfo->imageExtent.height == 0u ||
        (pCreateInfo->imageUsage & ~(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
          VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) != 0u ||
        (pCreateInfo->imageUsage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0u ||
        pCreateInfo->imageSharingMode != VK_SHARING_MODE_EXCLUSIVE ||
        pCreateInfo->queueFamilyIndexCount != 0u ||
        pCreateInfo->pQueueFamilyIndices != 0 ||
        pCreateInfo->preTransform != VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR ||
        pCreateInfo->compositeAlpha != VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR ||
        pCreateInfo->presentMode != VK_PRESENT_MODE_FIFO_KHR) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (old_swapchain != 0) {
        if (old_swapchain->device != device_impl || old_swapchain->surface != surface ||
            old_swapchain->destroy_requested != 0u || old_swapchain->retired != 0u ||
            surface->active_swapchain != old_swapchain)
            return VK_ERROR_OUT_OF_DATE_KHR;
    } else if (surface->active_swapchain != 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!vk_surface_retain(surface)) return VK_ERROR_SURFACE_LOST_KHR;
    image_count = pCreateInfo->minImageCount;
    first_private_image = old_swapchain == 0 ? 1u : 0u;
    swapchain = (vk_swapchain_impl_t *)vk_heap_alloc(sizeof(*swapchain));
    if (swapchain == 0) {
        vk_surface_release(surface);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memset(swapchain, 0, sizeof(*swapchain));
    swapchain->images = (VkImage *)vk_heap_alloc(
        (size_t)image_count * sizeof(*swapchain->images));
    if (swapchain->images == 0) {
        vk_heap_free(swapchain);
        vk_surface_release(surface);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memset(swapchain->images, 0, (size_t)image_count * sizeof(*swapchain->images));
    swapchain->device = device_impl;
    swapchain->surface = surface;
    swapchain->image_count = image_count;
    swapchain->present_x = surface->x;
    swapchain->present_y = surface->y;
    swapchain->available_event = vex_event_counter_create(
        image_count, VEX_EVENT_COUNTER_SEMAPHORE);
    if (swapchain->available_event == 0u ||
        swapchain->available_event == UINT64_MAX) {
        result = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto rollback;
    }
    for (uint32_t i = 0u; i < image_count; ++i) {
        vk_image_impl_t *image = (vk_image_impl_t *)vk_heap_alloc(sizeof(*image));
        if (image == 0) {
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto rollback;
        }
        memset(image, 0, sizeof(*image));
        image->device = device_impl;
        image->swapchain = swapchain;
        image->index = i;
        image->width = surface->width;
        image->height = surface->height;
        image->format = VK_FORMAT_B8G8R8A8_UNORM;
        image->tiling = VK_IMAGE_TILING_OPTIMAL;
        if (!vk_image_init_subresources(image, 1u, 1u, VK_IMAGE_LAYOUT_UNDEFINED)) {
            vk_heap_free(image);
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto rollback;
        }
        image->backing_identity = surface->info.backing_identity;
        image->usage = pCreateInfo->imageUsage;
        image->references = 1u;
        image->presentation_surface = i == 0u && old_swapchain == 0
            ? surface->capability : 0u;
        image->owned_pixels = i == 0u && old_swapchain == 0
            ? surface->pixels : 0;
        image->owned_stride = i == 0u && old_swapchain == 0
            ? surface->info.stride : 0u;
        image->available_event = vex_event_counter_create(
            1u, VEX_EVENT_COUNTER_SEMAPHORE);
        if (image->available_event == 0u ||
            image->available_event == UINT64_MAX) {
            image->swapchain = 0;
            image->destroy_requested = 1u;
            vk_image_release(image);
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto rollback;
        }
        if (i >= first_private_image) {
            result = vk_swapchain_image_create_backing(image);
            if (result != VK_SUCCESS) {
                image->swapchain = 0;
                image->destroy_requested = 1u;
                vk_image_release(image);
                goto rollback;
            }
        }
        swapchain->images[i] = (VkImage)(uintptr_t)image;
    }
    if (old_swapchain != 0) old_swapchain->retired = 1u;
    surface->active_swapchain = swapchain;
    *pSwapchain = (VkSwapchainKHR)(uintptr_t)swapchain;
    return VK_SUCCESS;

rollback:
    for (uint32_t i = 0u; i < image_count; ++i) {
        vk_image_impl_t *image = (vk_image_impl_t *)(uintptr_t)swapchain->images[i];
        if (image != 0) {
            image->swapchain = 0;
            image->destroy_requested = 1u;
            vk_image_release(image);
        }
    }
    if (swapchain->available_event != 0u)
        (void)vex_event_counter_release(swapchain->available_event);
    vk_heap_free(swapchain->images);
    vk_heap_free(swapchain);
    vk_surface_release(surface);
    return result;
}
void vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *pAllocator) {
    vk_device_impl_t *device_impl = (vk_device_impl_t *)(uintptr_t)device;
    vk_swapchain_impl_t *impl = (vk_swapchain_impl_t *)(uintptr_t)swapchain;
    (void)pAllocator;
    if (impl == 0 || impl->destroy_requested != 0u || impl->device != device_impl)
        return;
    /* A swapchain's image backings remain pinned by compositor jobs until
       completion. Drain those jobs before dropping the last local handles. */
    (void)vkDeviceWaitIdle(device);
    impl->destroy_requested = 1u;
    if (impl->images != 0) {
      for (uint32_t i = 0u; i < impl->image_count; ++i) {
        vk_image_impl_t *image = (vk_image_impl_t *)(uintptr_t)impl->images[i];
        if (image != 0 && image->swapchain == impl) {
            image->swapchain = 0;
            image->destroy_requested = 1u;
            vk_image_release(image);
        }
      }
    }
    if (impl->surface != 0) {
        if (impl->surface->active_swapchain == impl)
            impl->surface->active_swapchain = 0;
        vk_surface_release(impl->surface);
    }
    if (impl->available_event != 0u) {
        (void)vex_event_counter_release(impl->available_event);
        impl->available_event = 0u;
    }
    vk_heap_free(impl->images);
    vk_heap_free(impl);
}

VkResult vkGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t *pSwapchainImageCount, VkImage *pSwapchainImages) {
    vk_device_impl_t *device_impl = (vk_device_impl_t *)(uintptr_t)device;
    vk_swapchain_impl_t *impl = (vk_swapchain_impl_t *)(uintptr_t)swapchain;
    if (pSwapchainImageCount == 0 || device_impl == 0 || impl == 0 ||
        impl->device != device_impl || impl->destroy_requested != 0u)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (pSwapchainImages == 0) {
        *pSwapchainImageCount = impl->image_count;
        return VK_SUCCESS;
    }
    {
        const uint32_t capacity = *pSwapchainImageCount;
        const uint32_t count = capacity < impl->image_count
            ? capacity : impl->image_count;
        for (uint32_t i = 0u; i < count; ++i) pSwapchainImages[i] = impl->images[i];
        *pSwapchainImageCount = count;
        return count < impl->image_count ? VK_INCOMPLETE : VK_SUCCESS;
    }
}

VkResult vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex) {
    vk_device_impl_t *device_impl = (vk_device_impl_t *)(uintptr_t)device;
    vk_swapchain_impl_t *impl = (vk_swapchain_impl_t *)(uintptr_t)swapchain;
    vk_semaphore_impl_t *sem = (vk_semaphore_impl_t *)(uintptr_t)semaphore;
    vk_fence_impl_t *fence_impl = (vk_fence_impl_t *)(uintptr_t)fence;
    if (pImageIndex == 0 || device_impl == 0 || impl == 0 ||
        impl->device != device_impl || impl->destroy_requested != 0u)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (impl->retired != 0u) return VK_ERROR_OUT_OF_DATE_KHR;
    if (semaphore != VK_NULL_HANDLE &&
        (sem == 0 || sem->device != device_impl ||
         __atomic_load_n(&sem->signaled, __ATOMIC_ACQUIRE) != VK_FALSE ||
         sem->destroy_requested != 0u))
        return VK_ERROR_INITIALIZATION_FAILED;
    if (fence != VK_NULL_HANDLE &&
        (fence_impl == 0 || fence_impl->device != device_impl ||
         fence_impl->destroy_requested != 0u || fence_impl->pending != 0u ||
         fence_impl->signaled != VK_FALSE)) return VK_ERROR_INITIALIZATION_FAILED;
    {
        uint64_t token = 0u;
        const uint32_t flags = timeout == 0u
            ? VEX_EVENT_COUNTER_NONBLOCK : 0u;
        const int waited = vex_event_counter_read(impl->available_event,
            &token, flags, timeout == 0u ? 0u : vk_timeout_ns_to_ticks(timeout));
        if (waited == -2) return timeout == 0u ? VK_NOT_READY : VK_TIMEOUT;
        if (waited == -4) return VK_TIMEOUT;
        if (waited != 0 || token != 1u) return VK_ERROR_DEVICE_LOST;
        for (uint32_t offset = 0u; offset < impl->image_count; ++offset) {
            const uint32_t i = (impl->next_acquire_index + offset) % impl->image_count;
            vk_image_impl_t *image = (vk_image_impl_t *)(uintptr_t)impl->images[i];
            uint64_t image_token = 0u;
            uint32_t state;
            int read_result;
            if (image == 0 || image->available_event == 0u) continue;
            read_result = vex_event_counter_read(image->available_event,
                &image_token, VEX_EVENT_COUNTER_NONBLOCK, 0u);
            if (read_result == -2) continue;
            if (read_result != 0 || image_token != 1u) {
                (void)vex_event_counter_signal(impl->available_event, 1u);
                return VK_ERROR_DEVICE_LOST;
            }
            state = __atomic_load_n(&image->wsi_state, __ATOMIC_ACQUIRE);
            while (state == 0u || state == 2u) {
                if (__atomic_compare_exchange_n(&image->wsi_state, &state, 1u,
                        0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                    *pImageIndex = i;
                    impl->next_acquire_index = (i + 1u) % impl->image_count;
                    if (sem != 0) {
                        const VkResult signal_result =
                            vk_binary_semaphore_signal(sem);
                        if (signal_result != VK_SUCCESS) {
                            __atomic_store_n(&image->wsi_state, 0u,
                                             __ATOMIC_RELEASE);
                            (void)vex_event_counter_signal(image->available_event,
                                                           1u);
                            (void)vex_event_counter_signal(impl->available_event,
                                                           1u);
                            return signal_result;
                        }
                        vk_semaphore_note_external_signal(sem);
                    }
                    if (fence_impl != 0) fence_impl->signaled = VK_TRUE;
                    return VK_SUCCESS;
                }
            }
            (void)vex_event_counter_signal(image->available_event, 1u);
        }
        /* Global and per-image credits are one invariant. Restore the global
           credit if no matching image token was found. */
        (void)vex_event_counter_signal(impl->available_event, 1u);
        return VK_ERROR_DEVICE_LOST;
    }
}
VkResult vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool) {
    vk_command_pool_impl_t *pool;
    if (device == VK_NULL_HANDLE || pCommandPool == 0 || pCreateInfo == 0 ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO ||
        pCreateInfo->queueFamilyIndex != 0u) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *pCommandPool = VK_NULL_HANDLE;
    if (pAllocator != 0 || pCreateInfo->pNext != 0) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    pool = (vk_command_pool_impl_t *)vk_heap_alloc(sizeof(*pool));
    if (pool == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    pool->device = (vk_device_impl_t *)(uintptr_t)device;
    pool->first_buffer = 0;
    pool->flags = pCreateInfo->flags;
    *pCommandPool = (VkCommandPool)(uintptr_t)pool;
    return VK_SUCCESS;
}

void vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks *pAllocator) {
    vk_command_pool_impl_t *pool = (vk_command_pool_impl_t *)(uintptr_t)commandPool;
    (void)device;
    (void)pAllocator;
    if (pool == 0) return;
    while (pool->first_buffer != 0) {
        vk_command_buffer_impl_t *cmd = pool->first_buffer;
        pool->first_buffer = cmd->next_in_pool;
        cmd->next_in_pool = 0;
        cmd->destroy_requested = 1u;
        cmd->pool = 0;
        vk_command_buffer_release(cmd);
    }
    vk_heap_free(pool);
}

VkResult vkResetCommandPool(VkDevice device, VkCommandPool commandPool, VkFlags flags) {
    vk_command_pool_impl_t *pool = (vk_command_pool_impl_t *)(uintptr_t)commandPool;
    if (pool == 0 || pool->device != (vk_device_impl_t *)(uintptr_t)device || flags != 0u) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (vk_command_buffer_impl_t *cmd = pool->first_buffer; cmd != 0; cmd = cmd->next_in_pool) {
        if (cmd->pending_submissions != 0u) return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (vk_command_buffer_impl_t *cmd = pool->first_buffer; cmd != 0; cmd = cmd->next_in_pool) {
        vk_clear_records(cmd);
        cmd->bound_image = VK_NULL_HANDLE;
    }
    return VK_SUCCESS;
}

VkResult vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo, VkCommandBuffer *pCommandBuffers) {
    vk_command_pool_impl_t *pool;
    (void)device;
    if (pAllocateInfo == 0 || pCommandBuffers == 0 ||
        pAllocateInfo->sType != VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO ||
        pAllocateInfo->pNext != 0 || pAllocateInfo->commandBufferCount == 0u ||
        (pAllocateInfo->level != VK_COMMAND_BUFFER_LEVEL_PRIMARY&&pAllocateInfo->level!=VK_COMMAND_BUFFER_LEVEL_SECONDARY)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    pool = (vk_command_pool_impl_t *)(uintptr_t)pAllocateInfo->commandPool;
    if (pool == 0 || pool->device != (vk_device_impl_t *)(uintptr_t)device) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (uint32_t i = 0u; i < pAllocateInfo->commandBufferCount; ++i) {
        pCommandBuffers[i] = VK_NULL_HANDLE;
    }
    for (uint32_t i = 0u; i < pAllocateInfo->commandBufferCount; ++i) {
        vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)vk_heap_alloc(sizeof(*cmd));
        if (cmd == 0) {
            vkFreeCommandBuffers(device, pAllocateInfo->commandPool, i, pCommandBuffers);
            for (uint32_t j = 0u; j < i; ++j) pCommandBuffers[j] = VK_NULL_HANDLE;
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        memset(cmd, 0, sizeof(*cmd));
        cmd->device = pool->device;
        cmd->level=pAllocateInfo->level;
        cmd->pool = pool;
        cmd->next_in_pool = pool->first_buffer;
        pool->first_buffer = cmd;
        cmd->active_query=0;
    cmd->first_record = 0;
        cmd->last_record = 0;
        cmd->record_error = VK_SUCCESS;
        cmd->recording = 0u;
        cmd->executable = 0u;
        cmd->destroy_requested = 0u;
        cmd->usage_flags = 0u;
        cmd->references = 1u;
        cmd->pending_submissions = 0u;
        cmd->bound_image = 0;
        cmd->framebuffer = 0;
        cmd->pipeline = 0;
        cmd->vertex_buffer = 0;
        cmd->vertex_buffer_offset = 0u;
        cmd->index_buffer = 0;
        cmd->index_buffer_offset = 0u;
        cmd->index_type = VK_INDEX_TYPE_UINT32;
        memset(&cmd->viewport, 0, sizeof(cmd->viewport));
        memset(&cmd->scissor, 0, sizeof(cmd->scissor));
        cmd->viewport_set = 0u;
        cmd->scissor_set = 0u;
    cmd->dynamic_set=0u;
        cmd->active_framebuffer = 0;
        cmd->active_render_pass = 0u;
        memset(cmd->push_constants, 0, sizeof(cmd->push_constants));
        memset(cmd->push_constant_initialized, 0, sizeof(cmd->push_constant_initialized));
        pCommandBuffers[i] = (VkCommandBuffer)(uintptr_t)cmd;
    }
    return VK_SUCCESS;
}

void vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers) {
    vk_command_pool_impl_t *pool = (vk_command_pool_impl_t *)(uintptr_t)commandPool;
    if (pCommandBuffers == 0 || pool == 0 || pool->device != (vk_device_impl_t *)(uintptr_t)device) {
        return;
    }
    for (uint32_t i = 0u; i < commandBufferCount; ++i) {
        vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)pCommandBuffers[i];
        vk_command_buffer_impl_t **link = &pool->first_buffer;
        if (cmd == 0 || cmd->pool != pool || cmd->destroy_requested != 0u) {
            continue;
        }
        while (*link != 0 && *link != cmd) link = &(*link)->next_in_pool;
        if (*link != cmd) continue;
        *link = cmd->next_in_pool;
        cmd->next_in_pool = 0;
        cmd->destroy_requested = 1u;
        cmd->pool = 0;
        vk_command_buffer_release(cmd);
    }
}

VkResult vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    const VkCommandBufferUsageFlags supported_usage =
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT |
        VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    if (cmd == 0 || cmd->destroy_requested != 0u || cmd->pool == 0 ||
        cmd->pending_submissions != 0u || pBeginInfo == 0 ||
        pBeginInfo->sType != VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO ||
        pBeginInfo->pNext != 0 ||
        (cmd->level==VK_COMMAND_BUFFER_LEVEL_PRIMARY&&pBeginInfo->pInheritanceInfo!=0) ||
        (pBeginInfo->flags & ~supported_usage) != 0u ||
        (pBeginInfo->flags & supported_usage) == supported_usage) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if(cmd->level==VK_COMMAND_BUFFER_LEVEL_SECONDARY){
        const VkCommandBufferInheritanceInfo *in=pBeginInfo->pInheritanceInfo;
        if(!in||in->sType!=VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO||in->pNext||in->renderPass||in->framebuffer||in->subpass||in->occlusionQueryEnable||in->queryFlags||in->pipelineStatistics)return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    vk_clear_records(cmd);
    cmd->usage_flags = pBeginInfo->flags;
    cmd->bound_image = 0;
    cmd->framebuffer = 0;
    cmd->pipeline = 0;
    cmd->vertex_buffer = 0;
    cmd->vertex_buffer_offset = 0u;
    cmd->index_buffer = 0;
        cmd->index_buffer_offset = 0u;
    cmd->index_type = VK_INDEX_TYPE_UINT32;
    memset(&cmd->viewport, 0, sizeof(cmd->viewport));
    memset(&cmd->scissor, 0, sizeof(cmd->scissor));
    cmd->viewport_set = 0u;
    cmd->scissor_set = 0u;
    cmd->dynamic_set=0u;
    cmd->active_framebuffer = 0;
    cmd->active_render_pass = 0u;
    memset(cmd->push_constants, 0, sizeof(cmd->push_constants));
    memset(cmd->push_constant_initialized, 0, sizeof(cmd->push_constant_initialized));
    cmd->recording = 1u;
        return VK_SUCCESS;
    }

VkResult vkEndCommandBuffer(VkCommandBuffer commandBuffer) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    if (cmd == 0 || cmd->recording == 0u) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if ((cmd->active_framebuffer != 0 || cmd->active_query!=0) && cmd->record_error == VK_SUCCESS) {
        cmd->record_error = VK_ERROR_INITIALIZATION_FAILED;
    }
    cmd->recording = 0u;
    cmd->executable = cmd->record_error == VK_SUCCESS ? 1u : 0u;
    return cmd->record_error;
}

VkResult vkResetCommandBuffer(VkCommandBuffer commandBuffer, VkFlags flags) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    if (cmd == 0 || cmd->destroy_requested != 0u || cmd->pool == 0 ||
        cmd->pending_submissions != 0u || flags != 0u ||
        (cmd->pool->flags & VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT) == 0u) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    vk_clear_records(cmd);
    cmd->bound_image = 0;
    cmd->framebuffer = 0;
    cmd->pipeline = 0;
    cmd->vertex_buffer = 0;
    cmd->vertex_buffer_offset = 0u;
    cmd->index_buffer = 0;
    cmd->index_buffer_offset = 0u;
    cmd->index_type = VK_INDEX_TYPE_UINT32;
    memset(&cmd->viewport, 0, sizeof(cmd->viewport));
    memset(&cmd->scissor, 0, sizeof(cmd->scissor));
    cmd->viewport_set = 0u;
    cmd->scissor_set = 0u;
    cmd->dynamic_set=0u;
    cmd->active_framebuffer = 0;
    cmd->active_render_pass = 0u;
    memset(cmd->push_constants, 0, sizeof(cmd->push_constants));
    memset(cmd->push_constant_initialized, 0, sizeof(cmd->push_constant_initialized));
    return VK_SUCCESS;
}

/* Secondary records retain their command object. Generation validation
   makes rerecording invalidate captured primaries instead of silently changing
   deferred work. The first supported contract is outside render passes. */
void vkCmdExecuteCommands(VkCommandBuffer primary,uint32_t count,const VkCommandBuffer *commands){
    vk_command_buffer_impl_t *p=(void *)(uintptr_t)primary;
    if(!p||!p->recording)return;
    if(p->level!=VK_COMMAND_BUFFER_LEVEL_PRIMARY||p->active_render_pass||!count||!commands){vk_record_unsupported(p);return;}
    for(uint32_t i=0;i<count;++i){vk_command_buffer_impl_t *c=(void *)(uintptr_t)commands[i];
        if(!c||c->device!=p->device||c->destroy_requested||c->level!=VK_COMMAND_BUFFER_LEVEL_SECONDARY||!c->executable||c->recording||
           (c->pending_submissions&&!(c->usage_flags&VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT))){vk_record_unsupported(p);return;}
    }
    for(uint32_t i=0;i<count;++i){vk_command_buffer_impl_t *c=(void *)(uintptr_t)commands[i];
        vk_recorded_command_t *r=vk_append_record(p,VK_RECORD_EXECUTE_SECONDARY);
        if(!r)return;if(!vk_command_buffer_retain(c)){p->record_error=VK_ERROR_OUT_OF_HOST_MEMORY;return;}
        r->args.secondary.command=c;r->args.secondary.generation=c->generation;
    }
}

/* One shared record traversal for preflight and pending lifetime accounting.
   Vulkan forbids nesting secondary command buffers. */
typedef struct vk_record_iterator {vk_recorded_command_t *next,*resume;} vk_record_iterator_t;
static vk_recorded_command_t *vk_record_next(vk_record_iterator_t *it){
    vk_recorded_command_t *r=it->next;
    if(!r){r=it->resume;it->resume=0;}
    if(!r)return 0;it->next=r->next;
    if(r->kind==VK_RECORD_EXECUTE_SECONDARY){it->resume=r->next;it->next=r->args.secondary.command->first_record;}
    return r;
}

void vkCmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image,
        VkImageLayout imageLayout, const VkClearColorValue *pColor,
        uint32_t rangeCount, const VkImageSubresourceRange *pRanges) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    vk_image_impl_t *impl = (vk_image_impl_t *)(uintptr_t)image;
    if (cmd == 0 || cmd->recording == 0u) return;
    if (cmd->active_framebuffer != 0 || impl == 0 || pColor == 0 ||
        impl->device != cmd->device || impl->destroy_requested != 0u ||
        !vk_format_is_color_unorm(impl->format) || pRanges == 0 || rangeCount == 0u ||
        (impl->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0u ||
        (imageLayout != VK_IMAGE_LAYOUT_GENERAL &&
         imageLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)) {
        vk_record_unsupported(cmd); return;
    }
    for (uint32_t i = 0u; i < rangeCount; ++i) {
        VkImageSubresourceRange range;
        if (!vk_image_range(impl, &pRanges[i], &range)) {
            vk_record_unsupported(cmd); return;
        }
    }
    for (uint32_t i = 0u; i < rangeCount; ++i) {
        vk_recorded_command_t *record = vk_append_record(cmd, VK_RECORD_CLEAR_COLOR);
        if (record == 0) return;
        if (!vk_image_retain(impl)) {
            cmd->record_error = VK_ERROR_OUT_OF_HOST_MEMORY; return;
        }
        record->args.clear.image = impl;
        record->args.clear.color = pack_rgba(pColor);
        record->args.clear.layout = imageLayout;
        (void)vk_image_range(impl, &pRanges[i], &record->args.clear.range);
    }
    cmd->bound_image = image;
}

void vkCmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image,
        VkImageLayout imageLayout, const VkClearDepthStencilValue *pDepthStencil,
        uint32_t rangeCount, const VkImageSubresourceRange *pRanges) {
    vk_command_buffer_impl_t *cmd =
        (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    vk_image_impl_t *impl = (vk_image_impl_t *)(uintptr_t)image;
    if (cmd == 0 || cmd->recording == 0u) return;
    if (cmd->active_framebuffer != 0 || impl == 0 || pDepthStencil == 0 ||
        pRanges == 0 || rangeCount == 0u || impl->device != cmd->device ||
        impl->destroy_requested != 0u || !vk_format_is_ds(impl->format) ||
        (impl->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0u ||
        (imageLayout != VK_IMAGE_LAYOUT_GENERAL &&
         imageLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)) {
        vk_record_unsupported(cmd);
        return;
    }
    for (uint32_t i = 0u; i < rangeCount; ++i) {
        VkImageSubresourceRange range;
        if (!vk_image_range(impl, &pRanges[i], &range)||((range.aspectMask&VK_IMAGE_ASPECT_DEPTH_BIT)&&!(pDepthStencil->depth>=0.0f&&pDepthStencil->depth<=1.0f))) {
            vk_record_unsupported(cmd); return;
        }
    }
    for (uint32_t i = 0u; i < rangeCount; ++i) {
        vk_recorded_command_t *record;
        record = vk_append_record(cmd, VK_RECORD_CLEAR_DEPTH_STENCIL);
        if (record == 0) return;
        if (!vk_image_retain(impl)) {
            cmd->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
            return;
        }
        record->args.clear_depth_stencil.image = impl;
        record->args.clear_depth_stencil.depth = pDepthStencil->depth;
        record->args.clear_depth_stencil.stencil = pDepthStencil->stencil;
        record->args.clear_depth_stencil.layout = imageLayout;
        (void)vk_image_range(impl, &pRanges[i], &record->args.clear_depth_stencil.range);
    }
}

VkResult vkCreateBuffer(VkDevice device, const VkBufferCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer) {
    vk_buffer_impl_t *buffer;
    if (device == VK_NULL_HANDLE || pCreateInfo == 0 || pBuffer == 0 ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO ||
        pCreateInfo->size == 0u || pCreateInfo->size > SIZE_MAX ||
        pCreateInfo->usage == 0u) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *pBuffer = VK_NULL_HANDLE;
    if (pAllocator != 0 || pCreateInfo->pNext != 0 || pCreateInfo->flags != 0u ||
        pCreateInfo->sharingMode != 0u) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    buffer = (vk_buffer_impl_t *)vk_heap_alloc(sizeof(*buffer));
    if (buffer == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    buffer->size = pCreateInfo->size;
    buffer->device = (vk_device_impl_t *)(uintptr_t)device;
    buffer->usage = pCreateInfo->usage;
    buffer->memory = 0;
    buffer->memory_offset = 0u;
    buffer->references = 1u;
    buffer->destroy_requested = 0u;
    *pBuffer = (VkBuffer)(uintptr_t)buffer;
    return VK_SUCCESS;
}

void vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *pAllocator) {
    vk_buffer_impl_t *impl = (vk_buffer_impl_t *)(uintptr_t)buffer;
    (void)device;
    (void)pAllocator;
    if (impl != 0 && impl->destroy_requested == 0u) {
        impl->destroy_requested = 1u;
        vk_buffer_release(impl);
    }
}

void vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements *pMemoryRequirements) {
    vk_buffer_impl_t *impl = (vk_buffer_impl_t *)(uintptr_t)buffer;
    (void)device;
    if (impl == 0 || pMemoryRequirements == 0) {
        return;
    }
    pMemoryRequirements->size = impl->size;
    pMemoryRequirements->alignment = 4u;
    pMemoryRequirements->memoryTypeBits = 1u;
}

VkResult vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo, const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMemory) {
    vk_device_impl_t *device_impl = (vk_device_impl_t *)(uintptr_t)device;
    vk_memory_impl_t *memory;
    const VkVexMemoryImportInfoVEX *import_info = 0;
    if (device_impl == 0 || pAllocateInfo == 0 || pMemory == 0 ||
        pAllocateInfo->sType != VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO ||
        pAllocateInfo->allocationSize == 0u ||
        pAllocateInfo->allocationSize > SIZE_MAX || pAllocateInfo->memoryTypeIndex != 0u) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *pMemory = VK_NULL_HANDLE;
    if (pAllocator != 0) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (pAllocateInfo->pNext != 0) {
        import_info = (const VkVexMemoryImportInfoVEX *)pAllocateInfo->pNext;
        if (!device_impl->instance->vex_surface_enabled ||
            import_info->sType != VK_STRUCTURE_TYPE_VEX_MEMORY_IMPORT_INFO_EXPERIMENTAL ||
            import_info->pNext != 0 || import_info->surfaceCapability == 0u) {
            return VK_ERROR_INVALID_EXTERNAL_HANDLE;
        }
    }
    memory = (vk_memory_impl_t *)vk_heap_alloc(sizeof(*memory));
    if (memory == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memset(memory, 0, sizeof(*memory));
    if ((import_info != 0
            ? vk_memory_attach_surface(memory, import_info->surfaceCapability,
                                       pAllocateInfo->allocationSize)
            : vk_memory_create_vex_backing(memory,
                                           pAllocateInfo->allocationSize)) != 0) {
        vk_heap_free(memory);
        return import_info != 0 ? VK_ERROR_INVALID_EXTERNAL_HANDLE
                                : VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    memory->device = device_impl;
    memory->references = 1u;
    memory->destroy_requested = 0u;
    *pMemory = (VkDeviceMemory)(uintptr_t)memory;
    return VK_SUCCESS;
}

VkResult vkGetVexMemoryCapabilityVEX(VkDevice device, VkDeviceMemory memory,
                                     uint64_t *pSurfaceCapability) {
    vk_device_impl_t *device_impl = (vk_device_impl_t *)(uintptr_t)device;
    vk_memory_impl_t *memory_impl = (vk_memory_impl_t *)(uintptr_t)memory;
    vex_surface_query_req_t query = {0};
    if (device_impl == 0 || memory_impl == 0 || pSurfaceCapability == 0 ||
        memory_impl->destroy_requested != 0u ||
        !device_impl->instance->vex_surface_enabled ||
        memory_impl->surface_capability == 0u) {
        return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    }
    query.surface_handle = memory_impl->surface_capability;
    if (vex_surface_query(&query) != 0 ||
        (query.capability_rights & VEX_SURFACE_RIGHT_EXPORT) == 0u) {
        return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    }
    *pSurfaceCapability = memory_impl->surface_capability;
    return VK_SUCCESS;
}

void vkFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks *pAllocator) {
    vk_memory_impl_t *impl = (vk_memory_impl_t *)(uintptr_t)memory;
    (void)device;
    (void)pAllocator;
    if (impl == 0 || impl->destroy_requested != 0u) {
        return;
    }
    impl->destroy_requested = 1u;
    vk_memory_release(impl);
}

VkResult vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset) {
    vk_buffer_impl_t *buffer_impl = (vk_buffer_impl_t *)(uintptr_t)buffer;
    vk_memory_impl_t *memory_impl = (vk_memory_impl_t *)(uintptr_t)memory;
    if (buffer_impl == 0 || memory_impl == 0 ||
        buffer_impl->device != (vk_device_impl_t *)(uintptr_t)device ||
        memory_impl->device != buffer_impl->device ||
        buffer_impl->destroy_requested != 0u ||
        memory_impl->destroy_requested != 0u || buffer_impl->memory != 0 ||
        (memoryOffset & 3u) != 0u ||
        memoryOffset > memory_impl->size ||
        buffer_impl->size > memory_impl->size - memoryOffset) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!vk_memory_retain(memory_impl)) return VK_ERROR_OUT_OF_HOST_MEMORY;
    buffer_impl->memory = memory_impl;
    buffer_impl->memory_offset = memoryOffset;
    return VK_SUCCESS;
}

VkResult vkMapMemory(VkDevice device,VkDeviceMemory memory,VkDeviceSize offset,VkDeviceSize size,VkFlags flags,void **out){
    vk_memory_impl_t *m=(void*)(uintptr_t)memory;
    if(out)*out=0;
    if(!m||m->device!=(void*)(uintptr_t)device||m->destroy_requested||m->host_mapped||!out||flags||offset>=m->size)return VK_ERROR_MEMORY_MAP_FAILED;
    if(size==VK_WHOLE_SIZE)size=m->size-offset;
    if(!size||size>m->size-offset)return VK_ERROR_MEMORY_MAP_FAILED;
    m->host_mapped=1u;m->host_offset=offset;m->host_size=size;*out=m->bytes+offset;return VK_SUCCESS;
}
void vkUnmapMemory(VkDevice device,VkDeviceMemory memory){
    vk_memory_impl_t *m=(void*)(uintptr_t)memory;
    if(m&&m->device==(void*)(uintptr_t)device&&!m->destroy_requested){m->host_mapped=0u;m->host_offset=m->host_size=0u;}
}
static VkResult vk_host_memory_ranges(VkDevice device,uint32_t count,const VkMappedMemoryRange *ranges){
    if(!device||!count||!ranges)return VK_ERROR_INITIALIZATION_FAILED;
    for(uint32_t i=0u;i<count;++i){const VkMappedMemoryRange *r=&ranges[i];vk_memory_impl_t *m=(void*)(uintptr_t)r->memory;
        if(r->sType!=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE||r->pNext||!m||m->device!=(void*)(uintptr_t)device||
           m->destroy_requested||!m->host_mapped||r->offset<m->host_offset||r->offset>m->host_offset+m->host_size)return VK_ERROR_MEMORY_MAP_FAILED;
        VkDeviceSize size=r->size==VK_WHOLE_SIZE?m->size-r->offset:r->size;
        if(!size||size>m->host_offset+m->host_size-r->offset)return VK_ERROR_MEMORY_MAP_FAILED;
    }
    /* This memory type is HOST_COHERENT: ordering is real, cache maintenance
       is unnecessary on the software backend's coherent Vex CPU mapping. */
    __atomic_thread_fence(__ATOMIC_SEQ_CST);return VK_SUCCESS;
}
VkResult vkFlushMappedMemoryRanges(VkDevice d,uint32_t n,const VkMappedMemoryRange *r){return vk_host_memory_ranges(d,n,r);}
VkResult vkInvalidateMappedMemoryRanges(VkDevice d,uint32_t n,const VkMappedMemoryRange *r){return vk_host_memory_ranges(d,n,r);}

VkResult vkCreateImage(VkDevice device, const VkImageCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkImage *pImage) {
    vk_image_impl_t *image;
    if (device == VK_NULL_HANDLE || pCreateInfo == 0 || pImage == 0 ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO ||
        pCreateInfo->extent.width == 0u || pCreateInfo->extent.height == 0u ||
        pCreateInfo->extent.width > 4096u || pCreateInfo->extent.height > 4096u ||
        pCreateInfo->extent.width > SIZE_MAX / 4u / pCreateInfo->extent.height) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *pImage = VK_NULL_HANDLE;
    if (pAllocator != 0 || pCreateInfo->pNext != 0 || pCreateInfo->flags != 0u ||
        pCreateInfo->imageType != VK_IMAGE_TYPE_2D ||
        (!vk_format_is_color_unorm(pCreateInfo->format) &&
         !vk_format_is_ds(pCreateInfo->format)) ||
        pCreateInfo->extent.depth != 1u || pCreateInfo->mipLevels == 0u ||
        pCreateInfo->mipLevels > VK_VEX_MAX_IMAGE_MIPS ||
        pCreateInfo->arrayLayers == 0u ||
        pCreateInfo->arrayLayers > VK_VEX_MAX_IMAGE_LAYERS || !vk_sample_count_supported(pCreateInfo->samples) ||
        (pCreateInfo->samples != VK_SAMPLE_COUNT_1_BIT &&
         (pCreateInfo->mipLevels != 1u || pCreateInfo->tiling != VK_IMAGE_TILING_OPTIMAL ||
          (pCreateInfo->usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0u)) ||
        (pCreateInfo->tiling != VK_IMAGE_TILING_LINEAR &&
         pCreateInfo->tiling != VK_IMAGE_TILING_OPTIMAL) ||
        ((pCreateInfo->usage&VK_IMAGE_USAGE_STORAGE_BIT)&&
         (pCreateInfo->format!=VK_FORMAT_R8G8B8A8_UNORM||pCreateInfo->samples!=VK_SAMPLE_COUNT_1_BIT)) ||
        pCreateInfo->usage == 0u ||
        (pCreateInfo->usage & ~(VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) != 0u ||
        (vk_format_is_color_unorm(pCreateInfo->format) &&
         (pCreateInfo->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0u) ||
        ((vk_format_is_ds(pCreateInfo->format)) &&
         (pCreateInfo->tiling != VK_IMAGE_TILING_OPTIMAL ||
          (pCreateInfo->usage & ~(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) != 0u)) ||
        pCreateInfo->sharingMode != 0u ||
        pCreateInfo->initialLayout != VK_IMAGE_LAYOUT_UNDEFINED) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    {
        uint32_t extent = pCreateInfo->extent.width > pCreateInfo->extent.height
            ? pCreateInfo->extent.width : pCreateInfo->extent.height;
        uint32_t max_mips = 1u;
        while (extent > 1u) { extent >>= 1u; ++max_mips; }
        if (pCreateInfo->mipLevels > max_mips) return VK_ERROR_INITIALIZATION_FAILED;
    }
    image = (vk_image_impl_t *)vk_heap_alloc(sizeof(*image));
    if (image == 0) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(image, 0, sizeof(*image));
    image->device = (vk_device_impl_t *)(uintptr_t)device;
    image->swapchain = 0;
    image->index = 0u;
    image->width = pCreateInfo->extent.width;
    image->height = pCreateInfo->extent.height;
    image->format = pCreateInfo->format;
    image->tiling = pCreateInfo->tiling;
    image->samples = pCreateInfo->samples;
    if (!vk_image_init_subresources(image, pCreateInfo->mipLevels,
            pCreateInfo->arrayLayers, pCreateInfo->initialLayout)) {
        vk_heap_free(image);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    image->usage = pCreateInfo->usage;
    image->memory = 0;
    image->memory_offset = 0u;
    image->owned_pixels = 0;
    image->owned_stride = 0u;
    image->references = 1u;
    image->owns_pixels = 0u;
    image->destroy_requested = 0u;
    *pImage = (VkImage)(uintptr_t)image;
    return VK_SUCCESS;
}

void vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks *pAllocator) {
    vk_image_impl_t *impl = (vk_image_impl_t *)(uintptr_t)image;
    (void)device;
    (void)pAllocator;
    if (impl == 0) {
        return;
    }
    if (impl->swapchain == 0 && impl->destroy_requested == 0u) {
        impl->destroy_requested = 1u;
        vk_image_release(impl);
    }
}

void vkGetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements *pMemoryRequirements) {
    vk_image_impl_t *impl = (vk_image_impl_t *)(uintptr_t)image;
    (void)device;
    if (impl == 0 || pMemoryRequirements == 0) {
        return;
    }
    pMemoryRequirements->size = impl->allocation_size;
    pMemoryRequirements->alignment = 4u;
    pMemoryRequirements->memoryTypeBits = 1u;
}

void vkGetImageSubresourceLayout(VkDevice device, VkImage image,
        const VkImageSubresource *pSubresource, VkSubresourceLayout *pLayout) {
    vk_image_impl_t *impl = (vk_image_impl_t *)(uintptr_t)image;
    vk_image_subresource_impl_t *sub;
    if (pLayout == 0) return;
    memset(pLayout, 0, sizeof(*pLayout));
    if (impl == 0 || impl->device != (vk_device_impl_t *)(uintptr_t)device ||
        impl->destroy_requested != 0u || impl->tiling != VK_IMAGE_TILING_LINEAR ||
        pSubresource == 0 || pSubresource->aspectMask != impl->aspects ||
        (sub = vk_image_subresource(impl, pSubresource->mipLevel,
                                   pSubresource->arrayLayer)) == 0) return;
    pLayout->offset = sub->offset;
    pLayout->size = sub->size;
    pLayout->rowPitch = sub->row_pitch;
    pLayout->arrayPitch = impl->layer_pitch;
    /* depthPitch is undefined for these 2D images. */
}

VkResult vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset) {
    vk_image_impl_t *image_impl = (vk_image_impl_t *)(uintptr_t)image;
    vk_memory_impl_t *memory_impl = (vk_memory_impl_t *)(uintptr_t)memory;
    VkDeviceSize needed;
    if (image_impl == 0 || memory_impl == 0 ||
        image_impl->device != (vk_device_impl_t *)(uintptr_t)device ||
        memory_impl->device != image_impl->device ||
        image_impl->destroy_requested != 0u ||
        memory_impl->destroy_requested != 0u || image_impl->memory != 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    needed = image_impl->allocation_size;
    if ((memoryOffset & 3u) != 0u || memoryOffset > memory_impl->size ||
        needed > memory_impl->size - memoryOffset) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!vk_memory_retain(memory_impl)) return VK_ERROR_OUT_OF_HOST_MEMORY;
    image_impl->memory = memory_impl;
    image_impl->memory_offset = memoryOffset;
    return VK_SUCCESS;
}

VkResult vkCreateImageView(VkDevice device, const VkImageViewCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkImageView *pView) {
    vk_image_view_impl_t *view;
    vk_image_impl_t *image;
    VkImageSubresourceRange range;
    if (device == VK_NULL_HANDLE || pCreateInfo == 0 || pView == 0 ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *pView = VK_NULL_HANDLE;
    image = (vk_image_impl_t *)(uintptr_t)pCreateInfo->image;
    if (pAllocator != 0 || pCreateInfo->pNext != 0 || pCreateInfo->flags != 0u ||
        image == 0 || image->device != (vk_device_impl_t *)(uintptr_t)device ||
        image->destroy_requested != 0u ||
        (pCreateInfo->viewType != VK_IMAGE_VIEW_TYPE_2D &&
         pCreateInfo->viewType != VK_IMAGE_VIEW_TYPE_2D_ARRAY) ||
        pCreateInfo->format != image->format ||
        pCreateInfo->components.r != VK_COMPONENT_SWIZZLE_IDENTITY ||
        pCreateInfo->components.g != VK_COMPONENT_SWIZZLE_IDENTITY ||
        pCreateInfo->components.b != VK_COMPONENT_SWIZZLE_IDENTITY ||
        pCreateInfo->components.a != VK_COMPONENT_SWIZZLE_IDENTITY ||
        !vk_image_range(image, &pCreateInfo->subresourceRange, &range) ||
        (pCreateInfo->viewType == VK_IMAGE_VIEW_TYPE_2D && range.layerCount != 1u)) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    view = (vk_image_view_impl_t *)vk_heap_alloc(sizeof(*view));
    if (view == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    if (!vk_image_retain(image)) {
        vk_heap_free(view);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    view->image = image;
    view->type = pCreateInfo->viewType;
    view->range = range;
    view->references = 1u;
    view->destroy_requested = 0u;
    *pView = (VkImageView)(uintptr_t)view;
    return VK_SUCCESS;
}

void vkDestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks *pAllocator) {
    vk_image_view_impl_t *view = (vk_image_view_impl_t *)(uintptr_t)imageView;
    (void)device;
    (void)pAllocator;
    if (view != 0 && view->destroy_requested == 0u) {
        view->destroy_requested = 1u;
        vk_image_view_release(view);
    }
}

VkResult vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkShaderModule *pShaderModule) {
    vk_shader_module_impl_t *shader;
    size_t word_count;
    size_t offset;
    if (device == VK_NULL_HANDLE || pCreateInfo == 0 || pShaderModule == 0 ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *pShaderModule = VK_NULL_HANDLE;
    if (pAllocator != 0 || pCreateInfo->pNext != 0 || pCreateInfo->flags != 0u) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (pCreateInfo->pCode == 0 || pCreateInfo->codeSize < 5u * sizeof(uint32_t) ||
        (pCreateInfo->codeSize % sizeof(uint32_t)) != 0u) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    word_count = pCreateInfo->codeSize / sizeof(uint32_t);
    if (pCreateInfo->pCode[0] != 0x07230203u ||
        (pCreateInfo->pCode[1] >> 16u) != 1u ||
        pCreateInfo->pCode[3] == 0u || pCreateInfo->pCode[4] != 0u) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (offset = 5u; offset < word_count;) {
        size_t instruction_words = pCreateInfo->pCode[offset] >> 16u;
        if (instruction_words == 0u || instruction_words > word_count - offset) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        offset += instruction_words;
    }
    shader = (vk_shader_module_impl_t *)vk_heap_alloc(sizeof(*shader));
    if (shader == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    shader->code = (uint32_t *)vk_heap_alloc(pCreateInfo->codeSize);
    if (shader->code == 0) {
        vk_heap_free(shader);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memcpy(shader->code, pCreateInfo->pCode, pCreateInfo->codeSize);
    shader->device = (vk_device_impl_t *)(uintptr_t)device;
    shader->code_size = pCreateInfo->codeSize;
    shader->references = 1u;
    shader->destroy_requested = 0u;
    *pShaderModule = (VkShaderModule)(uintptr_t)shader;
    return VK_SUCCESS;
}

void vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks *pAllocator) {
    vk_shader_module_impl_t *shader = (vk_shader_module_impl_t *)(uintptr_t)shaderModule;
    (void)device;
    (void)pAllocator;
    if (shader != 0 && shader->destroy_requested == 0u) {
        shader->destroy_requested = 1u;
        vk_shader_module_release(shader);
    }
}

VkResult vkCreateRenderPass(VkDevice device, const VkRenderPassCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass) {
    vk_device_impl_t *dev = (vk_device_impl_t *)(uintptr_t)device;
    const VkAttachmentDescription *attachment;
    const VkSubpassDescription *subpass;
    const VkAttachmentReference *color_ref;
    const VkAttachmentReference *depth_ref;
    const VkAttachmentDescription *depth_attachment = 0, *resolve_attachment=0;
    const VkAttachmentReference *resolve_ref=0;
    vk_render_pass_impl_t *render_pass;
    if (dev == 0 || pCreateInfo == 0 || pRenderPass == 0 ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *pRenderPass = VK_NULL_HANDLE;
    if (pAllocator != 0 || pCreateInfo->pNext != 0 || pCreateInfo->flags != 0u ||
        (pCreateInfo->attachmentCount == 0u || pCreateInfo->attachmentCount > 3u) ||
        pCreateInfo->pAttachments == 0 ||
        pCreateInfo->subpassCount != 1u || pCreateInfo->pSubpasses == 0 ||
        (pCreateInfo->dependencyCount && !pCreateInfo->pDependencies)) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    /* A single queue executes the CPU backend in total order. External
       dependencies receive stronger execution/visibility ordering than their
       scopes request; self-dependencies require in-pass barriers, still refused. */
    for(uint32_t i=0u;i<pCreateInfo->dependencyCount;++i){
        const VkSubpassDependency *d=&pCreateInfo->pDependencies[i];
        if(!((d->srcSubpass==VK_SUBPASS_EXTERNAL&&d->dstSubpass==0u)||
             (d->srcSubpass==0u&&d->dstSubpass==VK_SUBPASS_EXTERNAL))||
           !d->srcStageMask||!d->dstStageMask||((d->srcStageMask|d->dstStageMask)&~0x1ffffu)||
           ((d->srcAccessMask|d->dstAccessMask)&~0x1ffffu)||(d->dependencyFlags&~VK_DEPENDENCY_BY_REGION_BIT))return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    subpass=&pCreateInfo->pSubpasses[0];
    if(subpass->colorAttachmentCount!=1u||subpass->pColorAttachments==0||
       subpass->pColorAttachments[0].attachment>=pCreateInfo->attachmentCount)return VK_ERROR_FEATURE_NOT_PRESENT;
    color_ref=subpass->pColorAttachments;attachment=&pCreateInfo->pAttachments[color_ref->attachment];
    depth_ref=subpass->pDepthStencilAttachment;
    if(depth_ref&&depth_ref->attachment==VK_ATTACHMENT_UNUSED)depth_ref=0;
    resolve_ref=subpass->pResolveAttachments;
    if(resolve_ref&&resolve_ref->attachment==VK_ATTACHMENT_UNUSED)resolve_ref=0;
    if(pCreateInfo->attachmentCount!=1u+(depth_ref!=0)+(resolve_ref!=0))return VK_ERROR_FEATURE_NOT_PRESENT;
    if(depth_ref){
        if(depth_ref->attachment>=pCreateInfo->attachmentCount||depth_ref->attachment==color_ref->attachment)return VK_ERROR_INITIALIZATION_FAILED;
        depth_attachment=&pCreateInfo->pAttachments[depth_ref->attachment];
    }
    if(resolve_ref){
        if(resolve_ref->attachment>=pCreateInfo->attachmentCount||resolve_ref->attachment==color_ref->attachment||
           (depth_ref&&resolve_ref->attachment==depth_ref->attachment))return VK_ERROR_INITIALIZATION_FAILED;
        resolve_attachment=&pCreateInfo->pAttachments[resolve_ref->attachment];
        if(attachment->samples==VK_SAMPLE_COUNT_1_BIT||resolve_attachment->samples!=VK_SAMPLE_COUNT_1_BIT||
           resolve_attachment->format!=attachment->format||resolve_attachment->flags||
           resolve_attachment->loadOp!=VK_ATTACHMENT_LOAD_OP_DONT_CARE||
           resolve_attachment->storeOp>VK_ATTACHMENT_STORE_OP_DONT_CARE||
           resolve_attachment->stencilLoadOp!=VK_ATTACHMENT_LOAD_OP_DONT_CARE||resolve_attachment->stencilStoreOp!=VK_ATTACHMENT_STORE_OP_DONT_CARE||
           (resolve_ref->layout!=VK_IMAGE_LAYOUT_GENERAL&&resolve_ref->layout!=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)||
           (resolve_attachment->initialLayout!=VK_IMAGE_LAYOUT_UNDEFINED&&resolve_attachment->initialLayout!=VK_IMAGE_LAYOUT_GENERAL&&resolve_attachment->initialLayout!=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)||
           (resolve_attachment->finalLayout!=VK_IMAGE_LAYOUT_GENERAL&&resolve_attachment->finalLayout!=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL&&resolve_attachment->finalLayout!=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL&&resolve_attachment->finalLayout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
           return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (subpass->pipelineBindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS ||
        subpass->inputAttachmentCount != 0u || subpass->pInputAttachments != 0 ||
        subpass->colorAttachmentCount != 1u || subpass->pColorAttachments == 0 ||
        subpass->preserveAttachmentCount != 0u || subpass->pPreserveAttachments != 0) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (depth_ref != 0 &&
        ((depth_ref->layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
          depth_ref->layout != VK_IMAGE_LAYOUT_GENERAL) ||
         depth_attachment->flags != 0u ||
         !vk_format_is_ds(depth_attachment->format) ||
         depth_attachment->samples != attachment->samples ||
         (depth_attachment->loadOp != VK_ATTACHMENT_LOAD_OP_CLEAR &&
          depth_attachment->loadOp != VK_ATTACHMENT_LOAD_OP_LOAD &&
          depth_attachment->loadOp != VK_ATTACHMENT_LOAD_OP_DONT_CARE) ||
         (depth_attachment->storeOp != VK_ATTACHMENT_STORE_OP_STORE &&
          depth_attachment->storeOp != VK_ATTACHMENT_STORE_OP_DONT_CARE) ||
         (depth_attachment->format == VK_FORMAT_D32_SFLOAT &&
          (depth_attachment->stencilLoadOp != VK_ATTACHMENT_LOAD_OP_DONT_CARE || depth_attachment->stencilStoreOp != VK_ATTACHMENT_STORE_OP_DONT_CARE)) ||
         (vk_format_has_stencil(depth_attachment->format) &&
          (depth_attachment->stencilLoadOp > VK_ATTACHMENT_LOAD_OP_DONT_CARE ||
           depth_attachment->stencilStoreOp > VK_ATTACHMENT_STORE_OP_DONT_CARE)) ||
         (depth_attachment->initialLayout != VK_IMAGE_LAYOUT_UNDEFINED &&
          depth_attachment->initialLayout != VK_IMAGE_LAYOUT_GENERAL &&
          depth_attachment->initialLayout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) ||
         (depth_attachment->finalLayout != VK_IMAGE_LAYOUT_GENERAL &&
          depth_attachment->finalLayout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) ||
         (((vk_format_has_depth(depth_attachment->format) && depth_attachment->loadOp == VK_ATTACHMENT_LOAD_OP_LOAD) ||
           (vk_format_has_stencil(depth_attachment->format) && depth_attachment->stencilLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD)) &&
          depth_attachment->initialLayout == VK_IMAGE_LAYOUT_UNDEFINED)))
        return VK_ERROR_FEATURE_NOT_PRESENT;
    color_ref = subpass->pColorAttachments;
    if (attachment->flags != 0u || !vk_format_is_color_unorm(attachment->format) ||
        !vk_sample_count_supported(attachment->samples) ||
        (attachment->loadOp != VK_ATTACHMENT_LOAD_OP_CLEAR &&
         attachment->loadOp != VK_ATTACHMENT_LOAD_OP_LOAD &&
         attachment->loadOp != VK_ATTACHMENT_LOAD_OP_DONT_CARE) ||
        (attachment->storeOp != VK_ATTACHMENT_STORE_OP_STORE &&
         attachment->storeOp != VK_ATTACHMENT_STORE_OP_DONT_CARE) ||
        attachment->stencilLoadOp != VK_ATTACHMENT_LOAD_OP_DONT_CARE ||
        attachment->stencilStoreOp != VK_ATTACHMENT_STORE_OP_DONT_CARE ||
        (attachment->initialLayout != VK_IMAGE_LAYOUT_UNDEFINED &&
         attachment->initialLayout != VK_IMAGE_LAYOUT_GENERAL &&
         attachment->initialLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
         attachment->initialLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
         attachment->initialLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
         attachment->initialLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) ||
        (attachment->finalLayout != VK_IMAGE_LAYOUT_GENERAL &&
         attachment->finalLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
         attachment->finalLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
         attachment->finalLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
         attachment->finalLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) ||
        (attachment->loadOp == VK_ATTACHMENT_LOAD_OP_LOAD &&
         attachment->initialLayout == VK_IMAGE_LAYOUT_UNDEFINED) ||
        (color_ref->layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
         color_ref->layout != VK_IMAGE_LAYOUT_GENERAL)) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    render_pass = (vk_render_pass_impl_t *)vk_heap_alloc(sizeof(*render_pass));
    if (render_pass == 0) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(render_pass,0,sizeof(*render_pass));
    render_pass->attachment_count=pCreateInfo->attachmentCount;
    render_pass->color_index=color_ref->attachment;
    render_pass->depth_index=depth_ref?depth_ref->attachment:0u;
    render_pass->resolve_index=resolve_ref?resolve_ref->attachment:0u;
    render_pass->has_resolve=resolve_ref!=0;
    render_pass->resolve_initial_layout=resolve_attachment?resolve_attachment->initialLayout:VK_IMAGE_LAYOUT_UNDEFINED;
    render_pass->resolve_subpass_layout=resolve_ref?resolve_ref->layout:VK_IMAGE_LAYOUT_UNDEFINED;
    render_pass->resolve_final_layout=resolve_attachment?resolve_attachment->finalLayout:VK_IMAGE_LAYOUT_UNDEFINED;
    render_pass->device = dev;
    render_pass->color_format = attachment->format;
    render_pass->samples = attachment->samples;
    render_pass->depth_format = depth_attachment != 0 ? depth_attachment->format : 0u;
    render_pass->stencil_load_op = depth_attachment != 0 ? depth_attachment->stencilLoadOp : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    render_pass->stencil_store_op = depth_attachment != 0 ? depth_attachment->stencilStoreOp : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    render_pass->color_load_op = attachment->loadOp;
    render_pass->color_store_op = attachment->storeOp;
    render_pass->depth_load_op = depth_attachment != 0
        ? depth_attachment->loadOp : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    render_pass->depth_store_op = depth_attachment != 0
        ? depth_attachment->storeOp : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    render_pass->initial_layout = attachment->initialLayout;
    render_pass->subpass_layout = color_ref->layout;
    render_pass->final_layout = attachment->finalLayout;
    render_pass->depth_initial_layout = depth_attachment != 0
        ? depth_attachment->initialLayout : VK_IMAGE_LAYOUT_UNDEFINED;
    render_pass->depth_subpass_layout = depth_ref != 0
        ? depth_ref->layout : VK_IMAGE_LAYOUT_UNDEFINED;
    render_pass->depth_final_layout = depth_attachment != 0
        ? depth_attachment->finalLayout : VK_IMAGE_LAYOUT_UNDEFINED;
    render_pass->has_depth = depth_attachment != 0 ? 1u : 0u;
    render_pass->references = 1u;
    render_pass->destroy_requested = 0u;
    *pRenderPass = (VkRenderPass)(uintptr_t)render_pass;
    return VK_SUCCESS;
}

void vkDestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks *pAllocator) {
    vk_render_pass_impl_t *impl = (vk_render_pass_impl_t *)(uintptr_t)renderPass;
    (void)device;
    (void)pAllocator;
    if (impl != 0 && impl->destroy_requested == 0u) {
        impl->destroy_requested = 1u;
        vk_render_pass_release(impl);
    }
}

VkResult vkCreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkFramebuffer *pFramebuffer) {
    vk_device_impl_t *dev = (vk_device_impl_t *)(uintptr_t)device;
    vk_render_pass_impl_t *render_pass;
    vk_image_view_impl_t *view;
    vk_image_view_impl_t *depth_view = 0,*resolve_view=0;
    vk_framebuffer_impl_t *framebuffer;
    int render_pass_retained = 0;
    int depth_view_retained = 0;
    if (dev == 0 || pCreateInfo == 0 || pFramebuffer == 0 ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *pFramebuffer = VK_NULL_HANDLE;
    render_pass = (vk_render_pass_impl_t *)(uintptr_t)pCreateInfo->renderPass;
    if (pAllocator != 0 || pCreateInfo->pNext != 0 || pCreateInfo->flags != 0u ||
        render_pass == 0 || render_pass->device != dev ||
        render_pass->destroy_requested != 0u ||
        pCreateInfo->attachmentCount != render_pass->attachment_count ||
        pCreateInfo->pAttachments == 0 || pCreateInfo->width == 0u ||
        pCreateInfo->height == 0u || pCreateInfo->layers != 1u) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    view = (vk_image_view_impl_t *)(uintptr_t)pCreateInfo->pAttachments[render_pass->color_index];
    if (view == 0 || view->destroy_requested != 0u || view->image == 0 ||
        view->image->destroy_requested != 0u || view->image->format != render_pass->color_format ||
        view->image->samples != render_pass->samples ||
        (view->image->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0u ||
        view->range.levelCount != 1u ||
        pCreateInfo->width > vk_image_subresource(view->image, view->range.baseMipLevel,
                                                 view->range.baseArrayLayer)->width ||
        pCreateInfo->height > vk_image_subresource(view->image, view->range.baseMipLevel,
                                                  view->range.baseArrayLayer)->height) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (render_pass->has_depth != 0u) {
        depth_view = (vk_image_view_impl_t *)(uintptr_t)pCreateInfo->pAttachments[render_pass->depth_index];
        if (depth_view == 0 || depth_view->destroy_requested != 0u ||
            depth_view->image == 0 || depth_view->image->destroy_requested != 0u ||
            depth_view->image->format != render_pass->depth_format ||
            depth_view->image->samples != render_pass->samples ||
            (depth_view->image->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0u ||
            depth_view->range.levelCount != 1u ||
            pCreateInfo->width > vk_image_subresource(depth_view->image, depth_view->range.baseMipLevel,
                                                      depth_view->range.baseArrayLayer)->width ||
            pCreateInfo->height > vk_image_subresource(depth_view->image, depth_view->range.baseMipLevel,
                                                       depth_view->range.baseArrayLayer)->height)
            return VK_ERROR_INITIALIZATION_FAILED;
    }
    if(render_pass->has_resolve){
        resolve_view=(void*)(uintptr_t)pCreateInfo->pAttachments[render_pass->resolve_index];
        if(!resolve_view||resolve_view->destroy_requested||!resolve_view->image||resolve_view->image->device!=dev||
           resolve_view->image->destroy_requested||resolve_view->image->format!=render_pass->color_format||resolve_view->image->samples!=1u||
           !(resolve_view->image->usage&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)||resolve_view->range.levelCount!=1u||
           pCreateInfo->width>vk_image_subresource(resolve_view->image,resolve_view->range.baseMipLevel,resolve_view->range.baseArrayLayer)->width||
           pCreateInfo->height>vk_image_subresource(resolve_view->image,resolve_view->range.baseMipLevel,resolve_view->range.baseArrayLayer)->height)
           return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!vk_render_pass_retain(render_pass)) return VK_ERROR_OUT_OF_HOST_MEMORY;
    render_pass_retained = 1;
    if (!vk_image_view_retain(view)) {
        vk_render_pass_release(render_pass);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    if (depth_view != 0) {
        if (!vk_image_view_retain(depth_view)) {
            vk_image_view_release(view);
            vk_render_pass_release(render_pass);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        depth_view_retained = 1;
    }
    if(resolve_view&&!vk_image_view_retain(resolve_view)){
        vk_image_view_release(depth_view);vk_image_view_release(view);vk_render_pass_release(render_pass);return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    framebuffer = (vk_framebuffer_impl_t *)vk_heap_alloc(sizeof(*framebuffer));
    if (framebuffer == 0) {
        vk_image_view_release(resolve_view);
        if (depth_view_retained) vk_image_view_release(depth_view);
        vk_image_view_release(view);
        if (render_pass_retained) vk_render_pass_release(render_pass);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    framebuffer->device = dev;
    framebuffer->render_pass = render_pass;
    framebuffer->attachment0 = view;
    framebuffer->depth_attachment = depth_view;
    framebuffer->resolve_attachment=resolve_view;
    framebuffer->width = pCreateInfo->width;
    framebuffer->height = pCreateInfo->height;
    framebuffer->references = 1u;
    framebuffer->destroy_requested = 0u;
    *pFramebuffer = (VkFramebuffer)(uintptr_t)framebuffer;
    return VK_SUCCESS;
}

void vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks *pAllocator) {
    vk_framebuffer_impl_t *impl = (vk_framebuffer_impl_t *)(uintptr_t)framebuffer;
    (void)device;
    (void)pAllocator;
    if (impl != 0 && impl->destroy_requested == 0u) {
        impl->destroy_requested = 1u;
        vk_framebuffer_release(impl);
    }
}

VkResult vkCreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkPipelineLayout *pPipelineLayout) {
    vk_pipeline_layout_impl_t *layout;
    if (device == VK_NULL_HANDLE || pCreateInfo == 0 || pPipelineLayout == 0 ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO)
        return VK_ERROR_INITIALIZATION_FAILED;
    *pPipelineLayout = VK_NULL_HANDLE;
    if (pAllocator != 0 || pCreateInfo->pNext != 0 || pCreateInfo->flags != 0u ||
        pCreateInfo->setLayoutCount > 8u ||
        (pCreateInfo->setLayoutCount != 0u && pCreateInfo->pSetLayouts == 0) ||
        pCreateInfo->pushConstantRangeCount > 4u ||
        (pCreateInfo->pushConstantRangeCount != 0u && pCreateInfo->pPushConstantRanges == 0))
        return VK_ERROR_FEATURE_NOT_PRESENT;
    for (uint32_t i = 0u; i < pCreateInfo->pushConstantRangeCount; ++i) {
        const VkPushConstantRange *range = &pCreateInfo->pPushConstantRanges[i];
        if (range->stageFlags == 0u || (range->offset & 3u) != 0u ||
            (range->size & 3u) != 0u || range->size == 0u ||
            range->offset > 128u || range->size > 128u - range->offset)
            return VK_ERROR_INITIALIZATION_FAILED;
        for (uint32_t j = 0u; j < i; ++j) {
            const VkPushConstantRange *prior = &pCreateInfo->pPushConstantRanges[j];
            const uint32_t a_end = range->offset + range->size;
            const uint32_t b_end = prior->offset + prior->size;
            if ((range->stageFlags & prior->stageFlags) != 0u &&
                range->offset < b_end && prior->offset < a_end)
                return VK_ERROR_INITIALIZATION_FAILED;
        }
    }
    layout = (vk_pipeline_layout_impl_t *)vk_heap_alloc(sizeof(*layout));
    if (layout == 0) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(layout, 0, sizeof(*layout));
    if (pCreateInfo->setLayoutCount != 0u) {
        layout->set_layouts = (VkDescriptorSetLayout *)vk_heap_alloc(
            pCreateInfo->setLayoutCount * sizeof(*layout->set_layouts));
        if (layout->set_layouts == 0) { vk_heap_free(layout); return VK_ERROR_OUT_OF_HOST_MEMORY; }
        for (uint32_t i = 0u; i < pCreateInfo->setLayoutCount; ++i) {
            vk_descriptor_set_layout_impl_t *set_layout =
                (vk_descriptor_set_layout_impl_t *)(uintptr_t)pCreateInfo->pSetLayouts[i];
            if (set_layout == 0 || set_layout->device != (vk_device_impl_t *)(uintptr_t)device ||
                set_layout->destroy_requested != 0u || set_layout->references == UINT32_MAX) {
                for (uint32_t j = 0u; j < i; ++j)
                    vk_layout_release((vk_descriptor_set_layout_impl_t *)(uintptr_t)layout->set_layouts[j]);
                vk_heap_free(layout->set_layouts); vk_heap_free(layout);
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            ++set_layout->references;
            layout->set_layouts[i] = pCreateInfo->pSetLayouts[i];
        }
    }
    if (pCreateInfo->pushConstantRangeCount != 0u)
        memcpy(layout->push_ranges, pCreateInfo->pPushConstantRanges,
               pCreateInfo->pushConstantRangeCount * sizeof(layout->push_ranges[0]));
    layout->device = (vk_device_impl_t *)(uintptr_t)device;
    layout->set_layout_count = pCreateInfo->setLayoutCount;
    layout->push_constant_range_count = pCreateInfo->pushConstantRangeCount;
    layout->references = 1u;
    *pPipelineLayout = (VkPipelineLayout)(uintptr_t)layout;
    return VK_SUCCESS;
}

void vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks *pAllocator) {
    vk_pipeline_layout_impl_t *layout = (vk_pipeline_layout_impl_t *)(uintptr_t)pipelineLayout;
    (void)device;
    (void)pAllocator;
    if (layout != 0 && layout->destroy_requested == 0u) {
        layout->destroy_requested = 1u;
        vk_pipeline_layout_release(layout);
    }
}

#include "vex_render_engine.h"

/* The software graphics path intentionally accepts a small, auditable SPIR-V
 * profile. Unsupported instructions fail pipeline creation instead of being
 * accepted and silently skipped by the renderer. */































/* Reflect std140 recursively rather than treating a block as a vec4 list.
   Byte loads remain bounded by the descriptor range; arrays carry an explicit
   SPIR-V ArrayStride, independent of the descriptor-array element number. */


static int vk_spv_uniform_layout(const vk_spv_profile_t *p,uint32_t type,uint32_t depth,uint32_t *a,uint32_t *size) {
    return vk_spv_block_layout(p,type,depth,a,size,1);
}
/* Mark data words only, excluding layout padding. All recursion/offsets are
   validated before any bit shift or host read of the 128-byte Vulkan bank. */










static int vk_spv_static_memory_valid(const vk_shader_module_impl_t *,const vk_spv_profile_t *);
/* SPIR-V integer arithmetic requires equal bit width/component counts;
   signedness may differ (glslang emits uint + int constants in ordinary loops). */










static int vk_spv_profile_bound_set_supported(const vk_spv_profile_t *profile,
                                              vk_pipeline_impl_t *pipeline,
                                              vk_descriptor_set_impl_t *const bound_sets[8]) {
    if (profile == 0 || pipeline == 0 || bound_sets == 0) return 0;
    if (profile->descriptor_count == 0u) return 1;
    for (uint32_t set_number = 0u; set_number < pipeline->layout->set_layout_count;
         ++set_number) {
        int required = 0;
        for (uint32_t id = 1u; id < profile->bound; ++id) {
            if (profile->descriptor_type_by_id[id] != 0u &&
                profile->descriptor_set[id] == set_number) {
                required = 1;
                break;
            }
        }
        if (required != 0) {
            vk_descriptor_set_impl_t *set = bound_sets[set_number];
            vk_descriptor_set_layout_impl_t *expected =
                (vk_descriptor_set_layout_impl_t *)(uintptr_t)
                    pipeline->layout->set_layouts[set_number];
            if (set == 0 || set->layout == 0 || expected == 0 || set->pool == 0 ||
                set->pool->device != pipeline->device ||
                !vk_layout_compatible(set->layout, expected)) return 0;
        }
    }
    return 1;
}





static int vk_spv_read_uniform_data(vk_descriptor_set_impl_t *const bound_sets[8],
                                    uint32_t set_number, uint32_t binding_number,
                                    uint32_t element_index, uint32_t byte_offset, uint32_t byte_count,
                                    uint32_t *words) {
    vk_descriptor_set_impl_t *set;
    vk_descriptor_value_impl_t *value = 0;
    vk_buffer_impl_t *buffer;
    vk_memory_impl_t *memory;
    if (bound_sets == 0 || words == 0 || byte_count == 0u || set_number >= 8u ||
        (set = bound_sets[set_number]) == 0 || set->layout == 0) return 0;
    for (uint32_t i = 0u; i < set->layout->binding_count; ++i) {
        const vk_descriptor_layout_binding_impl_t *binding = &set->layout->bindings[i];
        if (binding->binding.binding == binding_number &&
            binding->binding.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER &&
            element_index < binding->binding.descriptorCount &&
            binding->value_offset + element_index < set->value_count) {
            value = &set->values[binding->value_offset + element_index];
            break;
        }
    }
    if (value == 0 || value->initialized == 0u ||
        value->type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
        (buffer = value->resource.buffer.buffer) == 0 ||
        (memory = buffer->memory) == 0 || memory->bytes == 0 ||
        buffer->memory_offset > memory->size ||
        buffer->size > memory->size - buffer->memory_offset ||
        value->resource.buffer.offset > buffer->size ||
        value->resource.buffer.range > buffer->size - value->resource.buffer.offset ||
        byte_offset > value->resource.buffer.range ||
        byte_count > value->resource.buffer.range - byte_offset ||
        buffer->memory_offset > (uint64_t)SIZE_MAX ||
        value->resource.buffer.offset > (uint64_t)SIZE_MAX - buffer->memory_offset ||
        buffer->memory_offset + value->resource.buffer.offset > SIZE_MAX - byte_offset)
        return 0;
    {
        uint64_t absolute_offset = buffer->memory_offset +
            value->resource.buffer.offset + byte_offset;
        if (absolute_offset > memory->size ||
            byte_count > memory->size - absolute_offset) return 0;
        memcpy(words, memory->bytes + (size_t)absolute_offset,
               byte_count);
    }
    return 1;
}









/* Explicit LOD is relative to the view, and is clamped by the sampler and
   the view range. The bounded implicit profile is accepted only at LOD zero;
   it must not pretend to implement derivatives for a mipmapped sampler. */


#include "vulkan_shader_memory.h"



typedef struct vk_cache_entry {
    struct vk_cache_entry *next;
    uint32_t stage,bytes;
    uint32_t *code;
    vk_spv_profile_t *profile;
} vk_cache_entry_t;
typedef struct vk_pipeline_cache_impl {
    vk_device_impl_t *device;
    vk_cache_entry_t *head;
    uint64_t retained_bytes;
} vk_pipeline_cache_impl_t;
static void vk_cache_entry_free(vk_cache_entry_t *entry){if(entry){vk_heap_free(entry->code);vk_heap_free(entry->profile);vk_heap_free(entry);}}
static void vk_cache_insert(vk_pipeline_cache_impl_t *cache,const vk_shader_module_impl_t *module,uint32_t stage,const vk_spv_profile_t *profile){
    if(!cache)return;
    uint64_t bytes=sizeof(vk_cache_entry_t)+sizeof(*profile)+module->code_size;
    /* Optional cache retention budget; shader support does not depend on it. */
    uint64_t previous=__atomic_fetch_add(&cache->retained_bytes,bytes,__ATOMIC_ACQ_REL);
    if(previous>16u*1024u*1024u||bytes>16u*1024u*1024u-previous){__atomic_fetch_sub(&cache->retained_bytes,bytes,__ATOMIC_ACQ_REL);return;}
    vk_cache_entry_t *e=vk_heap_alloc(sizeof(*e));
    if(!e){__atomic_fetch_sub(&cache->retained_bytes,bytes,__ATOMIC_ACQ_REL);return;}memset(e,0,sizeof(*e));
    e->code=vk_heap_alloc(module->code_size);e->profile=vk_heap_alloc(sizeof(*profile));
    if(!e->code||!e->profile){vk_cache_entry_free(e);__atomic_fetch_sub(&cache->retained_bytes,bytes,__ATOMIC_ACQ_REL);return;}
    memcpy(e->code,module->code,module->code_size);*e->profile=*profile;e->profile->workspace=0;
    e->bytes=(uint32_t)module->code_size;e->stage=stage;
    e->next=__atomic_load_n(&cache->head,__ATOMIC_ACQUIRE);
    while(!__atomic_compare_exchange_n(&cache->head,&e->next,e,0,__ATOMIC_RELEASE,__ATOMIC_ACQUIRE)){}
}
static int vk_cached_profile(vk_pipeline_cache_impl_t *cache,const vk_shader_module_impl_t *module,uint32_t stage,vk_spv_profile_t *profile){
    if(cache)for(vk_cache_entry_t *e=__atomic_load_n(&cache->head,__ATOMIC_ACQUIRE);e;e=e->next)
        if(e->stage==stage&&e->bytes==module->code_size&&memcmp(e->code,module->code,e->bytes)==0){*profile=*e->profile;return 1;}
    if(!vk_spv_parse_profile(module,(VkShaderStageFlagBits)stage,profile))return 0;
    vk_cache_insert(cache,module,stage,profile);return 1;
}
void vkDestroyPipelineCache(VkDevice device,VkPipelineCache handle,const VkAllocationCallbacks *a){
    (void)a;vk_pipeline_cache_impl_t *cache=(void*)(uintptr_t)handle;if(!cache||cache->device!=(void*)(uintptr_t)device)return;
    vk_cache_entry_t *e=cache->head;while(e){vk_cache_entry_t *next=e->next;vk_cache_entry_free(e);e=next;}vk_heap_free(cache);
}
VkResult vkCreatePipelineCache(VkDevice device,const VkPipelineCacheCreateInfo *ci,const VkAllocationCallbacks *a,VkPipelineCache *out){
    if(out)*out=VK_NULL_HANDLE;
    if(!device||!ci||!out||ci->sType!=VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO||(ci->initialDataSize&&!ci->pInitialData))return VK_ERROR_INITIALIZATION_FAILED;
    if(a||ci->pNext||ci->flags)return VK_ERROR_FEATURE_NOT_PRESENT;
    vk_pipeline_cache_impl_t *cache=vk_heap_alloc(sizeof(*cache));if(!cache)return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(cache,0,sizeof(*cache));cache->device=(void*)(uintptr_t)device;
    /* Invalid/incompatible cache data is ignored, as specified by Vulkan.
       Deserialize code only after strict size checks and full SPIR-V validation. */
    if(ci->initialDataSize>=sizeof(VkPipelineCacheHeaderVersionOne)){
        VkPipelineCacheHeaderVersionOne h;memcpy(&h,ci->pInitialData,sizeof(h));
        if(h.headerSize==sizeof(h)&&h.headerVersion==VK_PIPELINE_CACHE_HEADER_VERSION_ONE&&h.vendorID==0u&&h.deviceID==0u&&memcmp(h.pipelineCacheUUID,vk_cache_uuid,VK_UUID_SIZE)==0){
            vk_spv_profile_t *profile=vk_heap_alloc(sizeof(*profile));
            if(!profile){vk_heap_free(cache);return VK_ERROR_OUT_OF_HOST_MEMORY;}
            size_t offset=sizeof(h);
            while(ci->initialDataSize-offset>=8u){
                uint32_t stage,bytes;memcpy(&stage,(const uint8_t*)ci->pInitialData+offset,4u);memcpy(&bytes,(const uint8_t*)ci->pInitialData+offset+4u,4u);offset+=8u;
                if(bytes<20u||(bytes&3u)||bytes>ci->initialDataSize-offset)break;
                VkShaderModuleCreateInfo sm={0};sm.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;sm.codeSize=bytes;sm.pCode=(const uint32_t*)((const uint8_t*)ci->pInitialData+offset);
                VkShaderModule module=0;
                if(vkCreateShaderModule(device,&sm,0,&module)==VK_SUCCESS){
                    if(vk_spv_parse_profile((void*)(uintptr_t)module,(VkShaderStageFlagBits)stage,profile))vk_cache_insert(cache,(void*)(uintptr_t)module,stage,profile);
                    vkDestroyShaderModule(device,module,0);
                }
                offset+=bytes;
            }
            vk_heap_free(profile);
        }
    }
    *out=(VkPipelineCache)(uintptr_t)cache;return VK_SUCCESS;
}
VkResult vkGetPipelineCacheData(VkDevice device,VkPipelineCache handle,size_t *size,void *out){
    vk_pipeline_cache_impl_t *cache=(void*)(uintptr_t)handle;
    if(!cache||cache->device!=(void*)(uintptr_t)device||!size)return VK_ERROR_INITIALIZATION_FAILED;
    size_t needed=sizeof(VkPipelineCacheHeaderVersionOne);
    vk_cache_entry_t *head=__atomic_load_n(&cache->head,__ATOMIC_ACQUIRE);
    for(vk_cache_entry_t *e=head;e;e=e->next){if(e->bytes>SIZE_MAX-needed-8u)return VK_ERROR_OUT_OF_HOST_MEMORY;needed+=8u+e->bytes;}
    if(!out){*size=needed;return VK_SUCCESS;}
    size_t capacity=*size;*size=0u;if(capacity<sizeof(VkPipelineCacheHeaderVersionOne))return VK_INCOMPLETE;
    VkPipelineCacheHeaderVersionOne h={0};h.headerSize=sizeof(h);h.headerVersion=VK_PIPELINE_CACHE_HEADER_VERSION_ONE;memcpy(h.pipelineCacheUUID,vk_cache_uuid,VK_UUID_SIZE);
    memcpy(out,&h,sizeof(h));size_t written=sizeof(h);
    for(vk_cache_entry_t *e=head;e;e=e->next){if(e->bytes+8u>capacity-written){*size=written;return VK_INCOMPLETE;}
        memcpy((uint8_t*)out+written,&e->stage,4u);memcpy((uint8_t*)out+written+4u,&e->bytes,4u);memcpy((uint8_t*)out+written+8u,e->code,e->bytes);written+=8u+e->bytes;}
    *size=written;return VK_SUCCESS;
}
VkResult vkMergePipelineCaches(VkDevice device,VkPipelineCache destination,uint32_t count,const VkPipelineCache *sources){
    vk_pipeline_cache_impl_t *dest=(void*)(uintptr_t)destination;
    if(!dest||dest->device!=(void*)(uintptr_t)device||!count||!sources)return VK_ERROR_INITIALIZATION_FAILED;
    for(uint32_t i=0;i<count;++i){vk_pipeline_cache_impl_t *source=(void*)(uintptr_t)sources[i];if(!source||source==dest||source->device!=dest->device)return VK_ERROR_INITIALIZATION_FAILED;}
    for(uint32_t i=0;i<count;++i){vk_pipeline_cache_impl_t *source=(void*)(uintptr_t)sources[i];
        for(vk_cache_entry_t *e=__atomic_load_n(&source->head,__ATOMIC_ACQUIRE);e;e=e->next){int found=0;
            for(vk_cache_entry_t *d=__atomic_load_n(&dest->head,__ATOMIC_ACQUIRE);d;d=d->next)if(d->bytes==e->bytes&&d->stage==e->stage&&memcmp(d->code,e->code,e->bytes)==0){found=1;break;}
            if(!found){vk_shader_module_impl_t module={0};module.code=e->code;module.code_size=e->bytes;vk_cache_insert(dest,&module,e->stage,e->profile);}
        }
    }
    return VK_SUCCESS;
}

static VkResult vk_create_graphics_pipeline(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo *pCreateInfos, const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines,vk_spv_profile_t *vp,vk_spv_profile_t *fp) {
    vk_device_impl_t *dev = (vk_device_impl_t *)(uintptr_t)device;
    const VkGraphicsPipelineCreateInfo *info;
    vk_pipeline_layout_impl_t *layout;
    vk_render_pass_impl_t *render_pass;
    vk_shader_module_impl_t *vertex_shader = 0;
    vk_shader_module_impl_t *fragment_shader = 0;
    vk_pipeline_impl_t *pipeline;
    uint8_t dynamic_viewport = 0u;
    uint8_t dynamic_scissor = 0u;
    uint32_t dynamic_mask=0u;
    if (dev == 0 || createInfoCount == 0u || pCreateInfos == 0 || pPipelines == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (uint32_t i = 0u; i < createInfoCount; ++i) {
        pPipelines[i] = VK_NULL_HANDLE;
    }
    if (pAllocator != 0 || createInfoCount != 1u) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    vk_pipeline_cache_impl_t *cache=(void*)(uintptr_t)pipelineCache;
    if(cache&&cache->device!=dev)return VK_ERROR_INITIALIZATION_FAILED;
    info = &pCreateInfos[0];
    if (info->pDynamicState != 0) {
        const VkPipelineDynamicStateCreateInfo *dynamic = info->pDynamicState;
        if (dynamic->sType != VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO ||
            dynamic->pNext != 0 || dynamic->flags != 0u ||
            dynamic->dynamicStateCount > 9u ||
            (dynamic->dynamicStateCount != 0u && dynamic->pDynamicStates == 0)) {
            return VK_ERROR_FEATURE_NOT_PRESENT;
        }
        for (uint32_t i=0u;i<dynamic->dynamicStateCount;++i){
            VkDynamicState state=dynamic->pDynamicStates[i];
            if(state<VK_DYNAMIC_STATE_VIEWPORT||state>VK_DYNAMIC_STATE_STENCIL_REFERENCE||
               state==VK_DYNAMIC_STATE_DEPTH_BOUNDS||(dynamic_mask&(1u<<state)))return VK_ERROR_FEATURE_NOT_PRESENT;
            dynamic_mask|=1u<<state;
        }
        dynamic_viewport=(dynamic_mask&(1u<<VK_DYNAMIC_STATE_VIEWPORT))!=0u;
        dynamic_scissor=(dynamic_mask&(1u<<VK_DYNAMIC_STATE_SCISSOR))!=0u;
    }
    if (info->sType != VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO ||
        info->pNext != 0 || info->flags != 0u || info->stageCount != 2u ||
        info->pStages == 0 || info->layout == VK_NULL_HANDLE ||
        info->renderPass == VK_NULL_HANDLE || info->subpass != 0u ||
        info->basePipelineHandle != VK_NULL_HANDLE || info->basePipelineIndex != -1 ||
        info->pVertexInputState == 0 || info->pInputAssemblyState == 0 ||
        info->pViewportState == 0 || info->pRasterizationState == 0 ||
        info->pMultisampleState == 0 || info->pColorBlendState == 0 ||
        info->pTessellationState != 0 ||
        (info->pDepthStencilState != 0 &&
         (info->pDepthStencilState->sType != VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO ||
          info->pDepthStencilState->pNext != 0 || info->pDepthStencilState->flags != 0u ||
          (info->pDepthStencilState->depthTestEnable != VK_FALSE &&
           info->pDepthStencilState->depthTestEnable != VK_TRUE) ||
          (info->pDepthStencilState->depthWriteEnable != VK_FALSE &&
           info->pDepthStencilState->depthWriteEnable != VK_TRUE) ||
          info->pDepthStencilState->depthCompareOp < VK_COMPARE_OP_NEVER ||
          info->pDepthStencilState->depthCompareOp > VK_COMPARE_OP_ALWAYS ||
          info->pDepthStencilState->depthBoundsTestEnable != VK_FALSE ||
          (info->pDepthStencilState->stencilTestEnable != VK_FALSE && info->pDepthStencilState->stencilTestEnable != VK_TRUE)))) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (info->pDepthStencilState != 0 && info->pDepthStencilState->stencilTestEnable) {
        const VkStencilOpState states[2] = {info->pDepthStencilState->front, info->pDepthStencilState->back};
        for (uint32_t i = 0u; i < 2u; ++i)
            if (states[i].compareOp < VK_COMPARE_OP_NEVER || states[i].compareOp > VK_COMPARE_OP_ALWAYS ||
                states[i].failOp < VK_STENCIL_OP_KEEP || states[i].failOp > VK_STENCIL_OP_DECREMENT_AND_WRAP ||
                states[i].passOp < VK_STENCIL_OP_KEEP || states[i].passOp > VK_STENCIL_OP_DECREMENT_AND_WRAP ||
                states[i].depthFailOp < VK_STENCIL_OP_KEEP || states[i].depthFailOp > VK_STENCIL_OP_DECREMENT_AND_WRAP)
                return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (info->pVertexInputState->sType != VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO ||
        info->pVertexInputState->pNext != 0 || info->pVertexInputState->flags != 0u ||
        info->pVertexInputState->vertexBindingDescriptionCount > 1u ||
        info->pVertexInputState->vertexAttributeDescriptionCount >
            VK_SPV_MAX_INTERFACE_LOCATIONS ||
        (info->pVertexInputState->vertexBindingDescriptionCount != 0u &&
         info->pVertexInputState->pVertexBindingDescriptions == 0) ||
        (info->pVertexInputState->vertexAttributeDescriptionCount != 0u &&
         info->pVertexInputState->pVertexAttributeDescriptions == 0) ||
        info->pInputAssemblyState->sType != VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO ||
        info->pInputAssemblyState->pNext != 0 || info->pInputAssemblyState->flags != 0u ||
        (info->pInputAssemblyState->topology != VK_PRIMITIVE_TOPOLOGY_POINT_LIST &&
         info->pInputAssemblyState->topology != VK_PRIMITIVE_TOPOLOGY_LINE_LIST &&
         info->pInputAssemblyState->topology != VK_PRIMITIVE_TOPOLOGY_LINE_STRIP &&
         info->pInputAssemblyState->topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST &&
         info->pInputAssemblyState->topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP &&
         info->pInputAssemblyState->topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN) ||
        info->pInputAssemblyState->primitiveRestartEnable != VK_FALSE ||
        info->pViewportState->sType != VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO ||
        info->pViewportState->pNext != 0 || info->pViewportState->flags != 0u ||
        info->pViewportState->viewportCount != 1u || info->pViewportState->scissorCount != 1u ||
        (dynamic_viewport == 0u && info->pViewportState->pViewports == 0) ||
        (dynamic_scissor == 0u && info->pViewportState->pScissors == 0) ||
        info->pRasterizationState->sType != VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO ||
        info->pRasterizationState->pNext != 0 || info->pRasterizationState->flags != 0u ||
        info->pRasterizationState->depthClampEnable != VK_FALSE ||
        info->pRasterizationState->rasterizerDiscardEnable != VK_FALSE ||
        info->pRasterizationState->polygonMode != VK_POLYGON_MODE_FILL ||
        (info->pRasterizationState->cullMode &
         ~(VK_CULL_MODE_FRONT_BIT | VK_CULL_MODE_BACK_BIT)) != 0u ||
        (info->pRasterizationState->frontFace != VK_FRONT_FACE_COUNTER_CLOCKWISE &&
         info->pRasterizationState->frontFace != VK_FRONT_FACE_CLOCKWISE) ||
        (info->pRasterizationState->depthBiasEnable != VK_FALSE && info->pRasterizationState->depthBiasEnable != VK_TRUE) ||
        (info->pRasterizationState->depthBiasEnable && !(dynamic_mask&(1u<<VK_DYNAMIC_STATE_DEPTH_BIAS)) &&
         (info->pRasterizationState->depthBiasClamp!=0.0f || !vk_clip_float_is_finite(info->pRasterizationState->depthBiasConstantFactor) || !vk_clip_float_is_finite(info->pRasterizationState->depthBiasSlopeFactor))) ||
        (!(dynamic_mask&(1u<<VK_DYNAMIC_STATE_LINE_WIDTH)) && info->pRasterizationState->lineWidth != 1.0f) ||
        info->pMultisampleState->sType != VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO ||
        info->pMultisampleState->pNext != 0 || info->pMultisampleState->flags != 0u ||
        !vk_sample_count_supported(info->pMultisampleState->rasterizationSamples) ||
        info->pMultisampleState->sampleShadingEnable != VK_FALSE ||
        (info->pMultisampleState->alphaToCoverageEnable != VK_FALSE && info->pMultisampleState->alphaToCoverageEnable != VK_TRUE) ||
        info->pMultisampleState->alphaToOneEnable != VK_FALSE ||
        info->pColorBlendState->sType != VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO ||
        info->pColorBlendState->pNext != 0 || info->pColorBlendState->flags != 0u ||
        (info->pColorBlendState->logicOpEnable != VK_FALSE && info->pColorBlendState->logicOpEnable != VK_TRUE) ||
        (info->pColorBlendState->logicOpEnable && (!dev->logic_op || info->pColorBlendState->logicOp<VK_LOGIC_OP_CLEAR || info->pColorBlendState->logicOp>VK_LOGIC_OP_SET)) ||
        info->pColorBlendState->attachmentCount != 1u || info->pColorBlendState->pAttachments == 0 ||
        (info->pColorBlendState->pAttachments[0].blendEnable != VK_FALSE &&
         info->pColorBlendState->pAttachments[0].blendEnable != VK_TRUE) ||
        !vk_blend_factor_supported(info->pColorBlendState->pAttachments[0].srcColorBlendFactor, 0) ||
        !vk_blend_factor_supported(info->pColorBlendState->pAttachments[0].dstColorBlendFactor, 0) ||
        !vk_blend_factor_supported(info->pColorBlendState->pAttachments[0].srcAlphaBlendFactor, 1) ||
        !vk_blend_factor_supported(info->pColorBlendState->pAttachments[0].dstAlphaBlendFactor, 1) ||
        !vk_blend_op_supported(info->pColorBlendState->pAttachments[0].colorBlendOp) ||
        !vk_blend_op_supported(info->pColorBlendState->pAttachments[0].alphaBlendOp) ||
        (info->pColorBlendState->pAttachments[0].colorWriteMask &
         ~(VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT)) != 0u) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    {
        const VkViewport *v = info->pViewportState->pViewports;
        const VkRect2D *s = info->pViewportState->pScissors;
        if ((dynamic_viewport == 0u &&
             (v == 0 || !(v->width > 0.0f) || !(v->height > 0.0f) ||
              !(v->x >= 0.0f) || !(v->y >= 0.0f) ||
              v->x + v->width > 4096.0f || v->y + v->height > 4096.0f ||
              !(v->minDepth >= 0.0f) || !(v->maxDepth <= 1.0f) ||
              v->minDepth > v->maxDepth)) ||
            (dynamic_scissor == 0u &&
             (s == 0 || s->offset.x < 0 || s->offset.y < 0 ||
              s->extent.width == 0u || s->extent.height == 0u ||
              s->offset.x > 4096 || s->offset.y > 4096 ||
              s->extent.width > 4096u - (uint32_t)s->offset.x ||
              s->extent.height > 4096u - (uint32_t)s->offset.y)))
            return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    layout = (vk_pipeline_layout_impl_t *)(uintptr_t)info->layout;
    render_pass = (vk_render_pass_impl_t *)(uintptr_t)info->renderPass;
    if (layout == 0 || render_pass == 0 || layout->device != dev || render_pass->device != dev ||
        layout->destroy_requested != 0u || render_pass->destroy_requested != 0u ||
        layout->references == UINT32_MAX || render_pass->references == UINT32_MAX) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (render_pass->samples != info->pMultisampleState->rasterizationSamples) return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0u; i < info->stageCount; ++i) {
        const VkPipelineShaderStageCreateInfo *stage = &info->pStages[i];
        vk_shader_module_impl_t *module = (vk_shader_module_impl_t *)(uintptr_t)stage->module;
        if (stage->sType != VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO ||
            stage->pNext != 0 || stage->flags != 0u || stage->module == VK_NULL_HANDLE ||
            stage->pName == 0 || !string_equals_local(stage->pName, "main") ||
            stage->pSpecializationInfo != 0 || module == 0 || module->device != dev ||
            module->destroy_requested != 0u) return VK_ERROR_INITIALIZATION_FAILED;
        if (stage->stage == VK_SHADER_STAGE_VERTEX_BIT && vertex_shader == 0) vertex_shader = module;
        else if (stage->stage == VK_SHADER_STAGE_FRAGMENT_BIT && fragment_shader == 0) fragment_shader = module;
        else return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (vertex_shader == 0 || fragment_shader == 0) return VK_ERROR_FEATURE_NOT_PRESENT;
    if (!vk_cached_profile(cache,vertex_shader, VK_SHADER_STAGE_VERTEX_BIT, &(*vp)) ||
        !vk_spv_profile_layout_supported(&(*vp), layout, VK_SHADER_STAGE_VERTEX_BIT) ||
        !vk_cached_profile(cache,fragment_shader, VK_SHADER_STAGE_FRAGMENT_BIT, &(*fp)) ||
        !vk_spv_profile_layout_supported(&(*fp), layout, VK_SHADER_STAGE_FRAGMENT_BIT) ||
        ((*fp).fragment_varying_mask &
         ~(*vp).vertex_varying_mask) != 0u)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    for (uint32_t location = 0u;
         location < VK_SPV_MAX_INTERFACE_LOCATIONS; ++location) {
        if (((*fp).fragment_varying_mask & (1u << location)) != 0u &&
            (!vk_spv_interface_f32_components(&(*vp),
                 (*vp).vertex_varying_id[location], 3u) ||
             (*vp).vertex_varying_components[location] !=
                 (*fp).fragment_varying_components[location]))
            return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if ((*vp).attribute_mask != 0u) {
        const VkVertexInputBindingDescription *binding =
            info->pVertexInputState->pVertexBindingDescriptions;
        const VkVertexInputAttributeDescription *attributes =
            info->pVertexInputState->pVertexAttributeDescriptions;
        uint8_t supplied_mask = 0u;
        if (info->pVertexInputState->vertexBindingDescriptionCount != 1u ||
            info->pVertexInputState->vertexAttributeDescriptionCount !=
                vk_spv_popcount8((*vp).attribute_mask) ||
            binding == 0 || attributes == 0 || binding->binding != 0u ||
            binding->stride > 2048u ||
            (binding->inputRate != VK_VERTEX_INPUT_RATE_VERTEX && binding->inputRate != VK_VERTEX_INPUT_RATE_INSTANCE) ||
            info->pVertexInputState->vertexAttributeDescriptionCount >
                VK_SPV_MAX_INTERFACE_LOCATIONS)
            return VK_ERROR_FEATURE_NOT_PRESENT;
        for (uint32_t i = 0u;
             i < info->pVertexInputState->vertexAttributeDescriptionCount; ++i) {
            const VkVertexInputAttributeDescription *attribute = &attributes[i];
            uint32_t format_components, format_size;
            if (attribute->location >= VK_SPV_MAX_INTERFACE_LOCATIONS ||
                ((*vp).attribute_mask & (1u << attribute->location)) == 0u ||
                (supplied_mask & (1u << attribute->location)) != 0u ||
                attribute->binding != 0u ||
                !vk_vertex_format_info(attribute->format, &format_components,
                    &format_size, 0, 0) ||
                format_components != (*vp).attribute_components[attribute->location] ||
                attribute->offset > 2047u)
                return VK_ERROR_FEATURE_NOT_PRESENT;
            supplied_mask |= (uint8_t)(1u << attribute->location);
        }
        if (supplied_mask != (*vp).attribute_mask)
            return VK_ERROR_FEATURE_NOT_PRESENT;
    } else if (info->pVertexInputState->vertexBindingDescriptionCount != 0u ||
               info->pVertexInputState->vertexAttributeDescriptionCount != 0u) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    pipeline = (vk_pipeline_impl_t *)vk_heap_alloc(sizeof(*pipeline));
    if (pipeline == 0) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(pipeline, 0, sizeof(*pipeline));
    if (!vk_shader_module_retain(vertex_shader)) {
        vk_heap_free(pipeline);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    if (!vk_shader_module_retain(fragment_shader)) {
        vk_shader_module_release(vertex_shader);
        vk_heap_free(pipeline);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    if (!vk_render_pass_retain(render_pass)) {
        vk_shader_module_release(fragment_shader);
        vk_shader_module_release(vertex_shader);
        vk_heap_free(pipeline);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    ++layout->references;
    pipeline->device = dev;
    pipeline->layout = layout;
    pipeline->render_pass = render_pass;
    pipeline->vertex_shader = vertex_shader;
    pipeline->fragment_shader = fragment_shader;
    if (dynamic_viewport == 0u) pipeline->viewport = *info->pViewportState->pViewports;
    if (dynamic_scissor == 0u) pipeline->scissor = *info->pViewportState->pScissors;
    pipeline->dynamic_viewport = dynamic_viewport;
    pipeline->dynamic_scissor = dynamic_scissor;
    pipeline->dynamic_mask=dynamic_mask;
    pipeline->depth_bias_enable=info->pRasterizationState->depthBiasEnable;
    pipeline->depth_bias_constant=info->pRasterizationState->depthBiasConstantFactor;
    pipeline->depth_bias_slope=info->pRasterizationState->depthBiasSlopeFactor;
    pipeline->logic_op_enable=info->pColorBlendState->logicOpEnable;
    pipeline->logic_op=info->pColorBlendState->logicOp;
    pipeline->samples = info->pMultisampleState->rasterizationSamples;
    pipeline->sample_mask = info->pMultisampleState->pSampleMask != 0 ? info->pMultisampleState->pSampleMask[0] : UINT32_MAX;
    pipeline->alpha_to_coverage = info->pMultisampleState->alphaToCoverageEnable;
    pipeline->topology = info->pInputAssemblyState->topology;
    pipeline->cull_mode = info->pRasterizationState->cullMode;
    pipeline->front_face = info->pRasterizationState->frontFace;
    pipeline->depth_test_enable = info->pDepthStencilState != 0 && vk_format_has_depth(render_pass->depth_format)
        ? info->pDepthStencilState->depthTestEnable : VK_FALSE;
    pipeline->depth_write_enable = info->pDepthStencilState != 0
        ? info->pDepthStencilState->depthWriteEnable : VK_FALSE;
    pipeline->depth_compare_op = info->pDepthStencilState != 0
        ? info->pDepthStencilState->depthCompareOp : VK_COMPARE_OP_ALWAYS;
    pipeline->stencil_test_enable = info->pDepthStencilState != 0 ? info->pDepthStencilState->stencilTestEnable : VK_FALSE;
    if (info->pDepthStencilState != 0) {
        pipeline->stencil_front = info->pDepthStencilState->front;
        pipeline->stencil_back = info->pDepthStencilState->back;
    }
    pipeline->color_blend = info->pColorBlendState->pAttachments[0];
    memcpy(pipeline->blend_constants, info->pColorBlendState->blendConstants,
           sizeof(pipeline->blend_constants));
    if ((*vp).attribute_mask != 0u) {
        pipeline->vertex_binding = info->pVertexInputState->pVertexBindingDescriptions[0];
        for (uint32_t i = 0u;
             i < info->pVertexInputState->vertexAttributeDescriptionCount; ++i) {
            const VkVertexInputAttributeDescription *attribute =
                &info->pVertexInputState->pVertexAttributeDescriptions[i];
            pipeline->vertex_attributes[attribute->location] = *attribute;
        }
        pipeline->vertex_attribute_mask = (*vp).attribute_mask;
    }
    pipeline->references = 1u;
    pipeline->destroy_requested = 0u;
    pipeline->vertex_profile = (vk_spv_profile_t *)vk_heap_alloc(sizeof((*vp)));
    pipeline->fragment_profile = (vk_spv_profile_t *)vk_heap_alloc(sizeof((*fp)));
    if (pipeline->vertex_profile == 0 || pipeline->fragment_profile == 0) {
        vk_pipeline_release(pipeline);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    pipeline->shader_workspace[0]=vk_heap_alloc((*vp).bound*sizeof(vk_spv_value_t));
    pipeline->shader_workspace[1]=vk_heap_alloc((*fp).bound*sizeof(vk_spv_value_t));
    if(!pipeline->shader_workspace[0]||!pipeline->shader_workspace[1]){vk_pipeline_release(pipeline);return VK_ERROR_OUT_OF_HOST_MEMORY;}
    (*vp).workspace=pipeline->shader_workspace[0];(*fp).workspace=pipeline->shader_workspace[1];
    *pipeline->vertex_profile = (*vp);
    *pipeline->fragment_profile = (*fp);
    pPipelines[0] = (VkPipeline)(uintptr_t)pipeline;
    return VK_SUCCESS;
}

/* Specialization is applied to a private immutable module copy; the caller's
   shader module and its default constants remain untouched. */
static VkResult vk_specialize_module(VkDevice device,VkShaderModule source,const VkSpecializationInfo *spec,VkShaderModule *out){
    vk_shader_module_impl_t *module=(void*)(uintptr_t)source;
    if(!module||module->device!=(void*)(uintptr_t)device||module->destroy_requested)return VK_ERROR_INITIALIZATION_FAILED;
    uint32_t words=(uint32_t)(module->code_size/4u),bound=module->code[3];
    if(bound==0u||bound>VK_SPV_MAX_IDS)return VK_ERROR_FEATURE_NOT_PRESENT;
    if(spec){
        if((spec->mapEntryCount&&!spec->pMapEntries)||(spec->dataSize&&!spec->pData))return VK_ERROR_INITIALIZATION_FAILED;
        for(uint32_t i=0;i<spec->mapEntryCount;++i){const VkSpecializationMapEntry *e=&spec->pMapEntries[i];
            if(e->offset>spec->dataSize||e->size>spec->dataSize-e->offset)return VK_ERROR_INITIALIZATION_FAILED;
            for(uint32_t j=0;j<i;++j)if(spec->pMapEntries[j].constantID==e->constantID)return VK_ERROR_INITIALIZATION_FAILED;
        }
    }
    uint32_t *code=vk_heap_alloc(module->code_size),*ids=vk_heap_alloc(bound*4u);uint8_t *kind=vk_heap_alloc(bound);
    VkResult result=VK_ERROR_OUT_OF_HOST_MEMORY;
    if(!code||!ids||!kind)goto done;
    memcpy(code,module->code,20u);memset(ids,0xff,bound*4u);memset(kind,0,bound);
    for(uint32_t off=5u;off<words;){const uint32_t *w=module->code+off;uint32_t wc=w[0]>>16u,op=w[0]&65535u;
        if(op==71u&&wc==4u&&w[2]==1u){if(w[1]>=bound||ids[w[1]]!=UINT32_MAX){result=VK_ERROR_INITIALIZATION_FAILED;goto done;}ids[w[1]]=w[3];}
        if(op==20u&&wc==2u&&w[1]<bound)kind[w[1]]=1u;
        if((op==21u||op==22u)&&wc>=3u&&w[1]<bound&&w[2]==32u)kind[w[1]]=2u;
        off+=wc;
    }
    uint32_t write=5u;
    for(uint32_t off=5u;off<words;){const uint32_t *w=module->code+off;uint32_t wc=w[0]>>16u,op=w[0]&65535u;
        if(op==71u&&wc==4u&&w[2]==1u){off+=wc;continue;}
        memcpy(code+write,w,wc*4u);
        if(op>=48u&&op<=52u){
            if(op==52u){result=VK_ERROR_FEATURE_NOT_PRESENT;goto done;}
            if(wc<3u||w[1]>=bound||w[2]>=bound){result=VK_ERROR_INITIALIZATION_FAILED;goto done;}
            uint32_t value=op==48u?1u:op==49u?0u:wc==4u?w[3]:0u;
            if(op==48u||op==49u||op==50u){
                if((op==50u&&(wc!=4u||kind[w[1]]!=2u))||(op!=50u&&(wc!=3u||kind[w[1]]!=1u))){result=VK_ERROR_FEATURE_NOT_PRESENT;goto done;}
                if(spec)for(uint32_t i=0;i<spec->mapEntryCount;++i)if(ids[w[2]]==spec->pMapEntries[i].constantID){
                    if(spec->pMapEntries[i].size!=4u){result=VK_ERROR_INITIALIZATION_FAILED;goto done;}
                    memcpy(&value,(const uint8_t*)spec->pData+spec->pMapEntries[i].offset,4u);
                }
                code[write]=(wc<<16u)|(op==50u?43u:value?41u:42u);if(op==50u)code[write+3u]=value;
            }else code[write]=(wc<<16u)|44u;
        }
        write+=wc;off+=wc;
    }
    VkShaderModuleCreateInfo ci={0};ci.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;ci.codeSize=write*4u;ci.pCode=code;
    result=vkCreateShaderModule(device,&ci,0,out);
done:vk_heap_free(code);vk_heap_free(ids);vk_heap_free(kind);return result;
}
VkResult vkCreateGraphicsPipelines(VkDevice device,VkPipelineCache cache,uint32_t count,const VkGraphicsPipelineCreateInfo *infos,const VkAllocationCallbacks *allocator,VkPipeline *out){
    if(!device||!count||!infos||!out)return VK_ERROR_INITIALIZATION_FAILED;
    for(uint32_t i=0;i<count;++i)out[i]=VK_NULL_HANDLE;
    vk_spv_profile_t *vp=vk_heap_alloc(sizeof(*vp)),*fp=vk_heap_alloc(sizeof(*fp));
    if(!vp||!fp){vk_heap_free(vp);vk_heap_free(fp);return VK_ERROR_OUT_OF_HOST_MEMORY;}
    VkResult result=VK_SUCCESS;
    for(uint32_t i=0;i<count;++i){
        VkGraphicsPipelineCreateInfo ci=infos[i];VkPipelineShaderStageCreateInfo stages[2];VkShaderModule copies[2]={0};
        VkResult current=VK_ERROR_FEATURE_NOT_PRESENT;
        if(ci.stageCount==2u&&ci.pStages){
            memcpy(stages,ci.pStages,sizeof(stages));ci.pStages=stages;
            for(uint32_t j=0;j<2u;++j){current=vk_specialize_module(device,stages[j].module,stages[j].pSpecializationInfo,&copies[j]);
                if(current!=VK_SUCCESS)break;stages[j].module=copies[j];stages[j].pSpecializationInfo=0;
            }
            if(current==VK_SUCCESS)current=vk_create_graphics_pipeline(device,cache,1u,&ci,allocator,&out[i],vp,fp);
        }
        for(uint32_t j=0;j<2u;++j)if(copies[j])vkDestroyShaderModule(device,copies[j],0);
        if(current!=VK_SUCCESS)result=current;
    }
    vk_heap_free(vp);vk_heap_free(fp);return result;
}

#include "vulkan_compute.h"

void vkDestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks *pAllocator) {
    vk_pipeline_impl_t *impl = (vk_pipeline_impl_t *)(uintptr_t)pipeline;
    (void)device;
    (void)pAllocator;
    if (impl != 0 && impl->destroy_requested == 0u) {
        impl->destroy_requested = 1u;
        vk_pipeline_release(impl);
    }
}

void vkGetRenderAreaGranularity(VkDevice device,VkRenderPass pass,VkExtent2D *out){
    vk_render_pass_impl_t *p=(void*)(uintptr_t)pass;
    if(out){out->width=out->height=0u;if(p&&p->device==(void*)(uintptr_t)device&&!p->destroy_requested)out->width=out->height=1u;}
}

void vkCmdClearAttachments(VkCommandBuffer cb,uint32_t count,const VkClearAttachment *attachments,uint32_t rectCount,const VkClearRect *rects){
    vk_command_buffer_impl_t *cmd=(void*)(uintptr_t)cb;if(!cmd||!cmd->recording)return;
    vk_framebuffer_impl_t *fb=cmd->active_framebuffer;
    if(!cmd->active_render_pass||!fb||(count&&!attachments)||(rectCount&&!rects)){vk_record_unsupported(cmd);return;}
    for(uint32_t i=0u;i<count;++i){const VkClearAttachment *a=&attachments[i];
        if(a->aspectMask==VK_IMAGE_ASPECT_COLOR_BIT){if(a->colorAttachment!=0u){vk_record_unsupported(cmd);return;}}
        else if(!a->aspectMask||(a->aspectMask&~(VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT))||!fb->depth_attachment||
                (a->aspectMask&~fb->depth_attachment->image->aspects)||
                ((a->aspectMask&VK_IMAGE_ASPECT_DEPTH_BIT)&&!(a->clearValue.depthStencil.depth>=0.0f&&a->clearValue.depthStencil.depth<=1.0f))){vk_record_unsupported(cmd);return;}
    }
    for(uint32_t i=0u;i<rectCount;++i){const VkClearRect *r=&rects[i];const VkRect2D *area=&cmd->active_render_area;
        if(r->baseArrayLayer||r->layerCount!=1u||!r->rect.extent.width||!r->rect.extent.height||
           r->rect.offset.x<area->offset.x||r->rect.offset.y<area->offset.y||
           (uint64_t)(r->rect.offset.x-area->offset.x)+r->rect.extent.width>area->extent.width||
           (uint64_t)(r->rect.offset.y-area->offset.y)+r->rect.extent.height>area->extent.height){vk_record_unsupported(cmd);return;}
    }
    for(uint32_t i=0u;i<count;++i)for(uint32_t j=0u;j<rectCount;++j){
        vk_recorded_command_t *r=vk_append_record(cmd,VK_RECORD_CLEAR_ATTACHMENTS);if(!r)return;
        if(!vk_framebuffer_retain(fb)){cmd->record_error=VK_ERROR_OUT_OF_HOST_MEMORY;return;}
        r->args.clear_attachments.framebuffer=fb;r->args.clear_attachments.rect=rects[j].rect;
        r->args.clear_attachments.aspects=attachments[i].aspectMask;r->args.clear_attachments.value=attachments[i].clearValue;
    }
}

void vkCmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin, uint32_t contents) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    vk_framebuffer_impl_t *framebuffer;
    vk_render_pass_impl_t *render_pass;
    vk_image_impl_t *image;
    vk_image_impl_t *depth_image;
    vk_recorded_command_t *record;
    if (cmd == 0 || cmd->recording == 0u) return;
    if(cmd->level==VK_COMMAND_BUFFER_LEVEL_SECONDARY){vk_record_unsupported(cmd);return;}
    if (pRenderPassBegin == 0 ||
        pRenderPassBegin->sType != VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO ||
        pRenderPassBegin->pNext != 0 || contents != VK_SUBPASS_CONTENTS_INLINE ||
        cmd->active_framebuffer != 0) {
        vk_record_unsupported(cmd);
        return;
    }
    framebuffer = (vk_framebuffer_impl_t *)(uintptr_t)pRenderPassBegin->framebuffer;
    render_pass = (vk_render_pass_impl_t *)(uintptr_t)pRenderPassBegin->renderPass;
    if (framebuffer == 0 || render_pass == 0 || framebuffer->device != cmd->device ||
        framebuffer->destroy_requested != 0u || framebuffer->render_pass != render_pass ||
        render_pass->destroy_requested != 0u ||
        pRenderPassBegin->renderArea.offset.x < 0 || pRenderPassBegin->renderArea.offset.y < 0 ||
        (uint32_t)pRenderPassBegin->renderArea.offset.x > framebuffer->width ||
        (uint32_t)pRenderPassBegin->renderArea.offset.y > framebuffer->height ||
        pRenderPassBegin->renderArea.extent.width == 0u ||
        pRenderPassBegin->renderArea.extent.height == 0u ||
        pRenderPassBegin->renderArea.extent.width > framebuffer->width -
            (uint32_t)pRenderPassBegin->renderArea.offset.x ||
        pRenderPassBegin->renderArea.extent.height > framebuffer->height -
            (uint32_t)pRenderPassBegin->renderArea.offset.y) {
        vk_record_unsupported(cmd);
        return;
    }
    image = framebuffer->attachment0 == 0 ? 0 : framebuffer->attachment0->image;
    depth_image = framebuffer->depth_attachment == 0
        ? 0 : framebuffer->depth_attachment->image;
    if (image == 0 || image->memory == 0 ||
        (render_pass->color_load_op == VK_ATTACHMENT_LOAD_OP_CLEAR &&
         (pRenderPassBegin->clearValueCount <= render_pass->color_index || pRenderPassBegin->pClearValues == 0)) ||
        (render_pass->has_depth != 0u &&
         (depth_image == 0 || vk_image_subresource_pixels(depth_image,
              framebuffer->depth_attachment->range.baseMipLevel, framebuffer->depth_attachment->range.baseArrayLayer) == 0 ||
          (((vk_format_has_depth(render_pass->depth_format) && render_pass->depth_load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) ||
            (vk_format_has_stencil(render_pass->depth_format) && render_pass->stencil_load_op == VK_ATTACHMENT_LOAD_OP_CLEAR)) &&
           (pRenderPassBegin->clearValueCount <= render_pass->depth_index || pRenderPassBegin->pClearValues == 0)))) ||
        (pRenderPassBegin->clearValueCount != 0u && pRenderPassBegin->pClearValues == 0)) {
        vk_record_unsupported(cmd);
        return;
    }
    {
        float clear_depth = 1.0f;
        if (render_pass->has_depth != 0u && vk_format_has_depth(render_pass->depth_format) &&
            render_pass->depth_load_op == VK_ATTACHMENT_LOAD_OP_CLEAR)
            clear_depth = pRenderPassBegin->pClearValues[render_pass->depth_index].depthStencil.depth;
        if (!(clear_depth >= 0.0f) || !(clear_depth <= 1.0f)) {
            vk_record_unsupported(cmd);
            return;
        }
    }
    record = vk_append_record(cmd, VK_RECORD_BEGIN_RENDER_PASS);
    if (record == 0) return;
    if (!vk_framebuffer_retain(framebuffer)) {
        cmd->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
        return;
    }
    record->args.begin_render_pass.framebuffer = framebuffer;
    record->args.begin_render_pass.area = pRenderPassBegin->renderArea;
    record->args.begin_render_pass.clear_color = pRenderPassBegin->clearValueCount <= render_pass->color_index ?
        0u : pack_rgba(&pRenderPassBegin->pClearValues[render_pass->color_index].color);
    record->args.begin_render_pass.clear_depth =
        render_pass->has_depth != 0u && pRenderPassBegin->clearValueCount > render_pass->depth_index
            ? pRenderPassBegin->pClearValues[render_pass->depth_index].depthStencil.depth : 1.0f;
    record->args.begin_render_pass.clear_stencil = render_pass->has_depth && pRenderPassBegin->clearValueCount > render_pass->depth_index
        ? pRenderPassBegin->pClearValues[render_pass->depth_index].depthStencil.stencil : 0u;
    cmd->active_framebuffer = framebuffer;
    cmd->active_render_pass = 1u;
    cmd->active_render_area = pRenderPassBegin->renderArea;
}

void vkCmdEndRenderPass(VkCommandBuffer commandBuffer) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    vk_framebuffer_impl_t *framebuffer;
    vk_image_impl_t *image;
    vk_render_pass_impl_t *render_pass;
    vk_recorded_command_t *record;
    if (cmd == 0 || cmd->recording == 0u) return;
    framebuffer = cmd->active_framebuffer;
    if (framebuffer == 0 || framebuffer->attachment0 == 0 ||
        (image = framebuffer->attachment0->image) == 0 ||
        (render_pass = framebuffer->render_pass) == 0) {
        vk_record_unsupported(cmd);
        return;
    }
    record = vk_append_record(cmd, VK_RECORD_END_RENDER_PASS);
    if (record == 0) return;
    if (!vk_image_retain(image)) {
        cmd->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
        return;
    }
    if (framebuffer->depth_attachment != 0 &&
        !vk_image_retain(framebuffer->depth_attachment->image)) {
        vk_image_release(image);
        cmd->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
        return;
    }
    if(render_pass->has_resolve){
        if(!vk_framebuffer_retain(framebuffer)){
            vk_image_release(image);vk_image_release(framebuffer->depth_attachment?framebuffer->depth_attachment->image:0);
            cmd->record_error=VK_ERROR_OUT_OF_HOST_MEMORY;return;
        }
        record->args.end_render_pass.resolve_framebuffer=framebuffer;
        record->args.end_render_pass.resolve_area=cmd->active_render_area;
    }
    record->args.end_render_pass.image = image;
    record->args.end_render_pass.depth_image = framebuffer->depth_attachment != 0
        ? framebuffer->depth_attachment->image : 0;
    record->args.end_render_pass.subresource = vk_view_subresource(framebuffer->attachment0);
    record->args.end_render_pass.depth_subresource = framebuffer->depth_attachment != 0
        ? vk_view_subresource(framebuffer->depth_attachment) : 0u;
    record->args.end_render_pass.expected_layout = render_pass->subpass_layout;
    record->args.end_render_pass.final_layout = render_pass->final_layout;
    record->args.end_render_pass.depth_expected_layout = render_pass->depth_subpass_layout;
    record->args.end_render_pass.depth_final_layout = render_pass->depth_final_layout;
    cmd->active_framebuffer = 0;
    cmd->active_render_pass = 0u;
}

void vkCmdBindPipeline(VkCommandBuffer commandBuffer, uint32_t pipelineBindPoint, VkPipeline pipeline) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    vk_pipeline_impl_t *impl = (vk_pipeline_impl_t *)(uintptr_t)pipeline;
    if (cmd == 0 || cmd->recording == 0u) return;
    if ((pipelineBindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS && pipelineBindPoint!=VK_PIPELINE_BIND_POINT_COMPUTE) || impl == 0 || impl->bind_point!=pipelineBindPoint ||
        impl->device != cmd->device || impl->destroy_requested != 0u ||
        !vk_pipeline_retain(impl)) {
        vk_record_unsupported(cmd);
        return;
    }
    if(pipelineBindPoint==VK_PIPELINE_BIND_POINT_COMPUTE){vk_pipeline_release(cmd->compute_pipeline);cmd->compute_pipeline=impl;}
    else {vk_pipeline_release(cmd->pipeline);cmd->pipeline=impl;}
}

void vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    vk_buffer_impl_t *buffer;
    if (cmd == 0 || cmd->recording == 0u) return;
    if (firstBinding != 0u || bindingCount != 1u || pBuffers == 0 || pOffsets == 0 ||
        (buffer = (vk_buffer_impl_t *)(uintptr_t)pBuffers[0]) == 0 ||
        buffer->device != cmd->device || buffer->destroy_requested != 0u ||
        (buffer->usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) == 0u ||
        pOffsets[0] > buffer->size || !vk_buffer_retain(buffer)) {
        vk_record_unsupported(cmd);
        return;
    }
    vk_buffer_release(cmd->vertex_buffer);
    cmd->vertex_buffer = buffer;
    cmd->vertex_buffer_offset = pOffsets[0];
}

void vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    vk_buffer_impl_t *impl = (vk_buffer_impl_t *)(uintptr_t)buffer;
    VkDeviceSize alignment;
    if (cmd == 0 || cmd->recording == 0u) return;
    if (indexType == VK_INDEX_TYPE_UINT16) alignment = 2u;
    else if (indexType == VK_INDEX_TYPE_UINT32) alignment = 4u;
    else {
        vk_record_unsupported(cmd);
        return;
    }
    if (impl == 0 || impl->device != cmd->device ||
        impl->destroy_requested != 0u ||
        (impl->usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) == 0u ||
        (offset % alignment) != 0u || offset > impl->size ||
        !vk_buffer_retain(impl)) {
        vk_record_unsupported(cmd);
        return;
    }
    vk_buffer_release(cmd->index_buffer);
    cmd->index_buffer = impl;
    cmd->index_buffer_offset = offset;
    cmd->index_type = indexType;
}

void vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport *pViewports) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    if (cmd == 0 || cmd->recording == 0u) return;
    if (firstViewport != 0u || viewportCount != 1u || pViewports == 0 ||
        !(pViewports[0].width > 0.0f) || !(pViewports[0].height > 0.0f) ||
        !(pViewports[0].x >= 0.0f) || !(pViewports[0].y >= 0.0f) ||
        pViewports[0].x + pViewports[0].width > 4096.0f ||
        pViewports[0].y + pViewports[0].height > 4096.0f ||
        !(pViewports[0].minDepth >= 0.0f) || !(pViewports[0].maxDepth <= 1.0f) ||
        pViewports[0].minDepth > pViewports[0].maxDepth) {
        vk_record_unsupported(cmd);
        return;
    }
    cmd->viewport = pViewports[0];
    cmd->viewport_set = 1u;
}

void vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D *pScissors) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    if (cmd == 0 || cmd->recording == 0u) return;
    if (firstScissor != 0u || scissorCount != 1u || pScissors == 0 ||
        pScissors[0].offset.x < 0 || pScissors[0].offset.y < 0 ||
        pScissors[0].extent.width == 0u || pScissors[0].extent.height == 0u ||
        pScissors[0].offset.x > 4096 || pScissors[0].offset.y > 4096 ||
        pScissors[0].extent.width > 4096u - (uint32_t)pScissors[0].offset.x ||
        pScissors[0].extent.height > 4096u - (uint32_t)pScissors[0].offset.y) {
        vk_record_unsupported(cmd);
        return;
    }
    cmd->scissor = pScissors[0];
    cmd->scissor_set = 1u;
}

void vkCmdSetLineWidth(VkCommandBuffer cb,float width){
    vk_command_buffer_impl_t *cmd=(void*)(uintptr_t)cb;if(!cmd||!cmd->recording)return;
    if(width!=1.0f){vk_record_unsupported(cmd);return;}cmd->dynamic_set|=1u<<VK_DYNAMIC_STATE_LINE_WIDTH;
}
void vkCmdSetDepthBias(VkCommandBuffer cb,float constant,float clamp,float slope){
    vk_command_buffer_impl_t *cmd=(void*)(uintptr_t)cb;if(!cmd||!cmd->recording)return;
    if(clamp!=0.0f||!vk_clip_float_is_finite(constant)||!vk_clip_float_is_finite(slope)){vk_record_unsupported(cmd);return;}
    cmd->draw_state.depth_bias_constant=constant;cmd->draw_state.depth_bias_slope=slope;cmd->dynamic_set|=1u<<VK_DYNAMIC_STATE_DEPTH_BIAS;
}
void vkCmdSetBlendConstants(VkCommandBuffer cb,const float values[4]){
    vk_command_buffer_impl_t *cmd=(void*)(uintptr_t)cb;if(!cmd||!cmd->recording)return;
    if(!values){vk_record_unsupported(cmd);return;}memcpy(cmd->draw_state.blend_constants,values,16u);cmd->dynamic_set|=1u<<VK_DYNAMIC_STATE_BLEND_CONSTANTS;
}
static void vk_set_stencil_dynamic(VkCommandBuffer cb,VkStencilFaceFlags faces,uint32_t value,VkDynamicState state){
    vk_command_buffer_impl_t *cmd=(void*)(uintptr_t)cb;if(!cmd||!cmd->recording)return;
    if(!faces||(faces&~VK_STENCIL_FACE_FRONT_AND_BACK)){vk_record_unsupported(cmd);return;}
    for(uint32_t i=0u;i<2u;++i)if(faces&(1u<<i)){
        if(state==VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK)cmd->draw_state.stencil[i].compareMask=value;
        else if(state==VK_DYNAMIC_STATE_STENCIL_WRITE_MASK)cmd->draw_state.stencil[i].writeMask=value;
        else cmd->draw_state.stencil[i].reference=value;
    }
    /* Front and back can be set independently. Record validity per face. */
    if(faces&VK_STENCIL_FACE_FRONT_BIT)cmd->dynamic_set|=1u<<state;
    if(faces&VK_STENCIL_FACE_BACK_BIT)cmd->dynamic_set|=1u<<(state+16u);
}
void vkCmdSetStencilCompareMask(VkCommandBuffer cb,VkStencilFaceFlags faces,uint32_t value){vk_set_stencil_dynamic(cb,faces,value,VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK);}
void vkCmdSetStencilWriteMask(VkCommandBuffer cb,VkStencilFaceFlags faces,uint32_t value){vk_set_stencil_dynamic(cb,faces,value,VK_DYNAMIC_STATE_STENCIL_WRITE_MASK);}
void vkCmdSetStencilReference(VkCommandBuffer cb,VkStencilFaceFlags faces,uint32_t value){vk_set_stencil_dynamic(cb,faces,value,VK_DYNAMIC_STATE_STENCIL_REFERENCE);}

static void vk_record_draw(vk_command_buffer_impl_t *cmd, uint32_t vertex_count,
                           uint32_t instance_count, uint32_t first_vertex,
                           uint32_t first_instance, uint8_t indexed,
                           uint32_t first_index, int32_t vertex_offset) {
    vk_recorded_command_t *record;
    const vk_spv_profile_t *vertex_profile=cmd?cmd->pipeline?cmd->pipeline->vertex_profile:0:0;
    const vk_spv_profile_t *fragment_profile=cmd?cmd->pipeline?cmd->pipeline->fragment_profile:0:0;
    VkDeviceSize index_size = 0u;
    if (cmd == 0 || cmd->recording == 0u) return;
    if(cmd->pipeline){
        uint32_t required=cmd->pipeline->dynamic_mask&~3u;
        if(!cmd->pipeline->depth_bias_enable)required&=~(1u<<VK_DYNAMIC_STATE_DEPTH_BIAS);
        if(!cmd->pipeline->stencil_test_enable)required&=~((1u<<6u)|(1u<<7u)|(1u<<8u));
        required|=(required&((1u<<6u)|(1u<<7u)|(1u<<8u)))<<16u;
        if((cmd->dynamic_set&required)!=required){vk_record_unsupported(cmd);return;}
    }
    if (cmd->active_render_pass == 0u || cmd->active_framebuffer == 0 ||
        cmd->pipeline == 0 || cmd->pipeline->render_pass != cmd->active_framebuffer->render_pass ||
        (cmd->pipeline->dynamic_viewport != 0u && cmd->viewport_set == 0u) ||
        (cmd->pipeline->dynamic_scissor != 0u && cmd->scissor_set == 0u) ||
        (instance_count && first_instance > UINT32_MAX-(instance_count-1u)) ||
        (indexed == 0u && vertex_count != 0u &&
         first_vertex > UINT32_MAX - (vertex_count - 1u))) {
        vk_record_unsupported(cmd);
        return;
    }
    if (indexed != 0u && vertex_count != 0u) {
        if (cmd->index_buffer == 0 || cmd->index_buffer->device != cmd->device ||
            cmd->index_buffer->destroy_requested != 0u ||
            (cmd->index_buffer->usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) == 0u) {
            vk_record_unsupported(cmd);
            return;
        }
        index_size = cmd->index_type == VK_INDEX_TYPE_UINT16 ? 2u :
                     cmd->index_type == VK_INDEX_TYPE_UINT32 ? 4u : 0u;
        if (index_size == 0u ||
            (uint64_t)first_index > (UINT64_MAX - cmd->index_buffer_offset) / index_size) {
            vk_record_unsupported(cmd);
            return;
        }
        VkDeviceSize index_start = cmd->index_buffer_offset +
            (VkDeviceSize)first_index * index_size;
        if (index_start > cmd->index_buffer->size ||
            (VkDeviceSize)vertex_count >
                (cmd->index_buffer->size - index_start) / index_size) {
            vk_record_unsupported(cmd);
            return;
        }
    }
    if (vertex_profile==0 ||
        !vk_spv_profile_bound_set_supported(vertex_profile, cmd->pipeline,
                                            cmd->bound_descriptor_sets) ||
        !vk_spv_profile_push_initialized(vertex_profile, VK_SHADER_STAGE_VERTEX_BIT,
                                         cmd->push_constant_initialized) ||
        fragment_profile==0 ||
        !vk_spv_profile_bound_set_supported(fragment_profile, cmd->pipeline,
                                            cmd->bound_descriptor_sets) ||
        !vk_spv_profile_push_initialized(fragment_profile, VK_SHADER_STAGE_FRAGMENT_BIT,
                                         cmd->push_constant_initialized)) {
        vk_record_unsupported(cmd);
        return;
    }
    if (cmd->pipeline->vertex_attribute_mask != 0u && vertex_count != 0u &&
        (cmd->vertex_buffer == 0 || cmd->vertex_buffer->device != cmd->device ||
         cmd->vertex_buffer->destroy_requested != 0u ||
         (cmd->vertex_buffer->usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) == 0u ||
         cmd->vertex_buffer_offset > cmd->vertex_buffer->size)) {
        vk_record_unsupported(cmd);
        return;
    }
    for (uint32_t i = 0u; i < cmd->pipeline->layout->set_layout_count; ++i) {
        vk_descriptor_set_impl_t *set = cmd->bound_descriptor_sets[i];
        vk_descriptor_set_layout_impl_t *expected =
            (vk_descriptor_set_layout_impl_t *)(uintptr_t)cmd->pipeline->layout->set_layouts[i];
        if (set != 0 && (set->layout == 0 || !vk_layout_compatible(set->layout, expected) ||
                         __atomic_load_n(&set->references, __ATOMIC_ACQUIRE) == UINT32_MAX)) {
            vk_record_unsupported(cmd);
            return;
        }
    }
    record = vk_append_record(cmd, VK_RECORD_DRAW);
    if (record == 0) return;
    if (!vk_pipeline_retain(cmd->pipeline)) {
        cmd->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
        return;
    }
    if (!vk_framebuffer_retain(cmd->active_framebuffer)) {
        vk_pipeline_release(cmd->pipeline);
        cmd->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
        return;
    }
    if(cmd->active_query){
        if(!vk_ref_retain_count(&cmd->active_query->references)){vk_framebuffer_release(cmd->active_framebuffer);vk_pipeline_release(cmd->pipeline);cmd->record_error=VK_ERROR_OUT_OF_HOST_MEMORY;return;}
        record->args.draw.query_pool=cmd->active_query;record->args.draw.query_index=cmd->active_query_index;
    }
    record->args.draw.pipeline = cmd->pipeline;
    record->args.draw.framebuffer = cmd->active_framebuffer;
    record->args.draw.render_area = cmd->active_render_area;
    record->args.draw.vertex_count = vertex_count;
    record->args.draw.instance_count = instance_count;
    record->args.draw.first_vertex = first_vertex;
    record->args.draw.first_instance = first_instance;
    record->args.draw.indexed = indexed;
    record->args.draw.first_index = first_index;
    record->args.draw.vertex_offset = vertex_offset;
    record->args.draw.viewport = cmd->pipeline->dynamic_viewport != 0u
        ? cmd->viewport : cmd->pipeline->viewport;
    record->args.draw.scissor = cmd->pipeline->dynamic_scissor != 0u
        ? cmd->scissor : cmd->pipeline->scissor;
    vk_draw_state_t *state=&record->args.draw.state;
    state->stencil[0]=cmd->pipeline->stencil_front;state->stencil[1]=cmd->pipeline->stencil_back;
    memcpy(state->blend_constants,(cmd->pipeline->dynamic_mask&(1u<<VK_DYNAMIC_STATE_BLEND_CONSTANTS))?cmd->draw_state.blend_constants:cmd->pipeline->blend_constants,16u);
    state->depth_bias_constant=(cmd->pipeline->dynamic_mask&(1u<<VK_DYNAMIC_STATE_DEPTH_BIAS))?cmd->draw_state.depth_bias_constant:cmd->pipeline->depth_bias_constant;
    state->depth_bias_slope=(cmd->pipeline->dynamic_mask&(1u<<VK_DYNAMIC_STATE_DEPTH_BIAS))?cmd->draw_state.depth_bias_slope:cmd->pipeline->depth_bias_slope;
    for(uint32_t i=0u;i<2u;++i){
        if(cmd->pipeline->dynamic_mask&(1u<<VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK))state->stencil[i].compareMask=cmd->draw_state.stencil[i].compareMask;
        if(cmd->pipeline->dynamic_mask&(1u<<VK_DYNAMIC_STATE_STENCIL_WRITE_MASK))state->stencil[i].writeMask=cmd->draw_state.stencil[i].writeMask;
        if(cmd->pipeline->dynamic_mask&(1u<<VK_DYNAMIC_STATE_STENCIL_REFERENCE))state->stencil[i].reference=cmd->draw_state.stencil[i].reference;
    }
    if (indexed != 0u && vertex_count != 0u) {
        if (!vk_buffer_retain(cmd->index_buffer)) {
            cmd->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
            return;
        }
        record->args.draw.index_buffer = cmd->index_buffer;
        record->args.draw.index_buffer_offset = cmd->index_buffer_offset;
        record->args.draw.index_type = cmd->index_type;
    }
    memcpy(record->args.draw.push_constants, cmd->push_constants,
           sizeof(record->args.draw.push_constants));
    memcpy(record->args.draw.push_constant_initialized, cmd->push_constant_initialized,
           sizeof(record->args.draw.push_constant_initialized));
    for (uint32_t i = 0u; i < cmd->pipeline->layout->set_layout_count; ++i) {
        vk_descriptor_set_impl_t *set = cmd->bound_descriptor_sets[i];
        if (set == 0) continue;
        if (!vk_ref_retain_count(&set->references)) {
            cmd->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
            return;
        }
        record->args.draw.descriptor_sets[i] = set;
    }
    if (cmd->pipeline->vertex_attribute_mask != 0u && vertex_count != 0u) {
        if (!vk_buffer_retain(cmd->vertex_buffer)) {
            cmd->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
            return;
        }
        record->args.draw.vertex_buffer = cmd->vertex_buffer;
        record->args.draw.vertex_buffer_offset = cmd->vertex_buffer_offset;
    }
}

void vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    vk_record_draw((vk_command_buffer_impl_t *)(uintptr_t)commandBuffer,
                   vertexCount, instanceCount, firstVertex, firstInstance,
                   0u, 0u, 0);
}

void vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    vk_record_draw((vk_command_buffer_impl_t *)(uintptr_t)commandBuffer,
                   indexCount, instanceCount, 0u, firstInstance,
                   1u, firstIndex, vertexOffset);
}
/* bufferImageHeight is the stride between array layers, including padding.
   Use checked byte arithmetic before any backing access. */
static int vk_buffer_image_region(vk_buffer_impl_t *buffer,
                                  vk_image_impl_t *image,
                                  const VkBufferImageCopy *r,
                                  uint64_t *row_pitch, uint64_t *layer_pitch) {
    uint64_t row, height, layer, last_row, capacity;
    if (buffer == 0 || image == 0 || r == 0 ||
        image->samples != 1u ||
        !vk_image_layers(image, &r->imageSubresource, &r->imageOffset, &r->imageExtent) ||
        (r->bufferOffset & 3u) != 0u || r->bufferOffset > buffer->size) return 0;
    const uint64_t texel = vk_aspect_bytes(image->format,r->imageSubresource.aspectMask);
    row = r->bufferRowLength == 0u ? r->imageExtent.width : r->bufferRowLength;
    height = r->bufferImageHeight == 0u ? r->imageExtent.height : r->bufferImageHeight;
    if (row < r->imageExtent.width || height < r->imageExtent.height ||
        row > UINT64_MAX / texel / height) return 0;
    row *= texel;
    layer = row * height;
    last_row = row * (r->imageExtent.height - 1u) + (uint64_t)r->imageExtent.width * texel;
    capacity = buffer->size - r->bufferOffset;
    if (last_row > capacity ||
        (uint64_t)(r->imageSubresource.layerCount - 1u) >
            (capacity - last_row) / layer) return 0;
    if (row_pitch != 0) *row_pitch = row;
    if (layer_pitch != 0) *layer_pitch = layer;
    return 1;
}

static int vk_buffer_image_regions_disjoint(vk_buffer_impl_t *buffer,
        vk_image_impl_t *image, uint32_t count, const VkBufferImageCopy *regions);

void vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer buffer,
        VkImage image, uint32_t imageLayout, uint32_t regionCount,
        const VkBufferImageCopy *pRegions) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    vk_buffer_impl_t *src = (vk_buffer_impl_t *)(uintptr_t)buffer;
    vk_image_impl_t *dst = (vk_image_impl_t *)(uintptr_t)image;
    if (cmd == 0 || cmd->recording == 0u) return;
    if (cmd->active_framebuffer != 0 || src == 0 || dst == 0 ||
        src->device != cmd->device || dst->device != cmd->device ||
        src->destroy_requested != 0u || dst->destroy_requested != 0u ||
        pRegions == 0 || regionCount == 0u ||
        (src->usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) == 0u ||
        (dst->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0u ||
        (imageLayout != VK_IMAGE_LAYOUT_GENERAL && imageLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)) {
        vk_record_unsupported(cmd); return;
    }
    for (uint32_t i = 0u; i < regionCount; ++i)
        if (!vk_buffer_image_region(src, dst, &pRegions[i], 0, 0)) {
            vk_record_unsupported(cmd); return;
        }
    if (!vk_buffer_image_regions_disjoint(src, dst, regionCount, pRegions)) {
        vk_record_unsupported(cmd); return;
    }
    for (uint32_t i = 0u; i < regionCount; ++i) {
        vk_recorded_command_t *record = vk_append_record(cmd, VK_RECORD_COPY_BUFFER_TO_IMAGE);
        if (record == 0) return;
        if (!vk_buffer_retain(src)) { cmd->record_error = VK_ERROR_OUT_OF_HOST_MEMORY; return; }
        record->args.copy_buffer_to_image.src = src;
        if (!vk_image_retain(dst)) { cmd->record_error = VK_ERROR_OUT_OF_HOST_MEMORY; return; }
        record->args.copy_buffer_to_image.dst = dst;
        record->args.copy_buffer_to_image.layout = (VkImageLayout)imageLayout;
        record->args.copy_buffer_to_image.region = pRegions[i];
    }
    cmd->bound_image = image;
}

void vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage image,
        VkImageLayout imageLayout, VkBuffer buffer, uint32_t regionCount,
        const VkBufferImageCopy *pRegions) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    vk_image_impl_t *src = (vk_image_impl_t *)(uintptr_t)image;
    vk_buffer_impl_t *dst = (vk_buffer_impl_t *)(uintptr_t)buffer;
    if (cmd == 0 || cmd->recording == 0u) return;
    if (cmd->active_framebuffer != 0 || src == 0 || dst == 0 ||
        src->device != cmd->device || dst->device != cmd->device ||
        src->destroy_requested != 0u || dst->destroy_requested != 0u ||
        pRegions == 0 || regionCount == 0u ||
        (src->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0u ||
        (dst->usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) == 0u ||
        (imageLayout != VK_IMAGE_LAYOUT_GENERAL && imageLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)) {
        vk_record_unsupported(cmd); return;
    }
    for (uint32_t i = 0u; i < regionCount; ++i)
        if (!vk_buffer_image_region(dst, src, &pRegions[i], 0, 0)) {
            vk_record_unsupported(cmd); return;
        }
    if (!vk_buffer_image_regions_disjoint(dst, src, regionCount, pRegions)) {
        vk_record_unsupported(cmd); return;
    }
    for (uint32_t i = 0u; i < regionCount; ++i) {
        vk_recorded_command_t *record = vk_append_record(cmd, VK_RECORD_COPY_IMAGE_TO_BUFFER);
        if (record == 0) return;
        if (!vk_image_retain(src)) { cmd->record_error = VK_ERROR_OUT_OF_HOST_MEMORY; return; }
        record->args.copy_image_to_buffer.src = src;
        if (!vk_buffer_retain(dst)) { cmd->record_error = VK_ERROR_OUT_OF_HOST_MEMORY; return; }
        record->args.copy_image_to_buffer.dst = dst;
        record->args.copy_image_to_buffer.layout = imageLayout;
        record->args.copy_image_to_buffer.region = pRegions[i];
    }
}

static int vk_rows_overlap(uint64_t a, uint64_t a_pitch, uint64_t a_width,
                            uint32_t a_height, uint64_t b, uint64_t b_pitch,
                            uint64_t b_width, uint32_t b_height) {
    uint32_t ay = 0u, by = 0u;
    if (a >= b + (uint64_t)(b_height - 1u) * b_pitch + b_width ||
        b >= a + (uint64_t)(a_height - 1u) * a_pitch + a_width) return 0;
    while (ay < a_height && by < b_height) {
        const uint64_t ar = a + (uint64_t)ay * a_pitch;
        const uint64_t br = b + (uint64_t)by * b_pitch;
        if (ar < br + b_width && br < ar + a_width) return 1;
        if (ar + a_width <= br) ++ay;
        else ++by;
    }
    return 0;
}

/* The union of all source rows must be disjoint from all destination rows,
   including distinct local memory imports of the same backing. */
static int vk_buffer_image_regions_disjoint(vk_buffer_impl_t *buffer,
        vk_image_impl_t *image, uint32_t count, const VkBufferImageCopy *regions) {
    const uint64_t iid = image->memory != 0 ? image->memory->backing_identity : image->backing_identity;
    if (buffer->memory == 0 || iid == 0u || buffer->memory->backing_identity != iid) return 1;
    for (uint32_t a = 0u; a < count; ++a) {
        uint64_t row, layer;
        const VkBufferImageCopy *br = &regions[a];
        if (!vk_buffer_image_region(buffer, image, br, &row, &layer)) return 0;
        for (uint32_t al = 0u; al < br->imageSubresource.layerCount; ++al) {
            uint64_t bo = buffer->memory_offset + br->bufferOffset + (uint64_t)al * layer;
            for (uint32_t b = 0u; b < count; ++b) {
                const VkBufferImageCopy *ir = &regions[b];
                for (uint32_t bl = 0u; bl < ir->imageSubresource.layerCount; ++bl) {
                    vk_image_subresource_impl_t *sub = vk_image_subresource(image,
                        ir->imageSubresource.mipLevel, ir->imageSubresource.baseArrayLayer + bl);
                    uint64_t io = (image->memory != 0 ? image->memory_offset : 0u) + sub->offset +
                        (uint64_t)ir->imageOffset.y * sub->row_pitch + (uint64_t)ir->imageOffset.x * vk_format_texel_bytes(image->format);
                    if (vk_rows_overlap(bo, row, (uint64_t)br->imageExtent.width * vk_format_texel_bytes(image->format), br->imageExtent.height,
                            io, sub->row_pitch, (uint64_t)ir->imageExtent.width * vk_format_texel_bytes(image->format), ir->imageExtent.height)) return 0;
                }
            }
        }
    }
    return 1;
}

/* Compare backing identity, not VkDeviceMemory/VA identity: separately
   imported mappings of the same Vex backing still alias each other. */
static int vk_image_regions_overlap(vk_image_impl_t *src,
        const VkImageSubresourceLayers *sl, const VkOffset3D *so, const VkExtent3D *se,
        vk_image_impl_t *dst, const VkImageSubresourceLayers *dl,
        const VkOffset3D *dof, const VkExtent3D *de) {
    const uint64_t sid = src->memory != 0 ? src->memory->backing_identity : src->backing_identity;
    const uint64_t did = dst->memory != 0 ? dst->memory->backing_identity : dst->backing_identity;
    if (sid == 0u || sid != did) return 0;
    for (uint32_t a = 0u; a < sl->layerCount; ++a) {
        vk_image_subresource_impl_t *ss = vk_image_subresource(src, sl->mipLevel, sl->baseArrayLayer + a);
        uint64_t offset_s = (src->memory != 0 ? src->memory_offset : 0u) + ss->offset +
            (uint64_t)so->y * ss->row_pitch + (uint64_t)so->x * (vk_format_texel_bytes(src->format)*src->samples);
        for (uint32_t b = 0u; b < dl->layerCount; ++b) {
            vk_image_subresource_impl_t *ds = vk_image_subresource(dst, dl->mipLevel, dl->baseArrayLayer + b);
            uint64_t offset_d = (dst->memory != 0 ? dst->memory_offset : 0u) + ds->offset +
                (uint64_t)dof->y * ds->row_pitch + (uint64_t)dof->x * (vk_format_texel_bytes(dst->format)*dst->samples);
            if (vk_rows_overlap(offset_s, ss->row_pitch, (uint64_t)se->width * (vk_format_texel_bytes(src->format)*src->samples), se->height,
                                offset_d, ds->row_pitch, (uint64_t)de->width * (vk_format_texel_bytes(dst->format)*dst->samples), de->height)) return 1;
        }
    }
    return 0;
}

void vkCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy *pRegions) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    vk_image_impl_t *src = (vk_image_impl_t *)(uintptr_t)srcImage;
    vk_image_impl_t *dst = (vk_image_impl_t *)(uintptr_t)dstImage;
    if (cmd == 0 || cmd->recording == 0u) return;
    if (cmd->active_framebuffer != 0 || src == 0 || dst == 0 || pRegions == 0 || regionCount == 0u ||
        src->device != cmd->device || dst->device != cmd->device ||
        src->destroy_requested != 0u || dst->destroy_requested != 0u ||
        src->format != dst->format || src->samples != dst->samples ||
        (src->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0u ||
        (dst->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0u ||
        (srcImageLayout != VK_IMAGE_LAYOUT_GENERAL &&
         srcImageLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) ||
        (dstImageLayout != VK_IMAGE_LAYOUT_GENERAL &&
         dstImageLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)) {
        vk_record_unsupported(cmd);
        return;
    }
    for (uint32_t i = 0u; i < regionCount; ++i) {
        const VkImageCopy *r = &pRegions[i];
        if (!vk_image_layers(src, &r->srcSubresource, &r->srcOffset, &r->extent) ||
            !vk_image_layers(dst, &r->dstSubresource, &r->dstOffset, &r->extent) ||
            r->srcSubresource.layerCount != r->dstSubresource.layerCount || r->srcSubresource.aspectMask != r->dstSubresource.aspectMask) {
            vk_record_unsupported(cmd); return;
        }
    }
    for (uint32_t i = 0u; i < regionCount; ++i)
        for (uint32_t j = 0u; j < regionCount; ++j) {
            const VkImageCopy *a = &pRegions[i], *b = &pRegions[j];
            if (vk_image_regions_overlap(src, &a->srcSubresource, &a->srcOffset, &a->extent,
                    dst, &b->dstSubresource, &b->dstOffset, &b->extent)) {
                vk_record_unsupported(cmd); return;
            }
        }
    for (uint32_t i = 0u; i < regionCount; ++i) {
        const VkImageCopy *r = &pRegions[i];
        vk_recorded_command_t *record;
        if (!vk_image_layers(src, &r->srcSubresource, &r->srcOffset, &r->extent) ||
            !vk_image_layers(dst, &r->dstSubresource, &r->dstOffset, &r->extent) ||
            r->srcSubresource.layerCount != r->dstSubresource.layerCount || r->srcSubresource.aspectMask != r->dstSubresource.aspectMask) {
            vk_record_unsupported(cmd); return;
        }
        record = vk_append_record(cmd, VK_RECORD_COPY_IMAGE);
        if (record == 0) return;
        if (!vk_image_retain(src)) {
            cmd->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
            return;
        }
        record->args.copy_image.src = src;
        if (!vk_image_retain(dst)) {
            cmd->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
            return;
        }
        record->args.copy_image.dst = dst;
        record->args.copy_image.src_layout = srcImageLayout;
        record->args.copy_image.dst_layout = dstImageLayout;
        record->args.copy_image.region = *r;
    }
    cmd->bound_image = dstImage;
}

void vkCmdResolveImage(VkCommandBuffer commandBuffer,VkImage srcImage,VkImageLayout srcLayout,
        VkImage dstImage,VkImageLayout dstLayout,uint32_t count,const VkImageResolve *regions) {
    vk_command_buffer_impl_t *cmd=(void*)(uintptr_t)commandBuffer;
    vk_image_impl_t *src=(void*)(uintptr_t)srcImage,*dst=(void*)(uintptr_t)dstImage;
    if(cmd==0||!cmd->recording)return;
    if(cmd->active_render_pass||src==0||dst==0||src->device!=cmd->device||dst->device!=cmd->device||
       src->destroy_requested||dst->destroy_requested||src->samples<=1u||dst->samples!=1u||
       src->format!=dst->format||!vk_format_is_color_unorm(src->format)||count==0u||regions==0||
       !(src->usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)||!(dst->usage&VK_IMAGE_USAGE_TRANSFER_DST_BIT)||
       (srcLayout!=VK_IMAGE_LAYOUT_GENERAL&&srcLayout!=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)||
       (dstLayout!=VK_IMAGE_LAYOUT_GENERAL&&dstLayout!=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)){
        vk_record_unsupported(cmd);return;
    }
    for(uint32_t i=0u;i<count;++i){
        const VkImageResolve *r=&regions[i];
        if(!vk_image_layers(src,&r->srcSubresource,&r->srcOffset,&r->extent)||
           !vk_image_layers(dst,&r->dstSubresource,&r->dstOffset,&r->extent)||
           r->srcSubresource.layerCount!=r->dstSubresource.layerCount){vk_record_unsupported(cmd);return;}
        for(uint32_t j=0u;j<count;++j)
            if(vk_image_regions_overlap(src,&r->srcSubresource,&r->srcOffset,&r->extent,
                    dst,&regions[j].dstSubresource,&regions[j].dstOffset,&regions[j].extent)){
                vk_record_unsupported(cmd);return;
            }
    }
    for(uint32_t i=0u;i<count;++i){
        vk_recorded_command_t *r=vk_append_record(cmd,VK_RECORD_RESOLVE_IMAGE);if(!r)return;
        if(!vk_image_retain(src)){cmd->record_error=VK_ERROR_OUT_OF_HOST_MEMORY;return;}
        r->args.copy_image.src=src;
        if(!vk_image_retain(dst)){cmd->record_error=VK_ERROR_OUT_OF_HOST_MEMORY;return;}
        r->args.copy_image.dst=dst;r->args.copy_image.src_layout=srcLayout;r->args.copy_image.dst_layout=dstLayout;
        const VkImageResolve *in=&regions[i];
        r->args.copy_image.region=(VkImageCopy){in->srcSubresource,in->srcOffset,in->dstSubresource,in->dstOffset,in->extent};
    }
}

static int vk_blit_region_supported(const vk_image_impl_t *src,
        const vk_image_impl_t *dst, const VkImageBlit *region) {
    VkExtent3D src_extent, dst_extent;
    vk_image_subresource_impl_t *ss, *ds;
    if (src == 0 || dst == 0 || region == 0 ||
        region->srcSubresource.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT ||
        region->dstSubresource.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT ||
        region->srcSubresource.layerCount != region->dstSubresource.layerCount ||
        (ss = vk_image_subresource(src, region->srcSubresource.mipLevel,
                                  region->srcSubresource.baseArrayLayer)) == 0 ||
        (ds = vk_image_subresource(dst, region->dstSubresource.mipLevel,
                                  region->dstSubresource.baseArrayLayer)) == 0 ||
        region->srcOffsets[0].x < 0 || region->srcOffsets[0].y < 0 ||
        region->dstOffsets[0].x < 0 || region->dstOffsets[0].y < 0 ||
        region->srcOffsets[1].x > (int32_t)ss->width ||
        region->srcOffsets[1].y > (int32_t)ss->height ||
        region->dstOffsets[1].x > (int32_t)ds->width ||
        region->dstOffsets[1].y > (int32_t)ds->height ||
        region->srcOffsets[1].x <= region->srcOffsets[0].x ||
        region->srcOffsets[1].y <= region->srcOffsets[0].y ||
        region->dstOffsets[1].x <= region->dstOffsets[0].x ||
        region->dstOffsets[1].y <= region->dstOffsets[0].y ||
        region->srcOffsets[1].z != 1 || region->dstOffsets[1].z != 1) return 0;
    src_extent = (VkExtent3D){ (uint32_t)(region->srcOffsets[1].x - region->srcOffsets[0].x),
        (uint32_t)(region->srcOffsets[1].y - region->srcOffsets[0].y), 1u };
    dst_extent = (VkExtent3D){ (uint32_t)(region->dstOffsets[1].x - region->dstOffsets[0].x),
        (uint32_t)(region->dstOffsets[1].y - region->dstOffsets[0].y), 1u };
    return vk_image_layers(src, &region->srcSubresource, &region->srcOffsets[0], &src_extent) &&
        vk_image_layers(dst, &region->dstSubresource, &region->dstOffsets[0], &dst_extent);
}

void vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage,
                    VkImageLayout srcImageLayout, VkImage dstImage,
                    VkImageLayout dstImageLayout, uint32_t regionCount,
                    const VkImageBlit *pRegions, VkFilter filter) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    vk_image_impl_t *src = (vk_image_impl_t *)(uintptr_t)srcImage;
    vk_image_impl_t *dst = (vk_image_impl_t *)(uintptr_t)dstImage;
    if (cmd == 0 || cmd->recording == 0u) return;
    if (cmd->active_framebuffer != 0 || src == 0 || dst == 0 ||
        src->device != cmd->device || dst->device != cmd->device ||
        src->destroy_requested != 0u || dst->destroy_requested != 0u ||
        src->samples != 1u || dst->samples != 1u ||
        !vk_format_is_color_unorm(src->format) ||
        !vk_format_is_color_unorm(dst->format) || regionCount == 0u || pRegions == 0 ||
        (src->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0u ||
        (dst->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0u ||
        (srcImageLayout != VK_IMAGE_LAYOUT_GENERAL &&
         srcImageLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) ||
        (dstImageLayout != VK_IMAGE_LAYOUT_GENERAL &&
         dstImageLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) ||
        (filter != VK_FILTER_NEAREST && filter != VK_FILTER_LINEAR)) {
        vk_record_unsupported(cmd);
        return;
    }
    for (uint32_t i = 0u; i < regionCount; ++i) {
        if (!vk_blit_region_supported(src, dst, &pRegions[i])) {
            vk_record_unsupported(cmd);
            return;
        }
    }
    for (uint32_t i = 0u; i < regionCount; ++i)
        for (uint32_t j = 0u; j < regionCount; ++j) {
            const VkImageBlit *a = &pRegions[i], *b = &pRegions[j];
            VkExtent3D se = { (uint32_t)(a->srcOffsets[1].x - a->srcOffsets[0].x),
                (uint32_t)(a->srcOffsets[1].y - a->srcOffsets[0].y), 1u };
            VkExtent3D de = { (uint32_t)(b->dstOffsets[1].x - b->dstOffsets[0].x),
                (uint32_t)(b->dstOffsets[1].y - b->dstOffsets[0].y), 1u };
            if (vk_image_regions_overlap(src, &a->srcSubresource, &a->srcOffsets[0], &se,
                    dst, &b->dstSubresource, &b->dstOffsets[0], &de)) {
                vk_record_unsupported(cmd); return;
            }
        }
    for (uint32_t i = 0u; i < regionCount; ++i) {
        vk_recorded_command_t *record = vk_append_record(cmd, VK_RECORD_BLIT_IMAGE);
        if (record == 0) return;
        if (!vk_image_retain(src)) {
            cmd->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
            return;
        }
        record->args.blit_image.src = src;
        if (!vk_image_retain(dst)) {
            cmd->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
            return;
        }
        record->args.blit_image.dst = dst;
        record->args.blit_image.src_layout = srcImageLayout;
        record->args.blit_image.dst_layout = dstImageLayout;
        record->args.blit_image.region = pRegions[i];
        record->args.blit_image.filter = filter;
    }
}

static void vk_record_buffer_write(VkCommandBuffer cb,VkBuffer buffer,VkDeviceSize offset,VkDeviceSize size,uint32_t value,const void *data){
    vk_command_buffer_impl_t *cmd=(void*)(uintptr_t)cb;vk_buffer_impl_t *b=(void*)(uintptr_t)buffer;
    if(!cmd||!cmd->recording)return;
    if(!b||b->destroy_requested||b->device!=cmd->device||cmd->active_render_pass||!(b->usage&VK_BUFFER_USAGE_TRANSFER_DST_BIT)||
       !b->memory||offset>=b->size||(offset&3u)){vk_record_unsupported(cmd);return;}
    if(size==VK_WHOLE_SIZE&&!data)size=(b->size-offset)&~3ull;
    if(!size||(size&3u)||size>b->size-offset||(data&&size>65536u)){vk_record_unsupported(cmd);return;}
    uint8_t *copy=0;if(data){copy=vk_heap_alloc((size_t)size);if(!copy){cmd->record_error=VK_ERROR_OUT_OF_HOST_MEMORY;return;}memcpy(copy,data,(size_t)size);}
    vk_recorded_command_t *r=vk_append_record(cmd,VK_RECORD_WRITE_BUFFER);
    if(!r||!vk_buffer_retain(b)){vk_heap_free(copy);cmd->record_error=VK_ERROR_OUT_OF_HOST_MEMORY;return;}
    r->args.write_buffer.buffer=b;r->args.write_buffer.offset=offset;r->args.write_buffer.size=size;r->args.write_buffer.value=value;r->args.write_buffer.data=copy;
}
void vkCmdFillBuffer(VkCommandBuffer cb,VkBuffer b,VkDeviceSize o,VkDeviceSize n,uint32_t v){vk_record_buffer_write(cb,b,o,n,v,0);}
void vkCmdUpdateBuffer(VkCommandBuffer cb,VkBuffer b,VkDeviceSize o,VkDeviceSize n,const void *p){
    if(!p){vk_record_unsupported((void*)(uintptr_t)cb);return;}vk_record_buffer_write(cb,b,o,n,0,p);
}
VkResult vkCreateEvent(VkDevice d,const VkEventCreateInfo *ci,const VkAllocationCallbacks *a,VkEvent *out){
    if(out)*out=VK_NULL_HANDLE;
    if(!d||!ci||!out||ci->sType!=VK_STRUCTURE_TYPE_EVENT_CREATE_INFO)return VK_ERROR_INITIALIZATION_FAILED;
    if(ci->pNext||ci->flags||a)return VK_ERROR_FEATURE_NOT_PRESENT;
    vk_event_impl_t *e=vk_heap_alloc(sizeof(*e));if(!e)return VK_ERROR_OUT_OF_HOST_MEMORY;memset(e,0,sizeof(*e));
    e->device=(void*)(uintptr_t)d;e->references=1u;e->wake_counter=vex_event_counter_create(0u,VEX_EVENT_COUNTER_SEMAPHORE);
    if(!e->wake_counter){vk_heap_free(e);return VK_ERROR_OUT_OF_HOST_MEMORY;}*out=(VkEvent)(uintptr_t)e;return VK_SUCCESS;
}
void vkDestroyEvent(VkDevice d,VkEvent event,const VkAllocationCallbacks *a){
    (void)a;vk_event_impl_t *e=(void*)(uintptr_t)event;if(e&&e->device==(void*)(uintptr_t)d&&!e->destroy_requested){e->destroy_requested=1u;vk_event_release(e);}
}
static VkResult vk_event_set(vk_event_impl_t *e){
    if(!__atomic_exchange_n(&e->signaled,1u,__ATOMIC_SEQ_CST)){
        uint32_t waiters=__atomic_load_n(&e->waiters,__ATOMIC_SEQ_CST);
        if(waiters&&vex_event_counter_signal(e->wake_counter,waiters)!=0)return VK_ERROR_DEVICE_LOST;
    }
    return VK_SUCCESS;
}
VkResult vkGetEventStatus(VkDevice d,VkEvent event){
    vk_event_impl_t *e=(void*)(uintptr_t)event;if(!e||e->device!=(void*)(uintptr_t)d||e->destroy_requested)return VK_ERROR_INITIALIZATION_FAILED;
    return __atomic_load_n(&e->signaled,__ATOMIC_SEQ_CST)?VK_EVENT_SET:VK_EVENT_RESET;
}
VkResult vkSetEvent(VkDevice d,VkEvent event){vk_event_impl_t *e=(void*)(uintptr_t)event;
    if(!e||e->device!=(void*)(uintptr_t)d||e->destroy_requested)return VK_ERROR_INITIALIZATION_FAILED;return vk_event_set(e);}
VkResult vkResetEvent(VkDevice d,VkEvent event){vk_event_impl_t *e=(void*)(uintptr_t)event;
    if(!e||e->device!=(void*)(uintptr_t)d||e->destroy_requested)return VK_ERROR_INITIALIZATION_FAILED;
    __atomic_store_n(&e->signaled,0u,__ATOMIC_RELEASE);return VK_SUCCESS;}
static VkResult vk_event_wait(vk_event_impl_t *e){
    for(;;){
        __atomic_fetch_add(&e->waiters,1u,__ATOMIC_SEQ_CST);
        if(__atomic_load_n(&e->signaled,__ATOMIC_SEQ_CST)){__atomic_fetch_sub(&e->waiters,1u,__ATOMIC_ACQ_REL);return VK_SUCCESS;}
        uint64_t count=0u;int result=vex_event_counter_read(e->wake_counter,&count,0u,0u);
        __atomic_fetch_sub(&e->waiters,1u,__ATOMIC_ACQ_REL);
        if(result!=0)return VK_ERROR_DEVICE_LOST;
    }
}
static void vk_record_event(VkCommandBuffer cb,VkEvent event,VkPipelineStageFlags stages,vk_record_kind_t kind){
    vk_command_buffer_impl_t *cmd=(void*)(uintptr_t)cb;vk_event_impl_t *e=(void*)(uintptr_t)event;
    if(!cmd||!cmd->recording)return;
    if(!e||e->device!=cmd->device||e->destroy_requested||cmd->active_render_pass||!stages||(stages&~0x0001ffffu)){
        vk_record_unsupported(cmd);return;}
    vk_recorded_command_t *r=vk_append_record(cmd,kind);
    if(!r||!vk_ref_retain_count(&e->references)){cmd->record_error=VK_ERROR_OUT_OF_HOST_MEMORY;return;}
    r->args.event.event=e;
}
void vkCmdSetEvent(VkCommandBuffer cb,VkEvent e,VkPipelineStageFlags stages){vk_record_event(cb,e,stages,VK_RECORD_EVENT_SET);}
void vkCmdResetEvent(VkCommandBuffer cb,VkEvent e,VkPipelineStageFlags stages){vk_record_event(cb,e,stages,VK_RECORD_EVENT_RESET);}

void vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier *pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    (void)srcStageMask;
    (void)dstStageMask;
    (void)dependencyFlags;
    (void)memoryBarrierCount;
    (void)pMemoryBarriers;
    (void)bufferMemoryBarrierCount;
    (void)pBufferMemoryBarriers;
    if (cmd == 0 || cmd->recording == 0u) return;
    if (cmd->active_framebuffer != 0 || (dependencyFlags & ~VK_DEPENDENCY_BY_REGION_BIT) != 0u ||
        !srcStageMask||!dstStageMask||(srcStageMask & ~0x0001ffffu)||(dstStageMask & ~0x0001ffffu)||
        (memoryBarrierCount&&!pMemoryBarriers)||(bufferMemoryBarrierCount&&!pBufferMemoryBarriers)||
        (imageMemoryBarrierCount != 0u && pImageMemoryBarriers == 0)) {
        vk_record_unsupported(cmd);
        return;
    }
    for(uint32_t i=0u;i<memoryBarrierCount;++i){const VkMemoryBarrier *b=&pMemoryBarriers[i];
        if(b->sType!=VK_STRUCTURE_TYPE_MEMORY_BARRIER||b->pNext||((b->srcAccessMask|b->dstAccessMask)&~0x0001ffffu)){vk_record_unsupported(cmd);return;}}
    for(uint32_t i=0u;i<bufferMemoryBarrierCount;++i){const VkBufferMemoryBarrier *b=&pBufferMemoryBarriers[i];vk_buffer_impl_t *buffer=(void*)(uintptr_t)b->buffer;
        if(b->sType!=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER||b->pNext||!buffer||buffer->destroy_requested||buffer->device!=cmd->device||!buffer->memory||
           ((b->srcAccessMask|b->dstAccessMask)&~0x0001ffffu)||b->offset>=buffer->size||!b->size||(b->size!=VK_WHOLE_SIZE&&b->size>buffer->size-b->offset)||
           !((b->srcQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED&&b->dstQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED)||(b->srcQueueFamilyIndex==0u&&b->dstQueueFamilyIndex==0u))){vk_record_unsupported(cmd);return;}}
    for(uint32_t i=0u;i<bufferMemoryBarrierCount;++i){vk_recorded_command_t *r=vk_append_record(cmd,VK_RECORD_MEMORY_BARRIER);vk_buffer_impl_t *buffer=(void*)(uintptr_t)pBufferMemoryBarriers[i].buffer;
        if(!r||!vk_buffer_retain(buffer)){cmd->record_error=VK_ERROR_OUT_OF_HOST_MEMORY;return;}r->args.memory_barrier.buffer=buffer;}
    if(!vk_append_record(cmd,VK_RECORD_MEMORY_BARRIER))return;
    for (uint32_t i = 0u; i < imageMemoryBarrierCount; ++i) {
        const VkImageMemoryBarrier *barrier = &pImageMemoryBarriers[i];
        vk_recorded_command_t *record;
        vk_image_impl_t *image = (vk_image_impl_t *)(uintptr_t)barrier->image;
        VkImageSubresourceRange range;
        if (barrier->sType != VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER || barrier->pNext != 0 ||
            ((barrier->srcAccessMask|barrier->dstAccessMask)&~0x0001ffffu)||
            image == 0 || image->device != cmd->device || image->destroy_requested != 0u ||
            !vk_image_range(image, &barrier->subresourceRange, &range) ||
            (image->format==VK_FORMAT_D32_SFLOAT_S8_UINT && range.aspectMask!=image->aspects) ||
            !vk_image_layout_supported(image, barrier->oldLayout, 1) ||
            !vk_image_layout_supported(image, barrier->newLayout, 0) ||
            !((barrier->srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED &&
               barrier->dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED) ||
              (barrier->srcQueueFamilyIndex == 0u && barrier->dstQueueFamilyIndex == 0u))) {
            vk_record_unsupported(cmd); return;
        }
        record = vk_append_record(cmd, VK_RECORD_IMAGE_BARRIER);
        if (record == 0) return;
        if (!vk_image_retain(image)) {
            cmd->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
            return;
        }
        record->args.image_barrier.image = image;
        record->args.image_barrier.old_layout = barrier->oldLayout;
        record->args.image_barrier.new_layout = barrier->newLayout;
        record->args.image_barrier.range = range;
    }
}

VkResult vkCreateFence(VkDevice device, const VkFenceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkFence *pFence) {
    vk_fence_impl_t *fence;
    if (device == VK_NULL_HANDLE || pFence == 0 || pCreateInfo == 0 ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_FENCE_CREATE_INFO) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *pFence = VK_NULL_HANDLE;
    if (pAllocator != 0 || pCreateInfo->pNext != 0 ||
        (pCreateInfo->flags & ~VK_FENCE_CREATE_SIGNALED_BIT) != 0u) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    fence = (vk_fence_impl_t *)vk_heap_alloc(sizeof(*fence));
    if (fence == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memset(fence, 0, sizeof(*fence));
    fence->device = (vk_device_impl_t *)(uintptr_t)device;
    fence->signaled = (pCreateInfo != 0 && (pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT) != 0u) ? VK_TRUE : VK_FALSE;
    fence->references = 1u;
    fence->kernel_queue = 0u;
    fence->serial = 0u;
    *pFence = (VkFence)(uintptr_t)fence;
    return VK_SUCCESS;
}

void vkDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks *pAllocator) {
    (void)pAllocator;
    vk_fence_impl_t *impl = (vk_fence_impl_t *)(uintptr_t)fence;
    if (impl != 0 && impl->device == (vk_device_impl_t *)(uintptr_t)device &&
        impl->destroy_requested == 0u) {
        impl->destroy_requested = 1u;
        vk_fence_release(impl);
    }
}

VkResult vkResetFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences) {
    if (fenceCount == 0u || pFences == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    /* Validate the complete array before changing any fence. */
    for (uint32_t i = 0u; i < fenceCount; ++i) {
        vk_fence_impl_t *fence = (vk_fence_impl_t *)(uintptr_t)pFences[i];
        if (fence == 0 || fence->destroy_requested != 0u ||
            fence->device != (vk_device_impl_t *)(uintptr_t)device)
            return VK_ERROR_INITIALIZATION_FAILED;
        if (fence->pending != 0u && vk_fence_refresh(fence) != VK_SUCCESS)
            return VK_ERROR_INITIALIZATION_FAILED;
        for (uint32_t j = 0u; j < i; ++j) {
            if (pFences[j] == pFences[i]) return VK_ERROR_INITIALIZATION_FAILED;
        }
    }
    for (uint32_t i = 0u; i < fenceCount; ++i)
    {
        vk_fence_impl_t *fence = (vk_fence_impl_t *)(uintptr_t)pFences[i];
        fence->signaled = VK_FALSE;
        fence->kernel_queue = 0u;
        fence->serial = 0u;
    }
    return VK_SUCCESS;
}

static VkResult vk_fence_refresh(vk_fence_impl_t *fence) {
    int state;
    if (fence == 0) return VK_ERROR_INITIALIZATION_FAILED;
    if (fence->signaled != VK_FALSE) return VK_SUCCESS;
    if (fence->kernel_queue == 0u || fence->serial == 0u)
        return VK_NOT_READY;
    state = vex_gpu_fence_query(fence->kernel_queue, fence->serial);
    if (state == 0) {
        fence->signaled = VK_TRUE;
        fence->pending = 0u;
        return VK_SUCCESS;
    }
    if (state == 1) return VK_NOT_READY;
    if (state == -2) {
        fence->pending = 0u;
        return VK_ERROR_DEVICE_LOST;
    }
    return VK_ERROR_INITIALIZATION_FAILED;
}

VkResult vkWaitForFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences, VkBool32 waitAll, uint64_t timeout) {
    if (fenceCount == 0u || pFences == 0 ||
        (waitAll != VK_FALSE && waitAll != VK_TRUE)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (uint32_t i = 0u; i < fenceCount; ++i) {
        vk_fence_impl_t *fence = (vk_fence_impl_t *)(uintptr_t)pFences[i];
        if (fence == 0 || fence->device != (vk_device_impl_t *)(uintptr_t)device)
            return VK_ERROR_INITIALIZATION_FAILED;
        for (uint32_t j = 0u; j < i; ++j) {
            if (pFences[j] == pFences[i]) return VK_ERROR_INITIALIZATION_FAILED;
        }
    }
    {
        uint32_t satisfied = 0u;
        uint32_t unsubmitted = 0u;
        uint64_t queue_handle = 0u;
        uint64_t wait_serial = waitAll != VK_FALSE ? 0u : UINT64_MAX;
        for (uint32_t i = 0u; i < fenceCount; ++i) {
            vk_fence_impl_t *fence = (vk_fence_impl_t *)(uintptr_t)pFences[i];
            const VkResult state = vk_fence_refresh(fence);
            if (state == VK_SUCCESS) {
                ++satisfied;
                continue;
            }
            if (state != VK_NOT_READY) return state;
            if (fence->kernel_queue == 0u || fence->serial == 0u) {
                ++unsubmitted;
                continue;
            }
            if (queue_handle != 0u && queue_handle != fence->kernel_queue)
                return VK_ERROR_FEATURE_NOT_PRESENT;
            queue_handle = fence->kernel_queue;
            if (waitAll != VK_FALSE) {
                if (fence->serial > wait_serial) wait_serial = fence->serial;
            } else if (fence->serial < wait_serial) {
                wait_serial = fence->serial;
            }
        }
        if ((waitAll != VK_FALSE && satisfied == fenceCount) ||
            (waitAll == VK_FALSE && satisfied != 0u)) return VK_SUCCESS;
        if (unsubmitted != 0u &&
            (waitAll != VK_FALSE || queue_handle == 0u)) {
            return timeout == 0u ? VK_TIMEOUT : VK_ERROR_FEATURE_NOT_PRESENT;
        }
        if (queue_handle == 0u || wait_serial == 0u ||
            wait_serial == UINT64_MAX) return VK_ERROR_INITIALIZATION_FAILED;
        if (timeout == 0u) return VK_TIMEOUT;
        {
            /* Scheduler ticks are 10 ms. Round a finite Vulkan nanosecond
             * timeout up to the scheduler's representable resolution. */
            uint64_t timeout_ticks = 0u;
            int waited;
            if (timeout != UINT64_MAX) {
                const uint64_t tick_ns = 10000000ull;
                timeout_ticks = timeout / tick_ns;
                if (timeout % tick_ns != 0u) ++timeout_ticks;
                if (timeout_ticks == 0u) timeout_ticks = 1u;
            }
            waited = vex_gpu_fence_wait(queue_handle, wait_serial,
                                        timeout_ticks);
            if (waited == -3) return VK_TIMEOUT;
            if (waited == -2) return VK_ERROR_DEVICE_LOST;
            if (waited != 0) return VK_ERROR_INITIALIZATION_FAILED;
        }
        satisfied = 0u;
        for (uint32_t i = 0u; i < fenceCount; ++i) {
            const VkResult state = vk_fence_refresh(
                (vk_fence_impl_t *)(uintptr_t)pFences[i]);
            if (state == VK_SUCCESS) ++satisfied;
            else if (state != VK_NOT_READY) return state;
        }
        return ((waitAll != VK_FALSE && satisfied == fenceCount) ||
                (waitAll == VK_FALSE && satisfied != 0u))
            ? VK_SUCCESS : VK_TIMEOUT;
    }
}

VkResult vkGetFenceStatus(VkDevice device, VkFence fence) {
    vk_fence_impl_t *impl = (vk_fence_impl_t *)(uintptr_t)fence;
    if (impl == 0 || impl->device != (vk_device_impl_t *)(uintptr_t)device)
        return VK_ERROR_INITIALIZATION_FAILED;
    return vk_fence_refresh(impl);
}

VkResult vkCreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSemaphore *pSemaphore) {
    vk_semaphore_impl_t *semaphore;
    if (device == VK_NULL_HANDLE || pSemaphore == 0 || pCreateInfo == 0 ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO)
        return VK_ERROR_INITIALIZATION_FAILED;
    *pSemaphore = VK_NULL_HANDLE;
    if (pAllocator != 0 || pCreateInfo->pNext != 0 || pCreateInfo->flags != 0u)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    semaphore = (vk_semaphore_impl_t *)vk_heap_alloc(sizeof(*semaphore));
    if (semaphore == 0) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(semaphore, 0, sizeof(*semaphore));
    semaphore->device = (vk_device_impl_t *)(uintptr_t)device;
    semaphore->event_capability = vex_event_counter_create(
        0u, VEX_EVENT_COUNTER_SEMAPHORE);
    if (semaphore->event_capability == 0u ||
        (int64_t)semaphore->event_capability < 0) {
        vk_heap_free(semaphore);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    semaphore->signaled = VK_FALSE;
    semaphore->references = 1u;
    *pSemaphore = (VkSemaphore)(uintptr_t)semaphore;
    return VK_SUCCESS;
}

void vkDestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks *pAllocator) {
    (void)pAllocator;
    vk_semaphore_impl_t *impl = (vk_semaphore_impl_t *)(uintptr_t)semaphore;
    if (impl != 0 && impl->device == (vk_device_impl_t *)(uintptr_t)device &&
        impl->destroy_requested == 0u) {
        impl->destroy_requested = 1u;
        vk_semaphore_release(impl);
    }
}

































static int32_t vk_point_sample_pixel(float coordinate) {
    /* Pixel centers are at n+0.5. Resolve an exact midpoint toward the lower
       index; viewport validation keeps coordinate in [0,4096], so the
       conversion remains in int32 range. */
    const float shifted = coordinate - 1.0f;
    int32_t pixel = (int32_t)shifted;
    if ((float)pixel < shifted) ++pixel; /* ceil(coordinate - 1) */
    return pixel;
}











typedef struct vk_submit_image_layout {
    vk_image_impl_t *image;
    uint32_t subresource;
    VkImageLayout layout;
} vk_submit_image_layout_t;

typedef struct vk_submit_layout_table {
    vk_submit_image_layout_t *entries;
    size_t count;
    size_t capacity;
    int out_of_memory;
} vk_submit_layout_table_t;

static VkImageLayout *vk_submit_layout_for(vk_submit_layout_table_t *table,
                                           vk_image_impl_t *image, uint32_t index) {
    if (table == 0 || image == 0 || image->subresources == 0 ||
        index >= image->mip_levels * image->array_layers) return 0;
    for (size_t i = 0u; i < table->count; ++i)
        if (table->entries[i].image == image && table->entries[i].subresource == index)
            return &table->entries[i].layout;
    if (table->count == table->capacity) {
        size_t capacity = table->capacity == 0u ? 16u : table->capacity * 2u;
        vk_submit_image_layout_t *entries;
        if (capacity <= table->capacity || capacity > SIZE_MAX / sizeof(*entries) ||
            (entries = (vk_submit_image_layout_t *)vk_heap_alloc(capacity * sizeof(*entries))) == 0) {
            table->out_of_memory = 1; return 0;
        }
        if (table->entries != 0)
            memcpy(entries, table->entries, table->count * sizeof(*entries));
        vk_heap_free(table->entries);
        table->entries = entries;
        table->capacity = capacity;
    }
    table->entries[table->count].image = image;
    table->entries[table->count].subresource = index;
    table->entries[table->count].layout = image->subresources[index].scheduled_layout;
    return &table->entries[table->count++].layout;
}

static int vk_submit_range(vk_submit_layout_table_t *table, vk_image_impl_t *image,
                            const VkImageSubresourceRange *range,
                            VkImageLayout expected, VkImageLayout next,
                            int transition) {
    for (uint32_t layer = range->baseArrayLayer;
         layer < range->baseArrayLayer + range->layerCount; ++layer) {
        for (uint32_t mip = range->baseMipLevel;
             mip < range->baseMipLevel + range->levelCount; ++mip) {
            VkImageLayout *layout = vk_submit_layout_for(table, image,
                layer * image->mip_levels + mip);
            if (layout == 0 ||
                (!(transition && expected == VK_IMAGE_LAYOUT_UNDEFINED) && *layout != expected)) return 0;
            if (transition) *layout = next;
        }
    }
    return 1;
}

static int vk_submit_layers(vk_submit_layout_table_t *table, vk_image_impl_t *image,
                             const VkImageSubresourceLayers *layers,
                             VkImageLayout expected) {
    VkImageSubresourceRange range = { layers->aspectMask, layers->mipLevel, 1u,
        layers->baseArrayLayer, layers->layerCount };
    return vk_submit_range(table, image, &range, expected, expected, 0);
}

static int vk_validate_shader_descriptors(const vk_spv_profile_t *profile,
        VkShaderStageFlagBits stage, vk_descriptor_set_impl_t *const bound_sets[8],
        const vk_recorded_command_t *record, vk_submit_layout_table_t *table) {
    uint32_t words[1];
    if(profile==0||profile->stage!=stage||!vk_spv_resources_supported(profile,bound_sets))return 0;
    for (uint32_t id = 1u; id < profile->bound; ++id) {
        for (uint32_t element = 0u; element < profile->descriptor_elements[id]; ++element) {
            if (profile->descriptor_type_by_id[id] == (uint8_t)VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
                const uint32_t size = profile->descriptor_required_size[id];
                if (size == 0u || size > 65536u ||
                    !vk_spv_read_uniform_data(bound_sets, profile->descriptor_set[id],
                        profile->descriptor_binding[id], element, size - 4u, 4u, words)) return 0;
            } else if (profile->descriptor_type_by_id[id] == (uint8_t)VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                vk_image_view_impl_t *view; vk_sampler_impl_t *sampler; VkImageLayout layout;
                if (!vk_spv_get_combined_image(bound_sets, profile->descriptor_set[id],
                        profile->descriptor_binding[id], element, &view, &sampler, &layout) ||
                    (profile->implicit_samples && sampler->max_lod != 0.0f) ||
                    !vk_submit_range(table, view->image, &view->range, layout, layout, 0)) return 0;
            }else if(profile->descriptor_type_by_id[id]==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE){
                vk_descriptor_value_impl_t *d=vk_spv_descriptor_value(bound_sets,profile->descriptor_set[id],profile->descriptor_binding[id],element);
                vk_image_view_impl_t *v=d?d->resource.image.view:0;
                if(!v||!vk_submit_range(table,v->image,&v->range,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,0))return 0;
            }
        }
    }
    return record != 0 && vk_spv_profile_push_initialized(profile, stage,
        record->args.draw.push_constant_initialized);
}

static VkResult vk_preflight_submit_layouts(uint32_t submit_count,
        const VkSubmitInfo *submits, vk_submit_image_layout_t **out_layouts,
        size_t *out_layout_count) {
    vk_submit_layout_table_t table = {0};
    if (out_layouts == 0 || out_layout_count == 0) return VK_ERROR_INITIALIZATION_FAILED;
    *out_layouts = 0; *out_layout_count = 0u;
    for (uint32_t i = 0u; i < submit_count; ++i) {
        for (uint32_t j = 0u; j < submits[i].commandBufferCount; ++j) {
            vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)submits[i].pCommandBuffers[j];
            vk_record_iterator_t iterator={cmd->first_record,0};
            for (vk_recorded_command_t *r; (r=vk_record_next(&iterator))!=0;) {
                switch (r->kind) {
                case VK_RECORD_EXECUTE_SECONDARY: {
                    vk_command_buffer_impl_t *c=r->args.secondary.command;
                    if(!c||!c->executable||c->generation!=r->args.secondary.generation||c->level!=VK_COMMAND_BUFFER_LEVEL_SECONDARY)goto invalid;
                    break;
                }
                case VK_RECORD_CLEAR_COLOR:
                    if (!vk_submit_range(&table, r->args.clear.image, &r->args.clear.range,
                            r->args.clear.layout, r->args.clear.layout, 0)) goto invalid;
                    break;
                case VK_RECORD_CLEAR_DEPTH_STENCIL:
                    if (!vk_submit_range(&table, r->args.clear_depth_stencil.image,
                            &r->args.clear_depth_stencil.range, r->args.clear_depth_stencil.layout,
                            r->args.clear_depth_stencil.layout, 0)) goto invalid;
                    break;
                case VK_RECORD_IMAGE_BARRIER:
                    if (!vk_submit_range(&table, r->args.image_barrier.image, &r->args.image_barrier.range,
                            r->args.image_barrier.old_layout, r->args.image_barrier.new_layout, 1)) goto invalid;
                    break;
                case VK_RECORD_BEGIN_RENDER_PASS: {
                    vk_framebuffer_impl_t *fb = r->args.begin_render_pass.framebuffer;
                    vk_render_pass_impl_t *pass = fb->render_pass;
                    VkImageSubresourceRange range = fb->attachment0->range;
                    range.layerCount = 1u;
                    if (!vk_submit_range(&table, fb->attachment0->image, &range,
                            pass->initial_layout, pass->subpass_layout, 1)) goto invalid;
                    if (pass->has_depth) {
                        range = fb->depth_attachment->range; range.layerCount = 1u;
                        if (!vk_submit_range(&table, fb->depth_attachment->image, &range,
                                pass->depth_initial_layout, pass->depth_subpass_layout, 1)) goto invalid;
                    }
                    if(fb->resolve_attachment){
                        VkImageLayout *layout=vk_submit_layout_for(&table,fb->resolve_attachment->image,vk_view_subresource(fb->resolve_attachment));
                        if(!layout||(pass->resolve_initial_layout!=VK_IMAGE_LAYOUT_UNDEFINED&&*layout!=pass->resolve_initial_layout))goto invalid;
                        *layout=pass->resolve_subpass_layout;
                    }
                    break;
                }
                case VK_RECORD_END_RENDER_PASS: {
                    VkImageLayout *layout = vk_submit_layout_for(&table,
                        r->args.end_render_pass.image, r->args.end_render_pass.subresource);
                    if (layout == 0 || *layout != r->args.end_render_pass.expected_layout) goto invalid;
                    *layout = r->args.end_render_pass.final_layout;
                    if (r->args.end_render_pass.depth_image != 0) {
                        layout = vk_submit_layout_for(&table, r->args.end_render_pass.depth_image,
                            r->args.end_render_pass.depth_subresource);
                        if (layout == 0 || *layout != r->args.end_render_pass.depth_expected_layout) goto invalid;
                        *layout = r->args.end_render_pass.depth_final_layout;
                    }
                    if(r->args.end_render_pass.resolve_framebuffer){
                        vk_framebuffer_impl_t *fb=r->args.end_render_pass.resolve_framebuffer;
                        VkImageLayout *layout=vk_submit_layout_for(&table,fb->resolve_attachment->image,vk_view_subresource(fb->resolve_attachment));
                        if(!layout||*layout!=fb->render_pass->resolve_subpass_layout)goto invalid;
                        *layout=fb->render_pass->resolve_final_layout;
                    }
                    break;
                }
                case VK_RECORD_COPY_BUFFER_TO_IMAGE:
                    if (!vk_submit_layers(&table, r->args.copy_buffer_to_image.dst,
                            &r->args.copy_buffer_to_image.region.imageSubresource,
                            r->args.copy_buffer_to_image.layout)) goto invalid;
                    break;
                case VK_RECORD_COPY_IMAGE_TO_BUFFER:
                    if (!vk_submit_layers(&table, r->args.copy_image_to_buffer.src,
                            &r->args.copy_image_to_buffer.region.imageSubresource,
                            r->args.copy_image_to_buffer.layout)) goto invalid;
                    break;
                case VK_RECORD_RESOLVE_IMAGE:
                case VK_RECORD_COPY_IMAGE:
                    if (!vk_submit_layers(&table, r->args.copy_image.src, &r->args.copy_image.region.srcSubresource,
                            r->args.copy_image.src_layout) ||
                        !vk_submit_layers(&table, r->args.copy_image.dst, &r->args.copy_image.region.dstSubresource,
                            r->args.copy_image.dst_layout)) goto invalid;
                    break;
                case VK_RECORD_BLIT_IMAGE:
                    if (!vk_submit_layers(&table, r->args.blit_image.src, &r->args.blit_image.region.srcSubresource,
                            r->args.blit_image.src_layout) ||
                        !vk_submit_layers(&table, r->args.blit_image.dst, &r->args.blit_image.region.dstSubresource,
                            r->args.blit_image.dst_layout)) goto invalid;
                    break;
                case VK_RECORD_DISPATCH: {
                    if(!vk_compute_descriptors(r->args.dispatch.pipeline,r->args.dispatch.sets))goto invalid;
                    const vk_spv_profile_t *p=r->args.dispatch.pipeline->compute_profile;
                    for(uint32_t id=1u;id<p->bound;++id)if(p->descriptor_type_by_id[id]==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE){
                        for(uint32_t e=0u;e<p->descriptor_elements[id];++e){
                            vk_descriptor_value_impl_t *d=vk_spv_descriptor_value(r->args.dispatch.sets,p->descriptor_set[id],p->descriptor_binding[id],e);
                            vk_image_view_impl_t *v=d?d->resource.image.view:0;
                            if(!v||!vk_submit_range(&table,v->image,&v->range,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,0))goto invalid;
                        }
                    }break;
                }
                case VK_RECORD_DRAW: {
                    vk_pipeline_impl_t *pipeline = r->args.draw.pipeline;
                    if (pipeline == 0 || !vk_validate_draw_vertex_buffer(r) ||
                        !vk_validate_shader_descriptors(pipeline->vertex_profile, VK_SHADER_STAGE_VERTEX_BIT,
                            r->args.draw.descriptor_sets, r, &table) ||
                        !vk_validate_shader_descriptors(pipeline->fragment_profile, VK_SHADER_STAGE_FRAGMENT_BIT,
                            r->args.draw.descriptor_sets, r, &table)) goto invalid;
                    break;
                }
                default: break;
                }
            }
        }
    }
    *out_layouts = table.entries; *out_layout_count = table.count;
    return VK_SUCCESS;
invalid:
    vk_heap_free(table.entries);
    return table.out_of_memory ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
}

static uint32_t vk_blit_linear_pixel(const uint32_t *source, uint32_t stride,
                                     uint32_t format,
                                     const VkImageBlit *region,
                                     uint32_t x, uint32_t y) {
    const uint32_t src_x0 = (uint32_t)region->srcOffsets[0].x;
    const uint32_t src_y0 = (uint32_t)region->srcOffsets[0].y;
    const uint32_t src_x1 = (uint32_t)region->srcOffsets[1].x;
    const uint32_t src_y1 = (uint32_t)region->srcOffsets[1].y;
    const uint32_t dst_w = (uint32_t)(region->dstOffsets[1].x - region->dstOffsets[0].x);
    const uint32_t dst_h = (uint32_t)(region->dstOffsets[1].y - region->dstOffsets[0].y);
    const uint32_t src_w = src_x1 - src_x0;
    const uint32_t src_h = src_y1 - src_y0;
    const uint32_t dx = x - (uint32_t)region->dstOffsets[0].x;
    const uint32_t dy = y - (uint32_t)region->dstOffsets[0].y;
    float fx = (float)src_x0 + ((float)dx + 0.5f) * (float)src_w /
               (float)dst_w - 0.5f;
    float fy = (float)src_y0 + ((float)dy + 0.5f) * (float)src_h /
               (float)dst_h - 0.5f;
    uint32_t x0, y0, x1, y1;
    float tx, ty;
    uint32_t p00, p10, p01, p11;
    uint32_t output = 0u;
    if (fx < (float)src_x0) fx = (float)src_x0;
    if (fy < (float)src_y0) fy = (float)src_y0;
    if (fx > (float)(src_x1 - 1u)) fx = (float)(src_x1 - 1u);
    if (fy > (float)(src_y1 - 1u)) fy = (float)(src_y1 - 1u);
    x0 = (uint32_t)fx; y0 = (uint32_t)fy;
    x1 = x0 + 1u < src_x1 ? x0 + 1u : x0;
    y1 = y0 + 1u < src_y1 ? y0 + 1u : y0;
    tx = fx - (float)x0; ty = fy - (float)y0;
    p00 = source[(uint64_t)y0 * stride + x0];
    p10 = source[(uint64_t)y0 * stride + x1];
    p01 = source[(uint64_t)y1 * stride + x0];
    p11 = source[(uint64_t)y1 * stride + x1];
    p00 = vk_pixel_to_canonical(format, p00);
    p10 = vk_pixel_to_canonical(format, p10);
    p01 = vk_pixel_to_canonical(format, p01);
    p11 = vk_pixel_to_canonical(format, p11);
    for (uint32_t shift = 0u; shift < 32u; shift += 8u) {
        const float c00 = (float)((p00 >> shift) & 0xffu);
        const float c10 = (float)((p10 >> shift) & 0xffu);
        const float c01 = (float)((p01 >> shift) & 0xffu);
        const float c11 = (float)((p11 >> shift) & 0xffu);
        const float top = c00 + (c10 - c00) * tx;
        const float bottom = c01 + (c11 - c01) * tx;
        const uint32_t channel = (uint32_t)(top + (bottom - top) * ty + 0.5f);
        output |= (channel & 0xffu) << shift;
    }
    return output;
}

static VkResult vk_execute_blit_plane(const vk_recorded_command_t *record,
        vk_image_plane_t *src, vk_image_plane_t *dst) {
    const VkImageBlit *region = &record->args.blit_image.region;
    uint32_t *source_pixels;
    uint32_t *destination_pixels;
    const uint32_t src_w = (uint32_t)(region->srcOffsets[1].x - region->srcOffsets[0].x);
    const uint32_t src_h = (uint32_t)(region->srcOffsets[1].y - region->srcOffsets[0].y);
    const uint32_t dst_w = (uint32_t)(region->dstOffsets[1].x - region->dstOffsets[0].x);
    const uint32_t dst_h = (uint32_t)(region->dstOffsets[1].y - region->dstOffsets[0].y);
    if (src == 0 || dst == 0 || src->layout != record->args.blit_image.src_layout ||
        dst->layout != record->args.blit_image.dst_layout ||
        src_w == 0u || src_h == 0u ||
        dst_w == 0u || dst_h == 0u ||
        (source_pixels = src->pixels) == 0 ||
        (destination_pixels = dst->pixels) == 0) {
        return VK_ERROR_DEVICE_LOST;
    }
    for (uint32_t y = (uint32_t)region->dstOffsets[0].y;
         y < (uint32_t)region->dstOffsets[1].y; ++y) {
        for (uint32_t x = (uint32_t)region->dstOffsets[0].x;
             x < (uint32_t)region->dstOffsets[1].x; ++x) {
            uint32_t color;
            if (record->args.blit_image.filter == VK_FILTER_NEAREST) {
                const uint32_t dx = x - (uint32_t)region->dstOffsets[0].x;
                const uint32_t dy = y - (uint32_t)region->dstOffsets[0].y;
                uint32_t sx = (uint32_t)region->srcOffsets[0].x +
                    (uint32_t)(((uint64_t)(2u * dx + 1u) * src_w) /
                               ((uint64_t)2u * dst_w));
                uint32_t sy = (uint32_t)region->srcOffsets[0].y +
                    (uint32_t)(((uint64_t)(2u * dy + 1u) * src_h) /
                               ((uint64_t)2u * dst_h));
                if (sx >= (uint32_t)region->srcOffsets[1].x)
                    sx = (uint32_t)region->srcOffsets[1].x - 1u;
                if (sy >= (uint32_t)region->srcOffsets[1].y)
                    sy = (uint32_t)region->srcOffsets[1].y - 1u;
                color = vk_pixel_to_canonical(src->format,
                    source_pixels[(uint64_t)sy * src->width + sx]);
            } else if (record->args.blit_image.filter == VK_FILTER_LINEAR) {
                color = vk_blit_linear_pixel(source_pixels, src->width,
                                            src->format, region, x, y);
            } else {
                return VK_ERROR_DEVICE_LOST;
            }
            destination_pixels[(uint64_t)y * dst->width + x] =
                vk_pixel_from_canonical(dst->format, color);
        }
    }
    return VK_SUCCESS;
}

static VkResult vk_execute_blit(const vk_recorded_command_t *record) {
    vk_image_impl_t *src = record->args.blit_image.src;
    vk_image_impl_t *dst = record->args.blit_image.dst;
    const VkImageBlit *r = &record->args.blit_image.region;
    if (!vk_blit_region_supported(src, dst, r)) return VK_ERROR_DEVICE_LOST;
    for (uint32_t layer = 0u; layer < r->srcSubresource.layerCount; ++layer) {
        vk_image_plane_t source, destination;
        VkResult result;
        if (!vk_image_plane(src, r->srcSubresource.mipLevel,
                r->srcSubresource.baseArrayLayer + layer, &source) ||
            !vk_image_plane(dst, r->dstSubresource.mipLevel,
                r->dstSubresource.baseArrayLayer + layer, &destination)) return VK_ERROR_DEVICE_LOST;
        result = vk_execute_blit_plane(record, &source, &destination);
        if (result != VK_SUCCESS) return result;
    }
    return VK_SUCCESS;
}

static int vk_execute_image_layers(vk_image_impl_t *image,
        const VkImageSubresourceLayers *layers, VkImageLayout layout) {
    for (uint32_t layer = 0u; layer < layers->layerCount; ++layer) {
        vk_image_subresource_impl_t *sub = vk_image_subresource(image, layers->mipLevel,
            layers->baseArrayLayer + layer);
        if (sub == 0 || sub->layout != layout ||
            vk_image_subresource_pixels(image, layers->mipLevel,
                layers->baseArrayLayer + layer) == 0) return 0;
    }
    return 1;
}

static VkResult vk_execute_buffer_image(const vk_recorded_command_t *record, int to_image) {
    vk_buffer_impl_t *buffer = to_image ? record->args.copy_buffer_to_image.src
        : record->args.copy_image_to_buffer.dst;
    vk_image_impl_t *image = to_image ? record->args.copy_buffer_to_image.dst
        : record->args.copy_image_to_buffer.src;
    const VkBufferImageCopy *r = to_image ? &record->args.copy_buffer_to_image.region
        : &record->args.copy_image_to_buffer.region;
    VkImageLayout layout = to_image ? record->args.copy_buffer_to_image.layout
        : record->args.copy_image_to_buffer.layout;
    uint64_t row_pitch, layer_pitch;
    uint8_t *bytes;
    if (!vk_buffer_image_region(buffer, image, r, &row_pitch, &layer_pitch) ||
        buffer->memory == 0 || buffer->memory->bytes == 0 ||
        buffer->memory_offset > buffer->memory->size ||
        buffer->size > buffer->memory->size - buffer->memory_offset ||
        !vk_execute_image_layers(image, &r->imageSubresource, layout)) return VK_ERROR_DEVICE_LOST;
    bytes = buffer->memory->bytes + buffer->memory_offset + r->bufferOffset;
    for (uint32_t layer = 0u; layer < r->imageSubresource.layerCount; ++layer) {
        vk_image_subresource_impl_t *sub = vk_image_subresource(image,
            r->imageSubresource.mipLevel, r->imageSubresource.baseArrayLayer + layer);
        uint8_t *pixels = (uint8_t *)vk_image_subresource_pixels(image,
            r->imageSubresource.mipLevel, r->imageSubresource.baseArrayLayer + layer);
        for (uint32_t y = 0u; y < r->imageExtent.height; ++y) {
            uint8_t *buffer_row = bytes + layer * layer_pitch + y * row_pitch;
            uint8_t *image_row = pixels + ((uint64_t)r->imageOffset.y + y) * sub->row_pitch +
                (uint64_t)r->imageOffset.x * vk_format_texel_bytes(image->format);
            const uint32_t aspect=vk_aspect_bytes(image->format,r->imageSubresource.aspectMask);
            image_row+=vk_aspect_offset(image->format,r->imageSubresource.aspectMask);
            for(uint32_t x=0u;x<r->imageExtent.width;++x){
                if(to_image)memcpy(image_row+x*vk_format_texel_bytes(image->format),buffer_row+x*aspect,aspect);
                else memcpy(buffer_row+x*aspect,image_row+x*vk_format_texel_bytes(image->format),aspect);
            }
        }
    }
    return VK_SUCCESS;
}

static VkResult vk_execute_image_copy(const vk_recorded_command_t *record) {
    const VkImageCopy *r = &record->args.copy_image.region;
    vk_image_impl_t *src = record->args.copy_image.src, *dst = record->args.copy_image.dst;
    if (!vk_image_layers(src, &r->srcSubresource, &r->srcOffset, &r->extent) ||
        !vk_image_layers(dst, &r->dstSubresource, &r->dstOffset, &r->extent) ||
        !vk_execute_image_layers(src, &r->srcSubresource, record->args.copy_image.src_layout) ||
        !vk_execute_image_layers(dst, &r->dstSubresource, record->args.copy_image.dst_layout))
        return VK_ERROR_DEVICE_LOST;
    for (uint32_t layer = 0u; layer < r->srcSubresource.layerCount; ++layer) {
        vk_image_subresource_impl_t *source = vk_image_subresource(src,
            r->srcSubresource.mipLevel, r->srcSubresource.baseArrayLayer + layer);
        vk_image_subresource_impl_t *destination = vk_image_subresource(dst,
            r->dstSubresource.mipLevel, r->dstSubresource.baseArrayLayer + layer);
        const uint8_t *source_pixels = (const uint8_t *)vk_image_subresource_pixels(src,
            r->srcSubresource.mipLevel, r->srcSubresource.baseArrayLayer + layer);
        uint8_t *destination_pixels = (uint8_t *)vk_image_subresource_pixels(dst,
            r->dstSubresource.mipLevel, r->dstSubresource.baseArrayLayer + layer);
        for (uint32_t y = 0u; y < r->extent.height; ++y) {
            const uint8_t *from = source_pixels + ((uint64_t)r->srcOffset.y + y) * source->row_pitch +
                (uint64_t)r->srcOffset.x * (vk_format_texel_bytes(src->format)*src->samples);
            uint8_t *to = destination_pixels + ((uint64_t)r->dstOffset.y + y) * destination->row_pitch +
                (uint64_t)r->dstOffset.x * (vk_format_texel_bytes(dst->format)*dst->samples);
            const uint32_t aspect=vk_aspect_bytes(src->format,r->srcSubresource.aspectMask);
            from+=vk_aspect_offset(src->format,r->srcSubresource.aspectMask);
            to+=vk_aspect_offset(dst->format,r->dstSubresource.aspectMask);
            for(uint32_t x=0u;x<r->extent.width*src->samples;++x)
                memcpy(to+x*vk_format_texel_bytes(dst->format),from+x*vk_format_texel_bytes(src->format),aspect);
        }
    }
    return VK_SUCCESS;
}

/* UNORM color resolves average the actual stored samples, never a fabricated
   callback or a copy of sample zero. Source/destination refer to retained Vex
   backings, and execution remains entirely on the queue worker. */
static VkResult vk_execute_resolve(const vk_recorded_command_t *record) {
    const VkImageCopy *r=&record->args.copy_image.region;
    vk_image_impl_t *src=record->args.copy_image.src,*dst=record->args.copy_image.dst;
    if(src->samples<=1u||dst->samples!=1u||src->format!=dst->format||
       !vk_format_is_color_unorm(src->format)||
       !vk_execute_image_layers(src,&r->srcSubresource,record->args.copy_image.src_layout)||
       !vk_execute_image_layers(dst,&r->dstSubresource,record->args.copy_image.dst_layout))return VK_ERROR_DEVICE_LOST;
    for(uint32_t layer=0u;layer<r->srcSubresource.layerCount;++layer){
        vk_image_plane_t a,b;
        if(!vk_image_plane(src,r->srcSubresource.mipLevel,r->srcSubresource.baseArrayLayer+layer,&a)||
           !vk_image_plane(dst,r->dstSubresource.mipLevel,r->dstSubresource.baseArrayLayer+layer,&b))return VK_ERROR_DEVICE_LOST;
        const uint32_t *from=a.pixels;uint32_t *to=b.pixels;
        for(uint32_t y=0u;y<r->extent.height;++y)for(uint32_t x=0u;x<r->extent.width;++x){
            uint32_t sum[4]={0};
            uint64_t index=(((uint64_t)r->srcOffset.y+y)*a.width+r->srcOffset.x+x)*a.samples;
            for(uint32_t sample=0u;sample<a.samples;++sample){
                uint32_t color=vk_pixel_to_canonical(a.format,from[index+sample]);
                for(uint32_t c=0u;c<4u;++c)sum[c]+=(color>>(8u*c))&255u;
            }
            uint32_t color=0u;
            for(uint32_t c=0u;c<4u;++c)color|=((sum[c]+a.samples/2u)/a.samples)<<(8u*c);
            to[((uint64_t)r->dstOffset.y+y)*b.width+r->dstOffset.x+x]=vk_pixel_from_canonical(b.format,color);
        }
    }
    return VK_SUCCESS;
}

void vkCmdWaitEvents(VkCommandBuffer cb,uint32_t count,const VkEvent *events,VkPipelineStageFlags src,VkPipelineStageFlags dst,
        uint32_t memoryCount,const VkMemoryBarrier *memory,uint32_t bufferCount,const VkBufferMemoryBarrier *buffers,uint32_t imageCount,const VkImageMemoryBarrier *images){
    vk_command_buffer_impl_t *cmd=(void*)(uintptr_t)cb;
    if(!cmd||!cmd->recording)return;
    if(!count||!events){vk_record_unsupported(cmd);return;}
    for(uint32_t i=0u;i<count;++i)vk_record_event(cb,events[i],src,VK_RECORD_EVENT_WAIT);
    vkCmdPipelineBarrier(cb,src,dst,0u,memoryCount,memory,bufferCount,buffers,imageCount,images);
}

static VkResult vk_query_results(vk_query_pool_impl_t *p,uint32_t first,uint32_t count,size_t bytes,void *out,VkDeviceSize stride,VkQueryResultFlags flags);

static VkResult vk_execute_records(vk_command_buffer_impl_t *cmd) {
    for (vk_recorded_command_t *record = cmd->first_record; record != 0; record = record->next) {
        if(record->kind==VK_RECORD_EXECUTE_SECONDARY){
            vk_command_buffer_impl_t *c=record->args.secondary.command;
            if(!c||c->generation!=record->args.secondary.generation)return VK_ERROR_DEVICE_LOST;
            VkResult result=vk_execute_records(c);if(result!=VK_SUCCESS)return result;
        }else if(record->kind==VK_RECORD_DISPATCH){VkResult result=vk_execute_dispatch(record);if(result!=VK_SUCCESS)return result;}
        else if(record->kind==VK_RECORD_CLEAR_ATTACHMENTS){
            vk_framebuffer_impl_t *fb=record->args.clear_attachments.framebuffer;
            const VkRect2D *r=&record->args.clear_attachments.rect;VkImageAspectFlags aspects=record->args.clear_attachments.aspects;
            vk_image_plane_t plane;vk_image_view_impl_t *view=aspects==VK_IMAGE_ASPECT_COLOR_BIT?fb->attachment0:fb->depth_attachment;
            if(!vk_view_plane(view,&plane)||plane.layout!=(aspects==VK_IMAGE_ASPECT_COLOR_BIT?fb->render_pass->subpass_layout:fb->render_pass->depth_subpass_layout))return VK_ERROR_DEVICE_LOST;
            if(aspects==VK_IMAGE_ASPECT_COLOR_BIT)fill_image_rect(&plane,r->offset.x,r->offset.y,r->extent.width,r->extent.height,pack_rgba(&record->args.clear_attachments.value.color));
            else {
                if(aspects&VK_IMAGE_ASPECT_DEPTH_BIT)fill_depth_rect(&plane,r->offset.x,r->offset.y,r->extent.width,r->extent.height,record->args.clear_attachments.value.depthStencil.depth);
                if(aspects&VK_IMAGE_ASPECT_STENCIL_BIT)fill_stencil_rect(&plane,r->offset.x,r->offset.y,r->extent.width,r->extent.height,record->args.clear_attachments.value.depthStencil.stencil);
            }
        }else if (record->kind == VK_RECORD_CLEAR_COLOR || record->kind == VK_RECORD_CLEAR_DEPTH_STENCIL) {
            int depth = record->kind == VK_RECORD_CLEAR_DEPTH_STENCIL;
            vk_image_impl_t *image = depth ? record->args.clear_depth_stencil.image : record->args.clear.image;
            const VkImageSubresourceRange *range = depth ? &record->args.clear_depth_stencil.range : &record->args.clear.range;
            VkImageLayout layout = depth ? record->args.clear_depth_stencil.layout : record->args.clear.layout;
            for (uint32_t layer = range->baseArrayLayer; layer < range->baseArrayLayer + range->layerCount; ++layer) {
                for (uint32_t mip = range->baseMipLevel; mip < range->baseMipLevel + range->levelCount; ++mip) {
                    vk_image_plane_t plane;
                    if (!vk_image_plane(image, mip, layer, &plane) || plane.layout != layout) return VK_ERROR_DEVICE_LOST;
                    if(depth) {
                        if(range->aspectMask&VK_IMAGE_ASPECT_STENCIL_BIT) fill_stencil_rect(&plane,0,0,(int32_t)plane.width,(int32_t)plane.height,record->args.clear_depth_stencil.stencil);
                        if(range->aspectMask&VK_IMAGE_ASPECT_DEPTH_BIT) fill_depth_rect(&plane,0,0,(int32_t)plane.width,(int32_t)plane.height,record->args.clear_depth_stencil.depth);
                    } else fill_image_rect(&plane, 0, 0, (int32_t)plane.width, (int32_t)plane.height,
                        record->args.clear.color);
                }
            }
        } else if(record->kind==VK_RECORD_QUERY_RESET||record->kind==VK_RECORD_QUERY_BEGIN||record->kind==VK_RECORD_QUERY_END||record->kind==VK_RECORD_QUERY_COPY){
            vk_query_pool_impl_t *p=record->args.query.pool;uint32_t first=record->args.query.first,count=record->args.query.count;
            if(!p)return VK_ERROR_DEVICE_LOST;
            if(record->kind==VK_RECORD_QUERY_RESET){
                for(uint32_t i=0u;i<count;++i){vk_query_slot_t *slot=&p->slots[first+i];
                    if(__atomic_load_n(&slot->state,__ATOMIC_ACQUIRE)==2u)return VK_ERROR_DEVICE_LOST;
                    __atomic_store_n(&slot->samples,0u,__ATOMIC_RELAXED);__atomic_store_n(&slot->state,1u,__ATOMIC_RELEASE);}
            }else if(record->kind==VK_RECORD_QUERY_BEGIN){
                if(__atomic_load_n(&p->slots[first].state,__ATOMIC_ACQUIRE)!=1u)return VK_ERROR_DEVICE_LOST;
                __atomic_store_n(&p->slots[first].samples,0u,__ATOMIC_RELAXED);__atomic_store_n(&p->slots[first].state,2u,__ATOMIC_RELEASE);
            }else if(record->kind==VK_RECORD_QUERY_END){
                if(__atomic_load_n(&p->slots[first].state,__ATOMIC_ACQUIRE)!=2u)return VK_ERROR_DEVICE_LOST;
                __atomic_store_n(&p->slots[first].state,3u,__ATOMIC_SEQ_CST);
                uint32_t waiters=__atomic_load_n(&p->waiters,__ATOMIC_SEQ_CST);
                if(waiters&&vex_event_counter_signal(p->wake_counter,waiters)!=0)return VK_ERROR_DEVICE_LOST;
            }else{
                vk_buffer_impl_t *b=record->args.query.buffer;
                VkResult result=vk_query_results(p,first,count,(size_t)(b->size-record->args.query.offset),b->memory->bytes+b->memory_offset+record->args.query.offset,record->args.query.stride,record->args.query.flags);
                if(result!=VK_SUCCESS&&result!=VK_NOT_READY)return result;
            }
        } else if(record->kind==VK_RECORD_WRITE_BUFFER){
            vk_buffer_impl_t *b=record->args.write_buffer.buffer;
            if(!b||!b->memory||!b->memory->bytes)return VK_ERROR_DEVICE_LOST;
            uint8_t *to=b->memory->bytes+b->memory_offset+record->args.write_buffer.offset;
            if(record->args.write_buffer.data)memcpy(to,record->args.write_buffer.data,(size_t)record->args.write_buffer.size);
            else for(VkDeviceSize o=0u;o<record->args.write_buffer.size;o+=4u)memcpy(to+o,&record->args.write_buffer.value,4u);
        } else if(record->kind==VK_RECORD_MEMORY_BARRIER){__atomic_thread_fence(__ATOMIC_SEQ_CST);
        } else if(record->kind==VK_RECORD_EVENT_SET||record->kind==VK_RECORD_EVENT_RESET||record->kind==VK_RECORD_EVENT_WAIT){
            vk_event_impl_t *e=record->args.event.event;if(!e)return VK_ERROR_DEVICE_LOST;
            if(record->kind==VK_RECORD_EVENT_SET){VkResult result=vk_event_set(e);if(result!=VK_SUCCESS)return result;}
            else if(record->kind==VK_RECORD_EVENT_RESET)__atomic_store_n(&e->signaled,0u,__ATOMIC_RELEASE);
            else {VkResult result=vk_event_wait(e);if(result!=VK_SUCCESS)return result;}
        } else if (record->kind == VK_RECORD_COPY_BUFFER) {
            vk_buffer_impl_t *src = record->args.copy_buffer.src;
            vk_buffer_impl_t *dst = record->args.copy_buffer.dst;
            const VkBufferCopy *region = &record->args.copy_buffer.region;
            if (src == 0 || dst == 0 || src->memory == 0 || dst->memory == 0 ||
                region->srcOffset > src->size || region->size > src->size - region->srcOffset ||
                region->dstOffset > dst->size || region->size > dst->size - region->dstOffset ||
                src->memory_offset > src->memory->size ||
                src->size > src->memory->size - src->memory_offset ||
                dst->memory_offset > dst->memory->size ||
                dst->size > dst->memory->size - dst->memory_offset) {
                return VK_ERROR_DEVICE_LOST;
            }
            memmove(dst->memory->bytes + dst->memory_offset + region->dstOffset,
                    src->memory->bytes + src->memory_offset + region->srcOffset,
                    (size_t)region->size);
        } else if (record->kind == VK_RECORD_IMAGE_BARRIER) {
            vk_image_impl_t *image = record->args.image_barrier.image;
            const VkImageSubresourceRange *range = &record->args.image_barrier.range;
            if (image == 0) return VK_ERROR_DEVICE_LOST;
            for (uint32_t layer = range->baseArrayLayer; layer < range->baseArrayLayer + range->layerCount; ++layer)
                for (uint32_t mip = range->baseMipLevel; mip < range->baseMipLevel + range->levelCount; ++mip) {
                    vk_image_subresource_impl_t *sub = vk_image_subresource(image, mip, layer);
                    if (record->args.image_barrier.old_layout != VK_IMAGE_LAYOUT_UNDEFINED &&
                        sub->layout != record->args.image_barrier.old_layout) return VK_ERROR_DEVICE_LOST;
                }
            for (uint32_t layer = range->baseArrayLayer; layer < range->baseArrayLayer + range->layerCount; ++layer)
                for (uint32_t mip = range->baseMipLevel; mip < range->baseMipLevel + range->levelCount; ++mip)
                    vk_image_subresource(image, mip, layer)->layout = record->args.image_barrier.new_layout;
        } else if (record->kind == VK_RECORD_BEGIN_RENDER_PASS) {
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            vk_framebuffer_impl_t *fb = record->args.begin_render_pass.framebuffer;
            vk_render_pass_impl_t *pass = fb->render_pass;
            vk_image_plane_t color, depth;
            const VkRect2D *area = &record->args.begin_render_pass.area;
            if (!vk_view_plane(fb->attachment0, &color) ||
                (pass->initial_layout != VK_IMAGE_LAYOUT_UNDEFINED && color.layout != pass->initial_layout) ||
                (pass->has_depth && (!vk_view_plane(fb->depth_attachment, &depth) ||
                  (pass->depth_initial_layout != VK_IMAGE_LAYOUT_UNDEFINED && depth.layout != pass->depth_initial_layout))))
                return VK_ERROR_DEVICE_LOST;
            fb->attachment0->image->subresources[vk_view_subresource(fb->attachment0)].layout = pass->subpass_layout;
            if(fb->resolve_attachment){
                vk_image_subresource_impl_t *sub=&fb->resolve_attachment->image->subresources[vk_view_subresource(fb->resolve_attachment)];
                if(pass->resolve_initial_layout!=VK_IMAGE_LAYOUT_UNDEFINED&&sub->layout!=pass->resolve_initial_layout)return VK_ERROR_DEVICE_LOST;
                sub->layout=pass->resolve_subpass_layout;
            }
            if (pass->color_load_op == VK_ATTACHMENT_LOAD_OP_CLEAR)
                fill_image_rect(&color, area->offset.x, area->offset.y,
                    (int32_t)area->extent.width, (int32_t)area->extent.height, record->args.begin_render_pass.clear_color);
            if (pass->has_depth) {
                fb->depth_attachment->image->subresources[vk_view_subresource(fb->depth_attachment)].layout = pass->depth_subpass_layout;
                if (vk_format_has_stencil(pass->depth_format) && pass->stencil_load_op == VK_ATTACHMENT_LOAD_OP_CLEAR)
                    fill_stencil_rect(&depth, area->offset.x, area->offset.y,
                        (int32_t)area->extent.width, (int32_t)area->extent.height, record->args.begin_render_pass.clear_stencil);
                if (vk_format_has_depth(pass->depth_format) && pass->depth_load_op == VK_ATTACHMENT_LOAD_OP_CLEAR)
                    fill_depth_rect(&depth, area->offset.x, area->offset.y,
                        (int32_t)area->extent.width, (int32_t)area->extent.height, record->args.begin_render_pass.clear_depth);
            }
        } else if (record->kind == VK_RECORD_END_RENDER_PASS) {
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            vk_image_subresource_impl_t *color = &record->args.end_render_pass.image->subresources[record->args.end_render_pass.subresource];
            vk_image_subresource_impl_t *depth = record->args.end_render_pass.depth_image == 0 ? 0 :
                &record->args.end_render_pass.depth_image->subresources[record->args.end_render_pass.depth_subresource];
            if (color->layout != record->args.end_render_pass.expected_layout ||
                (depth != 0 && depth->layout != record->args.end_render_pass.depth_expected_layout)) return VK_ERROR_DEVICE_LOST;
            vk_framebuffer_impl_t *fb=record->args.end_render_pass.resolve_framebuffer;
            if(fb){
                vk_recorded_command_t resolve={0};resolve.kind=VK_RECORD_RESOLVE_IMAGE;
                resolve.args.copy_image.src=fb->attachment0->image;resolve.args.copy_image.dst=fb->resolve_attachment->image;
                resolve.args.copy_image.src_layout=fb->render_pass->subpass_layout;resolve.args.copy_image.dst_layout=fb->render_pass->resolve_subpass_layout;
                VkImageCopy *r=&resolve.args.copy_image.region;
                r->srcSubresource=(VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT,fb->attachment0->range.baseMipLevel,fb->attachment0->range.baseArrayLayer,1u};
                r->dstSubresource=(VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT,fb->resolve_attachment->range.baseMipLevel,fb->resolve_attachment->range.baseArrayLayer,1u};
                r->srcOffset=r->dstOffset=(VkOffset3D){record->args.end_render_pass.resolve_area.offset.x,record->args.end_render_pass.resolve_area.offset.y,0};
                r->extent=(VkExtent3D){record->args.end_render_pass.resolve_area.extent.width,record->args.end_render_pass.resolve_area.extent.height,1u};
                VkResult result=vk_execute_resolve(&resolve);if(result!=VK_SUCCESS)return result;
                fb->resolve_attachment->image->subresources[vk_view_subresource(fb->resolve_attachment)].layout=fb->render_pass->resolve_final_layout;
            }
            color->layout = record->args.end_render_pass.final_layout;
            if (depth != 0) depth->layout = record->args.end_render_pass.depth_final_layout;
        } else if (record->kind == VK_RECORD_DRAW) {
            VkResult draw_result = vk_execute_draw(record);
            if (draw_result != VK_SUCCESS) return draw_result;
        } else if (record->kind == VK_RECORD_COPY_BUFFER_TO_IMAGE || record->kind == VK_RECORD_COPY_IMAGE_TO_BUFFER) {
            VkResult result = vk_execute_buffer_image(record, record->kind == VK_RECORD_COPY_BUFFER_TO_IMAGE);
            if (result != VK_SUCCESS) return result;
        } else if (record->kind == VK_RECORD_RESOLVE_IMAGE) {
            VkResult result=vk_execute_resolve(record);
            if(result!=VK_SUCCESS)return result;
        } else if (record->kind == VK_RECORD_COPY_IMAGE) {
            VkResult result = vk_execute_image_copy(record);
            if (result != VK_SUCCESS) return result;
        } else if (record->kind == VK_RECORD_BLIT_IMAGE) {
            VkResult blit_result = vk_execute_blit(record);
            if (blit_result != VK_SUCCESS) return blit_result;
        } else if (record->kind == VK_RECORD_BIND_DESCRIPTOR_SET) {
            /* The bind changes command-buffer state. Draw records snapshot the
               bound sets and consume them during submit-time shader execution. */
            if (record->args.bind_descriptor_set.set == 0) {
                return VK_ERROR_DEVICE_LOST;
            }
        } else {
            return VK_ERROR_DEVICE_LOST;
        }
    }
    return VK_SUCCESS;
}

static vk_submit_semaphore_state_t *vk_submit_semaphore_lookup(
    vk_submit_semaphore_state_t *states, size_t capacity,
    vk_semaphore_impl_t *semaphore, vk_device_impl_t *device) {
    uintptr_t hash;
    size_t slot;
    if (states == 0 || capacity == 0u || semaphore == 0 ||
        semaphore->device != device || semaphore->destroy_requested != 0u ||
        semaphore->references == 0u) return 0;
    hash = (uintptr_t)semaphore >> 4u;
    hash ^= hash >> 17u;
    hash *= (uintptr_t)0xed5ad4bbu;
    slot = (size_t)hash & (capacity - 1u);
    for (size_t probe = 0u; probe < capacity; ++probe) {
        vk_submit_semaphore_state_t *state = &states[slot];
        if (state->semaphore == semaphore) return state;
        if (state->semaphore == 0) {
            state->semaphore = semaphore;
            state->signaled = __atomic_load_n(&semaphore->signaled,
                                               __ATOMIC_ACQUIRE);
            state->pending_external_waits = __atomic_load_n(
                &semaphore->pending_external_waits, __ATOMIC_ACQUIRE);
            state->initial_signaled = state->signaled;
            return state;
        }
        slot = (slot + 1u) & (capacity - 1u);
    }
    return 0;
}

static VkResult vk_preflight_submit_semaphores(
    vk_device_impl_t *device, uint32_t submit_count,
    const VkSubmitInfo *submits,
    vk_submit_semaphore_state_t **out_states, size_t *out_capacity) {
    uint64_t operation_count = 0u;
    size_t capacity = 1u;
    vk_submit_semaphore_state_t *states;
    if (out_states == 0 || out_capacity == 0) return VK_ERROR_INITIALIZATION_FAILED;
    *out_states = 0;
    *out_capacity = 0u;
    for (uint32_t i = 0u; i < submit_count; ++i) {
        uint64_t operations = (uint64_t)submits[i].waitSemaphoreCount +
                              (uint64_t)submits[i].signalSemaphoreCount;
        if (operation_count > UINT64_MAX - operations) return VK_ERROR_OUT_OF_HOST_MEMORY;
        operation_count += operations;
    }
    if (operation_count == 0u) return VK_SUCCESS;
    if (operation_count > (uint64_t)((size_t)-1) / 2u)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    while ((uint64_t)capacity < operation_count * 2u) {
        if (capacity > (size_t)-1 / 2u) return VK_ERROR_OUT_OF_HOST_MEMORY;
        capacity *= 2u;
    }
    if (capacity > (size_t)-1 / sizeof(*states)) return VK_ERROR_OUT_OF_HOST_MEMORY;
    states = (vk_submit_semaphore_state_t *)vk_heap_alloc(capacity * sizeof(*states));
    if (states == 0) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(states, 0, capacity * sizeof(*states));

    /* Simulate binary state over the whole ordered batch before executing any
       command. Invalid waits/signals can therefore never partially render. */
    for (uint32_t i = 0u; i < submit_count; ++i) {
        const VkSubmitInfo *submit = &submits[i];
        const uint32_t marker = i + 1u;
        for (uint32_t w = 0u; w < submit->waitSemaphoreCount; ++w) {
            vk_semaphore_impl_t *semaphore =
                (vk_semaphore_impl_t *)(uintptr_t)submit->pWaitSemaphores[w];
            vk_submit_semaphore_state_t *state;
            VkPipelineStageFlags stage = submit->pWaitDstStageMask[w];
            if (stage == 0u || (stage & ~0x0001ffffu) != 0u) {
                vk_heap_free(states);
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            state = vk_submit_semaphore_lookup(states, capacity, semaphore, device);
            if (state == 0 || state->wait_submit == marker ||
                state->signal_submit == marker) {
                vk_heap_free(states);
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            state->wait_submit = marker;
            state->unresolved_wait = state->signaled == VK_FALSE
                ? VK_TRUE : VK_FALSE;
            if (state->signaled == VK_FALSE) {
                if (state->pending_external_waits == UINT32_MAX) {
                    vk_heap_free(states);
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                ++state->pending_external_waits;
            }
            /* An unsignaled wait is a real scheduler wait at execution time.
               It is not a feature error and must not be guessed during
               preflight; a producer may signal it from another execution
               context after this queue submission begins. */
            state->signaled = VK_FALSE;
        }
        for (uint32_t s = 0u; s < submit->signalSemaphoreCount; ++s) {
            vk_semaphore_impl_t *semaphore =
                (vk_semaphore_impl_t *)(uintptr_t)submit->pSignalSemaphores[s];
            vk_submit_semaphore_state_t *state =
                vk_submit_semaphore_lookup(states, capacity, semaphore, device);
            if (state == 0 || state->signal_submit == marker ||
                state->wait_submit == marker || state->signaled != VK_FALSE ||
                state->unresolved_wait != VK_FALSE) {
                vk_heap_free(states);
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            state->signal_submit = marker;
            state->signaled = VK_TRUE;
            state->unresolved_wait = VK_FALSE;
        }
    }
    *out_states = states;
    *out_capacity = capacity;
    return VK_SUCCESS;
}

/* Descriptor contents are read by the deferred renderer. Updating a set while
   any command buffer using it is pending would race that read and violate the
   Vulkan descriptor-set lifetime rules. Count every recorded reference so a
   set can be safely reused as soon as all associated submissions complete. */
static uint32_t vk_pending_targets(vk_recorded_command_t *r,uint32_t *targets[9]){
    uint32_t count=0u;
    if(r->kind==VK_RECORD_EXECUTE_SECONDARY)targets[count++]=&r->args.secondary.command->pending_submissions;
    if(r->kind==VK_RECORD_BIND_DESCRIPTOR_SET)targets[count++]=&r->args.bind_descriptor_set.set->pending_submissions;
    if(r->kind==VK_RECORD_DRAW||r->kind==VK_RECORD_DISPATCH){
        for(uint32_t i=0;i<8;++i){vk_descriptor_set_impl_t *set=r->kind==VK_RECORD_DRAW?r->args.draw.descriptor_sets[i]:r->args.dispatch.sets[i];
            if(set)targets[count++]=&set->pending_submissions;
        }
    }return count;
}
static int vk_command_buffer_set_descriptors_pending(vk_command_buffer_impl_t *command,int pending){
    if(!command)return 0;uint64_t processed=0u;
    vk_record_iterator_t it={command->first_record,0};
    for(vk_recorded_command_t *r;(r=vk_record_next(&it))!=0;){uint32_t *targets[9],count=vk_pending_targets(r,targets);
        for(uint32_t i=0;i<count;++i){uint32_t value=__atomic_load_n(targets[i],__ATOMIC_ACQUIRE);
            if(!pending){uint32_t left=__atomic_sub_fetch(targets[i],1u,__ATOMIC_ACQ_REL);
                if(!i&&r->kind==VK_RECORD_EXECUTE_SECONDARY&&!left&&(r->args.secondary.command->usage_flags&VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))r->args.secondary.command->executable=0u;
                continue;
            }
            if(!i&&r->kind==VK_RECORD_EXECUTE_SECONDARY&&value&&!(r->args.secondary.command->usage_flags&VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT))goto rollback;
            for(;;){if(value==UINT32_MAX)goto rollback;
                if(__atomic_compare_exchange_n(targets[i],&value,value+1u,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))break;
            }++processed;
        }
    }return 1;
rollback:
    it=(vk_record_iterator_t){command->first_record,0};
    for(vk_recorded_command_t *r;processed&&(r=vk_record_next(&it))!=0;){uint32_t *targets[9],count=vk_pending_targets(r,targets);
        for(uint32_t i=0;i<count&&processed;++i,--processed)(void)__atomic_sub_fetch(targets[i],1u,__ATOMIC_ACQ_REL);
    }return 0;
}

/* Single-producer (Vulkan requires external synchronization of VkQueue) and
   single-consumer linked queue. The event counter is the only sleep/wakeup
   mechanism; the worker never polls or waits with the submitting thread. */
static int vk_queue_enqueue(vk_queue_impl_t *queue, vk_queue_job_t *job) {
    vk_queue_job_t *previous;
    if (queue == 0 || job == 0 || queue->work_event == 0u ||
        __atomic_load_n(&queue->stop_worker, __ATOMIC_ACQUIRE) != 0u)
        return -1;
    job->next = 0;
    previous = __atomic_exchange_n(&queue->job_tail, job, __ATOMIC_ACQ_REL);
    if (previous == 0) return -1;
    __atomic_store_n(&previous->next, job, __ATOMIC_RELEASE);
    return vex_event_counter_signal(queue->work_event, 1u);
}

static vk_queue_job_t *vk_queue_pop(vk_queue_impl_t *queue) {
    vk_queue_job_t *head;
    vk_queue_job_t *next;
    if (queue == 0) return 0;
    head = queue->job_head;
    if (head == 0) return 0;
    next = __atomic_load_n(&head->next, __ATOMIC_ACQUIRE);
    if (next == 0) return 0;
    queue->job_head = next;
    if (head != queue->initial_job) vk_heap_free(head);
    return next;
}

static void vk_queue_job_release_payload(vk_queue_job_t *job) {
    if (job == 0) return;
    if (job->kind == VK_QUEUE_JOB_SUBMIT) {
        for (uint32_t i = 0u; job->args.submit.batches != 0 &&
             i < job->args.submit.count; ++i) {
            vk_queue_submit_batch_t *batch = &job->args.submit.batches[i];
            for (uint32_t w = 0u; w < batch->wait_count; ++w)
                vk_semaphore_release(batch->waits == 0 ? 0 : batch->waits[w]);
            for (uint32_t c = 0u; c < batch->command_count; ++c) {
                vk_command_buffer_impl_t *cmd = batch->commands == 0
                    ? 0 : batch->commands[c];
                if (cmd != 0) {
                    (void)vk_command_buffer_set_descriptors_pending(cmd, 0);
                    if (__atomic_sub_fetch(&cmd->pending_submissions, 1u,
                                           __ATOMIC_ACQ_REL) == 0u &&
                        (cmd->usage_flags &
                         VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) != 0u)
                        cmd->executable = 0u;
                    vk_command_buffer_release(cmd);
                }
            }
            for (uint32_t s = 0u; s < batch->signal_count; ++s)
                vk_semaphore_release(batch->signals == 0 ? 0 : batch->signals[s]);
            vk_heap_free(batch->waits);
            vk_heap_free(batch->commands);
            vk_heap_free(batch->signals);
        }
        vk_heap_free(job->args.submit.batches);
        vk_fence_release(job->args.submit.fence);
    } else if (job->kind == VK_QUEUE_JOB_PRESENT) {
        for (uint32_t w = 0u; w < job->args.present.wait_count; ++w)
            vk_semaphore_release(job->args.present.waits == 0
                ? 0 : job->args.present.waits[w]);
        vk_heap_free(job->args.present.waits);
        if (job->args.present.pending != 0 &&
            job->args.present.image != 0)
            __atomic_store_n(&job->args.present.image->wsi_state, 1u,
                             __ATOMIC_RELEASE);
        vk_image_release(job->args.present.image);
        if (job->args.present.pending != 0) {
            vk_pending_present_t *pending = job->args.present.pending;
            if (pending->surface_capability != 0u)
                (void)vex_gpu_surface_destroy(pending->surface_capability);
            vk_heap_free(pending);
        }
    }
    if (job->kind == VK_QUEUE_JOB_SUBMIT) {
        job->args.submit.batches = 0;
        job->args.submit.count = 0u;
    }
}

static VkResult vk_queue_present_execute(vk_queue_impl_t *queue,
                                         vk_queue_job_t *job) {
    vk_swapchain_impl_t *swapchain = job->args.present.swapchain;
    vk_image_impl_t *image = job->args.present.image;
    vk_pending_present_t *pending = job->args.present.pending;
    vex_gpu_surface_present_req_t present = {0};
    if (queue == 0 || swapchain == 0 || image == 0 || pending == 0 ||
        image->presentation_surface == 0u) return VK_ERROR_DEVICE_LOST;
    for (uint32_t i = 0u; i < job->args.present.wait_count; ++i) {
        VkResult waited = vk_binary_semaphore_wait(job->args.present.waits[i]);
        if (waited != VK_SUCCESS) return waited;
    }
    present.surface_handle = image->presentation_surface;
    present.source.x = 0;
    present.source.y = 0;
    present.source.w = (int32_t)image->width;
    present.source.h = (int32_t)image->height;
    present.destination.x = swapchain->present_x;
    present.destination.y = swapchain->present_y;
    present.destination.w = (int32_t)image->width;
    present.destination.h = (int32_t)image->height;
    present.damage = 0;
    present.damage_count = 0u;
    present.opacity = 255u;
    present.filter = VEX_GPU_SURFACE_FILTER_NEAREST;
    present.transform = VEX_GPU_SURFACE_TRANSFORM_IDENTITY;
    present.flags = VEX_GPU_SURFACE_PRESENT_ASYNC;
    present.image_available_event = image->available_event;
    present.swapchain_available_event = swapchain->available_event;
    if (vex_gpu_surface_present(&present) != 0)
        return VK_ERROR_DEVICE_LOST;

    image->present_serial = present.signal_serial;
    pending->serial = present.signal_serial;
    pending->next = 0;
    if (queue->pending_present_tail != 0)
        __atomic_store_n(&queue->pending_present_tail->next, pending,
                         __ATOMIC_RELEASE);
    else
        __atomic_store_n(&queue->pending_present_head, pending,
                         __ATOMIC_RELEASE);
    __atomic_store_n(&queue->pending_present_tail, pending, __ATOMIC_RELEASE);
    job->args.present.pending = 0;
    return VK_SUCCESS;
}

static VkResult vk_queue_job_execute(vk_queue_impl_t *queue,
                                     vk_queue_job_t *job) {
    if (queue == 0 || job == 0 ||
        __atomic_load_n(&queue->async_error, __ATOMIC_ACQUIRE) != 0)
        return VK_ERROR_DEVICE_LOST;
    if (job->kind == VK_QUEUE_JOB_PRESENT)
        return vk_queue_present_execute(queue, job);
    for (uint32_t i = 0u; i < job->args.submit.count; ++i) {
        vk_queue_submit_batch_t *batch = &job->args.submit.batches[i];
        for (uint32_t w = 0u; w < batch->wait_count; ++w) {
            VkResult waited = vk_binary_semaphore_wait(batch->waits[w]);
            if (waited != VK_SUCCESS) return waited;
        }
        for (uint32_t c = 0u; c < batch->command_count; ++c) {
            VkResult executed = vk_execute_records(batch->commands[c]);
            if (executed != VK_SUCCESS) return executed;
        }
        for (uint32_t s = 0u; s < batch->signal_count; ++s) {
            VkResult signaled = vk_binary_semaphore_signal(batch->signals[s]);
            if (signaled != VK_SUCCESS) return signaled;
        }
    }
    return VK_SUCCESS;
}

static void vk_queue_worker_entry(void *opaque) {
    vk_queue_impl_t *queue = (vk_queue_impl_t *)opaque;
    for (;;) {
        uint64_t wake_count = 0u;
        if (queue == 0 || vex_event_counter_read(queue->work_event,
                &wake_count, 0u, 0u) != 0) {
            if (queue != 0)
                __atomic_store_n(&queue->async_error, 1, __ATOMIC_RELEASE);
            break;
        }
        (void)wake_count;
        if (__atomic_load_n(&queue->stop_worker, __ATOMIC_ACQUIRE) != 0u)
            break;
        for (;;) {
            vk_queue_job_t *job = vk_queue_pop(queue);
            VkResult result;
            if (job == 0) break;
            result = vk_queue_job_execute(queue, job);
            if (result != VK_SUCCESS) {
                static const char message[] =
                    "vulkan queue worker: job execution failed\n";
                (void)vex_syscall1(VEX_SYS_DEBUG_LOG,
                                   (uint64_t)(uintptr_t)message);
                __atomic_store_n(&queue->async_error, 1, __ATOMIC_RELEASE);
            }
            /* Backend execution is over. Drop all submission-owned references
               and clear command-buffer pending state before publishing queue
               completion, so vkQueueWaitIdle/fences cannot return while a
               command buffer still appears in flight. */
            vk_queue_job_release_payload(job);
            job->payload_released = 1u;
            /* Queue completion is published only after execution and all
               semaphore operations and lifetime releases have completed. */
            if (job->serial != 0u && vex_gpu_submit_complete(queue->kernel_queue,
                    job->serial, result == VK_SUCCESS ? 0 : -1) != 0) {
                static const char message[] =
                    "vulkan queue worker: completion publish failed\n";
                (void)vex_syscall1(VEX_SYS_DEBUG_LOG,
                                   (uint64_t)(uintptr_t)message);
                __atomic_store_n(&queue->async_error, 1, __ATOMIC_RELEASE);
            }
        }
    }
    if (queue != 0) vex_thread_exit(0);
}

static VkResult vk_queue_build_submit_job(vk_queue_impl_t *queue,
        uint32_t submit_count, const VkSubmitInfo *submits,
        vk_queue_job_t **job_out) {
    vk_queue_job_t *job;
    if (queue == 0 || job_out == 0) return VK_ERROR_INITIALIZATION_FAILED;
    *job_out = 0;
    job = (vk_queue_job_t *)vk_heap_alloc(sizeof(*job));
    if (job == 0) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(job, 0, sizeof(*job));
    job->kind = VK_QUEUE_JOB_SUBMIT;
    job->args.submit.count = submit_count;
    if (submit_count != 0u) {
        if ((size_t)submit_count > SIZE_MAX / sizeof(*job->args.submit.batches)) {
            vk_heap_free(job);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        job->args.submit.batches = (vk_queue_submit_batch_t *)vk_heap_alloc(
            (size_t)submit_count * sizeof(*job->args.submit.batches));
        if (job->args.submit.batches == 0) {
            vk_heap_free(job);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        memset(job->args.submit.batches, 0,
               (size_t)submit_count * sizeof(*job->args.submit.batches));
    }
    for (uint32_t i = 0u; i < submit_count; ++i) {
        const VkSubmitInfo *submit = &submits[i];
        vk_queue_submit_batch_t *batch = &job->args.submit.batches[i];
        batch->wait_count = submit->waitSemaphoreCount;
        batch->command_count = submit->commandBufferCount;
        batch->signal_count = submit->signalSemaphoreCount;
        if ((size_t)batch->wait_count > SIZE_MAX / sizeof(*batch->waits) ||
            (size_t)batch->command_count > SIZE_MAX / sizeof(*batch->commands) ||
            (size_t)batch->signal_count > SIZE_MAX / sizeof(*batch->signals)) {
            vk_queue_job_release_payload(job);
            vk_heap_free(job);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        if (batch->wait_count != 0u) {
            batch->waits = (vk_semaphore_impl_t **)vk_heap_alloc(
                (size_t)batch->wait_count * sizeof(*batch->waits));
            if (batch->waits == 0) goto oom;
            memset(batch->waits, 0,
                   (size_t)batch->wait_count * sizeof(*batch->waits));
        }
        if (batch->command_count != 0u) {
            batch->commands = (vk_command_buffer_impl_t **)vk_heap_alloc(
                (size_t)batch->command_count * sizeof(*batch->commands));
            if (batch->commands == 0) goto oom;
            memset(batch->commands, 0,
                   (size_t)batch->command_count * sizeof(*batch->commands));
        }
        if (batch->signal_count != 0u) {
            batch->signals = (vk_semaphore_impl_t **)vk_heap_alloc(
                (size_t)batch->signal_count * sizeof(*batch->signals));
            if (batch->signals == 0) goto oom;
            memset(batch->signals, 0,
                   (size_t)batch->signal_count * sizeof(*batch->signals));
        }
        for (uint32_t w = 0u; w < batch->wait_count; ++w) {
            vk_semaphore_impl_t *sem = (vk_semaphore_impl_t *)(uintptr_t)
                submit->pWaitSemaphores[w];
            if (sem->device != queue->device || !vk_semaphore_retain(sem))
                goto invalid;
            batch->waits[w] = sem;
        }
        for (uint32_t c = 0u; c < batch->command_count; ++c) {
            vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)
                submit->pCommandBuffers[c];
            uint32_t pending;
            if (cmd == 0 || cmd->device != queue->device ||
                cmd->destroy_requested != 0u || cmd->executable == 0u ||
                !vk_command_buffer_retain(cmd)) goto invalid;
            pending = __atomic_load_n(&cmd->pending_submissions,
                                      __ATOMIC_ACQUIRE);
            for (;;) {
                if ((pending != 0u &&
                     (cmd->usage_flags &
                      VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT) == 0u) ||
                    pending == UINT32_MAX) {
                    vk_command_buffer_release(cmd);
                    goto invalid;
                }
                if (__atomic_compare_exchange_n(&cmd->pending_submissions,
                        &pending, pending + 1u, 0, __ATOMIC_ACQ_REL,
                        __ATOMIC_ACQUIRE)) break;
            }
            if (!vk_command_buffer_set_descriptors_pending(cmd, 1)) {
                (void)__atomic_sub_fetch(&cmd->pending_submissions, 1u,
                                         __ATOMIC_ACQ_REL);
                vk_command_buffer_release(cmd);
                goto invalid;
            }
            batch->commands[c] = cmd;
        }
        for (uint32_t s = 0u; s < batch->signal_count; ++s) {
            vk_semaphore_impl_t *sem = (vk_semaphore_impl_t *)(uintptr_t)
                submit->pSignalSemaphores[s];
            if (sem->device != queue->device || !vk_semaphore_retain(sem))
                goto invalid;
            batch->signals[s] = sem;
        }
    }
    *job_out = job;
    return VK_SUCCESS;
oom:
    vk_queue_job_release_payload(job);
    vk_heap_free(job);
    return VK_ERROR_OUT_OF_HOST_MEMORY;
invalid:
    vk_queue_job_release_payload(job);
    vk_heap_free(job);
    return VK_ERROR_INITIALIZATION_FAILED;
}

VkResult vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits, VkFence fence) {
    vk_queue_impl_t *impl = (vk_queue_impl_t *)(uintptr_t)queue;
    vk_fence_impl_t *fence_impl = (vk_fence_impl_t *)(uintptr_t)fence;
    vk_submit_semaphore_state_t *semaphore_states = 0;
    size_t semaphore_state_capacity = 0u;
    vk_submit_image_layout_t *layout_states = 0;
    size_t layout_state_count = 0u;
    vk_queue_job_t *job = 0;
    VkResult semaphore_preflight;
    uint64_t submission_serial = 0u;
    if (impl == 0 || (submitCount != 0u && pSubmits == 0)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (__atomic_load_n(&impl->async_error, __ATOMIC_ACQUIRE) != 0)
        return VK_ERROR_DEVICE_LOST;
    if (fence_impl != 0 && (fence_impl->device != impl->device ||
        fence_impl->destroy_requested != 0u || fence_impl->pending != 0u ||
        fence_impl->signaled != VK_FALSE))
        return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0u; i < submitCount; ++i) {
        const VkSubmitInfo *submit = &pSubmits[i];
        if (submit->sType != VK_STRUCTURE_TYPE_SUBMIT_INFO || submit->pNext != 0 ||
            (submit->waitSemaphoreCount != 0u &&
             (submit->pWaitSemaphores == 0 || submit->pWaitDstStageMask == 0)) ||
            (submit->commandBufferCount != 0u && submit->pCommandBuffers == 0) ||
            (submit->signalSemaphoreCount != 0u && submit->pSignalSemaphores == 0)) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        for (uint32_t j = 0u; j < submit->commandBufferCount; ++j) {
            vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)submit->pCommandBuffers[j];
            if (cmd == 0 || cmd->device != impl->device ||
                cmd->destroy_requested != 0u || cmd->executable == 0u ||cmd->level!=VK_COMMAND_BUFFER_LEVEL_PRIMARY ||
                (__atomic_load_n(&cmd->pending_submissions, __ATOMIC_ACQUIRE) != 0u &&
                 (cmd->usage_flags &
                  VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT) == 0u)) {
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            for (uint32_t prior_submit = 0u; prior_submit <= i; ++prior_submit) {
                const uint32_t limit = prior_submit == i ? j
                    : pSubmits[prior_submit].commandBufferCount;
                for (uint32_t prior = 0u; prior < limit; ++prior) {
                    if (pSubmits[prior_submit].pCommandBuffers[prior] ==
                            submit->pCommandBuffers[j] &&
                        (cmd->usage_flags &
                         VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT) == 0u)
                        return VK_ERROR_INITIALIZATION_FAILED;
                }
            }
        }
    }
    /* Preflight virtual image layouts in record order, including barriers and
       render-pass transitions from every command buffer in this batch. Shader
       descriptors are checked at their draw point against that virtual state;
       real image layouts remain untouched until execution below. */
    semaphore_preflight = vk_preflight_submit_layouts(submitCount, pSubmits,
        &layout_states, &layout_state_count);
    if (semaphore_preflight != VK_SUCCESS) return semaphore_preflight;
    semaphore_preflight = vk_preflight_submit_semaphores(
        impl->device, submitCount, pSubmits,
        &semaphore_states, &semaphore_state_capacity);
    if (semaphore_preflight != VK_SUCCESS) {
        vk_heap_free(layout_states);
        return semaphore_preflight;
    }
    semaphore_preflight = vk_queue_build_submit_job(impl, submitCount,
        pSubmits, &job);
    if (semaphore_preflight != VK_SUCCESS) {
        vk_heap_free(layout_states);
        vk_heap_free(semaphore_states);
        return semaphore_preflight;
    }
    if (fence_impl != 0) {
        if (!vk_fence_retain(fence_impl)) {
            vk_queue_job_release_payload(job);
            vk_heap_free(job);
            vk_heap_free(layout_states);
            vk_heap_free(semaphore_states);
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        job->args.submit.fence = fence_impl;
    }
    if (vex_gpu_submit_begin(impl->kernel_queue, &submission_serial) != 0 ||
        submission_serial == 0u) {
        vk_queue_job_release_payload(job);
        vk_heap_free(job);
        vk_heap_free(layout_states);
        vk_heap_free(semaphore_states);
        return VK_ERROR_DEVICE_LOST;
    }
    job->serial = submission_serial;

    /* Publish the planned state before waking the consumer. Later submissions
       validate against this tail state even while prior commands are queued. */
    for (size_t i = 0u; i < layout_state_count; ++i)
        layout_states[i].image->subresources[layout_states[i].subresource].scheduled_layout = layout_states[i].layout;
    if (semaphore_states != 0) {
        for (size_t i = 0u; i < semaphore_state_capacity; ++i) {
            if (semaphore_states[i].semaphore != 0)
                __atomic_store_n(&semaphore_states[i].semaphore->signaled,
                    semaphore_states[i].signaled, __ATOMIC_RELEASE);
            if (semaphore_states[i].semaphore != 0)
                __atomic_store_n(
                    &semaphore_states[i].semaphore->pending_external_waits,
                    semaphore_states[i].pending_external_waits,
                    __ATOMIC_RELEASE);
        }
    }
    if (fence_impl != 0) {
        fence_impl->signaled = VK_FALSE;
        fence_impl->pending = 1u;
        fence_impl->kernel_queue = impl->kernel_queue;
        fence_impl->serial = submission_serial;
    }
    impl->last_serial = submission_serial;
    if (vk_queue_enqueue(impl, job) != 0) {
        __atomic_store_n(&impl->async_error, 1, __ATOMIC_RELEASE);
        (void)vex_gpu_submit_complete(impl->kernel_queue, submission_serial, -1);
        /* The queue signal uses its private live event-counter capability and
           cannot fail in a healthy kernel. Keep the queued refs until device
           teardown if the kernel reports capability loss. */
        vk_heap_free(layout_states);
        vk_heap_free(semaphore_states);
        return VK_ERROR_DEVICE_LOST;
    }
    vk_heap_free(layout_states);
    vk_heap_free(semaphore_states);
    return VK_SUCCESS;
}

VkResult vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo) {
    vk_queue_impl_t *queue_impl = (vk_queue_impl_t *)(uintptr_t)queue;
    vk_swapchain_impl_t *swapchain;
    vk_image_impl_t *image;
    vk_pending_present_t *pending;
    vk_queue_job_t *job;
    vex_surface_query_req_t surface_query = {0};
    uint32_t expected_state;
    uint64_t serial = 0u;
    VkResult failure = VK_ERROR_INITIALIZATION_FAILED;
    if (queue_impl == 0 || queue_impl->device == 0 || pPresentInfo == 0 ||
        !queue_impl->device->swapchain_enabled ||
        pPresentInfo->sType != VK_STRUCTURE_TYPE_PRESENT_INFO_KHR ||
        pPresentInfo->pNext != 0 || pPresentInfo->swapchainCount != 1u ||
        pPresentInfo->pSwapchains == 0 || pPresentInfo->pImageIndices == 0 ||
        (pPresentInfo->waitSemaphoreCount != 0u &&
         pPresentInfo->pWaitSemaphores == 0) ||
        pPresentInfo->waitSemaphoreCount > 64u) return VK_ERROR_INITIALIZATION_FAILED;
    swapchain = (vk_swapchain_impl_t *)(uintptr_t)pPresentInfo->pSwapchains[0];
    if (swapchain == 0 || swapchain->device != queue_impl->device ||
        swapchain->destroy_requested != 0u || swapchain->surface == 0 ||
        !vk_surface_valid(swapchain->surface)) return VK_ERROR_OUT_OF_DATE_KHR;
    if (pPresentInfo->pImageIndices[0] >= swapchain->image_count)
        return VK_ERROR_INITIALIZATION_FAILED;
    image = (vk_image_impl_t *)(uintptr_t)swapchain->images[pPresentInfo->pImageIndices[0]];
    if (image == 0 || image->swapchain != swapchain ||
        image->subresources[0].scheduled_layout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ||
        image->presentation_surface == 0u) return VK_ERROR_INITIALIZATION_FAILED;

    for (uint32_t i = 0u; i < pPresentInfo->waitSemaphoreCount; ++i) {
        vk_semaphore_impl_t *sem =
            (vk_semaphore_impl_t *)(uintptr_t)pPresentInfo->pWaitSemaphores[i];
        if (sem == 0 || sem->device != queue_impl->device ||
            sem->event_capability == 0u || sem->destroy_requested != 0u)
            return VK_ERROR_INITIALIZATION_FAILED;
        if (__atomic_load_n(&sem->signaled, __ATOMIC_ACQUIRE) == VK_FALSE &&
            __atomic_load_n(&sem->pending_external_waits,
                            __ATOMIC_ACQUIRE) == UINT32_MAX)
            return VK_ERROR_INITIALIZATION_FAILED;
        for (uint32_t prior = 0u; prior < i; ++prior) {
            if (pPresentInfo->pWaitSemaphores[prior] ==
                pPresentInfo->pWaitSemaphores[i]) return VK_ERROR_INITIALIZATION_FAILED;
        }
    }
    surface_query.surface_handle = swapchain->surface->capability;
    if (vex_surface_query(&surface_query) != 0 ||
        surface_query.width != swapchain->surface->width ||
        surface_query.height != swapchain->surface->height ||
        surface_query.stride != swapchain->surface->info.stride ||
        surface_query.backing_identity != swapchain->surface->info.backing_identity)
        return VK_ERROR_OUT_OF_DATE_KHR;

    pending = (vk_pending_present_t *)vk_heap_alloc(sizeof(*pending));
    job = (vk_queue_job_t *)vk_heap_alloc(sizeof(*job));
    if (pending == 0 || job == 0) {
        vk_heap_free(pending);
        vk_heap_free(job);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memset(pending, 0, sizeof(*pending));
    memset(job, 0, sizeof(*job));
    pending->surface_capability = image->presentation_surface;
    if (vex_surface_retain(pending->surface_capability) != 0) {
        failure = VK_ERROR_SURFACE_LOST_KHR;
        goto present_fail;
    }
    if (!vk_image_retain(image)) {
        (void)vex_gpu_surface_destroy(pending->surface_capability);
        pending->surface_capability = 0u;
        failure = VK_ERROR_INITIALIZATION_FAILED;
        goto present_fail;
    }
    if (pPresentInfo->waitSemaphoreCount != 0u) {
        job->args.present.waits = (vk_semaphore_impl_t **)vk_heap_alloc(
            (size_t)pPresentInfo->waitSemaphoreCount *
            sizeof(*job->args.present.waits));
        if (job->args.present.waits == 0) {
            vk_image_release(image);
            (void)vex_gpu_surface_destroy(pending->surface_capability);
            pending->surface_capability = 0u;
            failure = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto present_fail;
        }
        memset(job->args.present.waits, 0,
               (size_t)pPresentInfo->waitSemaphoreCount *
               sizeof(*job->args.present.waits));
    }
    job->kind = VK_QUEUE_JOB_PRESENT;
    job->args.present.swapchain = swapchain;
    job->args.present.image = image;
    job->args.present.pending = pending;
    job->args.present.wait_count = pPresentInfo->waitSemaphoreCount;
    for (uint32_t i = 0u; i < pPresentInfo->waitSemaphoreCount; ++i) {
        vk_semaphore_impl_t *sem = (vk_semaphore_impl_t *)(uintptr_t)
            pPresentInfo->pWaitSemaphores[i];
        if (!vk_semaphore_retain(sem)) {
            failure = VK_ERROR_INITIALIZATION_FAILED;
            vk_queue_job_release_payload(job);
            vk_heap_free(job);
            return failure;
        }
        job->args.present.waits[i] = sem;
    }
    expected_state = 1u;
    if (!__atomic_compare_exchange_n(&image->wsi_state, &expected_state, 2u,
            0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        failure = VK_ERROR_INITIALIZATION_FAILED;
        vk_queue_job_release_payload(job);
        vk_heap_free(job);
        return failure;
    }
    if (vex_gpu_submit_begin(queue_impl->kernel_queue, &serial) != 0 ||
        serial == 0u) {
        __atomic_store_n(&image->wsi_state, 1u, __ATOMIC_RELEASE);
        failure = VK_ERROR_DEVICE_LOST;
        vk_queue_job_release_payload(job);
        vk_heap_free(job);
        return failure;
    }
    job->serial = serial;
    image->present_serial = serial;

    /* The local binary state represents the queue tail. Actual waits and the
       compositor handoff happen later on the queue worker. */
    for (uint32_t i = 0u; i < pPresentInfo->waitSemaphoreCount; ++i) {
        vk_semaphore_impl_t *sem = (vk_semaphore_impl_t *)(uintptr_t)
            pPresentInfo->pWaitSemaphores[i];
        if (__atomic_load_n(&sem->signaled, __ATOMIC_ACQUIRE) == VK_FALSE)
            __atomic_add_fetch(&sem->pending_external_waits, 1u,
                               __ATOMIC_ACQ_REL);
        __atomic_store_n(&sem->signaled, VK_FALSE, __ATOMIC_RELEASE);
    }
    queue_impl->last_serial = serial;
    if (vk_queue_enqueue(queue_impl, job) != 0) {
        __atomic_store_n(&image->wsi_state, 1u, __ATOMIC_RELEASE);
        __atomic_store_n(&queue_impl->async_error, 1, __ATOMIC_RELEASE);
        (void)vex_gpu_submit_complete(queue_impl->kernel_queue, serial, -1);
        /* Keep the linked job's references until worker/device teardown. */
        return VK_ERROR_DEVICE_LOST;
    }

    if (pPresentInfo->pResults != 0) pPresentInfo->pResults[0] = VK_SUCCESS;
    return VK_SUCCESS;

present_fail:
    vk_heap_free(pending);
    vk_heap_free(job);
    return failure;
}
VkResult vkQueueWaitIdle(VkQueue queue) {
    vk_queue_impl_t *impl = (vk_queue_impl_t *)(uintptr_t)queue;
    int waited;
    VkResult result = VK_SUCCESS;
    if (impl == 0 || impl->kernel_queue == 0u) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (impl->last_serial != 0u) {
        waited = vex_gpu_fence_wait(impl->kernel_queue, impl->last_serial, 0u);
        if (waited != 0) result = waited == -2 ? VK_ERROR_DEVICE_LOST
                                               : VK_ERROR_INITIALIZATION_FAILED;
    }
    {
        vk_pending_present_t *pending = __atomic_load_n(
            &impl->pending_present_head, __ATOMIC_ACQUIRE);
        while (pending != 0) {
            vex_gpu_surface_wait_req_t wait_req = {0};
            vk_pending_present_t *next = __atomic_load_n(&pending->next,
                                                          __ATOMIC_ACQUIRE);
            wait_req.surface_handle = pending->surface_capability;
            wait_req.serial = pending->serial;
            wait_req.timeout_ticks = 0u;
            if (vex_gpu_surface_wait(&wait_req) != 0)
                result = VK_ERROR_DEVICE_LOST;
            (void)vex_gpu_surface_destroy(pending->surface_capability);
            vk_heap_free(pending);
            pending = next;
        }
        __atomic_store_n(&impl->pending_present_head, 0,
                         __ATOMIC_RELEASE);
        __atomic_store_n(&impl->pending_present_tail, 0,
                         __ATOMIC_RELEASE);
        if (__atomic_load_n(&impl->async_error, __ATOMIC_ACQUIRE) != 0)
            result = VK_ERROR_DEVICE_LOST;
        return result;
    }
}

VkResult vkDeviceWaitIdle(VkDevice device) {
    vk_device_impl_t *impl = (vk_device_impl_t *)(uintptr_t)device;
    if (impl == 0) return VK_ERROR_INITIALIZATION_FAILED;
    for (vk_queue_impl_t *queue = impl->queues; queue != 0;
         queue = queue->next) {
        const VkResult result = vkQueueWaitIdle((VkQueue)(uintptr_t)queue);
        if (result != VK_SUCCESS) return result;
    }
    return VK_SUCCESS;
}

/* ── Descriptor set layout ───────────────────────────────────────────────── */

static int vk_descriptor_type_supported(VkDescriptorType type) {
    return type == VK_DESCRIPTOR_TYPE_SAMPLER ||
           type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
           type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
           type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
           type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || type==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE||
           type==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER||type==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
}

static vk_descriptor_layout_binding_impl_t *vk_layout_binding(
    vk_descriptor_set_layout_impl_t *layout, uint32_t binding) {
    if (layout == 0) return 0;
    for (uint32_t i = 0u; i < layout->binding_count; ++i) {
        if (layout->bindings[i].binding.binding == binding) return &layout->bindings[i];
    }
    return 0;
}

static int vk_layout_compatible(vk_descriptor_set_layout_impl_t *left,
                                vk_descriptor_set_layout_impl_t *right) {
    if (left == right) return left != 0;
    if (left == 0 || right == 0 || left->binding_count != right->binding_count)
        return 0;
    for (uint32_t i = 0u; i < left->binding_count; ++i) {
        const VkDescriptorSetLayoutBinding *a = &left->bindings[i].binding;
        vk_descriptor_layout_binding_impl_t *found = vk_layout_binding(right, a->binding);
        if (found == 0 || found->binding.descriptorType != a->descriptorType ||
            found->binding.descriptorCount != a->descriptorCount ||
            found->binding.stageFlags != a->stageFlags ||
            found->binding.pImmutableSamplers != a->pImmutableSamplers) return 0;
    }
    return 1;
}

static void vk_layout_release(vk_descriptor_set_layout_impl_t *layout) {
    if (layout != 0 && vk_ref_release_count(&layout->references)) {
        vk_heap_free(layout->bindings);
        vk_heap_free(layout);
    }
}

static void vk_pipeline_layout_release(vk_pipeline_layout_impl_t *layout) {
    if (layout != 0 && vk_ref_release_count(&layout->references)) {
        for (uint32_t i = 0u; i < layout->set_layout_count; ++i) {
            vk_layout_release((vk_descriptor_set_layout_impl_t *)(uintptr_t)layout->set_layouts[i]);
        }
        vk_heap_free(layout->set_layouts);
        vk_heap_free(layout);
    }
}

static void vk_descriptor_value_release(vk_descriptor_value_impl_t *value) {
    if (value == 0 || value->initialized == 0u) return;
    if (value->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
        value->type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
        vk_buffer_release(value->resource.buffer.buffer);
    } else if(value->type==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER||value->type==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER){
        vk_buffer_view_release(value->resource.texel);
    } else if (value->type == VK_DESCRIPTOR_TYPE_SAMPLER) {
        vk_sampler_release(value->resource.image.sampler);
    } else {
        vk_image_view_release(value->resource.image.view);
        vk_sampler_release(value->resource.image.sampler);
    }
    memset(value, 0, sizeof(*value));
}

static int vk_descriptor_value_clone(vk_descriptor_value_impl_t *dst,
                                     const vk_descriptor_value_impl_t *src) {
    if (dst == 0 || src == 0 || src->initialized == 0u) return 0;
    *dst = *src;
    if (src->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
        src->type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
        if (!vk_buffer_retain(src->resource.buffer.buffer)) goto fail;
    } else if(src->type==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER||src->type==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER){
        if(!vk_buffer_view_retain(src->resource.texel))goto fail;
    } else if (src->type == VK_DESCRIPTOR_TYPE_SAMPLER) {
        if (!vk_sampler_retain(src->resource.image.sampler)) goto fail;
    } else {
        if (!vk_image_view_retain(src->resource.image.view)) goto fail;
        if (src->resource.image.sampler != 0 &&
            !vk_sampler_retain(src->resource.image.sampler)) {
            vk_image_view_release(src->resource.image.view);
            goto fail;
        }
    }
    return 1;
fail:
    memset(dst, 0, sizeof(*dst));
    return 0;
}

static void vk_descriptor_pool_release(vk_descriptor_pool_impl_t *pool) {
    if (pool != 0 && vk_ref_release_count(&pool->references)) {
        vk_heap_free(pool);
    }
}

static void vk_descriptor_set_release(vk_descriptor_set_impl_t *set) {
    if (set == 0 || !vk_ref_release_count(&set->references)) return;
    vk_descriptor_pool_impl_t *pool = set->pool;
    vk_descriptor_set_impl_t **link = &pool->sets;
    while (*link != 0 && *link != set) link = &(*link)->pool_next;
    if (*link == set) *link = set->pool_next;
    for (uint32_t i = 0u; i < set->layout->binding_count; ++i) {
        const VkDescriptorSetLayoutBinding *binding = &set->layout->bindings[i].binding;
        if ((uint32_t)binding->descriptorType < 32u &&
            pool->used[binding->descriptorType] >= binding->descriptorCount) {
            pool->used[binding->descriptorType] -= binding->descriptorCount;
        }
    }
    if (pool->allocated != 0u) --pool->allocated;
    for (uint32_t i = 0u; i < set->value_count; ++i)
        vk_descriptor_value_release(&set->values[i]);
    vk_heap_free(set->values);
    vk_layout_release(set->layout);
    vk_heap_free(set);
    vk_descriptor_pool_release(pool);
}

static void vk_descriptor_set_drop_owner(vk_descriptor_set_impl_t *set) {
    if (set != 0 && set->destroy_requested == 0u) {
        set->destroy_requested = 1u;
        vk_descriptor_set_release(set);
    }
}

VkResult vkCreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDescriptorSetLayout *pSetLayout) {
    vk_descriptor_set_layout_impl_t *layout;
    if (device == VK_NULL_HANDLE || pCreateInfo == 0 || pSetLayout == 0 ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *pSetLayout = VK_NULL_HANDLE;
    if (pAllocator != 0 || pCreateInfo->pNext != 0 || pCreateInfo->flags != 0u ||
        pCreateInfo->bindingCount > 32u ||
        (pCreateInfo->bindingCount != 0u && pCreateInfo->pBindings == 0)) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    uint64_t values = 0u;
    for (uint32_t i = 0u; i < pCreateInfo->bindingCount; ++i) {
        const VkDescriptorSetLayoutBinding *binding = &pCreateInfo->pBindings[i];
        if (!vk_descriptor_type_supported(binding->descriptorType) ||
            binding->descriptorCount == 0u || binding->descriptorCount > 256u ||
            binding->stageFlags == 0u || binding->pImmutableSamplers != 0) {
            return VK_ERROR_FEATURE_NOT_PRESENT;
        }
        for (uint32_t j = 0u; j < i; ++j) {
            if (pCreateInfo->pBindings[j].binding == binding->binding)
                return VK_ERROR_INITIALIZATION_FAILED;
        }
        values += binding->descriptorCount;
        if (values > 4096u) return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    layout = (vk_descriptor_set_layout_impl_t *)vk_heap_alloc(sizeof(*layout));
    if (layout == 0) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(layout, 0, sizeof(*layout));
    if (pCreateInfo->bindingCount != 0u) {
        layout->bindings = (vk_descriptor_layout_binding_impl_t *)vk_heap_alloc(
            pCreateInfo->bindingCount * sizeof(*layout->bindings));
        if (layout->bindings == 0) { vk_heap_free(layout); return VK_ERROR_OUT_OF_HOST_MEMORY; }
        memset(layout->bindings, 0, pCreateInfo->bindingCount * sizeof(*layout->bindings));
        uint32_t offset = 0u;
        for (uint32_t i = 0u; i < pCreateInfo->bindingCount; ++i) {
            layout->bindings[i].binding = pCreateInfo->pBindings[i];
            layout->bindings[i].value_offset = offset;
            offset += pCreateInfo->pBindings[i].descriptorCount;
        }
    }
    layout->device = (vk_device_impl_t *)(uintptr_t)device;
    layout->binding_count = pCreateInfo->bindingCount;
    layout->value_count = (uint32_t)values;
    layout->references = 1u;
    *pSetLayout = (VkDescriptorSetLayout)(uintptr_t)layout;
    return VK_SUCCESS;
}

void vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks *pAllocator) {
    vk_descriptor_set_layout_impl_t *layout = (vk_descriptor_set_layout_impl_t *)(uintptr_t)descriptorSetLayout;
    (void)device;
    (void)pAllocator;
    if (layout != 0 && layout->destroy_requested == 0u) {
        layout->destroy_requested = 1u;
        vk_layout_release(layout);
    }
}

/* ── Descriptor pool ─────────────────────────────────────────────────────── */

VkResult vkCreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDescriptorPool *pDescriptorPool) {
    vk_descriptor_pool_impl_t *pool;
    if (device == VK_NULL_HANDLE || pCreateInfo == 0 || pDescriptorPool == 0 ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *pDescriptorPool = VK_NULL_HANDLE;
    if (pAllocator != 0 || pCreateInfo->pNext != 0 || pCreateInfo->maxSets == 0u ||
        pCreateInfo->poolSizeCount > 32u ||
        (pCreateInfo->poolSizeCount != 0u && pCreateInfo->pPoolSizes == 0) ||
        (pCreateInfo->flags & ~VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT) != 0u) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    pool = (vk_descriptor_pool_impl_t *)vk_heap_alloc(sizeof(*pool));
    if (pool == 0) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(pool, 0, sizeof(*pool));
    for (uint32_t i = 0u; i < pCreateInfo->poolSizeCount; ++i) {
        const VkDescriptorPoolSize *size = &pCreateInfo->pPoolSizes[i];
        if (!vk_descriptor_type_supported(size->type) || size->descriptorCount == 0u ||
            pool->capacity[size->type] > UINT32_MAX - size->descriptorCount) {
            vk_heap_free(pool);
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        pool->capacity[size->type] += size->descriptorCount;
    }
    pool->device = (vk_device_impl_t *)(uintptr_t)device;
    pool->max_sets = pCreateInfo->maxSets;
    pool->flags = pCreateInfo->flags;
    pool->references = 1u;
    *pDescriptorPool = (VkDescriptorPool)(uintptr_t)pool;
    return VK_SUCCESS;
}

void vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks *pAllocator) {
    vk_descriptor_pool_impl_t *pool = (vk_descriptor_pool_impl_t *)(uintptr_t)descriptorPool;
    (void)device;
    (void)pAllocator;
    if (pool == 0 || pool->destroy_requested != 0u) return;
    pool->destroy_requested = 1u;
    vk_descriptor_set_impl_t *set = pool->sets;
    while (set != 0) {
        vk_descriptor_set_impl_t *next = set->pool_next;
        vk_descriptor_set_drop_owner(set);
        set = next;
    }
    vk_descriptor_pool_release(pool);
}

VkResult vkResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkFlags flags) {
    vk_descriptor_pool_impl_t *pool = (vk_descriptor_pool_impl_t *)(uintptr_t)descriptorPool;
    if (pool == 0 || pool->device != (vk_device_impl_t *)(uintptr_t)device ||
        pool->destroy_requested != 0u || flags != 0u) return VK_ERROR_INITIALIZATION_FAILED;
    for (vk_descriptor_set_impl_t *set = pool->sets; set != 0; set = set->pool_next) {
        if (__atomic_load_n(&set->references, __ATOMIC_ACQUIRE) != 1u)
            return VK_ERROR_INITIALIZATION_FAILED;
    }
    while (pool->sets != 0) vk_descriptor_set_drop_owner(pool->sets);
    memset(pool->used, 0, sizeof(pool->used));
    return VK_SUCCESS;
}

/* ── Descriptor set ──────────────────────────────────────────────────────── */

VkResult vkAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo *pAllocateInfo, VkDescriptorSet *pDescriptorSets) {
    vk_descriptor_pool_impl_t *pool;
    vk_descriptor_set_impl_t **created;
    uint32_t needed[32] = {0};
    if (pAllocateInfo == 0 || pDescriptorSets == 0 ||
        pAllocateInfo->sType != VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO ||
        pAllocateInfo->pNext != 0 || pAllocateInfo->descriptorSetCount == 0u ||
        pAllocateInfo->descriptorSetCount > 128u || pAllocateInfo->pSetLayouts == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (uint32_t i = 0u; i < pAllocateInfo->descriptorSetCount; ++i)
        pDescriptorSets[i] = VK_NULL_HANDLE;
    pool = (vk_descriptor_pool_impl_t *)(uintptr_t)pAllocateInfo->descriptorPool;
    if (pool == 0 || pool->device != (vk_device_impl_t *)(uintptr_t)device ||
        pool->destroy_requested != 0u ||
        pAllocateInfo->descriptorSetCount > pool->max_sets - pool->allocated) {
        return VK_ERROR_OUT_OF_POOL_MEMORY;
    }
    for (uint32_t i = 0u; i < pAllocateInfo->descriptorSetCount; ++i) {
        vk_descriptor_set_layout_impl_t *layout =
            (vk_descriptor_set_layout_impl_t *)(uintptr_t)pAllocateInfo->pSetLayouts[i];
        if (layout == 0 || layout->device != pool->device || layout->destroy_requested != 0u)
            return VK_ERROR_INITIALIZATION_FAILED;
        for (uint32_t j = 0u; j < layout->binding_count; ++j) {
            const VkDescriptorSetLayoutBinding *binding = &layout->bindings[j].binding;
            if (needed[binding->descriptorType] > UINT32_MAX - binding->descriptorCount)
                return VK_ERROR_OUT_OF_POOL_MEMORY;
            needed[binding->descriptorType] += binding->descriptorCount;
        }
    }
    for (uint32_t i = 0u; i < 32u; ++i) {
        if (needed[i] > pool->capacity[i] - pool->used[i]) return VK_ERROR_OUT_OF_POOL_MEMORY;
    }
    created = (vk_descriptor_set_impl_t **)vk_heap_alloc(
        pAllocateInfo->descriptorSetCount * sizeof(*created));
    if (created == 0) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(created, 0, pAllocateInfo->descriptorSetCount * sizeof(*created));
    for (uint32_t i = 0u; i < pAllocateInfo->descriptorSetCount; ++i) {
        vk_descriptor_set_layout_impl_t *layout =
            (vk_descriptor_set_layout_impl_t *)(uintptr_t)pAllocateInfo->pSetLayouts[i];
        created[i] = (vk_descriptor_set_impl_t *)vk_heap_alloc(sizeof(*created[i]));
        if (created[i] == 0) goto allocate_fail;
        memset(created[i], 0, sizeof(*created[i]));
        if (layout->value_count != 0u) {
            created[i]->values = (vk_descriptor_value_impl_t *)vk_heap_alloc(
                layout->value_count * sizeof(*created[i]->values));
            if (created[i]->values == 0) goto allocate_fail;
            memset(created[i]->values, 0, layout->value_count * sizeof(*created[i]->values));
        }
        created[i]->pool = pool;
        created[i]->layout = layout;
        created[i]->value_count = layout->value_count;
        created[i]->references = 1u;
        created[i]->allocated = 1u;
    }
    for (uint32_t i = 0u; i < pAllocateInfo->descriptorSetCount; ++i) {
        vk_descriptor_set_impl_t *set = created[i];
        __atomic_add_fetch(&set->layout->references, 1u, __ATOMIC_ACQ_REL);
        __atomic_add_fetch(&pool->references, 1u, __ATOMIC_ACQ_REL);
        set->pool_next = pool->sets;
        pool->sets = set;
        pDescriptorSets[i] = (VkDescriptorSet)(uintptr_t)set;
    }
    pool->allocated += pAllocateInfo->descriptorSetCount;
    for (uint32_t i = 0u; i < 32u; ++i) pool->used[i] += needed[i];
    vk_heap_free(created);
    return VK_SUCCESS;

allocate_fail:
    for (uint32_t i = 0u; i < pAllocateInfo->descriptorSetCount; ++i) {
        if (created[i] != 0) {
            vk_heap_free(created[i]->values);
            vk_heap_free(created[i]);
        }
    }
    vk_heap_free(created);
    return VK_ERROR_OUT_OF_HOST_MEMORY;
}

VkResult vkFreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets) {
    vk_descriptor_pool_impl_t *pool = (vk_descriptor_pool_impl_t *)(uintptr_t)descriptorPool;
    if (pool == 0 || pool->device != (vk_device_impl_t *)(uintptr_t)device ||
        pool->destroy_requested != 0u || descriptorSetCount == 0u ||
        pDescriptorSets == 0 ||
        (pool->flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT) == 0u)
        return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0u; i < descriptorSetCount; ++i) {
        vk_descriptor_set_impl_t *set = (vk_descriptor_set_impl_t *)(uintptr_t)pDescriptorSets[i];
        if (set == 0 || set->pool != pool || set->destroy_requested != 0u)
            return VK_ERROR_INITIALIZATION_FAILED;
        for (uint32_t j = 0u; j < i; ++j)
            if (pDescriptorSets[j] == pDescriptorSets[i]) return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (uint32_t i = 0u; i < descriptorSetCount; ++i)
        vk_descriptor_set_drop_owner((vk_descriptor_set_impl_t *)(uintptr_t)pDescriptorSets[i]);
    return VK_SUCCESS;
}

static int vk_descriptor_make_value(vk_device_impl_t *device, VkDescriptorType type,
                                   const VkDescriptorBufferInfo *buffer_info,
                                   const VkDescriptorImageInfo *image_info,
                                   const VkBufferView *texel_info,
                                   vk_descriptor_value_impl_t *value) {
    memset(value, 0, sizeof(*value));
    value->type = type;
    if(type==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER||type==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER){
        if(!texel_info||buffer_info||image_info)return 0;
        vk_buffer_view_impl_t *view=(void *)(uintptr_t)*texel_info;
        VkBufferUsageFlags usage=type==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER?VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT:VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
        if(!view||view->device!=device||view->destroy_requested||!(view->buffer->usage&usage)||!vk_buffer_view_retain(view))return 0;
        value->resource.texel=view;value->initialized=1u;return 1;
    }
    if(texel_info)return 0;
    if (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
        type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
        vk_buffer_impl_t *buffer;
        VkDeviceSize range;
        if (buffer_info == 0 || image_info != 0) return 0;
        buffer = (vk_buffer_impl_t *)(uintptr_t)buffer_info->buffer;
        if (device == 0 || buffer == 0 || buffer->device != device ||
            buffer->destroy_requested != 0u || buffer->memory == 0 ||
            ((type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) &&
             (buffer->usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) == 0u) ||
            ((type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) &&
             (buffer->usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) == 0u) ||
            buffer_info->offset >= buffer->size) return 0;
        range = buffer_info->range == VK_WHOLE_SIZE
            ? buffer->size - buffer_info->offset : buffer_info->range;
        if (range == 0u || range > buffer->size - buffer_info->offset ||
            ((type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) &&
             (range > 65536u || (buffer_info->offset & 15u) != 0u)) ||
            ((type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) &&
             (range > 65536u || (buffer_info->offset & 15u) != 0u)) ||
            !vk_buffer_retain(buffer)) return 0;
        value->resource.buffer.buffer = buffer;
        value->resource.buffer.offset = buffer_info->offset;
        value->resource.buffer.range = range;
    } else {
        vk_sampler_impl_t *sampler = 0;
        vk_image_view_impl_t *view = 0;
        if (image_info == 0 || buffer_info != 0) return 0;
        if (image_info->sampler != VK_NULL_HANDLE) {
            sampler = (vk_sampler_impl_t *)(uintptr_t)image_info->sampler;
            if (sampler->device != device || sampler->destroy_requested != 0u ||
                !vk_sampler_retain(sampler)) return 0;
        }
        if (image_info->imageView != VK_NULL_HANDLE) {
            view = (vk_image_view_impl_t *)(uintptr_t)image_info->imageView;
            if (view->destroy_requested != 0u || view->image == 0 ||
                view->image->device != device || !vk_image_view_retain(view)) {
                vk_sampler_release(sampler);
                return 0;
            }
        }
        if ((type == VK_DESCRIPTOR_TYPE_SAMPLER &&
             (sampler == 0 || view != 0)) ||
            (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
             (sampler == 0 || view == 0)) ||
            ((type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE||type==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) &&
             (sampler != 0 || view == 0))) {
            vk_image_view_release(view);
            vk_sampler_release(sampler);
            return 0;
        }
        if (view != 0 &&
            ((view->image->usage & (type==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE?VK_IMAGE_USAGE_STORAGE_BIT:VK_IMAGE_USAGE_SAMPLED_BIT)) == 0u ||
             (type==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE&&(view->image->format!=VK_FORMAT_R8G8B8A8_UNORM||view->range.levelCount!=1u||image_info->imageLayout!=VK_IMAGE_LAYOUT_GENERAL)) ||
             (image_info->imageLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
              image_info->imageLayout != VK_IMAGE_LAYOUT_GENERAL))) {
            vk_image_view_release(view);
            vk_sampler_release(sampler);
            return 0;
        }
        value->resource.image.view = view;
        value->resource.image.sampler = sampler;
        value->resource.image.layout = image_info->imageLayout;
    }
    value->initialized = 1u;
    return 1;
}

void vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet *pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet *pDescriptorCopies) {
    vk_device_impl_t *device_impl = (vk_device_impl_t *)(uintptr_t)device;
    if (device_impl == 0 || (descriptorWriteCount != 0u && pDescriptorWrites == 0) ||
        (descriptorCopyCount != 0u && pDescriptorCopies == 0)) return;
    for (uint32_t i = 0u; i < descriptorWriteCount; ++i) {
        const VkWriteDescriptorSet *write = &pDescriptorWrites[i];
        vk_descriptor_set_impl_t *set = (vk_descriptor_set_impl_t *)(uintptr_t)write->dstSet;
        vk_descriptor_layout_binding_impl_t *binding;
        vk_descriptor_value_impl_t *temporary;
        if (write->sType != VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET ||
            write->pNext != 0 || write->descriptorCount == 0u ||
            set == 0 || set->pool->device != device_impl ||
            set->destroy_requested != 0u || set->pool->destroy_requested != 0u ||
            __atomic_load_n(&set->pending_submissions, __ATOMIC_ACQUIRE) != 0u ||
            (binding = vk_layout_binding(set->layout, write->dstBinding)) == 0 ||
            binding->binding.descriptorType != write->descriptorType ||
            write->dstArrayElement > binding->binding.descriptorCount ||
            write->descriptorCount > binding->binding.descriptorCount - write->dstArrayElement) continue;
        temporary = (vk_descriptor_value_impl_t *)vk_heap_alloc(
            write->descriptorCount * sizeof(*temporary));
        if (temporary == 0) continue;
        memset(temporary, 0, write->descriptorCount * sizeof(*temporary));
        uint32_t made = 0u;
        for (; made < write->descriptorCount; ++made) {
            const VkDescriptorBufferInfo *buffer_info = write->pBufferInfo != 0
                ? &write->pBufferInfo[made] : 0;
            const VkDescriptorImageInfo *image_info = write->pImageInfo != 0
                ? &write->pImageInfo[made] : 0;
            if (!vk_descriptor_make_value(device_impl, write->descriptorType, buffer_info,
                                           image_info, write->pTexelBufferView?&write->pTexelBufferView[made]:0, &temporary[made])) break;
        }
        if (made != write->descriptorCount) {
            for (uint32_t j = 0u; j < made; ++j)
                vk_descriptor_value_release(&temporary[j]);
            vk_heap_free(temporary);
            continue;
        }
        const uint32_t first = binding->value_offset + write->dstArrayElement;
        for (uint32_t j = 0u; j < write->descriptorCount; ++j) {
            vk_descriptor_value_release(&set->values[first + j]);
            set->values[first + j] = temporary[j];
            memset(&temporary[j], 0, sizeof(temporary[j]));
        }
        vk_heap_free(temporary);
    }
    for (uint32_t i = 0u; i < descriptorCopyCount; ++i) {
        const VkCopyDescriptorSet *copy = &pDescriptorCopies[i];
        vk_descriptor_set_impl_t *src = (vk_descriptor_set_impl_t *)(uintptr_t)copy->srcSet;
        vk_descriptor_set_impl_t *dst = (vk_descriptor_set_impl_t *)(uintptr_t)copy->dstSet;
        vk_descriptor_layout_binding_impl_t *src_binding;
        vk_descriptor_layout_binding_impl_t *dst_binding;
        vk_descriptor_value_impl_t *temporary;
        if (copy->sType != VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET || copy->pNext != 0 ||
            copy->descriptorCount == 0u || src == 0 || dst == 0 ||
            src->pool->device != device_impl || dst->pool->device != device_impl ||
            src->destroy_requested != 0u || dst->destroy_requested != 0u ||
            __atomic_load_n(&src->pending_submissions, __ATOMIC_ACQUIRE) != 0u ||
            __atomic_load_n(&dst->pending_submissions, __ATOMIC_ACQUIRE) != 0u ||
            (src_binding = vk_layout_binding(src->layout, copy->srcBinding)) == 0 ||
            (dst_binding = vk_layout_binding(dst->layout, copy->dstBinding)) == 0 ||
            src_binding->binding.descriptorType != dst_binding->binding.descriptorType ||
            copy->srcArrayElement > src_binding->binding.descriptorCount ||
            copy->dstArrayElement > dst_binding->binding.descriptorCount ||
            copy->descriptorCount > src_binding->binding.descriptorCount - copy->srcArrayElement ||
            copy->descriptorCount > dst_binding->binding.descriptorCount - copy->dstArrayElement) continue;
        temporary = (vk_descriptor_value_impl_t *)vk_heap_alloc(
            copy->descriptorCount * sizeof(*temporary));
        if (temporary == 0) continue;
        memset(temporary, 0, copy->descriptorCount * sizeof(*temporary));
        uint32_t cloned = 0u;
        for (; cloned < copy->descriptorCount; ++cloned) {
            if (!vk_descriptor_value_clone(&temporary[cloned],
                    &src->values[src_binding->value_offset + copy->srcArrayElement + cloned])) break;
        }
        if (cloned != copy->descriptorCount) {
            for (uint32_t j = 0u; j < cloned; ++j)
                vk_descriptor_value_release(&temporary[j]);
            vk_heap_free(temporary);
            continue;
        }
        for (uint32_t j = 0u; j < copy->descriptorCount; ++j) {
            vk_descriptor_value_impl_t *dst_value =
                &dst->values[dst_binding->value_offset + copy->dstArrayElement + j];
            vk_descriptor_value_release(dst_value);
            *dst_value = temporary[j];
            memset(&temporary[j], 0, sizeof(temporary[j]));
        }
        vk_heap_free(temporary);
    }
}

void vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, uint32_t pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t *pDynamicOffsets) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    vk_pipeline_layout_impl_t *layout_impl = (vk_pipeline_layout_impl_t *)(uintptr_t)layout;
    if (cmd == 0 || cmd->recording == 0u) return;
    if ((pipelineBindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS && pipelineBindPoint!=VK_PIPELINE_BIND_POINT_COMPUTE) || layout_impl == 0 ||
        layout_impl->device != cmd->device ||
        layout_impl->destroy_requested != 0u ||
        (descriptorSetCount != 0u && pDescriptorSets == 0) ||
        dynamicOffsetCount != 0u || pDynamicOffsets != 0 ||
        firstSet > layout_impl->set_layout_count ||
        descriptorSetCount > layout_impl->set_layout_count - firstSet) {
        vk_record_unsupported(cmd);
        return;
    }
    for (uint32_t i = 0u; i < descriptorSetCount; ++i) {
        vk_descriptor_set_impl_t *set = (vk_descriptor_set_impl_t *)(uintptr_t)pDescriptorSets[i];
        if (set == 0 || set->destroy_requested != 0u || set->pool->destroy_requested != 0u ||
            set->pool->device != cmd->device ||
            !vk_layout_compatible(set->layout,
                (vk_descriptor_set_layout_impl_t *)(uintptr_t)layout_impl->set_layouts[firstSet + i])) {
            vk_record_unsupported(cmd);
            return;
        }
    }
    for (uint32_t i = 0u; i < descriptorSetCount; ++i) {
        vk_descriptor_set_impl_t *set = (vk_descriptor_set_impl_t *)(uintptr_t)pDescriptorSets[i];
        vk_recorded_command_t *record = vk_append_record(cmd, VK_RECORD_BIND_DESCRIPTOR_SET);
        if (record == 0) return;
        if (!vk_ref_retain_count(&set->references)) {
            cmd->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
            return;
        }
        record->args.bind_descriptor_set.set = set;
        record->args.bind_descriptor_set.first_set = firstSet + i;
        if(pipelineBindPoint==VK_PIPELINE_BIND_POINT_COMPUTE)cmd->compute_descriptor_sets[firstSet+i]=set;
        else cmd->bound_descriptor_sets[firstSet + i] = set;
    }
}

void vkCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkFlags stageFlags, uint32_t offset, uint32_t size, const void *pValues) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    vk_pipeline_layout_impl_t *layout_impl = (vk_pipeline_layout_impl_t *)(uintptr_t)layout;
    uint32_t end;
    VkShaderStageFlags covered = 0u;
    static const VkShaderStageFlagBits supported_stages[3] = {
        VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_COMPUTE_BIT
    };
    if (cmd == 0 || cmd->recording == 0u) return;
    if (layout_impl == 0 || layout_impl->destroy_requested != 0u ||
        layout_impl->device != cmd->device || pValues == 0 || size == 0u ||
        stageFlags == 0u ||
        (stageFlags & ~(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)) != 0u ||
        size > UINT32_MAX - offset) {
        vk_record_unsupported(cmd);
        return;
    }
    end = offset + size;
    if ((offset & 3u) != 0u || (size & 3u) != 0u ||
        end > sizeof(cmd->push_constants[0])) {
        vk_record_unsupported(cmd);
        return;
    }
    for (uint32_t i = 0u; i < layout_impl->push_constant_range_count; ++i) {
        const VkPushConstantRange *range = &layout_impl->push_ranges[i];
        if (offset >= range->offset && end <= range->offset + range->size)
            covered |= range->stageFlags & stageFlags;
    }
    if ((covered & stageFlags) != stageFlags) {
        vk_record_unsupported(cmd);
        return;
    }
    for (uint32_t i = 0u; i < 3u; ++i) {
        if ((stageFlags & supported_stages[i]) != 0u) {
            memcpy(cmd->push_constants[i] + offset, pValues, size);
            for (uint32_t word = offset / 4u; word < end / 4u; ++word)
                cmd->push_constant_initialized[i] |= 1u << word;
        }
    }
}

/* ── Sampler ─────────────────────────────────────────────────────────────── */

VkResult vkCreateSampler(VkDevice device, const VkSamplerCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSampler *pSampler) {
    vk_sampler_impl_t *impl;
    if (device == VK_NULL_HANDLE || pCreateInfo == 0 || pSampler == 0 ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *pSampler = VK_NULL_HANDLE;
    if (pAllocator != 0 || pCreateInfo->pNext != 0 || pCreateInfo->flags != 0u ||
        (pCreateInfo->magFilter != VK_FILTER_NEAREST && pCreateInfo->magFilter != VK_FILTER_LINEAR) ||
        (pCreateInfo->minFilter != VK_FILTER_NEAREST && pCreateInfo->minFilter != VK_FILTER_LINEAR) ||
        pCreateInfo->magFilter != pCreateInfo->minFilter ||
        (pCreateInfo->mipmapMode != VK_SAMPLER_MIPMAP_MODE_NEAREST &&
         pCreateInfo->mipmapMode != VK_SAMPLER_MIPMAP_MODE_LINEAR) ||
        (pCreateInfo->addressModeU != VK_SAMPLER_ADDRESS_MODE_REPEAT &&
         pCreateInfo->addressModeU != VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE) ||
        (pCreateInfo->addressModeV != VK_SAMPLER_ADDRESS_MODE_REPEAT &&
         pCreateInfo->addressModeV != VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE) ||
        (pCreateInfo->addressModeW != VK_SAMPLER_ADDRESS_MODE_REPEAT &&
         pCreateInfo->addressModeW != VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE) ||
        pCreateInfo->anisotropyEnable != VK_FALSE ||
        pCreateInfo->compareEnable != VK_FALSE ||
        pCreateInfo->unnormalizedCoordinates != VK_FALSE ||
        !(pCreateInfo->minLod >= 0.0f && pCreateInfo->maxLod >= pCreateInfo->minLod &&
          pCreateInfo->maxLod <= VK_LOD_CLAMP_NONE) || pCreateInfo->mipLodBias != 0.0f) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    impl = (vk_sampler_impl_t *)vk_heap_alloc(sizeof(*impl));
    if (impl == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    impl->device         = (vk_device_impl_t *)(uintptr_t)device;
    impl->mag_filter     = pCreateInfo->magFilter;
    impl->min_filter     = pCreateInfo->minFilter;
    impl->mipmap_mode    = pCreateInfo->mipmapMode;
    impl->address_mode_u = pCreateInfo->addressModeU;
    impl->address_mode_v = pCreateInfo->addressModeV;
    impl->max_anisotropy = pCreateInfo->maxAnisotropy;
    impl->compare_enable = pCreateInfo->compareEnable;
    impl->compare_op     = pCreateInfo->compareOp;
    impl->min_lod        = pCreateInfo->minLod;
    impl->max_lod        = pCreateInfo->maxLod;
    impl->references = 1u;
    impl->destroy_requested = 0u;
    *pSampler = (VkSampler)(uintptr_t)impl;
    return VK_SUCCESS;
}

void vkDestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks *pAllocator) {
    vk_sampler_impl_t *impl = (vk_sampler_impl_t *)(uintptr_t)sampler;
    (void)device;
    (void)pAllocator;
    if (impl != 0 && impl->destroy_requested == 0u) {
        impl->destroy_requested = 1u;
        vk_sampler_release(impl);
    }
}

/* ── Query pool ──────────────────────────────────────────────────────────── */

VkResult vkCreateQueryPool(VkDevice d,const VkQueryPoolCreateInfo *ci,const VkAllocationCallbacks *a,VkQueryPool *out){
    if(out)*out=VK_NULL_HANDLE;
    if(!d||!ci||!out||ci->sType!=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO||!ci->queryCount)return VK_ERROR_INITIALIZATION_FAILED;
    if(ci->pNext||ci->flags||a||ci->queryType!=VK_QUERY_TYPE_OCCLUSION||ci->pipelineStatistics)return VK_ERROR_FEATURE_NOT_PRESENT;
    if(ci->queryCount>65536u)return VK_ERROR_OUT_OF_HOST_MEMORY;
    vk_query_pool_impl_t *p=vk_heap_alloc(sizeof(*p));if(!p)return VK_ERROR_OUT_OF_HOST_MEMORY;memset(p,0,sizeof(*p));
    p->device=(void*)(uintptr_t)d;p->query_count=ci->queryCount;p->references=1u;
    p->slots=vk_heap_alloc((size_t)p->query_count*sizeof(*p->slots));p->wake_counter=vex_event_counter_create(0u,VEX_EVENT_COUNTER_SEMAPHORE);
    if(!p->slots||!p->wake_counter){if(p->wake_counter)vex_event_counter_release(p->wake_counter);vk_heap_free(p->slots);vk_heap_free(p);return VK_ERROR_OUT_OF_HOST_MEMORY;}
    memset(p->slots,0,(size_t)p->query_count*sizeof(*p->slots));*out=(VkQueryPool)(uintptr_t)p;return VK_SUCCESS;
}
void vkDestroyQueryPool(VkDevice d,VkQueryPool qp,const VkAllocationCallbacks *a){
    (void)a;vk_query_pool_impl_t *p=(void*)(uintptr_t)qp;
    if(p&&p->device==(void*)(uintptr_t)d&&!p->destroy_requested){p->destroy_requested=1u;vk_query_release(p);}
}
static vk_recorded_command_t *vk_record_query(VkCommandBuffer cb,VkQueryPool qp,uint32_t first,uint32_t count,vk_record_kind_t kind){
    vk_command_buffer_impl_t *cmd=(void*)(uintptr_t)cb;vk_query_pool_impl_t *p=(void*)(uintptr_t)qp;
    if(!cmd||!cmd->recording)return 0;
    if(!p||p->device!=cmd->device||p->destroy_requested||first>=p->query_count||!count||count>p->query_count-first){vk_record_unsupported(cmd);return 0;}
    vk_recorded_command_t *r=vk_append_record(cmd,kind);
    if(!r||!vk_ref_retain_count(&p->references)){cmd->record_error=VK_ERROR_OUT_OF_HOST_MEMORY;return 0;}
    r->args.query.pool=p;r->args.query.first=first;r->args.query.count=count;return r;
}
void vkCmdResetQueryPool(VkCommandBuffer cb,VkQueryPool qp,uint32_t first,uint32_t count){
    vk_command_buffer_impl_t *cmd=(void*)(uintptr_t)cb;if(!cmd||!cmd->recording)return;
    if(cmd->active_render_pass||cmd->active_query){vk_record_unsupported(cmd);return;}
    (void)vk_record_query(cb,qp,first,count,VK_RECORD_QUERY_RESET);
}
void vkCmdBeginQuery(VkCommandBuffer cb,VkQueryPool qp,uint32_t query,VkQueryControlFlags flags){
    vk_command_buffer_impl_t *cmd=(void*)(uintptr_t)cb;if(!cmd||!cmd->recording)return;
    if(cmd->active_query||(flags&~VK_QUERY_CONTROL_PRECISE_BIT)||((flags&VK_QUERY_CONTROL_PRECISE_BIT)&&!cmd->device->occlusion_precise)){
        vk_record_unsupported(cmd);return;}
    if(vk_record_query(cb,qp,query,1u,VK_RECORD_QUERY_BEGIN)){cmd->active_query=(void*)(uintptr_t)qp;cmd->active_query_index=query;}
}
void vkCmdEndQuery(VkCommandBuffer cb,VkQueryPool qp,uint32_t query){
    vk_command_buffer_impl_t *cmd=(void*)(uintptr_t)cb;if(!cmd||!cmd->recording)return;
    if(cmd->active_query!=(void*)(uintptr_t)qp||cmd->active_query_index!=query){vk_record_unsupported(cmd);return;}
    if(vk_record_query(cb,qp,query,1u,VK_RECORD_QUERY_END))cmd->active_query=0;
}
static int vk_query_result_bounds(vk_query_pool_impl_t *p,uint32_t first,uint32_t count,size_t bytes,VkDeviceSize stride,VkQueryResultFlags flags){
    uint64_t scalar=flags&VK_QUERY_RESULT_64_BIT?8u:4u,element=scalar*(flags&VK_QUERY_RESULT_WITH_AVAILABILITY_BIT?2u:1u);
    return p&&count&&first<p->query_count&&count<=p->query_count-first&&!(flags&~(VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT|VK_QUERY_RESULT_PARTIAL_BIT))&&
        stride>=element&&stride%scalar==0u&&bytes>=element&&(uint64_t)(count-1u)<=(bytes-element)/stride;
}
static VkResult vk_query_results(vk_query_pool_impl_t *p,uint32_t first,uint32_t count,size_t bytes,void *out,VkDeviceSize stride,VkQueryResultFlags flags){
    if(!out||!vk_query_result_bounds(p,first,count,bytes,stride,flags))return VK_ERROR_INITIALIZATION_FAILED;
    if(flags&VK_QUERY_RESULT_WAIT_BIT)for(;;){
        __atomic_fetch_add(&p->waiters,1u,__ATOMIC_SEQ_CST);int all=1;
        for(uint32_t i=0u;i<count;++i)if(__atomic_load_n(&p->slots[first+i].state,__ATOMIC_SEQ_CST)!=3u){all=0;break;}
        if(all){__atomic_fetch_sub(&p->waiters,1u,__ATOMIC_ACQ_REL);break;}
        uint64_t wake=0u;int result=vex_event_counter_read(p->wake_counter,&wake,0u,0u);
        __atomic_fetch_sub(&p->waiters,1u,__ATOMIC_ACQ_REL);if(result!=0)return VK_ERROR_DEVICE_LOST;
    }
    VkResult result=VK_SUCCESS;uint32_t scalar=flags&VK_QUERY_RESULT_64_BIT?8u:4u;
    for(uint32_t i=0u;i<count;++i){vk_query_slot_t *slot=&p->slots[first+i];
        uint64_t available=__atomic_load_n(&slot->state,__ATOMIC_ACQUIRE)==3u?1u:0u;
        uint64_t samples=__atomic_load_n(&slot->samples,__ATOMIC_ACQUIRE);uint8_t *dst=(uint8_t*)out+i*stride;
        if(available||(flags&VK_QUERY_RESULT_PARTIAL_BIT))memcpy(dst,&samples,scalar);else result=VK_NOT_READY;
        if(flags&VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)memcpy(dst+scalar,&available,scalar);
    }
    return result;
}
VkResult vkGetQueryPoolResults(VkDevice d,VkQueryPool qp,uint32_t first,uint32_t count,size_t bytes,void *out,VkDeviceSize stride,VkQueryResultFlags flags){
    vk_query_pool_impl_t *p=(void*)(uintptr_t)qp;
    if(!p||p->device!=(void*)(uintptr_t)d||p->destroy_requested||!vk_ref_retain_count(&p->references))return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result=vk_query_results(p,first,count,bytes,out,stride,flags);vk_query_release(p);return result;
}
void vkCmdCopyQueryPoolResults(VkCommandBuffer cb,VkQueryPool qp,uint32_t first,uint32_t count,VkBuffer buffer,VkDeviceSize offset,VkDeviceSize stride,VkQueryResultFlags flags){
    vk_command_buffer_impl_t *cmd=(void*)(uintptr_t)cb;vk_buffer_impl_t *b=(void*)(uintptr_t)buffer;vk_query_pool_impl_t *p=(void*)(uintptr_t)qp;
    if(!cmd||!cmd->recording)return;
    if(cmd->active_render_pass||!b||b->destroy_requested||b->device!=cmd->device||!b->memory||!(b->usage&VK_BUFFER_USAGE_TRANSFER_DST_BIT)||
       offset>b->size||offset%(flags&VK_QUERY_RESULT_64_BIT?8u:4u)||!vk_query_result_bounds(p,first,count,(size_t)(b->size-offset),stride,flags)){
        vk_record_unsupported(cmd);return;}
    vk_recorded_command_t *r=vk_record_query(cb,qp,first,count,VK_RECORD_QUERY_COPY);if(!r)return;
    if(!vk_buffer_retain(b)){cmd->record_error=VK_ERROR_OUT_OF_HOST_MEMORY;return;}
    r->args.query.buffer=b;r->args.query.offset=offset;r->args.query.stride=stride;r->args.query.flags=flags;
}

void vkCmdWriteTimestamp(VkCommandBuffer commandBuffer, VkFlags pipelineStage, VkQueryPool queryPool, uint32_t query) {
    (void)pipelineStage;
    (void)queryPool;
    (void)query;
    vk_record_unsupported((vk_command_buffer_impl_t *)(uintptr_t)commandBuffer);
}

/* ── Physical device features / image format ─────────────────────────────── */

void vkGetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures *pFeatures) {
    (void)physicalDevice;
    if (pFeatures == 0) {
        return;
    }
    /* Report a conservative feature set: only the capabilities our
       software rasterizer actually supports. */
    {
        uint8_t *bytes = (uint8_t *)pFeatures;
        size_t i;
        for (i = 0u; i < sizeof(*pFeatures); ++i) {
            bytes[i] = 0;
        }
    }
    pFeatures->samplerAnisotropy = VK_FALSE;
    pFeatures->textureCompressionBC = VK_FALSE;
    pFeatures->shaderFloat64 = VK_FALSE;
    pFeatures->shaderInt64   = VK_FALSE;
    pFeatures->shaderInt16   = VK_FALSE;
    pFeatures->occlusionQueryPrecise=VK_TRUE;
    pFeatures->logicOp=VK_TRUE;
    pFeatures->shaderUniformBufferArrayDynamicIndexing = VK_TRUE;
    pFeatures->shaderSampledImageArrayDynamicIndexing = VK_TRUE;
    pFeatures->shaderStorageBufferArrayDynamicIndexing=VK_TRUE;
    pFeatures->shaderStorageImageArrayDynamicIndexing=VK_TRUE;
}

VkResult vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, uint32_t format, uint32_t type, uint32_t tiling, VkFlags usage, VkFlags flags, VkImageFormatProperties *pImageFormatProperties) {
    (void)physicalDevice;
    if (pImageFormatProperties == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    memset(pImageFormatProperties, 0, sizeof(*pImageFormatProperties));
    if ((!vk_format_is_color_unorm(format) && !vk_format_is_ds(format)) ||
        type != VK_IMAGE_TYPE_2D ||
        (tiling != VK_IMAGE_TILING_LINEAR && tiling != VK_IMAGE_TILING_OPTIMAL) ||
        flags != 0u || (usage&VK_IMAGE_USAGE_STORAGE_BIT&&format!=VK_FORMAT_R8G8B8A8_UNORM) || (usage & ~(VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) != 0u ||
        (vk_format_is_color_unorm(format) &&
         (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0u) ||
        ((vk_format_is_ds(format)) &&
         (tiling != VK_IMAGE_TILING_OPTIMAL ||
          (usage & ~(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) != 0u))) {
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    pImageFormatProperties->maxExtent.width  = 4096u;
    pImageFormatProperties->maxExtent.height = 4096u;
    pImageFormatProperties->maxExtent.depth  = 1u;
    pImageFormatProperties->maxMipLevels     = VK_VEX_MAX_IMAGE_MIPS;
    pImageFormatProperties->maxArrayLayers   = VK_VEX_MAX_IMAGE_LAYERS;
    pImageFormatProperties->sampleCounts = !(usage&VK_IMAGE_USAGE_STORAGE_BIT) && tiling==VK_IMAGE_TILING_OPTIMAL &&
        !(usage&VK_IMAGE_USAGE_SAMPLED_BIT) ? 15u : VK_SAMPLE_COUNT_1_BIT;
    VkDeviceSize texels = 0u;
    for (uint32_t side = 4096u; side != 0u; side >>= 1u) texels += (VkDeviceSize)side * side;
    pImageFormatProperties->maxResourceSize = texels * vk_format_texel_bytes((VkFormat)format) * VK_VEX_MAX_IMAGE_LAYERS *
        (pImageFormatProperties->sampleCounts==15u?8u:1u);
    return VK_SUCCESS;
}

/* ── Buffer copy ─────────────────────────────────────────────────────────── */

void vkCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy *pRegions) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    vk_buffer_impl_t *src = (vk_buffer_impl_t *)(uintptr_t)srcBuffer;
    vk_buffer_impl_t *dst = (vk_buffer_impl_t *)(uintptr_t)dstBuffer;
    if (cmd == 0 || cmd->recording == 0u) {
        return;
    }
    if (cmd->active_framebuffer != 0 || src == 0 || dst == 0 || pRegions == 0 || regionCount == 0u ||
        (src->usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) == 0u ||
        (dst->usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) == 0u) {
        vk_record_unsupported(cmd);
        return;
    }
    for (uint32_t i = 0u; i < regionCount; ++i) {
        vk_recorded_command_t *record;
        const VkBufferCopy *region = &pRegions[i];
        if (region->srcOffset > src->size || region->size > src->size - region->srcOffset ||
            region->dstOffset > dst->size || region->size > dst->size - region->dstOffset) {
            vk_record_unsupported(cmd);
            return;
        }
        record = vk_append_record(cmd, VK_RECORD_COPY_BUFFER);
        if (record == 0) return;
        if (!vk_buffer_retain(src)) {
            cmd->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
            return;
        }
        record->args.copy_buffer.src = src;
        if (!vk_buffer_retain(dst)) {
            cmd->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
            return;
        }
        record->args.copy_buffer.dst = dst;
        record->args.copy_buffer.region = *region;
    }
}

/* Core dispatch is shared; a device lookup never leaks instance/physical entrypoints. */
void vkGetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice physical,VkFormat format,VkImageType type,VkSampleCountFlagBits samples,VkImageUsageFlags usage,VkImageTiling tiling,uint32_t *count,VkSparseImageFormatProperties *properties){
    (void)physical;(void)format;(void)type;(void)samples;(void)usage;(void)tiling;(void)properties;if(count)*count=0u;
}
void vkGetImageSparseMemoryRequirements(VkDevice device,VkImage image,uint32_t *count,VkSparseImageMemoryRequirements *requirements){
    (void)device;(void)image;(void)requirements;if(count)*count=0u;
}
VkResult vkQueueBindSparse(VkQueue queue,uint32_t count,const VkBindSparseInfo *infos,VkFence fence){
    (void)queue;(void)count;(void)infos;(void)fence;return VK_ERROR_FEATURE_NOT_PRESENT;
}

static PFN_vkVoidFunction vk_core_proc(const char *name,int device_only){
    if(string_equals_local(name,"vkCmdExecuteCommands"))return (PFN_vkVoidFunction)vkCmdExecuteCommands;
    if(!device_only&&string_equals_local(name,"vkCreateInstance"))return (PFN_vkVoidFunction)vkCreateInstance;
    if(!device_only&&string_equals_local(name,"vkDestroyInstance"))return (PFN_vkVoidFunction)vkDestroyInstance;
    if(!device_only&&string_equals_local(name,"vkEnumeratePhysicalDevices"))return (PFN_vkVoidFunction)vkEnumeratePhysicalDevices;
    if(!device_only&&string_equals_local(name,"vkGetPhysicalDeviceFeatures"))return (PFN_vkVoidFunction)vkGetPhysicalDeviceFeatures;
    if(!device_only&&string_equals_local(name,"vkGetPhysicalDeviceFormatProperties"))return (PFN_vkVoidFunction)vkGetPhysicalDeviceFormatProperties;
    if(!device_only&&string_equals_local(name,"vkGetPhysicalDeviceImageFormatProperties"))return (PFN_vkVoidFunction)vkGetPhysicalDeviceImageFormatProperties;
    if(!device_only&&string_equals_local(name,"vkGetPhysicalDeviceProperties"))return (PFN_vkVoidFunction)vkGetPhysicalDeviceProperties;
    if(!device_only&&string_equals_local(name,"vkGetPhysicalDeviceQueueFamilyProperties"))return (PFN_vkVoidFunction)vkGetPhysicalDeviceQueueFamilyProperties;
    if(!device_only&&string_equals_local(name,"vkGetPhysicalDeviceMemoryProperties"))return (PFN_vkVoidFunction)vkGetPhysicalDeviceMemoryProperties;
    if(!device_only&&string_equals_local(name,"vkGetInstanceProcAddr"))return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    if(1&&string_equals_local(name,"vkGetDeviceProcAddr"))return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    if(!device_only&&string_equals_local(name,"vkCreateDevice"))return (PFN_vkVoidFunction)vkCreateDevice;
    if(1&&string_equals_local(name,"vkDestroyDevice"))return (PFN_vkVoidFunction)vkDestroyDevice;
    if(!device_only&&string_equals_local(name,"vkEnumerateInstanceExtensionProperties"))return (PFN_vkVoidFunction)vkEnumerateInstanceExtensionProperties;
    if(!device_only&&string_equals_local(name,"vkEnumerateDeviceExtensionProperties"))return (PFN_vkVoidFunction)vkEnumerateDeviceExtensionProperties;
    if(!device_only&&string_equals_local(name,"vkEnumerateInstanceLayerProperties"))return (PFN_vkVoidFunction)vkEnumerateInstanceLayerProperties;
    if(!device_only&&string_equals_local(name,"vkEnumerateDeviceLayerProperties"))return (PFN_vkVoidFunction)vkEnumerateDeviceLayerProperties;
    if(1&&string_equals_local(name,"vkGetDeviceQueue"))return (PFN_vkVoidFunction)vkGetDeviceQueue;
    if(1&&string_equals_local(name,"vkQueueSubmit"))return (PFN_vkVoidFunction)vkQueueSubmit;
    if(1&&string_equals_local(name,"vkQueueWaitIdle"))return (PFN_vkVoidFunction)vkQueueWaitIdle;
    if(1&&string_equals_local(name,"vkDeviceWaitIdle"))return (PFN_vkVoidFunction)vkDeviceWaitIdle;
    if(1&&string_equals_local(name,"vkAllocateMemory"))return (PFN_vkVoidFunction)vkAllocateMemory;
    if(1&&string_equals_local(name,"vkFreeMemory"))return (PFN_vkVoidFunction)vkFreeMemory;
    if(1&&string_equals_local(name,"vkMapMemory"))return (PFN_vkVoidFunction)vkMapMemory;
    if(1&&string_equals_local(name,"vkUnmapMemory"))return (PFN_vkVoidFunction)vkUnmapMemory;
    if(1&&string_equals_local(name,"vkFlushMappedMemoryRanges"))return (PFN_vkVoidFunction)vkFlushMappedMemoryRanges;
    if(1&&string_equals_local(name,"vkInvalidateMappedMemoryRanges"))return (PFN_vkVoidFunction)vkInvalidateMappedMemoryRanges;
    if(1&&string_equals_local(name,"vkBindBufferMemory"))return (PFN_vkVoidFunction)vkBindBufferMemory;
    if(1&&string_equals_local(name,"vkBindImageMemory"))return (PFN_vkVoidFunction)vkBindImageMemory;
    if(1&&string_equals_local(name,"vkGetBufferMemoryRequirements"))return (PFN_vkVoidFunction)vkGetBufferMemoryRequirements;
    if(1&&string_equals_local(name,"vkGetImageMemoryRequirements"))return (PFN_vkVoidFunction)vkGetImageMemoryRequirements;
    if(1&&string_equals_local(name,"vkGetImageSparseMemoryRequirements"))return (PFN_vkVoidFunction)vkGetImageSparseMemoryRequirements;
    if(!device_only&&string_equals_local(name,"vkGetPhysicalDeviceSparseImageFormatProperties"))return (PFN_vkVoidFunction)vkGetPhysicalDeviceSparseImageFormatProperties;
    if(1&&string_equals_local(name,"vkQueueBindSparse"))return (PFN_vkVoidFunction)vkQueueBindSparse;
    if(1&&string_equals_local(name,"vkCreateFence"))return (PFN_vkVoidFunction)vkCreateFence;
    if(1&&string_equals_local(name,"vkDestroyFence"))return (PFN_vkVoidFunction)vkDestroyFence;
    if(1&&string_equals_local(name,"vkResetFences"))return (PFN_vkVoidFunction)vkResetFences;
    if(1&&string_equals_local(name,"vkGetFenceStatus"))return (PFN_vkVoidFunction)vkGetFenceStatus;
    if(1&&string_equals_local(name,"vkWaitForFences"))return (PFN_vkVoidFunction)vkWaitForFences;
    if(1&&string_equals_local(name,"vkCreateSemaphore"))return (PFN_vkVoidFunction)vkCreateSemaphore;
    if(1&&string_equals_local(name,"vkDestroySemaphore"))return (PFN_vkVoidFunction)vkDestroySemaphore;
    if(1&&string_equals_local(name,"vkCreateQueryPool"))return (PFN_vkVoidFunction)vkCreateQueryPool;
    if(1&&string_equals_local(name,"vkDestroyQueryPool"))return (PFN_vkVoidFunction)vkDestroyQueryPool;
    if(1&&string_equals_local(name,"vkGetQueryPoolResults"))return (PFN_vkVoidFunction)vkGetQueryPoolResults;
    if(1&&string_equals_local(name,"vkCreateBuffer"))return (PFN_vkVoidFunction)vkCreateBuffer;
    if(1&&string_equals_local(name,"vkDestroyBuffer"))return (PFN_vkVoidFunction)vkDestroyBuffer;
    if(string_equals_local(name,"vkCreateBufferView"))return (PFN_vkVoidFunction)vkCreateBufferView;
    if(string_equals_local(name,"vkDestroyBufferView"))return (PFN_vkVoidFunction)vkDestroyBufferView;
    if(1&&string_equals_local(name,"vkCreateImage"))return (PFN_vkVoidFunction)vkCreateImage;
    if(1&&string_equals_local(name,"vkDestroyImage"))return (PFN_vkVoidFunction)vkDestroyImage;
    if(1&&string_equals_local(name,"vkGetImageSubresourceLayout"))return (PFN_vkVoidFunction)vkGetImageSubresourceLayout;
    if(1&&string_equals_local(name,"vkCreateImageView"))return (PFN_vkVoidFunction)vkCreateImageView;
    if(1&&string_equals_local(name,"vkDestroyImageView"))return (PFN_vkVoidFunction)vkDestroyImageView;
    if(1&&string_equals_local(name,"vkCreateCommandPool"))return (PFN_vkVoidFunction)vkCreateCommandPool;
    if(1&&string_equals_local(name,"vkDestroyCommandPool"))return (PFN_vkVoidFunction)vkDestroyCommandPool;
    if(1&&string_equals_local(name,"vkResetCommandPool"))return (PFN_vkVoidFunction)vkResetCommandPool;
    if(1&&string_equals_local(name,"vkAllocateCommandBuffers"))return (PFN_vkVoidFunction)vkAllocateCommandBuffers;
    if(1&&string_equals_local(name,"vkFreeCommandBuffers"))return (PFN_vkVoidFunction)vkFreeCommandBuffers;
    if(1&&string_equals_local(name,"vkBeginCommandBuffer"))return (PFN_vkVoidFunction)vkBeginCommandBuffer;
    if(1&&string_equals_local(name,"vkEndCommandBuffer"))return (PFN_vkVoidFunction)vkEndCommandBuffer;
    if(1&&string_equals_local(name,"vkResetCommandBuffer"))return (PFN_vkVoidFunction)vkResetCommandBuffer;
    if(1&&string_equals_local(name,"vkCmdCopyBuffer"))return (PFN_vkVoidFunction)vkCmdCopyBuffer;
    if(1&&string_equals_local(name,"vkCmdCopyImage"))return (PFN_vkVoidFunction)vkCmdCopyImage;
    if(1&&string_equals_local(name,"vkCmdCopyBufferToImage"))return (PFN_vkVoidFunction)vkCmdCopyBufferToImage;
    if(1&&string_equals_local(name,"vkCmdCopyImageToBuffer"))return (PFN_vkVoidFunction)vkCmdCopyImageToBuffer;
    if(1&&string_equals_local(name,"vkCmdUpdateBuffer"))return (PFN_vkVoidFunction)vkCmdUpdateBuffer;
    if(1&&string_equals_local(name,"vkCmdFillBuffer"))return (PFN_vkVoidFunction)vkCmdFillBuffer;
    if(1&&string_equals_local(name,"vkCmdPipelineBarrier"))return (PFN_vkVoidFunction)vkCmdPipelineBarrier;
    if(1&&string_equals_local(name,"vkCmdBeginQuery"))return (PFN_vkVoidFunction)vkCmdBeginQuery;
    if(1&&string_equals_local(name,"vkCmdEndQuery"))return (PFN_vkVoidFunction)vkCmdEndQuery;
    if(1&&string_equals_local(name,"vkCmdResetQueryPool"))return (PFN_vkVoidFunction)vkCmdResetQueryPool;
    if(1&&string_equals_local(name,"vkCmdWriteTimestamp"))return (PFN_vkVoidFunction)vkCmdWriteTimestamp;
    if(1&&string_equals_local(name,"vkCmdCopyQueryPoolResults"))return (PFN_vkVoidFunction)vkCmdCopyQueryPoolResults;
    if(1&&string_equals_local(name,"vkCreateEvent"))return (PFN_vkVoidFunction)vkCreateEvent;
    if(1&&string_equals_local(name,"vkDestroyEvent"))return (PFN_vkVoidFunction)vkDestroyEvent;
    if(1&&string_equals_local(name,"vkGetEventStatus"))return (PFN_vkVoidFunction)vkGetEventStatus;
    if(1&&string_equals_local(name,"vkSetEvent"))return (PFN_vkVoidFunction)vkSetEvent;
    if(1&&string_equals_local(name,"vkResetEvent"))return (PFN_vkVoidFunction)vkResetEvent;
    if(1&&string_equals_local(name,"vkCreateShaderModule"))return (PFN_vkVoidFunction)vkCreateShaderModule;
    if(1&&string_equals_local(name,"vkDestroyShaderModule"))return (PFN_vkVoidFunction)vkDestroyShaderModule;
    if(1&&string_equals_local(name,"vkCreatePipelineCache"))return (PFN_vkVoidFunction)vkCreatePipelineCache;
    if(1&&string_equals_local(name,"vkDestroyPipelineCache"))return (PFN_vkVoidFunction)vkDestroyPipelineCache;
    if(1&&string_equals_local(name,"vkGetPipelineCacheData"))return (PFN_vkVoidFunction)vkGetPipelineCacheData;
    if(1&&string_equals_local(name,"vkMergePipelineCaches"))return (PFN_vkVoidFunction)vkMergePipelineCaches;
    if(1&&string_equals_local(name,"vkDestroyPipeline"))return (PFN_vkVoidFunction)vkDestroyPipeline;
    if(1&&string_equals_local(name,"vkCreatePipelineLayout"))return (PFN_vkVoidFunction)vkCreatePipelineLayout;
    if(1&&string_equals_local(name,"vkDestroyPipelineLayout"))return (PFN_vkVoidFunction)vkDestroyPipelineLayout;
    if(1&&string_equals_local(name,"vkCreateSampler"))return (PFN_vkVoidFunction)vkCreateSampler;
    if(1&&string_equals_local(name,"vkDestroySampler"))return (PFN_vkVoidFunction)vkDestroySampler;
    if(1&&string_equals_local(name,"vkCreateDescriptorSetLayout"))return (PFN_vkVoidFunction)vkCreateDescriptorSetLayout;
    if(1&&string_equals_local(name,"vkDestroyDescriptorSetLayout"))return (PFN_vkVoidFunction)vkDestroyDescriptorSetLayout;
    if(1&&string_equals_local(name,"vkCreateDescriptorPool"))return (PFN_vkVoidFunction)vkCreateDescriptorPool;
    if(1&&string_equals_local(name,"vkDestroyDescriptorPool"))return (PFN_vkVoidFunction)vkDestroyDescriptorPool;
    if(1&&string_equals_local(name,"vkResetDescriptorPool"))return (PFN_vkVoidFunction)vkResetDescriptorPool;
    if(1&&string_equals_local(name,"vkAllocateDescriptorSets"))return (PFN_vkVoidFunction)vkAllocateDescriptorSets;
    if(1&&string_equals_local(name,"vkFreeDescriptorSets"))return (PFN_vkVoidFunction)vkFreeDescriptorSets;
    if(1&&string_equals_local(name,"vkUpdateDescriptorSets"))return (PFN_vkVoidFunction)vkUpdateDescriptorSets;
    if(string_equals_local(name,"vkCreateComputePipelines"))return (PFN_vkVoidFunction)vkCreateComputePipelines;
    if(string_equals_local(name,"vkCmdDispatch"))return (PFN_vkVoidFunction)vkCmdDispatch;
    if(string_equals_local(name,"vkCmdDispatchIndirect"))return (PFN_vkVoidFunction)vkCmdDispatchIndirect;
    if(1&&string_equals_local(name,"vkCmdBindPipeline"))return (PFN_vkVoidFunction)vkCmdBindPipeline;
    if(1&&string_equals_local(name,"vkCmdBindDescriptorSets"))return (PFN_vkVoidFunction)vkCmdBindDescriptorSets;
    if(1&&string_equals_local(name,"vkCmdClearColorImage"))return (PFN_vkVoidFunction)vkCmdClearColorImage;
    if(1&&string_equals_local(name,"vkCmdSetEvent"))return (PFN_vkVoidFunction)vkCmdSetEvent;
    if(1&&string_equals_local(name,"vkCmdResetEvent"))return (PFN_vkVoidFunction)vkCmdResetEvent;
    if(1&&string_equals_local(name,"vkCmdWaitEvents"))return (PFN_vkVoidFunction)vkCmdWaitEvents;
    if(1&&string_equals_local(name,"vkCmdPushConstants"))return (PFN_vkVoidFunction)vkCmdPushConstants;
    if(1&&string_equals_local(name,"vkCreateGraphicsPipelines"))return (PFN_vkVoidFunction)vkCreateGraphicsPipelines;
    if(1&&string_equals_local(name,"vkCreateFramebuffer"))return (PFN_vkVoidFunction)vkCreateFramebuffer;
    if(1&&string_equals_local(name,"vkDestroyFramebuffer"))return (PFN_vkVoidFunction)vkDestroyFramebuffer;
    if(1&&string_equals_local(name,"vkCreateRenderPass"))return (PFN_vkVoidFunction)vkCreateRenderPass;
    if(1&&string_equals_local(name,"vkDestroyRenderPass"))return (PFN_vkVoidFunction)vkDestroyRenderPass;
    if(1&&string_equals_local(name,"vkGetRenderAreaGranularity"))return (PFN_vkVoidFunction)vkGetRenderAreaGranularity;
    if(1&&string_equals_local(name,"vkCmdSetViewport"))return (PFN_vkVoidFunction)vkCmdSetViewport;
    if(1&&string_equals_local(name,"vkCmdSetScissor"))return (PFN_vkVoidFunction)vkCmdSetScissor;
    if(1&&string_equals_local(name,"vkCmdSetLineWidth"))return (PFN_vkVoidFunction)vkCmdSetLineWidth;
    if(1&&string_equals_local(name,"vkCmdSetDepthBias"))return (PFN_vkVoidFunction)vkCmdSetDepthBias;
    if(1&&string_equals_local(name,"vkCmdSetBlendConstants"))return (PFN_vkVoidFunction)vkCmdSetBlendConstants;
    if(1&&string_equals_local(name,"vkCmdSetStencilCompareMask"))return (PFN_vkVoidFunction)vkCmdSetStencilCompareMask;
    if(1&&string_equals_local(name,"vkCmdSetStencilWriteMask"))return (PFN_vkVoidFunction)vkCmdSetStencilWriteMask;
    if(1&&string_equals_local(name,"vkCmdSetStencilReference"))return (PFN_vkVoidFunction)vkCmdSetStencilReference;
    if(1&&string_equals_local(name,"vkCmdBindIndexBuffer"))return (PFN_vkVoidFunction)vkCmdBindIndexBuffer;
    if(1&&string_equals_local(name,"vkCmdBindVertexBuffers"))return (PFN_vkVoidFunction)vkCmdBindVertexBuffers;
    if(1&&string_equals_local(name,"vkCmdDraw"))return (PFN_vkVoidFunction)vkCmdDraw;
    if(1&&string_equals_local(name,"vkCmdDrawIndexed"))return (PFN_vkVoidFunction)vkCmdDrawIndexed;
    if(1&&string_equals_local(name,"vkCmdBlitImage"))return (PFN_vkVoidFunction)vkCmdBlitImage;
    if(1&&string_equals_local(name,"vkCmdClearDepthStencilImage"))return (PFN_vkVoidFunction)vkCmdClearDepthStencilImage;
    if(1&&string_equals_local(name,"vkCmdClearAttachments"))return (PFN_vkVoidFunction)vkCmdClearAttachments;
    if(1&&string_equals_local(name,"vkCmdResolveImage"))return (PFN_vkVoidFunction)vkCmdResolveImage;
    if(1&&string_equals_local(name,"vkCmdBeginRenderPass"))return (PFN_vkVoidFunction)vkCmdBeginRenderPass;
    if(1&&string_equals_local(name,"vkCmdEndRenderPass"))return (PFN_vkVoidFunction)vkCmdEndRenderPass;
    return 0;
}

PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance, const char *pName) {
    if (string_equals_local(pName, "vkGetInstanceProcAddr")) return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    if (string_equals_local(pName, "vkCreateInstance")) return (PFN_vkVoidFunction)vkCreateInstance;
    if (string_equals_local(pName, "vkEnumerateInstanceExtensionProperties")) return (PFN_vkVoidFunction)vkEnumerateInstanceExtensionProperties;
    if (string_equals_local(pName, "vkEnumerateInstanceLayerProperties")) return (PFN_vkVoidFunction)vkEnumerateInstanceLayerProperties;
    if (instance == VK_NULL_HANDLE) return 0;
    PFN_vkVoidFunction core=vk_core_proc(pName,0);if(core)return core;
    {
        vk_instance_impl_t *impl = (vk_instance_impl_t *)(uintptr_t)instance;
        if (impl->khr_surface_enabled != 0u) {
            if (string_equals_local(pName, "vkDestroySurfaceKHR")) return (PFN_vkVoidFunction)vkDestroySurfaceKHR;
            if (string_equals_local(pName, "vkGetPhysicalDeviceSurfaceSupportKHR")) return (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceSupportKHR;
            if (string_equals_local(pName, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) return (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
            if (string_equals_local(pName, "vkGetPhysicalDeviceSurfaceFormatsKHR")) return (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceFormatsKHR;
            if (string_equals_local(pName, "vkGetPhysicalDeviceSurfacePresentModesKHR")) return (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfacePresentModesKHR;
        }
        if (impl->vex_surface_enabled != 0u &&
            string_equals_local(pName, "vkCreateVexSurfaceVEX"))
            return (PFN_vkVoidFunction)vkCreateVexSurfaceVEX;
    }
    return 0;
}

PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice device, const char *pName) {
    vk_device_impl_t *device_impl = (vk_device_impl_t *)(uintptr_t)device;
    if (device_impl == 0) return 0;
    if (device_impl->swapchain_enabled != 0u) {
        if (string_equals_local(pName, "vkCreateSwapchainKHR")) return (PFN_vkVoidFunction)vkCreateSwapchainKHR;
        if (string_equals_local(pName, "vkDestroySwapchainKHR")) return (PFN_vkVoidFunction)vkDestroySwapchainKHR;
        if (string_equals_local(pName, "vkGetSwapchainImagesKHR")) return (PFN_vkVoidFunction)vkGetSwapchainImagesKHR;
        if (string_equals_local(pName, "vkAcquireNextImageKHR")) return (PFN_vkVoidFunction)vkAcquireNextImageKHR;
        if (string_equals_local(pName, "vkQueuePresentKHR")) return (PFN_vkVoidFunction)vkQueuePresentKHR;
    }
    if (device_impl->instance->vex_surface_enabled != 0u &&
        string_equals_local(pName, "vkGetVexMemoryCapabilityVEX"))
        return (PFN_vkVoidFunction)vkGetVexMemoryCapabilityVEX;
    return vk_core_proc(pName,1);
}
