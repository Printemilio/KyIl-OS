#include <unos/gpu_driver.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <unos/heap.h>
#include <unos/process_memory.h>

typedef struct gpu_software_context {
    struct limine_framebuffer *framebuffer;
} gpu_software_context_t;

typedef struct gpu_software_queue {
    uint64_t submitted_serial;
    uint64_t completed_serial;
    int32_t failure_status;
    uint32_t family;
} gpu_software_queue_t;

static gpu_software_context_t software_context;

static int software_probe(const gpu_driver_probe_info_t *probe) {
    return probe != NULL && probe->framebuffer != NULL &&
           probe->framebuffer->bpp == 32;
}

static int software_start(const gpu_driver_probe_info_t *probe, void **context_out) {
    if (!software_probe(probe) || context_out == NULL) {
        return -1;
    }
    software_context.framebuffer = probe->framebuffer;
    *context_out = &software_context;
    return 0;
}

static void software_stop(void *context) {
    gpu_software_context_t *software = context;

    if (software != NULL) {
        software->framebuffer = NULL;
    }
}

static int software_buffer_create(void *context,
                                  const gpu_driver_buffer_desc_t *desc,
                                  gpu_driver_resource_t *resource_out) {
    uint32_t *pixels;

    (void)context;
    if (desc == NULL || resource_out == NULL || desc->size == 0u) {
        return -1;
    }
    {
        uint64_t backing_id = 0u;
        void *mapping = NULL;
        if (process_memory_shared_create_contiguous(desc->size, &backing_id,
                                                   &mapping) != 0 ||
            mapping == NULL) {
            return -1;
        }
        pixels = (uint32_t *)mapping;
        resource_out->shared_backing_id = backing_id;
    }
    if (pixels == NULL) {
        process_memory_shared_release(resource_out->shared_backing_id);
        resource_out->shared_backing_id = 0u;
        return -1;
    }
    memset(pixels, 0, desc->size);
    resource_out->cookie = (void *)(uintptr_t)resource_out->shared_backing_id;
    resource_out->cpu_mapping = pixels;
    resource_out->stride = desc->stride;
    resource_out->size = desc->size;
    return 0;
}

static void software_buffer_destroy(void *context, gpu_driver_resource_t *resource) {
    (void)context;
    if (resource == NULL) {
        return;
    }
    if (resource->shared_backing_id != 0u) {
        process_memory_shared_release(resource->shared_backing_id);
    }
    memset(resource, 0, sizeof(*resource));
}

static void clip_rect(uint32_t width, uint32_t height,
                      const vex_rect_t *input,
                      uint32_t *x0, uint32_t *y0,
                      uint32_t *x1, uint32_t *y1) {
    int64_t left = 0;
    int64_t top = 0;
    int64_t right = width;
    int64_t bottom = height;

    if (input != NULL) {
        left = input->x;
        top = input->y;
        right = left + input->w;
        bottom = top + input->h;
        if (input->w <= 0 || input->h <= 0) {
            right = left;
            bottom = top;
        }
    }
    if (left < 0) {
        left = 0;
    }
    if (top < 0) {
        top = 0;
    }
    if (right > width) {
        right = width;
    }
    if (bottom > height) {
        bottom = height;
    }
    if (right < left) {
        right = left;
    }
    if (bottom < top) {
        bottom = top;
    }
    *x0 = (uint32_t)left;
    *y0 = (uint32_t)top;
    *x1 = (uint32_t)right;
    *y1 = (uint32_t)bottom;
}

static int software_buffer_fill_rect(void *context,
                                     gpu_driver_resource_t *resource,
                                     uint32_t width, uint32_t height,
                                     const vex_rect_t *rect, uint32_t color) {
    uint32_t x0;
    uint32_t y0;
    uint32_t x1;
    uint32_t y1;

    (void)context;
    if (resource == NULL || resource->cpu_mapping == NULL ||
        resource->stride < width) {
        return -1;
    }
    clip_rect(width, height, rect, &x0, &y0, &x1, &y1);
    for (uint32_t y = y0; y < y1; ++y) {
        uint32_t *row = resource->cpu_mapping + (size_t)y * resource->stride;
        for (uint32_t x = x0; x < x1; ++x) {
            row[x] = color;
        }
    }
    return 0;
}

