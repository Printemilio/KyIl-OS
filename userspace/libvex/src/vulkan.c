#include <vulkan/vulkan.h>
#include <string.h>
#include <unos/vex_abi.h>

#include "vex_vk.h"
#include "vex_syscall.h"

typedef struct vk_memory_impl vk_memory_impl_t;
typedef struct vk_buffer_impl vk_buffer_impl_t;

typedef struct vk_instance_impl {
    vexvk_instance_t *inner;
    vexvk_physical_device_info_t info;
} vk_instance_impl_t;

typedef struct vk_surface_impl {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} vk_surface_impl_t;

typedef struct vk_device_impl {
    vk_instance_impl_t *instance;
    vexvk_device_t *inner;
} vk_device_impl_t;

typedef struct vk_queue_impl {
    vk_device_impl_t *device;
} vk_queue_impl_t;

typedef struct vk_swapchain_impl {
    vk_device_impl_t *device;
    vexvk_swapchain_t *inner;
    uint32_t image_count;
    VkImage *images;
    int32_t present_x;
    int32_t present_y;
} vk_swapchain_impl_t;

typedef struct vk_image_impl {
    vk_swapchain_impl_t *swapchain;
    uint32_t index;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t layout;
    vk_memory_impl_t *memory;
    VkDeviceSize memory_offset;
    uint32_t *owned_pixels;
    uint32_t owned_stride;
} vk_image_impl_t;

typedef struct vk_image_view_impl {
    vk_image_impl_t *image;
} vk_image_view_impl_t;

typedef struct vk_shader_module_impl {
    size_t code_size;
} vk_shader_module_impl_t;

typedef struct vk_render_pass_impl {
    uint32_t color_format;
} vk_render_pass_impl_t;

typedef struct vk_framebuffer_impl {
    vk_render_pass_impl_t *render_pass;
    vk_image_view_impl_t *attachment0;
    uint32_t width;
    uint32_t height;
} vk_framebuffer_impl_t;

typedef struct vk_pipeline_layout_impl {
    uint32_t push_constant_range_count;
} vk_pipeline_layout_impl_t;

typedef struct vk_pipeline_impl {
    vk_pipeline_layout_impl_t *layout;
    vk_render_pass_impl_t *render_pass;
} vk_pipeline_impl_t;

typedef struct vk_command_pool_impl {
    vk_device_impl_t *device;
} vk_command_pool_impl_t;

typedef struct vk_command_buffer_impl {
    vk_device_impl_t *device;
    vexvk_cmd_buffer_t *inner;
    VkImage bound_image;
    vk_framebuffer_impl_t *framebuffer;
    vk_pipeline_impl_t *pipeline;
    vk_buffer_impl_t *vertex_buffer;
    VkDeviceSize vertex_buffer_offset;
    vk_buffer_impl_t *index_buffer;
    VkDeviceSize index_buffer_offset;
    VkIndexType index_type;
    VkViewport viewport;
    VkRect2D scissor;
    uint8_t push_constants[128];
    uint32_t push_constant_size;
} vk_command_buffer_impl_t;

struct vk_memory_impl {
    uint8_t *bytes;
    VkDeviceSize size;
};

typedef struct vk_buffer_impl {
    VkDeviceSize size;
    VkFlags usage;
    vk_memory_impl_t *memory;
    VkDeviceSize memory_offset;
} vk_buffer_impl_t;

typedef struct vk_fence_impl {
    VkBool32 signaled;
} vk_fence_impl_t;

typedef struct vk_semaphore_impl {
    VkBool32 signaled;
} vk_semaphore_impl_t;

typedef struct vk_descriptor_set_layout_impl {
    uint32_t binding_count;
    uint32_t reserved;
} vk_descriptor_set_layout_impl_t;

typedef struct vk_descriptor_pool_impl {
    uint32_t max_sets;
    uint32_t allocated;
} vk_descriptor_pool_impl_t;

typedef struct vk_descriptor_set_impl {
    vk_descriptor_pool_impl_t *pool;
} vk_descriptor_set_impl_t;

typedef struct vk_sampler_impl {
    uint32_t mag_filter;
    uint32_t min_filter;
    uint32_t mipmap_mode;
    uint32_t address_mode_u;
    uint32_t address_mode_v;
    float    max_anisotropy;
    VkBool32 compare_enable;
    uint32_t compare_op;
    float    min_lod;
    float    max_lod;
} vk_sampler_impl_t;

typedef struct vk_query_pool_impl {
    uint32_t query_type;
    uint32_t query_count;
} vk_query_pool_impl_t;

typedef struct vk_rect_vertex_impl {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    uint32_t color;
} vk_rect_vertex_impl_t;

typedef struct vk_vertex2d_impl {
    int32_t x;
    int32_t y;
    uint32_t color;
} vk_vertex2d_impl_t;

static void *vk_heap_alloc(size_t size) {
    return (void *)(uintptr_t)vex_syscall1(VEX_SYS_HEAP_ALLOC, (uint64_t)size);
}

static void vk_process_yield(void) {
    (void)vex_syscall0(VEX_SYS_PROCESS_YIELD);
}