static int software_buffer_clear(void *context, gpu_driver_resource_t *resource,
                                 uint32_t width, uint32_t height,
                                 uint32_t color) {
    vex_rect_t full = { 0, 0, (int32_t)width, (int32_t)height };

    return software_buffer_fill_rect(context, resource, width, height,
                                     &full, color);
}

static int software_buffer_blit(void *context, gpu_driver_resource_t *resource,
                                uint32_t width, uint32_t height,
                                const vex_gpu_command_t *command) {
    uint32_t x0;
    uint32_t y0;
    uint32_t x1;
    uint32_t y1;

    (void)context;
    if (resource == NULL || resource->cpu_mapping == NULL ||
        command == NULL || command->src_pixels == NULL ||
        command->src_stride == 0u || resource->stride < width) {
        return -1;
    }
    clip_rect(width, height, &command->rect, &x0, &y0, &x1, &y1);
    for (uint32_t y = y0; y < y1; ++y) {
        const uint32_t source_y = y - y0;
        uint32_t *destination;
        const uint32_t *source;
        uint32_t copy_width;

        if (source_y >= command->src_height) {
            break;
        }
        copy_width = x1 - x0;
        if (copy_width > command->src_width) {
            copy_width = command->src_width;
        }
        destination = resource->cpu_mapping +
                      (size_t)y * resource->stride + x0;
        source = command->src_pixels +
                 (size_t)source_y * command->src_stride;
        memcpy(destination, source, (size_t)copy_width * sizeof(uint32_t));
    }
    return 0;
}

static uint32_t blend_channel(uint32_t under, uint32_t over, uint32_t alpha) {
    return (over * alpha + under * (255u - alpha) + 127u) / 255u;
}

static uint32_t blend_pixel(uint32_t under, uint32_t over, uint32_t opacity,
                            uint32_t alpha_mode) {
    uint32_t source_alpha = opacity;
    uint32_t red = (over >> 16u) & 0xffu;
    uint32_t green = (over >> 8u) & 0xffu;
    uint32_t blue = over & 0xffu;

    if (alpha_mode != VEX_GPU_SURFACE_ALPHA_OPAQUE) {
        source_alpha = ((over >> 24u) & 0xffu) * opacity / 255u;
    }
    if (source_alpha == 0u) {
        return under;
    }
    if (source_alpha >= 255u) {
        return over & 0x00ffffffu;
    }
    if (alpha_mode == VEX_GPU_SURFACE_ALPHA_PREMULTIPLIED) {
        red = red * opacity / 255u;
        green = green * opacity / 255u;
        blue = blue * opacity / 255u;
        return ((red + (((under >> 16u) & 0xffu) *
                        (255u - source_alpha) + 127u) / 255u) << 16u) |
               ((green + (((under >> 8u) & 0xffu) *
                          (255u - source_alpha) + 127u) / 255u) << 8u) |
               (blue + ((under & 0xffu) *
                        (255u - source_alpha) + 127u) / 255u);
    }
    return (blend_channel((under >> 16u) & 0xffu, red, source_alpha) << 16u) |
           (blend_channel((under >> 8u) & 0xffu, green, source_alpha) << 8u) |
           blend_channel(under & 0xffu, blue, source_alpha);
}