static uint64_t vk_now_ns(void) {
    struct timespec ts;
    vex_clock_gettime_req_t req;

    ts.tv_sec = 0;
    ts.tv_nsec = 0;
    req.clock_id = 0;
    req.ts = &ts;
    if ((int64_t)vex_syscall1(VEX_SYS_CLOCK_GETTIME, (uint64_t)(uintptr_t)&req) != 0) {
        return 0u;
    }
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void vk_heap_free(void *ptr) {
    vex_heap_req_t req;
    if (ptr == 0) {
        return;
    }
    req.ptr = ptr;
    req.size = 0u;
    (void)vex_syscall1(VEX_SYS_HEAP_FREE, (uint64_t)(uintptr_t)&req);
}

static uint32_t pack_rgba(const VkClearColorValue *color) {
    uint32_t r = color == 0 ? 0u : (uint32_t)(color->float32[0] * 255.0f);
    uint32_t g = color == 0 ? 0u : (uint32_t)(color->float32[1] * 255.0f);
    uint32_t b = color == 0 ? 0u : (uint32_t)(color->float32[2] * 255.0f);
    return 0xff000000u | (r << 16u) | (g << 8u) | b;
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

static uint32_t *vk_image_pixels(vk_image_impl_t *image) {
    if (image == 0) {
        return 0;
    }
    if (image->owned_pixels != 0) {
        return image->owned_pixels;
    }
    if (image->memory == 0) {
        return 0;
    }
    return (uint32_t *)(void *)(image->memory->bytes + image->memory_offset);
}

static int32_t clamp_i32(int32_t value, int32_t min_value, int32_t max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void fill_image_rect(vk_image_impl_t *image, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    uint32_t *pixels = vk_image_pixels(image);
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;

    if (pixels == 0 || w <= 0 || h <= 0) {
        return;
    }

    x0 = clamp_i32(x, 0, (int32_t)image->width);
    y0 = clamp_i32(y, 0, (int32_t)image->height);
    x1 = clamp_i32(x + w, 0, (int32_t)image->width);
    y1 = clamp_i32(y + h, 0, (int32_t)image->height);
    if (x1 <= x0 || y1 <= y0) {
        return;
    }

    for (int32_t py = y0; py < y1; ++py) {
        uint32_t *row = pixels + (uint32_t)py * image->width;
        for (int32_t px = x0; px < x1; ++px) {
            row[px] = color;
        }
    }
}

static int64_t edge_function(int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t cx, int32_t cy) {
    return (int64_t)(cx - ax) * (int64_t)(by - ay) - (int64_t)(cy - ay) * (int64_t)(bx - ax);
}

static uint32_t lerp_color(uint32_t a, uint32_t b, uint32_t c, uint32_t wa, uint32_t wb, uint32_t wc, uint32_t sum) {
    uint32_t ar = (a >> 16u) & 0xffu;
    uint32_t ag = (a >> 8u) & 0xffu;
    uint32_t ab = a & 0xffu;
    uint32_t br = (b >> 16u) & 0xffu;
    uint32_t bg = (b >> 8u) & 0xffu;
    uint32_t bb = b & 0xffu;
    uint32_t cr = (c >> 16u) & 0xffu;
    uint32_t cg = (c >> 8u) & 0xffu;
    uint32_t cb = c & 0xffu;
    uint32_t r = (ar * wa + br * wb + cr * wc) / sum;
    uint32_t g = (ag * wa + bg * wb + cg * wc) / sum;
    uint32_t bch = (ab * wa + bb * wb + cb * wc) / sum;
    return 0xff000000u | (r << 16u) | (g << 8u) | bch;
}

static void rasterize_triangle(vk_image_impl_t *image, const vk_vertex2d_impl_t *v0, const vk_vertex2d_impl_t *v1, const vk_vertex2d_impl_t *v2, const VkRect2D *scissor) {
    uint32_t *pixels = vk_image_pixels(image);
    int32_t min_x;
    int32_t min_y;
    int32_t max_x;
    int32_t max_y;
    int64_t area;
    uint32_t area_abs;

    if (pixels == 0 || v0 == 0 || v1 == 0 || v2 == 0) {
        return;
    }

    min_x = v0->x < v1->x ? v0->x : v1->x;
    if (v2->x < min_x) min_x = v2->x;
    min_y = v0->y < v1->y ? v0->y : v1->y;
    if (v2->y < min_y) min_y = v2->y;
    max_x = v0->x > v1->x ? v0->x : v1->x;
    if (v2->x > max_x) max_x = v2->x;
    max_y = v0->y > v1->y ? v0->y : v1->y;
    if (v2->y > max_y) max_y = v2->y;

    if (scissor != 0) {
        int32_t sx0 = scissor->offset.x;
        int32_t sy0 = scissor->offset.y;
        int32_t sx1 = scissor->offset.x + (int32_t)scissor->extent.width;
        int32_t sy1 = scissor->offset.y + (int32_t)scissor->extent.height;
        if (min_x < sx0) min_x = sx0;
        if (min_y < sy0) min_y = sy0;
        if (max_x > sx1 - 1) max_x = sx1 - 1;
        if (max_y > sy1 - 1) max_y = sy1 - 1;
    }

    min_x = clamp_i32(min_x, 0, (int32_t)image->width - 1);
    min_y = clamp_i32(min_y, 0, (int32_t)image->height - 1);
    max_x = clamp_i32(max_x, 0, (int32_t)image->width - 1);
    max_y = clamp_i32(max_y, 0, (int32_t)image->height - 1);
    if (max_x < min_x || max_y < min_y) {
        return;
    }

    area = edge_function(v0->x, v0->y, v1->x, v1->y, v2->x, v2->y);
    if (area == 0) {
        return;
    }
    area_abs = (uint32_t)(area < 0 ? -area : area);

    for (int32_t py = min_y; py <= max_y; ++py) {
        uint32_t *row = pixels + (uint32_t)py * image->width;
        for (int32_t px = min_x; px <= max_x; ++px) {
            int64_t w0 = edge_function(v1->x, v1->y, v2->x, v2->y, px, py);
            int64_t w1 = edge_function(v2->x, v2->y, v0->x, v0->y, px, py);
            int64_t w2 = edge_function(v0->x, v0->y, v1->x, v1->y, px, py);

            if ((area > 0 && (w0 < 0 || w1 < 0 || w2 < 0)) ||
                (area < 0 && (w0 > 0 || w1 > 0 || w2 > 0))) {
                continue;
            }

            if (area < 0) {
                w0 = -w0;
                w1 = -w1;
                w2 = -w2;
            }

            row[px] = lerp_color(v0->color, v1->color, v2->color, (uint32_t)w0, (uint32_t)w1, (uint32_t)w2, area_abs);
        }
    }
}

static void draw_triangle_from_vertices(vk_command_buffer_impl_t *cmd, uint32_t i0, uint32_t i1, uint32_t i2) {
    const vk_vertex2d_impl_t *vertices;
    VkDeviceSize needed;
    vk_image_impl_t *image;

    if (cmd == 0 || cmd->bound_image == 0 || cmd->pipeline == 0 || cmd->vertex_buffer == 0 || cmd->vertex_buffer->memory == 0) {
        return;
    }

    needed = ((VkDeviceSize)(i2 > i1 ? (i2 > i0 ? i2 : i0) : (i1 > i0 ? i1 : i0)) + 1u) * sizeof(vk_vertex2d_impl_t);
    if (cmd->vertex_buffer_offset + needed > cmd->vertex_buffer->memory->size) {
        return;
    }

    image = (vk_image_impl_t *)(uintptr_t)cmd->bound_image;
    if (image == 0) {
        return;
    }

    vertices = (const vk_vertex2d_impl_t *)(const void *)(cmd->vertex_buffer->memory->bytes + cmd->vertex_buffer->memory_offset + cmd->vertex_buffer_offset);
    rasterize_triangle(image, &vertices[i0], &vertices[i1], &vertices[i2], &cmd->scissor);
}

static void copy_image_nearest(vk_image_impl_t *src, vk_image_impl_t *dst) {
    const uint32_t *src_pixels = vk_image_pixels(src);
    uint32_t *dst_pixels = vk_image_pixels(dst);

    if (src_pixels == 0 || dst_pixels == 0 || src->width == 0u || src->height == 0u || dst->width == 0u || dst->height == 0u) {
        return;
    }

    for (uint32_t y = 0u; y < dst->height; ++y) {
        uint32_t src_y = (uint32_t)(((uint64_t)y * src->height) / dst->height);
        uint32_t *dst_row = dst_pixels + (uint64_t)y * dst->width;
        const uint32_t *src_row = src_pixels + (uint64_t)src_y * src->width;
        for (uint32_t x = 0u; x < dst->width; ++x) {
            uint32_t src_x = (uint32_t)(((uint64_t)x * src->width) / dst->width);
            dst_row[x] = src_row[src_x];
        }
    }
}

VkResult vkCreateInstance(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkInstance *pInstance) {
    vk_instance_impl_t *instance;

    (void)pCreateInfo;
    (void)pAllocator;
    if (pInstance == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    instance = (vk_instance_impl_t *)vk_heap_alloc(sizeof(*instance));
    if (instance == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    if (vexvk_create_instance(&instance->inner) != 0 || vexvk_enumerate_physical_device(instance->inner, &instance->info) != 0) {
        vk_heap_free(instance);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *pInstance = (VkInstance)(uintptr_t)instance;
    return VK_SUCCESS;
}

void vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator) {
    vk_instance_impl_t *impl = (vk_instance_impl_t *)(uintptr_t)instance;
    (void)pAllocator;
    if (impl == 0) {
        return;
    }
    vexvk_destroy_instance(impl->inner);
    vk_heap_free(impl);
}

VkResult vkCreateVexSurfaceKHR(VkInstance instance, const VkVexSurfaceCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface) {
    vk_instance_impl_t *impl = (vk_instance_impl_t *)(uintptr_t)instance;
    vk_surface_impl_t *surface;

    (void)pAllocator;
    if (impl == 0 || pCreateInfo == 0 || pSurface == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    surface = (vk_surface_impl_t *)vk_heap_alloc(sizeof(*surface));
    if (surface == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    surface->x = pCreateInfo->x;
    surface->y = pCreateInfo->y;
    surface->width = pCreateInfo->width;
    surface->height = pCreateInfo->height;
    *pSurface = (VkSurfaceKHR)(uintptr_t)surface;
    return VK_SUCCESS;
}

void vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks *pAllocator) {
    (void)instance;
    (void)pAllocator;
    vk_heap_free((void *)(uintptr_t)surface);
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
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    pPhysicalDevices[0] = (VkPhysicalDevice)(uintptr_t)impl;
    *pPhysicalDeviceCount = 1u;
    return VK_SUCCESS;
}

void vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties *pProperties) {
    vk_instance_impl_t *impl = (vk_instance_impl_t *)(uintptr_t)physicalDevice;
    const char *name = "VexOS Vulkan Device";
    uint32_t i = 0u;

    if (impl == 0 || pProperties == 0) {
        return;
    }
    pProperties->apiVersion = VK_API_VERSION_1_0;
    pProperties->driverVersion = 1u;
    pProperties->vendorID = impl->info.vendor_id;
    pProperties->deviceID = impl->info.device_id;
    pProperties->deviceType = 1u;
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
    pMemoryProperties->memoryHeaps[0].size = (VkDeviceSize)impl->info.framebuffer_width * (VkDeviceSize)impl->info.framebuffer_height * 16u;
    pMemoryProperties->memoryHeaps[0].flags = 0u;
}

void vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, uint32_t format, VkFormatProperties *pFormatProperties) {
    (void)physicalDevice;
    if (pFormatProperties == 0) {
        return;
    }
    memset(pFormatProperties, 0, sizeof(*pFormatProperties));
    if (format == VK_FORMAT_B8G8R8A8_UNORM) {
        pFormatProperties->linearTilingFeatures = 1u;
        pFormatProperties->optimalTilingFeatures = 1u;
        pFormatProperties->bufferFeatures = 1u;
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
    pQueueFamilyProperties[0].queueFlags = VK_QUEUE_GRAPHICS_BIT;
    pQueueFamilyProperties[0].queueCount = 1u;
    pQueueFamilyProperties[0].timestampValidBits = 0u;
    pQueueFamilyProperties[0].minImageTransferGranularity.width = 1u;
    pQueueFamilyProperties[0].minImageTransferGranularity.height = 1u;
    pQueueFamilyProperties[0].minImageTransferGranularity.depth = 1u;
    *pQueueFamilyPropertyCount = 1u;
}

VkResult vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32 *pSupported) {
    (void)physicalDevice;
    (void)queueFamilyIndex;
    if (surface == VK_NULL_HANDLE || pSupported == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *pSupported = VK_TRUE;
    return VK_SUCCESS;
}

VkResult vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR *pSurfaceCapabilities) {
    vk_instance_impl_t *impl = (vk_instance_impl_t *)(uintptr_t)physicalDevice;
    vk_surface_impl_t *surface_impl = (vk_surface_impl_t *)(uintptr_t)surface;

    if (impl == 0 || surface_impl == 0 || pSurfaceCapabilities == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    pSurfaceCapabilities->minImageCount = 2u;
    pSurfaceCapabilities->maxImageCount = 4u;
    pSurfaceCapabilities->currentExtent.width = surface_impl->width != 0u ? surface_impl->width : impl->info.framebuffer_width;
    pSurfaceCapabilities->currentExtent.height = surface_impl->height != 0u ? surface_impl->height : impl->info.framebuffer_height;
    pSurfaceCapabilities->minImageExtent.width = 64u;
    pSurfaceCapabilities->minImageExtent.height = 64u;
    pSurfaceCapabilities->maxImageExtent.width = impl->info.framebuffer_width;
    pSurfaceCapabilities->maxImageExtent.height = impl->info.framebuffer_height;
    pSurfaceCapabilities->maxImageArrayLayers = 1u;
    pSurfaceCapabilities->supportedTransforms = 1u;
    pSurfaceCapabilities->currentTransform = 0u;
    pSurfaceCapabilities->supportedCompositeAlpha = 1u;
    pSurfaceCapabilities->supportedUsageFlags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return VK_SUCCESS;
}

VkResult vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pSurfaceFormatCount, VkSurfaceFormatKHR *pSurfaceFormats) {
    (void)physicalDevice;
    (void)surface;
    if (pSurfaceFormatCount == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (pSurfaceFormats == 0) {
        *pSurfaceFormatCount = 1u;
        return VK_SUCCESS;
    }
    if (*pSurfaceFormatCount == 0u) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    pSurfaceFormats[0].format = VK_FORMAT_B8G8R8A8_UNORM;
    pSurfaceFormats[0].colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    *pSurfaceFormatCount = 1u;
    return VK_SUCCESS;
}

VkResult vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pPresentModeCount, uint32_t *pPresentModes) {
    (void)physicalDevice;
    (void)surface;
    if (pPresentModeCount == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (pPresentModes == 0) {
        *pPresentModeCount = 2u;
        return VK_SUCCESS;
    }
    if (*pPresentModeCount < 2u) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    pPresentModes[0] = VK_PRESENT_MODE_FIFO_KHR;
    pPresentModes[1] = VK_PRESENT_MODE_IMMEDIATE_KHR;
    *pPresentModeCount = 2u;
    return VK_SUCCESS;
}

VkResult vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDevice *pDevice) {
    vk_instance_impl_t *instance = (vk_instance_impl_t *)(uintptr_t)physicalDevice;
    vk_device_impl_t *device;

    (void)pCreateInfo;
    (void)pAllocator;
    if (instance == 0 || pDevice == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    device = (vk_device_impl_t *)vk_heap_alloc(sizeof(*device));
    if (device == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    if (vexvk_create_device(instance->inner, &device->inner) != 0) {
        vk_heap_free(device);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    device->instance = instance;
    *pDevice = (VkDevice)(uintptr_t)device;
    return VK_SUCCESS;
}

void vkDestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator) {
    vk_device_impl_t *impl = (vk_device_impl_t *)(uintptr_t)device;
    (void)pAllocator;
    if (impl == 0) {
        return;
    }
    vexvk_destroy_device(impl->inner);
    vk_heap_free(impl);
}

void vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue) {
    vk_queue_impl_t *queue;
    (void)queueFamilyIndex;
    (void)queueIndex;
    if (pQueue == 0 || device == VK_NULL_HANDLE) {
        return;
    }
    queue = (vk_queue_impl_t *)vk_heap_alloc(sizeof(*queue));
    if (queue == 0) {
        *pQueue = VK_NULL_HANDLE;
        return;
    }
    queue->device = (vk_device_impl_t *)(uintptr_t)device;
    *pQueue = (VkQueue)(uintptr_t)queue;
}

VkResult vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain) {
    vk_swapchain_impl_t *swapchain;
    vk_surface_impl_t *surface;
    uint32_t image_count;

    (void)pAllocator;
    if (device == VK_NULL_HANDLE || pCreateInfo == 0 || pSwapchain == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    swapchain = (vk_swapchain_impl_t *)vk_heap_alloc(sizeof(*swapchain));
    if (swapchain == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    swapchain->device = (vk_device_impl_t *)(uintptr_t)device;
    surface = (vk_surface_impl_t *)(uintptr_t)pCreateInfo->surface;
    image_count = pCreateInfo->minImageCount < 2u ? 2u : pCreateInfo->minImageCount;
    if (surface != 0) {
        swapchain->present_x = surface->x;
        swapchain->present_y = surface->y;
    } else {
        swapchain->present_x = 0;
        swapchain->present_y = 0;
    }
    if (surface == 0 && swapchain->device->instance->info.framebuffer_width > pCreateInfo->imageExtent.width) {
        swapchain->present_x = (int32_t)((swapchain->device->instance->info.framebuffer_width - pCreateInfo->imageExtent.width) / 2u);
    }
    if (surface == 0 && swapchain->device->instance->info.framebuffer_height > pCreateInfo->imageExtent.height) {
        swapchain->present_y = (int32_t)((swapchain->device->instance->info.framebuffer_height - pCreateInfo->imageExtent.height) / 2u);
    }
    if (surface != 0 && surface->width != 0u && surface->height != 0u) {
        if (pCreateInfo->imageExtent.width > surface->width || pCreateInfo->imageExtent.height > surface->height) {
            vk_heap_free(swapchain);
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    } else if (swapchain->device->instance->info.framebuffer_width > pCreateInfo->imageExtent.width) {
        swapchain->present_x = (int32_t)((swapchain->device->instance->info.framebuffer_width - pCreateInfo->imageExtent.width) / 2u);
    } else {
        swapchain->present_x = 0;
    }
    if (surface == 0 && swapchain->device->instance->info.framebuffer_height > pCreateInfo->imageExtent.height) {
        swapchain->present_y = (int32_t)((swapchain->device->instance->info.framebuffer_height - pCreateInfo->imageExtent.height) / 2u);
    } else {
        swapchain->present_y = surface != 0 ? surface->y : 0;
    }
    if (vexvk_create_swapchain(swapchain->device->inner, pCreateInfo->imageExtent.width, pCreateInfo->imageExtent.height, image_count, &swapchain->inner) != 0) {
        vk_heap_free(swapchain);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    swapchain->image_count = image_count;
    swapchain->images = (VkImage *)vk_heap_alloc((size_t)image_count * sizeof(VkImage));
    if (swapchain->images == 0) {
        vexvk_destroy_swapchain(swapchain->inner);
        vk_heap_free(swapchain);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    for (uint32_t i = 0u; i < image_count; ++i) {
        vk_image_impl_t *image = (vk_image_impl_t *)vk_heap_alloc(sizeof(*image));
        if (image == 0) {
            for (uint32_t j = 0u; j < i; ++j) {
                vk_heap_free((void *)(uintptr_t)swapchain->images[j]);
            }
            vk_heap_free(swapchain->images);
            vexvk_destroy_swapchain(swapchain->inner);
            vk_heap_free(swapchain);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        image->swapchain = swapchain;
        image->index = i;
        image->width = pCreateInfo->imageExtent.width;
        image->height = pCreateInfo->imageExtent.height;
        image->format = pCreateInfo->imageFormat;
        image->layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        image->memory = 0;
        image->memory_offset = 0u;
        image->owned_pixels = (uint32_t *)vk_heap_alloc((size_t)image->width * (size_t)image->height * sizeof(uint32_t));
        image->owned_stride = image->width;
        if (image->owned_pixels == 0) {
            for (uint32_t j = 0u; j < i; ++j) {
                vk_image_impl_t *prev = (vk_image_impl_t *)(uintptr_t)swapchain->images[j];
                if (prev != 0) {
                    vk_heap_free(prev->owned_pixels);
                    vk_heap_free(prev);
                }
            }
            vk_heap_free(image);
            vk_heap_free(swapchain->images);
            vexvk_destroy_swapchain(swapchain->inner);
            vk_heap_free(swapchain);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        memset(image->owned_pixels, 0, (size_t)image->width * (size_t)image->height * sizeof(uint32_t));
        swapchain->images[i] = (VkImage)(uintptr_t)image;
    }
    *pSwapchain = (VkSwapchainKHR)(uintptr_t)swapchain;
    return VK_SUCCESS;
}

void vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *pAllocator) {
    vk_swapchain_impl_t *impl = (vk_swapchain_impl_t *)(uintptr_t)swapchain;
    (void)device;
    (void)pAllocator;
    if (impl == 0) {
        return;
    }
    if (impl->images != 0) {
        for (uint32_t i = 0u; i < impl->image_count; ++i) {
            vk_image_impl_t *image = (vk_image_impl_t *)(uintptr_t)impl->images[i];
            if (image != 0) {
                vk_heap_free(image->owned_pixels);
                vk_heap_free(image);
            }
        }
    }
    vk_heap_free(impl->images);
    vexvk_destroy_swapchain(impl->inner);
    vk_heap_free(impl);
}

VkResult vkGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t *pSwapchainImageCount, VkImage *pSwapchainImages) {
    vk_swapchain_impl_t *impl = (vk_swapchain_impl_t *)(uintptr_t)swapchain;
    (void)device;
    if (impl == 0 || pSwapchainImageCount == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (pSwapchainImages == 0) {
        *pSwapchainImageCount = impl->image_count;
        return VK_SUCCESS;
    }
    if (*pSwapchainImageCount < impl->image_count) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (uint32_t i = 0u; i < impl->image_count; ++i) {
        pSwapchainImages[i] = impl->images[i];
    }
    *pSwapchainImageCount = impl->image_count;
    return VK_SUCCESS;
}

VkResult vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex) {
    vk_swapchain_impl_t *impl = (vk_swapchain_impl_t *)(uintptr_t)swapchain;
    vk_semaphore_impl_t *semaphore_impl = (vk_semaphore_impl_t *)(uintptr_t)semaphore;
    vk_fence_impl_t *fence_impl = (vk_fence_impl_t *)(uintptr_t)fence;
    (void)device;
    (void)timeout;
    if (impl == 0 || pImageIndex == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (vexvk_acquire_next_image(impl->inner, pImageIndex) != 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (semaphore_impl != 0) {
        semaphore_impl->signaled = VK_TRUE;
    }
    if (fence_impl != 0) {
        fence_impl->signaled = VK_TRUE;
    }
    return VK_SUCCESS;
}

VkResult vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool) {
    vk_command_pool_impl_t *pool;
    (void)pCreateInfo;
    (void)pAllocator;
    if (device == VK_NULL_HANDLE || pCommandPool == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    pool = (vk_command_pool_impl_t *)vk_heap_alloc(sizeof(*pool));
    if (pool == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    pool->device = (vk_device_impl_t *)(uintptr_t)device;
    *pCommandPool = (VkCommandPool)(uintptr_t)pool;
    return VK_SUCCESS;
}

void vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks *pAllocator) {
    (void)device;
    (void)pAllocator;
    vk_heap_free((void *)(uintptr_t)commandPool);
}

VkResult vkResetCommandPool(VkDevice device, VkCommandPool commandPool, VkFlags flags) {
    (void)device;
    (void)flags;
    return commandPool == VK_NULL_HANDLE ? VK_ERROR_INITIALIZATION_FAILED : VK_SUCCESS;
}

VkResult vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo, VkCommandBuffer *pCommandBuffers) {
    vk_command_pool_impl_t *pool = (vk_command_pool_impl_t *)(uintptr_t)pAllocateInfo->commandPool;
    (void)device;
    if (pool == 0 || pAllocateInfo == 0 || pCommandBuffers == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (uint32_t i = 0u; i < pAllocateInfo->commandBufferCount; ++i) {
        vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)vk_heap_alloc(sizeof(*cmd));
        if (cmd == 0) {
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        if (vexvk_create_command_buffer(pool->device->inner, &cmd->inner) != 0) {
            vk_heap_free(cmd);
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        cmd->device = pool->device;
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
        pCommandBuffers[i] = (VkCommandBuffer)(uintptr_t)cmd;
    }
    return VK_SUCCESS;
}

void vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers) {
    (void)device;
    (void)commandPool;
    if (pCommandBuffers == 0) {
        return;
    }
    for (uint32_t i = 0u; i < commandBufferCount; ++i) {
        vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)pCommandBuffers[i];
        if (cmd == 0) {
            continue;
        }
        vexvk_destroy_command_buffer(cmd->inner);
        vk_heap_free(cmd);
    }
}

VkResult vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    (void)pBeginInfo;
    if (cmd == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    vexvk_cmd_reset(cmd->inner);
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
        return VK_SUCCESS;
    }

VkResult vkEndCommandBuffer(VkCommandBuffer commandBuffer) {
    return commandBuffer == VK_NULL_HANDLE ? VK_ERROR_INITIALIZATION_FAILED : VK_SUCCESS;
}

VkResult vkResetCommandBuffer(VkCommandBuffer commandBuffer, VkFlags flags) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    (void)flags;
    if (cmd == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    vexvk_cmd_reset(cmd->inner);
    cmd->bound_image = 0;
    cmd->framebuffer = 0;
    cmd->pipeline = 0;
    cmd->vertex_buffer = 0;
    cmd->vertex_buffer_offset = 0u;
    cmd->index_buffer = 0;
    cmd->index_buffer_offset = 0u;
    cmd->index_type = VK_INDEX_TYPE_UINT32;
    return VK_SUCCESS;
}

void vkCmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, uint32_t imageLayout, const VkClearColorValue *pColor, uint32_t rangeCount, const VkImageSubresourceRange *pRanges) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    (void)imageLayout;
    (void)rangeCount;
    (void)pRanges;
    if (cmd == 0) {
        return;
    }
    cmd->bound_image = image;
    {
        vk_image_impl_t *image_impl = (vk_image_impl_t *)(uintptr_t)image;
        if (image_impl != 0) {
            fill_image_rect(image_impl, 0, 0, (int32_t)image_impl->width, (int32_t)image_impl->height, pack_rgba(pColor));
        }
    }
}

VkResult vkCreateBuffer(VkDevice device, const VkBufferCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer) {
    vk_buffer_impl_t *buffer;
    (void)device;
    (void)pAllocator;
    if (pCreateInfo == 0 || pBuffer == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    buffer = (vk_buffer_impl_t *)vk_heap_alloc(sizeof(*buffer));
    if (buffer == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    buffer->size = pCreateInfo->size;
    buffer->usage = pCreateInfo->usage;
    buffer->memory = 0;
    buffer->memory_offset = 0u;
    *pBuffer = (VkBuffer)(uintptr_t)buffer;
    return VK_SUCCESS;
}

void vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *pAllocator) {
    (void)device;
    (void)pAllocator;
    vk_heap_free((void *)(uintptr_t)buffer);
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
    vk_memory_impl_t *memory;
    (void)device;
    (void)pAllocator;
    if (pAllocateInfo == 0 || pMemory == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    memory = (vk_memory_impl_t *)vk_heap_alloc(sizeof(*memory));
    if (memory == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memory->bytes = (uint8_t *)vk_heap_alloc((size_t)pAllocateInfo->allocationSize);
    if (memory->bytes == 0) {
        vk_heap_free(memory);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memory->size = pAllocateInfo->allocationSize;
    *pMemory = (VkDeviceMemory)(uintptr_t)memory;
    return VK_SUCCESS;
}

void vkFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks *pAllocator) {
    vk_memory_impl_t *impl = (vk_memory_impl_t *)(uintptr_t)memory;
    (void)device;
    (void)pAllocator;
    if (impl == 0) {
        return;
    }
    vk_heap_free(impl->bytes);
    vk_heap_free(impl);
}

VkResult vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset) {
    vk_buffer_impl_t *buffer_impl = (vk_buffer_impl_t *)(uintptr_t)buffer;
    vk_memory_impl_t *memory_impl = (vk_memory_impl_t *)(uintptr_t)memory;
    (void)device;
    if (buffer_impl == 0 || memory_impl == 0 || memoryOffset > memory_impl->size || buffer_impl->size > memory_impl->size - memoryOffset) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    buffer_impl->memory = memory_impl;
    buffer_impl->memory_offset = memoryOffset;
    return VK_SUCCESS;
}

VkResult vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkFlags flags, void **ppData) {
    vk_memory_impl_t *impl = (vk_memory_impl_t *)(uintptr_t)memory;
    (void)device;
    (void)flags;
    if (impl == 0 || ppData == 0 || offset > impl->size || size > impl->size - offset) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *ppData = impl->bytes + offset;
    return VK_SUCCESS;
}

void vkUnmapMemory(VkDevice device, VkDeviceMemory memory) {
    (void)device;
    (void)memory;
}

VkResult vkCreateImage(VkDevice device, const VkImageCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkImage *pImage) {
    vk_image_impl_t *image;
    (void)device;
    (void)pAllocator;
    if (pCreateInfo == 0 || pImage == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    image = (vk_image_impl_t *)vk_heap_alloc(sizeof(*image));
    if (image == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    image->swapchain = 0;
    image->index = 0u;
    image->width = pCreateInfo->extent.width;
    image->height = pCreateInfo->extent.height;
    image->format = pCreateInfo->format;
    image->layout = pCreateInfo->initialLayout;
    image->memory = 0;
    image->memory_offset = 0u;
    image->owned_pixels = 0;
    image->owned_stride = 0u;
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
    if (impl->swapchain == 0) {
        vk_heap_free(impl);
    }
}

void vkGetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements *pMemoryRequirements) {
    vk_image_impl_t *impl = (vk_image_impl_t *)(uintptr_t)image;
    (void)device;
    if (impl == 0 || pMemoryRequirements == 0) {
        return;
    }
    pMemoryRequirements->size = (VkDeviceSize)impl->width * (VkDeviceSize)impl->height * 4u;
    pMemoryRequirements->alignment = 4u;
    pMemoryRequirements->memoryTypeBits = 1u;
}

VkResult vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset) {
    vk_image_impl_t *image_impl = (vk_image_impl_t *)(uintptr_t)image;
    vk_memory_impl_t *memory_impl = (vk_memory_impl_t *)(uintptr_t)memory;
    VkDeviceSize needed;
    (void)device;
    if (image_impl == 0 || memory_impl == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    needed = (VkDeviceSize)image_impl->width * (VkDeviceSize)image_impl->height * 4u;
    if (memoryOffset > memory_impl->size || needed > memory_impl->size - memoryOffset) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    image_impl->memory = memory_impl;
    image_impl->memory_offset = memoryOffset;
    return VK_SUCCESS;
}

VkResult vkCreateImageView(VkDevice device, const VkImageViewCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkImageView *pView) {
    vk_image_view_impl_t *view;
    (void)device;
    (void)pAllocator;
    if (pCreateInfo == 0 || pView == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    view = (vk_image_view_impl_t *)vk_heap_alloc(sizeof(*view));
    if (view == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    view->image = (vk_image_impl_t *)(uintptr_t)pCreateInfo->image;
    *pView = (VkImageView)(uintptr_t)view;
    return VK_SUCCESS;
}

void vkDestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks *pAllocator) {
    (void)device;
    (void)pAllocator;
    vk_heap_free((void *)(uintptr_t)imageView);
}

VkResult vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkShaderModule *pShaderModule) {
    vk_shader_module_impl_t *shader;
    (void)device;
    (void)pAllocator;
    if (pCreateInfo == 0 || pShaderModule == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    shader = (vk_shader_module_impl_t *)vk_heap_alloc(sizeof(*shader));
    if (shader == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    shader->code_size = pCreateInfo->codeSize;
    *pShaderModule = (VkShaderModule)(uintptr_t)shader;
    return VK_SUCCESS;
}

void vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks *pAllocator) {
    (void)device;
    (void)pAllocator;
    vk_heap_free((void *)(uintptr_t)shaderModule);
}

VkResult vkCreateRenderPass(VkDevice device, const VkRenderPassCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass) {
    vk_render_pass_impl_t *render_pass;
    (void)device;
    (void)pAllocator;
    if (pCreateInfo == 0 || pRenderPass == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    render_pass = (vk_render_pass_impl_t *)vk_heap_alloc(sizeof(*render_pass));
    if (render_pass == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    render_pass->color_format = (pCreateInfo->attachmentCount != 0u && pCreateInfo->pAttachments != 0)
        ? pCreateInfo->pAttachments[0].format : VK_FORMAT_B8G8R8A8_UNORM;
    *pRenderPass = (VkRenderPass)(uintptr_t)render_pass;
    return VK_SUCCESS;
}

void vkDestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks *pAllocator) {
    (void)device;
    (void)pAllocator;
    vk_heap_free((void *)(uintptr_t)renderPass);
}

VkResult vkCreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkFramebuffer *pFramebuffer) {
    vk_framebuffer_impl_t *framebuffer;
    (void)device;
    (void)pAllocator;
    if (pCreateInfo == 0 || pFramebuffer == 0 || pCreateInfo->attachmentCount == 0u || pCreateInfo->pAttachments == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    framebuffer = (vk_framebuffer_impl_t *)vk_heap_alloc(sizeof(*framebuffer));
    if (framebuffer == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    framebuffer->render_pass = (vk_render_pass_impl_t *)(uintptr_t)pCreateInfo->renderPass;
    framebuffer->attachment0 = (vk_image_view_impl_t *)(uintptr_t)pCreateInfo->pAttachments[0];
    framebuffer->width = pCreateInfo->width;
    framebuffer->height = pCreateInfo->height;
    *pFramebuffer = (VkFramebuffer)(uintptr_t)framebuffer;
    return VK_SUCCESS;
}

void vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks *pAllocator) {
    (void)device;
    (void)pAllocator;
    vk_heap_free((void *)(uintptr_t)framebuffer);
}

VkResult vkCreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkPipelineLayout *pPipelineLayout) {
    vk_pipeline_layout_impl_t *layout;
    (void)device;
    (void)pAllocator;
    if (pPipelineLayout == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    layout = (vk_pipeline_layout_impl_t *)vk_heap_alloc(sizeof(*layout));
    if (layout == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    layout->push_constant_range_count = pCreateInfo == 0 ? 0u : pCreateInfo->pushConstantRangeCount;
    *pPipelineLayout = (VkPipelineLayout)(uintptr_t)layout;
    return VK_SUCCESS;
}

void vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks *pAllocator) {
    (void)device;
    (void)pAllocator;
    vk_heap_free((void *)(uintptr_t)pipelineLayout);
}

VkResult vkCreateGraphicsPipelines(VkDevice device, VkPipeline pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo *pCreateInfos, const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines) {
    (void)device;
    (void)pipelineCache;
    (void)pAllocator;
    if (createInfoCount == 0u || pCreateInfos == 0 || pPipelines == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (uint32_t i = 0u; i < createInfoCount; ++i) {
        vk_pipeline_impl_t *pipeline = (vk_pipeline_impl_t *)vk_heap_alloc(sizeof(*pipeline));
        if (pipeline == 0) {
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        pipeline->layout = (vk_pipeline_layout_impl_t *)(uintptr_t)pCreateInfos[i].layout;
        pipeline->render_pass = (vk_render_pass_impl_t *)(uintptr_t)pCreateInfos[i].renderPass;
        pPipelines[i] = (VkPipeline)(uintptr_t)pipeline;
    }
    return VK_SUCCESS;
}

void vkDestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks *pAllocator) {
    (void)device;
    (void)pAllocator;
    vk_heap_free((void *)(uintptr_t)pipeline);
}

void vkCmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin, uint32_t contents) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    (void)contents;
    if (cmd == 0 || pRenderPassBegin == 0) {
        return;
    }
    cmd->framebuffer = (vk_framebuffer_impl_t *)(uintptr_t)pRenderPassBegin->framebuffer;
    if (cmd->framebuffer != 0 && cmd->framebuffer->attachment0 != 0 && cmd->framebuffer->attachment0->image != 0) {
        cmd->bound_image = (VkImage)(uintptr_t)cmd->framebuffer->attachment0->image;
    }
}

void vkCmdEndRenderPass(VkCommandBuffer commandBuffer) {
    (void)commandBuffer;
}

void vkCmdBindPipeline(VkCommandBuffer commandBuffer, uint32_t pipelineBindPoint, VkPipeline pipeline) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    (void)pipelineBindPoint;
    if (cmd != 0) {
        cmd->pipeline = (vk_pipeline_impl_t *)(uintptr_t)pipeline;
    }
}

void vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    (void)firstBinding;
    if (cmd == 0 || bindingCount == 0u || pBuffers == 0) {
        return;
    }
    cmd->vertex_buffer = (vk_buffer_impl_t *)(uintptr_t)pBuffers[0];
    cmd->vertex_buffer_offset = pOffsets == 0 ? 0u : pOffsets[0];
}

void vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    if (cmd == 0) {
        return;
    }
    cmd->index_buffer = (vk_buffer_impl_t *)(uintptr_t)buffer;
    cmd->index_buffer_offset = offset;
    cmd->index_type = indexType;
}

void vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport *pViewports) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    (void)firstViewport;
    if (cmd != 0 && viewportCount != 0u && pViewports != 0) {
        cmd->viewport = pViewports[0];
    }
}

void vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D *pScissors) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    (void)firstScissor;
    if (cmd != 0 && scissorCount != 0u && pScissors != 0) {
        cmd->scissor = pScissors[0];
    }
}

void vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    (void)instanceCount;
    (void)firstInstance;
    if (cmd == 0 || cmd->bound_image == 0 || cmd->pipeline == 0 || vertexCount == 0u) {
        return;
    }
    if (vertexCount >= 3u) {
        for (uint32_t i = 0u; i + 2u < vertexCount; i += 3u) {
            draw_triangle_from_vertices(cmd, firstVertex + i, firstVertex + i + 1u, firstVertex + i + 2u);
        }
    }
}

void vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    const uint32_t *indices;
    (void)instanceCount;
    (void)firstInstance;
    if (cmd == 0 || cmd->index_buffer == 0 || cmd->index_buffer->memory == 0 || cmd->index_type != VK_INDEX_TYPE_UINT32) {
        return;
    }
    if (cmd->index_buffer_offset + ((VkDeviceSize)firstIndex + indexCount) * sizeof(uint32_t) > cmd->index_buffer->memory->size) {
        return;
    }
    indices = (const uint32_t *)(const void *)(cmd->index_buffer->memory->bytes + cmd->index_buffer->memory_offset + cmd->index_buffer_offset);
    for (uint32_t i = 0u; i + 2u < indexCount; i += 3u) {
        draw_triangle_from_vertices(
            cmd,
            indices[firstIndex + i] + (uint32_t)vertexOffset,
            indices[firstIndex + i + 1u] + (uint32_t)vertexOffset,
            indices[firstIndex + i + 2u] + (uint32_t)vertexOffset
        );
    }
}

void vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer buffer, VkImage image, uint32_t imageLayout, uint32_t regionCount, const VkBufferImageCopy *pRegions) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    vk_buffer_impl_t *buffer_impl = (vk_buffer_impl_t *)(uintptr_t)buffer;
    vk_image_impl_t *image_impl = (vk_image_impl_t *)(uintptr_t)image;
    const VkBufferImageCopy *region;
    const uint32_t *pixels;
    uint32_t row_length;
    uint32_t copy_height;
    uint32_t *dst_pixels;
    uint32_t row;
    uint32_t col;

    (void)imageLayout;
    if (cmd == 0 || buffer_impl == 0 || image_impl == 0 || buffer_impl->memory == 0 || pRegions == 0 || regionCount == 0u) {
        return;
    }
    region = &pRegions[0];
    row_length = region->bufferRowLength == 0u ? region->imageExtent.width : region->bufferRowLength;
    copy_height = region->bufferImageHeight == 0u ? region->imageExtent.height : region->bufferImageHeight;
    pixels = (const uint32_t *)(const void *)(buffer_impl->memory->bytes + buffer_impl->memory_offset + region->bufferOffset);
    dst_pixels = vk_image_pixels(image_impl);
    if (dst_pixels == 0) {
        return;
    }
    for (row = 0u; row < region->imageExtent.height && row < copy_height; ++row) {
        uint32_t *dst = dst_pixels + (uint64_t)(region->imageOffset.y + (int32_t)row) * image_impl->width + (uint32_t)region->imageOffset.x;
        const uint32_t *src = pixels + (uint64_t)row * row_length;
        for (col = 0u; col < region->imageExtent.width; ++col) {
            dst[col] = src[col];
        }
    }
    cmd->bound_image = image;
}

void vkCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, uint32_t srcImageLayout, VkImage dstImage, uint32_t dstImageLayout, uint32_t regionCount, const void *pRegions) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    vk_image_impl_t *src = (vk_image_impl_t *)(uintptr_t)srcImage;
    vk_image_impl_t *dst = (vk_image_impl_t *)(uintptr_t)dstImage;

    (void)srcImageLayout;
    (void)dstImageLayout;
    (void)regionCount;
    (void)pRegions;
    if (cmd == 0 || src == 0 || dst == 0) {
        return;
    }
    copy_image_nearest(src, dst);
    cmd->bound_image = dstImage;
}

void vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, uint32_t srcImageLayout, VkImage dstImage, uint32_t dstImageLayout, uint32_t regionCount, const void *pRegions, uint32_t filter) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    vk_image_impl_t *src = (vk_image_impl_t *)(uintptr_t)srcImage;
    vk_image_impl_t *dst = (vk_image_impl_t *)(uintptr_t)dstImage;

    (void)srcImageLayout;
    (void)dstImageLayout;
    (void)regionCount;
    (void)pRegions;
    (void)filter;
    if (cmd == 0 || src == 0 || dst == 0) {
        return;
    }
    copy_image_nearest(src, dst);
    cmd->bound_image = dstImage;
}

void vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkFlags srcStageMask, VkFlags dstStageMask, VkFlags dependencyFlags, uint32_t memoryBarrierCount, const void *pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const void *pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers) {
    (void)commandBuffer;
    (void)srcStageMask;
    (void)dstStageMask;
    (void)dependencyFlags;
    (void)memoryBarrierCount;
    (void)pMemoryBarriers;
    (void)bufferMemoryBarrierCount;
    (void)pBufferMemoryBarriers;
    if (pImageMemoryBarriers == 0) {
        return;
    }
    for (uint32_t i = 0u; i < imageMemoryBarrierCount; ++i) {
        vk_image_impl_t *image = (vk_image_impl_t *)(uintptr_t)pImageMemoryBarriers[i].image;
        if (image != 0) {
            image->layout = pImageMemoryBarriers[i].newLayout;
        }
    }
}

VkResult vkCreateFence(VkDevice device, const VkFenceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkFence *pFence) {
    vk_fence_impl_t *fence;
    (void)device;
    (void)pAllocator;
    if (pFence == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    fence = (vk_fence_impl_t *)vk_heap_alloc(sizeof(*fence));
    if (fence == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    fence->signaled = (pCreateInfo != 0 && (pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT) != 0u) ? VK_TRUE : VK_FALSE;
    *pFence = (VkFence)(uintptr_t)fence;
    return VK_SUCCESS;
}

void vkDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks *pAllocator) {
    (void)device;
    (void)pAllocator;
    vk_heap_free((void *)(uintptr_t)fence);
}

VkResult vkResetFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences) {
    (void)device;
    if (pFences == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (uint32_t i = 0u; i < fenceCount; ++i) {
        vk_fence_impl_t *fence = (vk_fence_impl_t *)(uintptr_t)pFences[i];
        if (fence != 0) {
            fence->signaled = VK_FALSE;
        }
    }
    return VK_SUCCESS;
}

VkResult vkWaitForFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences, VkBool32 waitAll, uint64_t timeout) {
    (void)device;
    if (pFences == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    {
        const uint64_t start = vk_now_ns();
        const uint64_t deadline = timeout == 0u ? start : (timeout == ~(uint64_t)0 ? ~(uint64_t)0 : start + timeout);
        for (;;) {
            uint32_t satisfied = 0u;

            for (uint32_t i = 0u; i < fenceCount; ++i) {
                vk_fence_impl_t *fence = (vk_fence_impl_t *)(uintptr_t)pFences[i];
                if (fence != 0 && fence->signaled != VK_FALSE) {
                    ++satisfied;
                } else if (!waitAll) {
                    /* keep scanning other fences */
                }
            }

            if ((waitAll && satisfied == fenceCount) || (!waitAll && satisfied != 0u)) {
                return VK_SUCCESS;
            }
            if (timeout == 0u) {
                return VK_NOT_READY;
            }
            if (deadline != ~(uint64_t)0 && vk_now_ns() >= deadline) {
                return VK_TIMEOUT;
            }
            vk_process_yield();
        }
    }
}

VkResult vkCreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSemaphore *pSemaphore) {
    vk_semaphore_impl_t *semaphore;
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;
    if (pSemaphore == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    semaphore = (vk_semaphore_impl_t *)vk_heap_alloc(sizeof(*semaphore));
    if (semaphore == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    semaphore->signaled = VK_FALSE;
    *pSemaphore = (VkSemaphore)(uintptr_t)semaphore;
    return VK_SUCCESS;
}

void vkDestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks *pAllocator) {
    (void)device;
    (void)pAllocator;
    vk_heap_free((void *)(uintptr_t)semaphore);
}

VkResult vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits, VkFence fence) {
    vk_queue_impl_t *impl = (vk_queue_impl_t *)(uintptr_t)queue;
    vk_fence_impl_t *fence_impl = (vk_fence_impl_t *)(uintptr_t)fence;
    if (impl == 0 || submitCount == 0u || pSubmits == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (fence_impl != 0) {
        fence_impl->signaled = VK_FALSE;
    }
    for (uint32_t i = 0u; i < submitCount; ++i) {
        const VkSubmitInfo *submit = &pSubmits[i];
        for (uint32_t w = 0u; w < submit->waitSemaphoreCount; ++w) {
            vk_semaphore_impl_t *semaphore = (vk_semaphore_impl_t *)(uintptr_t)submit->pWaitSemaphores[w];
            if (semaphore == 0 || semaphore->signaled == VK_FALSE) {
                return VK_NOT_READY;
            }
            semaphore->signaled = VK_FALSE;
        }
        for (uint32_t s = 0u; s < submit->signalSemaphoreCount; ++s) {
            vk_semaphore_impl_t *semaphore = (vk_semaphore_impl_t *)(uintptr_t)submit->pSignalSemaphores[s];
            if (semaphore != 0) {
                semaphore->signaled = VK_TRUE;
            }
        }
        for (uint32_t j = 0u; j < submit->commandBufferCount; ++j) {
            vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)submit->pCommandBuffers[j];
            vk_image_impl_t *image;
            uint32_t *pixels;

            if (cmd == 0 || cmd->bound_image == 0) {
                continue;
            }
            image = (vk_image_impl_t *)(uintptr_t)cmd->bound_image;
            if (image == 0 || image->swapchain == 0) {
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            pixels = vk_image_pixels(image);
            if (pixels != 0) {
                if (vexvk_cmd_blit(cmd->inner,
                    0,
                    0,
                    (int32_t)image->width,
                    (int32_t)image->height,
                    pixels,
                    image->width,
                    image->height,
                    image->width) != 0) {
                    return VK_ERROR_DEVICE_LOST;
                }
            }
            if (vexvk_queue_submit(impl->device->inner, image->swapchain->inner, image->index, cmd->inner) != 0) {
                return VK_ERROR_DEVICE_LOST;
            }
        }
    }
    if (fence_impl != 0) {
        fence_impl->signaled = VK_TRUE;
    }
    return VK_SUCCESS;
}

VkResult vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo) {
    vk_queue_impl_t *impl = (vk_queue_impl_t *)(uintptr_t)queue;
    if (impl == 0 || pPresentInfo == 0 || pPresentInfo->swapchainCount == 0u) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (uint32_t wait = 0u; wait < pPresentInfo->waitSemaphoreCount; ++wait) {
        vk_semaphore_impl_t *semaphore = (vk_semaphore_impl_t *)(uintptr_t)pPresentInfo->pWaitSemaphores[wait];
        if (semaphore == 0 || semaphore->signaled == VK_FALSE) {
            return VK_NOT_READY;
        }
        semaphore->signaled = VK_FALSE;
    }
    for (uint32_t i = 0u; i < pPresentInfo->swapchainCount; ++i) {
        vk_swapchain_impl_t *swapchain = (vk_swapchain_impl_t *)(uintptr_t)pPresentInfo->pSwapchains[i];
        uint32_t image_index;
        if (swapchain == 0 || pPresentInfo->pImageIndices == 0) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        image_index = pPresentInfo->pImageIndices[i];
        if (vexvk_queue_present(impl->device->inner, swapchain->inner, image_index, swapchain->present_x, swapchain->present_y) != 0) {
            return VK_ERROR_DEVICE_LOST;
        }
        if (pPresentInfo->pResults != 0) {
            pPresentInfo->pResults[i] = VK_SUCCESS;
        }
    }
    return VK_SUCCESS;
}