static int software_buffer_composite(void *context,
                                     const gpu_driver_composite_req_t *request) {
    int64_t clip_x0;
    int64_t clip_y0;
    int64_t clip_x1;
    int64_t clip_y1;
    const uint32_t opacity = request == NULL ? 0u :
        (request->opacity > 255u ? 255u : request->opacity);

    (void)context;
    if (request == NULL || request->source == NULL ||
        request->destination == NULL ||
        request->source->cpu_mapping == NULL ||
        request->destination->cpu_mapping == NULL ||
        request->filter != VEX_GPU_SURFACE_FILTER_NEAREST ||
        request->transform != VEX_GPU_SURFACE_TRANSFORM_IDENTITY ||
        request->source_rect.x < 0 || request->source_rect.y < 0 ||
        request->source_rect.w <= 0 || request->source_rect.h <= 0 ||
        request->destination_rect.w <= 0 ||
        request->destination_rect.h <= 0 ||
        (uint64_t)(uint32_t)request->source_rect.x +
            (uint32_t)request->source_rect.w > request->source_width ||
        (uint64_t)(uint32_t)request->source_rect.y +
            (uint32_t)request->source_rect.h > request->source_height) {
        return -1;
    }

    clip_x0 = request->destination_rect.x < 0
        ? 0 : request->destination_rect.x;
    clip_y0 = request->destination_rect.y < 0
        ? 0 : request->destination_rect.y;
    clip_x1 = (int64_t)request->destination_rect.x +
              request->destination_rect.w;
    clip_y1 = (int64_t)request->destination_rect.y +
              request->destination_rect.h;
    if (clip_x1 > request->destination_width) {
        clip_x1 = request->destination_width;
    }
    if (clip_y1 > request->destination_height) {
        clip_y1 = request->destination_height;
    }
    if (request->clip.w > 0 && request->clip.h > 0) {
        if(clip_x0 < request->clip.x)clip_x0=request->clip.x;
        if(clip_y0 < request->clip.y)clip_y0=request->clip.y;
        const int64_t right=(int64_t)request->clip.x+request->clip.w;
        const int64_t bottom=(int64_t)request->clip.y+request->clip.h;
        if(clip_x1>right)clip_x1=right;
        if(clip_y1>bottom)clip_y1=bottom;
    }
    if (clip_x1 <= clip_x0 || clip_y1 <= clip_y0 || opacity == 0u) {
        return 0;
    }

    if (request->alpha_mode == VEX_GPU_SURFACE_ALPHA_OPAQUE &&
        opacity == 255u &&
        request->source_rect.w == request->destination_rect.w &&
        request->source_rect.h == request->destination_rect.h) {
        const uint32_t source_x = (uint32_t)request->source_rect.x +
            (uint32_t)(clip_x0 - request->destination_rect.x);
        const uint32_t source_y = (uint32_t)request->source_rect.y +
            (uint32_t)(clip_y0 - request->destination_rect.y);
        const size_t row_bytes =
            (size_t)(clip_x1 - clip_x0) * sizeof(uint32_t);

        for (int64_t y = clip_y0; y < clip_y1; ++y) {
            const uint32_t src_y = source_y + (uint32_t)(y - clip_y0);
            memcpy(request->destination->cpu_mapping +
                       (size_t)y * request->destination->stride + clip_x0,
                   request->source->cpu_mapping +
                       (size_t)src_y * request->source->stride + source_x,
                   row_bytes);
        }
        return 0;
    }

    for (int64_t y = clip_y0; y < clip_y1; ++y) {
        const uint32_t destination_y =
            (uint32_t)(y - request->destination_rect.y);
        const uint32_t source_y = (uint32_t)request->source_rect.y +
            (uint32_t)((uint64_t)destination_y *
                       (uint32_t)request->source_rect.h /
                       (uint32_t)request->destination_rect.h);
        uint32_t *destination = request->destination->cpu_mapping +
            (size_t)y * request->destination->stride;
        const uint32_t *source = request->source->cpu_mapping +
            (size_t)source_y * request->source->stride;

        for (int64_t x = clip_x0; x < clip_x1; ++x) {
            const uint32_t destination_x =
                (uint32_t)(x - request->destination_rect.x);
            const uint32_t source_x = (uint32_t)request->source_rect.x +
                (uint32_t)((uint64_t)destination_x *
                           (uint32_t)request->source_rect.w /
                           (uint32_t)request->destination_rect.w);
            destination[x] = blend_pixel(destination[x], source[source_x],
                                         opacity, request->alpha_mode);
        }
    }
    return 0;
}