VkResult vkQueueWaitIdle(VkQueue queue) {
    if (queue == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    vk_process_yield();
    return VK_SUCCESS;
}

VkResult vkDeviceWaitIdle(VkDevice device) {
    if (device == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    vk_process_yield();
    return VK_SUCCESS;
}

/* ── Descriptor set layout ───────────────────────────────────────────────── */

VkResult vkCreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDescriptorSetLayout *pSetLayout) {
    vk_descriptor_set_layout_impl_t *impl;
    (void)device;
    (void)pAllocator;
    if (pSetLayout == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    impl = (vk_descriptor_set_layout_impl_t *)vk_heap_alloc(sizeof(*impl));
    if (impl == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    impl->binding_count = pCreateInfo != 0 ? pCreateInfo->bindingCount : 0u;
    impl->reserved = 0u;
    *pSetLayout = (VkDescriptorSetLayout)(uintptr_t)impl;
    return VK_SUCCESS;
}

void vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks *pAllocator) {
    (void)device;
    (void)pAllocator;
    vk_heap_free((void *)(uintptr_t)descriptorSetLayout);
}

/* ── Descriptor pool ─────────────────────────────────────────────────────── */

VkResult vkCreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDescriptorPool *pDescriptorPool) {
    vk_descriptor_pool_impl_t *impl;
    (void)device;
    (void)pAllocator;
    if (pDescriptorPool == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    impl = (vk_descriptor_pool_impl_t *)vk_heap_alloc(sizeof(*impl));
    if (impl == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    impl->max_sets = pCreateInfo != 0 ? pCreateInfo->maxSets : 0u;
    impl->allocated = 0u;
    *pDescriptorPool = (VkDescriptorPool)(uintptr_t)impl;
    return VK_SUCCESS;
}

void vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks *pAllocator) {
    (void)device;
    (void)pAllocator;
    vk_heap_free((void *)(uintptr_t)descriptorPool);
}

VkResult vkResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkFlags flags) {
    vk_descriptor_pool_impl_t *impl = (vk_descriptor_pool_impl_t *)(uintptr_t)descriptorPool;
    (void)device;
    (void)flags;
    if (impl != 0) {
        impl->allocated = 0u;
    }
    return VK_SUCCESS;
}

/* ── Descriptor set ──────────────────────────────────────────────────────── */

VkResult vkAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo *pAllocateInfo, VkDescriptorSet *pDescriptorSets) {
    vk_descriptor_pool_impl_t *pool;
    uint32_t i;
    (void)device;
    if (pAllocateInfo == 0 || pDescriptorSets == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    pool = (vk_descriptor_pool_impl_t *)(uintptr_t)pAllocateInfo->descriptorPool;
    for (i = 0u; i < pAllocateInfo->descriptorSetCount; ++i) {
        vk_descriptor_set_impl_t *set = (vk_descriptor_set_impl_t *)vk_heap_alloc(sizeof(*set));
        if (set == 0) {
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        set->pool = pool;
        if (pool != 0) {
            pool->allocated++;
        }
        pDescriptorSets[i] = (VkDescriptorSet)(uintptr_t)set;
    }
    return VK_SUCCESS;
}

VkResult vkFreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets) {
    uint32_t i;
    (void)device;
    (void)descriptorPool;
    if (pDescriptorSets == 0) {
        return VK_SUCCESS;
    }
    for (i = 0u; i < descriptorSetCount; ++i) {
        vk_descriptor_set_impl_t *set = (vk_descriptor_set_impl_t *)(uintptr_t)pDescriptorSets[i];
        if (set != 0 && set->pool != 0 && set->pool->allocated > 0u) {
            set->pool->allocated--;
        }
        vk_heap_free(set);
    }
    return VK_SUCCESS;
}

void vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet *pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet *pDescriptorCopies) {
    /* In a software renderer the descriptor bindings are no-ops: the shader
       emulation reads directly from memory via the vertex / uniform buffers. */
    (void)device;
    (void)descriptorWriteCount;
    (void)pDescriptorWrites;
    (void)descriptorCopyCount;
    (void)pDescriptorCopies;
}

void vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, uint32_t pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t *pDynamicOffsets) {
    /* Software path: binding is a no-op; actual data access is through vertex
       and uniform buffers already tracked in the command buffer. */
    (void)commandBuffer;
    (void)pipelineBindPoint;
    (void)layout;
    (void)firstSet;
    (void)descriptorSetCount;
    (void)pDescriptorSets;
    (void)dynamicOffsetCount;
    (void)pDynamicOffsets;
}

void vkCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkFlags stageFlags, uint32_t offset, uint32_t size, const void *pValues) {
    vk_command_buffer_impl_t *cmd = (vk_command_buffer_impl_t *)(uintptr_t)commandBuffer;
    uint32_t end;
    (void)layout;
    (void)stageFlags;
    if (cmd == 0 || pValues == 0 || size == 0u) {
        return;
    }
    end = offset + size;
    if (end > sizeof(cmd->push_constants)) {
        end = sizeof(cmd->push_constants);
        if (offset >= end) {
            return;
        }
        size = end - offset;
    }
    {
        const uint8_t *src = (const uint8_t *)pValues;
        uint32_t i;
        for (i = 0u; i < size; ++i) {
            cmd->push_constants[offset + i] = src[i];
        }
    }
    if (end > cmd->push_constant_size) {
        cmd->push_constant_size = end;
    }
}