static int software_present_cpu(void *context, const uint32_t *pixels,
                                uint32_t width, uint32_t height,
                                uint32_t stride, const vex_rect_t *rects,
                                size_t rect_count) {
    gpu_software_context_t *software = context;
    struct limine_framebuffer *framebuffer;
    vex_rect_t full;

    if (software == NULL || pixels == NULL || stride < width) {
        return -1;
    }
    framebuffer = software->framebuffer;
    if (framebuffer == NULL || framebuffer->bpp != 32) {
        return -1;
    }
    if (rects == NULL || rect_count == 0u) {
        full.x = 0;
        full.y = 0;
        full.w = (int32_t)(width < framebuffer->width
            ? width : framebuffer->width);
        full.h = (int32_t)(height < framebuffer->height
            ? height : framebuffer->height);
        rects = &full;
        rect_count = 1u;
    }

    for (size_t i = 0u; i < rect_count; ++i) {
        uint32_t x0;
        uint32_t y0;
        uint32_t x1;
        uint32_t y1;
        const uint32_t clipped_width = width < framebuffer->width
            ? width : (uint32_t)framebuffer->width;
        const uint32_t clipped_height = height < framebuffer->height
            ? height : (uint32_t)framebuffer->height;

        clip_rect(clipped_width, clipped_height, &rects[i],
                  &x0, &y0, &x1, &y1);
        for (uint32_t y = y0; y < y1; ++y) {
            uint32_t *destination = (uint32_t *)((uint8_t *)
                framebuffer->address + (uint64_t)y * framebuffer->pitch);
            const uint32_t *source = pixels + (size_t)y * stride;
            memcpy(destination + x0, source + x0,
                   (size_t)(x1 - x0) * sizeof(uint32_t));
        }
    }
    return 0;
}

static int software_present(void *context,
                            const gpu_driver_resource_t *resource,
                            uint32_t width, uint32_t height,
                            const vex_rect_t *rects, size_t rect_count) {
    if (resource == NULL || resource->cpu_mapping == NULL) {
        return -1;
    }
    return software_present_cpu(context, resource->cpu_mapping,
                                width, height, resource->stride,
                                rects, rect_count);
}

static int software_queue_create(void *context, uint32_t family,
                                 void **queue_out) {
    gpu_software_queue_t *queue;
    if (context == NULL || queue_out == NULL || family != 0u) return -1;
    *queue_out = NULL;
    queue = (gpu_software_queue_t *)kmalloc(sizeof(*queue));
    if (queue == NULL) return -1;
    memset(queue, 0, sizeof(*queue));
    queue->family = family;
    *queue_out = queue;
    return 0;
}

static void software_queue_destroy(void *context, void *queue_cookie) {
    (void)context;
    if (queue_cookie != NULL) kfree(queue_cookie);
}

static int software_queue_submission_begin(void *context, void *queue_cookie,
                                           uint64_t serial) {
    gpu_software_queue_t *queue = (gpu_software_queue_t *)queue_cookie;
    if (context == NULL || queue == NULL || serial == 0u ||
        queue->failure_status != 0 ||
        queue->submitted_serial == UINT64_MAX ||
        serial != queue->submitted_serial + 1u) return -1;
    queue->submitted_serial = serial;
    return 0;
}

static int software_queue_submission_complete(
    void *context, void *queue_cookie,
    const gpu_driver_timeline_point_t *point) {
    gpu_software_queue_t *queue = (gpu_software_queue_t *)queue_cookie;
    if (context == NULL || queue == NULL || point == NULL ||
        point->serial == 0u || point->serial != queue->completed_serial + 1u ||
        point->serial > queue->submitted_serial) return -1;
    queue->completed_serial = point->serial;
    if (point->status != 0) queue->failure_status = point->status;
    return 0;
}

static const gpu_driver_ops_t software_driver = {
    .abi_version = GPU_DRIVER_ABI_VERSION,
    .backend = VEX_GPU_BACKEND_SOFTWARE,
    .priority = 0u,
    .capabilities = GPU_DRIVER_CAP_CPU_MAP |
                    GPU_DRIVER_CAP_SCANOUT |
                    GPU_DRIVER_CAP_DAMAGE |
                    GPU_DRIVER_CAP_COMPOSITE |
                    GPU_DRIVER_CAP_TIMELINE_FENCES,
    .name = "software-framebuffer",
    .probe = software_probe,
    .start = software_start,
    .stop = software_stop,
    .buffer_create = software_buffer_create,
    .buffer_destroy = software_buffer_destroy,
    .buffer_clear = software_buffer_clear,
    .buffer_fill_rect = software_buffer_fill_rect,
    .buffer_blit = software_buffer_blit,
    .buffer_composite = software_buffer_composite,
    .present = software_present,
    .present_cpu = software_present_cpu,
    .queue_create = software_queue_create,
    .queue_destroy = software_queue_destroy,
    .queue_submission_begin = software_queue_submission_begin,
    .queue_submission_complete = software_queue_submission_complete
};

void gpu_driver_software_register(void) {
    (void)gpu_driver_register(&software_driver);
}