/* ── Sampler ─────────────────────────────────────────────────────────────── */

VkResult vkCreateSampler(VkDevice device, const VkSamplerCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSampler *pSampler) {
    vk_sampler_impl_t *impl;
    (void)device;
    (void)pAllocator;
    if (pSampler == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    impl = (vk_sampler_impl_t *)vk_heap_alloc(sizeof(*impl));
    if (impl == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    if (pCreateInfo != 0) {
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
    }
    *pSampler = (VkSampler)(uintptr_t)impl;
    return VK_SUCCESS;
}

void vkDestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks *pAllocator) {
    (void)device;
    (void)pAllocator;
    vk_heap_free((void *)(uintptr_t)sampler);
}

/* ── Query pool ──────────────────────────────────────────────────────────── */

VkResult vkCreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkQueryPool *pQueryPool) {
    vk_query_pool_impl_t *impl;
    (void)device;
    (void)pAllocator;
    if (pQueryPool == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    impl = (vk_query_pool_impl_t *)vk_heap_alloc(sizeof(*impl));
    if (impl == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    impl->query_type  = pCreateInfo != 0 ? pCreateInfo->queryType  : 0u;
    impl->query_count = pCreateInfo != 0 ? pCreateInfo->queryCount : 0u;
    *pQueryPool = (VkQueryPool)(uintptr_t)impl;
    return VK_SUCCESS;
}

void vkDestroyQueryPool(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks *pAllocator) {
    (void)device;
    (void)pAllocator;
    vk_heap_free((void *)(uintptr_t)queryPool);
}

void vkCmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount) {
    (void)commandBuffer;
    (void)queryPool;
    (void)firstQuery;
    (void)queryCount;
}

void vkCmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkFlags flags) {
    (void)commandBuffer;
    (void)queryPool;
    (void)query;
    (void)flags;
}

void vkCmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query) {
    (void)commandBuffer;
    (void)queryPool;
    (void)query;
}

VkResult vkGetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void *pData, VkDeviceSize stride, VkFlags flags) {
    (void)device;
    (void)queryPool;
    (void)firstQuery;
    (void)queryCount;
    (void)dataSize;
    (void)stride;
    (void)flags;
    /* Return 0 for all queries: not ready, value = 0. */
    if (pData != 0 && dataSize > 0u) {
        uint8_t *bytes = (uint8_t *)pData;
        size_t i;
        for (i = 0u; i < dataSize; ++i) {
            bytes[i] = 0;
        }
    }
    return VK_NOT_READY;
}

void vkCmdWriteTimestamp(VkCommandBuffer commandBuffer, VkFlags pipelineStage, VkQueryPool queryPool, uint32_t query) {
    (void)commandBuffer;
    (void)pipelineStage;
    (void)queryPool;
    (void)query;
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
    /* Mark features the software path does support */
    pFeatures->independentBlend  = VK_TRUE;
    pFeatures->vertexPipelineStoresAndAtomics = VK_TRUE;
    pFeatures->fragmentStoresAndAtomics       = VK_TRUE;
}

VkResult vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, uint32_t format, uint32_t type, uint32_t tiling, VkFlags usage, VkFlags flags, VkImageFormatProperties *pImageFormatProperties) {
    (void)physicalDevice;
    (void)format;
    (void)type;
    (void)tiling;
    (void)usage;
    (void)flags;
    if (pImageFormatProperties == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    pImageFormatProperties->maxExtent.width  = 16384u;
    pImageFormatProperties->maxExtent.height = 16384u;
    pImageFormatProperties->maxExtent.depth  = 1u;
    pImageFormatProperties->maxMipLevels     = 14u;
    pImageFormatProperties->maxArrayLayers   = 1u;
    pImageFormatProperties->sampleCounts     = 0x01u; /* VK_SAMPLE_COUNT_1_BIT */
    pImageFormatProperties->maxResourceSize  = (VkDeviceSize)0x40000000u; /* 1 GiB */
    return VK_SUCCESS;
}

/* ── Buffer copy ─────────────────────────────────────────────────────────── */

void vkCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy *pRegions) {
    vk_buffer_impl_t *src = (vk_buffer_impl_t *)(uintptr_t)srcBuffer;
    vk_buffer_impl_t *dst = (vk_buffer_impl_t *)(uintptr_t)dstBuffer;
    uint32_t i;
    (void)commandBuffer;
    if (src == 0 || dst == 0 || pRegions == 0) {
        return;
    }
    for (i = 0u; i < regionCount; ++i) {
        const VkBufferCopy *region = &pRegions[i];
        const uint8_t *src_bytes;
        uint8_t *dst_bytes;
        size_t   copy_size;
        if (src->memory == 0 || dst->memory == 0) {
            continue;
        }
        src_bytes = src->memory->bytes + region->srcOffset;
        dst_bytes = dst->memory->bytes + region->dstOffset;
        copy_size = (size_t)region->size;
        {
            size_t j;
            for (j = 0u; j < copy_size; ++j) {
                dst_bytes[j] = src_bytes[j];
            }
        }
    }
}

PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance, const char *pName) {
    (void)instance;
    if (string_equals_local(pName, "vkCreateInstance")) return (PFN_vkVoidFunction)vkCreateInstance;
    if (string_equals_local(pName, "vkDestroyInstance")) return (PFN_vkVoidFunction)vkDestroyInstance;
    if (string_equals_local(pName, "vkCreateVexSurfaceKHR")) return (PFN_vkVoidFunction)vkCreateVexSurfaceKHR;
    if (string_equals_local(pName, "vkDestroySurfaceKHR")) return (PFN_vkVoidFunction)vkDestroySurfaceKHR;
    if (string_equals_local(pName, "vkEnumeratePhysicalDevices")) return (PFN_vkVoidFunction)vkEnumeratePhysicalDevices;
    if (string_equals_local(pName, "vkGetPhysicalDeviceProperties")) return (PFN_vkVoidFunction)vkGetPhysicalDeviceProperties;
    if (string_equals_local(pName, "vkGetPhysicalDeviceMemoryProperties")) return (PFN_vkVoidFunction)vkGetPhysicalDeviceMemoryProperties;
    if (string_equals_local(pName, "vkGetPhysicalDeviceFormatProperties")) return (PFN_vkVoidFunction)vkGetPhysicalDeviceFormatProperties;
    if (string_equals_local(pName, "vkGetPhysicalDeviceQueueFamilyProperties")) return (PFN_vkVoidFunction)vkGetPhysicalDeviceQueueFamilyProperties;
    if (string_equals_local(pName, "vkGetPhysicalDeviceSurfaceSupportKHR")) return (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceSupportKHR;
    if (string_equals_local(pName, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) return (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
    if (string_equals_local(pName, "vkGetPhysicalDeviceSurfaceFormatsKHR")) return (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceFormatsKHR;
    if (string_equals_local(pName, "vkGetPhysicalDeviceSurfacePresentModesKHR")) return (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfacePresentModesKHR;
    if (string_equals_local(pName, "vkCreateDevice")) return (PFN_vkVoidFunction)vkCreateDevice;
    if (string_equals_local(pName, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    if (string_equals_local(pName, "vkGetPhysicalDeviceFeatures")) return (PFN_vkVoidFunction)vkGetPhysicalDeviceFeatures;
    if (string_equals_local(pName, "vkGetPhysicalDeviceImageFormatProperties")) return (PFN_vkVoidFunction)vkGetPhysicalDeviceImageFormatProperties;
    return 0;
}

PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice device, const char *pName) {
    (void)device;
    if (string_equals_local(pName, "vkGetDeviceQueue")) return (PFN_vkVoidFunction)vkGetDeviceQueue;
    if (string_equals_local(pName, "vkCreateSwapchainKHR")) return (PFN_vkVoidFunction)vkCreateSwapchainKHR;
    if (string_equals_local(pName, "vkDestroySwapchainKHR")) return (PFN_vkVoidFunction)vkDestroySwapchainKHR;
    if (string_equals_local(pName, "vkGetSwapchainImagesKHR")) return (PFN_vkVoidFunction)vkGetSwapchainImagesKHR;
    if (string_equals_local(pName, "vkAcquireNextImageKHR")) return (PFN_vkVoidFunction)vkAcquireNextImageKHR;
    if (string_equals_local(pName, "vkCreateCommandPool")) return (PFN_vkVoidFunction)vkCreateCommandPool;
    if (string_equals_local(pName, "vkDestroyCommandPool")) return (PFN_vkVoidFunction)vkDestroyCommandPool;
    if (string_equals_local(pName, "vkResetCommandPool")) return (PFN_vkVoidFunction)vkResetCommandPool;
    if (string_equals_local(pName, "vkAllocateCommandBuffers")) return (PFN_vkVoidFunction)vkAllocateCommandBuffers;
    if (string_equals_local(pName, "vkFreeCommandBuffers")) return (PFN_vkVoidFunction)vkFreeCommandBuffers;
    if (string_equals_local(pName, "vkBeginCommandBuffer")) return (PFN_vkVoidFunction)vkBeginCommandBuffer;
    if (string_equals_local(pName, "vkEndCommandBuffer")) return (PFN_vkVoidFunction)vkEndCommandBuffer;
    if (string_equals_local(pName, "vkResetCommandBuffer")) return (PFN_vkVoidFunction)vkResetCommandBuffer;
    if (string_equals_local(pName, "vkCmdClearColorImage")) return (PFN_vkVoidFunction)vkCmdClearColorImage;
    if (string_equals_local(pName, "vkCreateBuffer")) return (PFN_vkVoidFunction)vkCreateBuffer;
    if (string_equals_local(pName, "vkDestroyBuffer")) return (PFN_vkVoidFunction)vkDestroyBuffer;
    if (string_equals_local(pName, "vkGetBufferMemoryRequirements")) return (PFN_vkVoidFunction)vkGetBufferMemoryRequirements;
    if (string_equals_local(pName, "vkAllocateMemory")) return (PFN_vkVoidFunction)vkAllocateMemory;
    if (string_equals_local(pName, "vkFreeMemory")) return (PFN_vkVoidFunction)vkFreeMemory;
    if (string_equals_local(pName, "vkBindBufferMemory")) return (PFN_vkVoidFunction)vkBindBufferMemory;
    if (string_equals_local(pName, "vkMapMemory")) return (PFN_vkVoidFunction)vkMapMemory;
    if (string_equals_local(pName, "vkUnmapMemory")) return (PFN_vkVoidFunction)vkUnmapMemory;
    if (string_equals_local(pName, "vkCreateImage")) return (PFN_vkVoidFunction)vkCreateImage;
    if (string_equals_local(pName, "vkDestroyImage")) return (PFN_vkVoidFunction)vkDestroyImage;
    if (string_equals_local(pName, "vkGetImageMemoryRequirements")) return (PFN_vkVoidFunction)vkGetImageMemoryRequirements;
    if (string_equals_local(pName, "vkBindImageMemory")) return (PFN_vkVoidFunction)vkBindImageMemory;
    if (string_equals_local(pName, "vkCreateImageView")) return (PFN_vkVoidFunction)vkCreateImageView;
    if (string_equals_local(pName, "vkDestroyImageView")) return (PFN_vkVoidFunction)vkDestroyImageView;
    if (string_equals_local(pName, "vkCreateShaderModule")) return (PFN_vkVoidFunction)vkCreateShaderModule;
    if (string_equals_local(pName, "vkDestroyShaderModule")) return (PFN_vkVoidFunction)vkDestroyShaderModule;
    if (string_equals_local(pName, "vkCreateRenderPass")) return (PFN_vkVoidFunction)vkCreateRenderPass;
    if (string_equals_local(pName, "vkDestroyRenderPass")) return (PFN_vkVoidFunction)vkDestroyRenderPass;
    if (string_equals_local(pName, "vkCreateFramebuffer")) return (PFN_vkVoidFunction)vkCreateFramebuffer;
    if (string_equals_local(pName, "vkDestroyFramebuffer")) return (PFN_vkVoidFunction)vkDestroyFramebuffer;
    if (string_equals_local(pName, "vkCreatePipelineLayout")) return (PFN_vkVoidFunction)vkCreatePipelineLayout;
    if (string_equals_local(pName, "vkDestroyPipelineLayout")) return (PFN_vkVoidFunction)vkDestroyPipelineLayout;
    if (string_equals_local(pName, "vkCreateGraphicsPipelines")) return (PFN_vkVoidFunction)vkCreateGraphicsPipelines;
    if (string_equals_local(pName, "vkDestroyPipeline")) return (PFN_vkVoidFunction)vkDestroyPipeline;
    if (string_equals_local(pName, "vkCmdBeginRenderPass")) return (PFN_vkVoidFunction)vkCmdBeginRenderPass;
    if (string_equals_local(pName, "vkCmdEndRenderPass")) return (PFN_vkVoidFunction)vkCmdEndRenderPass;
    if (string_equals_local(pName, "vkCmdBindPipeline")) return (PFN_vkVoidFunction)vkCmdBindPipeline;
    if (string_equals_local(pName, "vkCmdBindVertexBuffers")) return (PFN_vkVoidFunction)vkCmdBindVertexBuffers;
    if (string_equals_local(pName, "vkCmdBindIndexBuffer")) return (PFN_vkVoidFunction)vkCmdBindIndexBuffer;
    if (string_equals_local(pName, "vkCmdSetViewport")) return (PFN_vkVoidFunction)vkCmdSetViewport;
    if (string_equals_local(pName, "vkCmdSetScissor")) return (PFN_vkVoidFunction)vkCmdSetScissor;
    if (string_equals_local(pName, "vkCmdDraw")) return (PFN_vkVoidFunction)vkCmdDraw;
    if (string_equals_local(pName, "vkCmdDrawIndexed")) return (PFN_vkVoidFunction)vkCmdDrawIndexed;
    if (string_equals_local(pName, "vkCmdCopyBufferToImage")) return (PFN_vkVoidFunction)vkCmdCopyBufferToImage;
    if (string_equals_local(pName, "vkCmdCopyImage")) return (PFN_vkVoidFunction)vkCmdCopyImage;
    if (string_equals_local(pName, "vkCmdBlitImage")) return (PFN_vkVoidFunction)vkCmdBlitImage;
    if (string_equals_local(pName, "vkCmdPipelineBarrier")) return (PFN_vkVoidFunction)vkCmdPipelineBarrier;
    if (string_equals_local(pName, "vkCreateFence")) return (PFN_vkVoidFunction)vkCreateFence;
    if (string_equals_local(pName, "vkDestroyFence")) return (PFN_vkVoidFunction)vkDestroyFence;
    if (string_equals_local(pName, "vkResetFences")) return (PFN_vkVoidFunction)vkResetFences;
    if (string_equals_local(pName, "vkWaitForFences")) return (PFN_vkVoidFunction)vkWaitForFences;
    if (string_equals_local(pName, "vkCreateSemaphore")) return (PFN_vkVoidFunction)vkCreateSemaphore;
    if (string_equals_local(pName, "vkDestroySemaphore")) return (PFN_vkVoidFunction)vkDestroySemaphore;
    if (string_equals_local(pName, "vkQueueSubmit")) return (PFN_vkVoidFunction)vkQueueSubmit;
    if (string_equals_local(pName, "vkQueuePresentKHR")) return (PFN_vkVoidFunction)vkQueuePresentKHR;
    if (string_equals_local(pName, "vkQueueWaitIdle")) return (PFN_vkVoidFunction)vkQueueWaitIdle;
    if (string_equals_local(pName, "vkDeviceWaitIdle")) return (PFN_vkVoidFunction)vkDeviceWaitIdle;
    /* Descriptor sets */
    if (string_equals_local(pName, "vkCreateDescriptorSetLayout")) return (PFN_vkVoidFunction)vkCreateDescriptorSetLayout;
    if (string_equals_local(pName, "vkDestroyDescriptorSetLayout")) return (PFN_vkVoidFunction)vkDestroyDescriptorSetLayout;
    if (string_equals_local(pName, "vkCreateDescriptorPool")) return (PFN_vkVoidFunction)vkCreateDescriptorPool;
    if (string_equals_local(pName, "vkDestroyDescriptorPool")) return (PFN_vkVoidFunction)vkDestroyDescriptorPool;
    if (string_equals_local(pName, "vkResetDescriptorPool")) return (PFN_vkVoidFunction)vkResetDescriptorPool;
    if (string_equals_local(pName, "vkAllocateDescriptorSets")) return (PFN_vkVoidFunction)vkAllocateDescriptorSets;
    if (string_equals_local(pName, "vkFreeDescriptorSets")) return (PFN_vkVoidFunction)vkFreeDescriptorSets;
    if (string_equals_local(pName, "vkUpdateDescriptorSets")) return (PFN_vkVoidFunction)vkUpdateDescriptorSets;
    if (string_equals_local(pName, "vkCmdBindDescriptorSets")) return (PFN_vkVoidFunction)vkCmdBindDescriptorSets;
    if (string_equals_local(pName, "vkCmdPushConstants")) return (PFN_vkVoidFunction)vkCmdPushConstants;
    /* Sampler */
    if (string_equals_local(pName, "vkCreateSampler")) return (PFN_vkVoidFunction)vkCreateSampler;
    if (string_equals_local(pName, "vkDestroySampler")) return (PFN_vkVoidFunction)vkDestroySampler;
    /* Query pool */
    if (string_equals_local(pName, "vkCreateQueryPool")) return (PFN_vkVoidFunction)vkCreateQueryPool;
    if (string_equals_local(pName, "vkDestroyQueryPool")) return (PFN_vkVoidFunction)vkDestroyQueryPool;
    if (string_equals_local(pName, "vkCmdResetQueryPool")) return (PFN_vkVoidFunction)vkCmdResetQueryPool;
    if (string_equals_local(pName, "vkCmdBeginQuery")) return (PFN_vkVoidFunction)vkCmdBeginQuery;
    if (string_equals_local(pName, "vkCmdEndQuery")) return (PFN_vkVoidFunction)vkCmdEndQuery;
    if (string_equals_local(pName, "vkGetQueryPoolResults")) return (PFN_vkVoidFunction)vkGetQueryPoolResults;
    if (string_equals_local(pName, "vkCmdWriteTimestamp")) return (PFN_vkVoidFunction)vkCmdWriteTimestamp;
    /* Buffer copy */
    if (string_equals_local(pName, "vkCmdCopyBuffer")) return (PFN_vkVoidFunction)vkCmdCopyBuffer;
    /* Physical device */
    if (string_equals_local(pName, "vkGetPhysicalDeviceFeatures")) return (PFN_vkVoidFunction)vkGetPhysicalDeviceFeatures;
    if (string_equals_local(pName, "vkGetPhysicalDeviceImageFormatProperties")) return (PFN_vkVoidFunction)vkGetPhysicalDeviceImageFormatProperties;
    return 0;
}
