#include <stddef.h>
#include <stdint.h>

#include <unos/vex_legacy_compat.h>
#include <vulkan/vulkan.h>

static size_t string_length(const char *text) {
    size_t length = 0u;
    while (text != 0 && text[length] != '\0') {
        ++length;
    }
    return length;
}

static void write_text(const char *text) {
    (void)write(STDOUT_FILENO, text, string_length(text));
}

static void append_text(char *buffer, size_t capacity, size_t *length, const char *text) {
    size_t i = 0u;
    while (text != 0 && text[i] != '\0' && *length + 1u < capacity) {
        buffer[(*length)++] = text[i++];
    }
    buffer[*length] = '\0';
}

static void append_u32(char *buffer, size_t capacity, size_t *length, uint32_t value) {
    char digits[16];
    size_t count = 0u;

    if (value == 0u) {
        append_text(buffer, capacity, length, "0");
        return;
    }
    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count > 0u && *length + 1u < capacity) {
        buffer[(*length)++] = digits[--count];
    }
    buffer[*length] = '\0';
}

static uint32_t mix_color(uint32_t left, uint32_t right, uint32_t step, uint32_t max_step) {
    uint32_t lr = (left >> 16u) & 0xffu;
    uint32_t lg = (left >> 8u) & 0xffu;
    uint32_t lb = left & 0xffu;
    uint32_t rr = (right >> 16u) & 0xffu;
    uint32_t rg = (right >> 8u) & 0xffu;
    uint32_t rb = right & 0xffu;
    uint32_t r;
    uint32_t g;
    uint32_t b;

    if (max_step == 0u) {
        return left;
    }

    r = (lr * (max_step - step) + rr * step) / max_step;
    g = (lg * (max_step - step) + rg * step) / max_step;
    b = (lb * (max_step - step) + rb * step) / max_step;
    return 0xff000000u | (r << 16u) | (g << 8u) | b;
}

static void fill_rect_u32(uint32_t *pixels, uint32_t width, uint32_t height, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    uint32_t x1 = x + w > width ? width : x + w;
    uint32_t y1 = y + h > height ? height : y + h;

    for (uint32_t py = y; py < y1; ++py) {
        for (uint32_t px = x; px < x1; ++px) {
            pixels[py * width + px] = color;
        }
    }
}

static void fill_circle_u32(uint32_t *pixels, uint32_t width, uint32_t height, int32_t cx, int32_t cy, int32_t radius, uint32_t color) {
    int32_t y0 = cy - radius;
    int32_t y1 = cy + radius;
    int32_t x0 = cx - radius;
    int32_t x1 = cx + radius;

    for (int32_t py = y0; py <= y1; ++py) {
        if (py < 0 || (uint32_t)py >= height) {
            continue;
        }
        for (int32_t px = x0; px <= x1; ++px) {
            int32_t dx;
            int32_t dy;

            if (px < 0 || (uint32_t)px >= width) {
                continue;
            }
            dx = px - cx;
            dy = py - cy;
            if (dx * dx + dy * dy <= radius * radius) {
                pixels[(uint32_t)py * width + (uint32_t)px] = color;
            }
        }
    }
}

static void fill_rounded_rect_u32(uint32_t *pixels, uint32_t width, uint32_t height, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t radius, uint32_t color) {
    uint32_t x1 = x + w > width ? width : x + w;
    uint32_t y1 = y + h > height ? height : y + h;

    for (uint32_t py = y; py < y1; ++py) {
        for (uint32_t px = x; px < x1; ++px) {
            uint32_t local_x = px - x;
            uint32_t local_y = py - y;
            uint32_t rx = local_x < radius ? radius - local_x : (local_x >= w - radius ? local_x - (w - radius - 1u) : 0u);
            uint32_t ry = local_y < radius ? radius - local_y : (local_y >= h - radius ? local_y - (h - radius - 1u) : 0u);
            if (rx == 0u || ry == 0u || (int32_t)(rx * rx + ry * ry) <= (int32_t)(radius * radius)) {
                pixels[py * width + px] = color;
            }
        }
    }
}

static void render_demo_scene(uint32_t *pixels, uint32_t width, uint32_t height, uint32_t frame) {
    uint32_t wobble = frame % 48u;
    int32_t phase = wobble < 24u ? (int32_t)wobble : (int32_t)(47u - wobble);
    uint32_t panel_x = (uint32_t)(34 + phase / 6);
    uint32_t panel_w = width - 68u;
    uint32_t panel_h = height - 42u;
    uint32_t panel_y = (uint32_t)(22 + phase / 8);
    uint32_t glow_x = width - 62u - (uint32_t)(phase / 3);
    uint32_t glow_y = 42u + (uint32_t)(phase / 4);
    uint32_t card_shift = (uint32_t)(phase / 5);

    for (uint32_t y = 0u; y < height; ++y) {
        uint32_t row_color = mix_color(0xff0b1020u, 0xff131b2eu, y, height == 0u ? 1u : height);
        for (uint32_t x = 0u; x < width; ++x) {
            uint32_t tint = (x / 7u + y / 9u) & 1u;
            pixels[y * width + x] = tint == 0u ? row_color : mix_color(row_color, 0xff172033u, 1u, 6u);
        }
    }

    fill_circle_u32(pixels, width, height, (int32_t)glow_x, (int32_t)glow_y, 34, 0xff1e3a8au);
    fill_circle_u32(pixels, width, height, (int32_t)glow_x + 24, (int32_t)glow_y + 14, 20, 0xff7c3aedu);
    fill_circle_u32(pixels, width, height, 48 - phase / 5, 40 + phase / 7, 22, 0xff0f766eu);

    fill_rounded_rect_u32(pixels, width, height, panel_x - 6u, panel_y - 6u, panel_w + 12u, panel_h + 12u, 18u, 0xff0a0d16u);
    fill_rounded_rect_u32(pixels, width, height, panel_x, panel_y, panel_w, panel_h, 16u, 0xff111827u);
    fill_rounded_rect_u32(pixels, width, height, panel_x + 16u, panel_y + 16u, panel_w - 32u, 18u, 9u, 0xff1f2937u);
    fill_circle_u32(pixels, width, height, (int32_t)panel_x + 28, (int32_t)panel_y + 25, 5, 0xfffb7185u);
    fill_circle_u32(pixels, width, height, (int32_t)panel_x + 46, (int32_t)panel_y + 25, 5, 0xfff59e0bu);
    fill_circle_u32(pixels, width, height, (int32_t)panel_x + 64, (int32_t)panel_y + 25, 5, 0xff10b981u);

    fill_rounded_rect_u32(pixels, width, height, panel_x + 22u + card_shift, panel_y + 54u, panel_w - 44u - card_shift, 50u, 12u, 0xff2563ebu);
    fill_rounded_rect_u32(pixels, width, height, panel_x + 28u + card_shift, panel_y + 60u, panel_w - 56u - card_shift, 10u, 5u, 0xff60a5fau);
    fill_rounded_rect_u32(pixels, width, height, panel_x + 40u, panel_y + 118u - card_shift, panel_w - 100u, 64u, 12u, 0xfff97316u);
    fill_rounded_rect_u32(pixels, width, height, panel_x + 54u, panel_y + 194u, 92u, 10u, 5u, 0xff374151u);
    fill_rounded_rect_u32(pixels, width, height, panel_x + 156u, panel_y + 194u, 134u, 10u, 5u, 0xff374151u);
}

int main(void) {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkBuffer staging = VK_NULL_HANDLE;
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkBuffer index_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
    VkDeviceMemory index_memory = VK_NULL_HANDLE;
    VkDeviceMemory image_memory = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImage offscreen = VK_NULL_HANDLE;
    VkImageView offscreen_view = VK_NULL_HANDLE;
    VkImageView swapchain_view = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkImage images[4];
    VkPhysicalDeviceProperties props;
    VkSwapchainCreateInfoKHR swapchain_info;
    VkVexSurfaceCreateInfoKHR surface_info;
    VkCommandPoolCreateInfo pool_info;
    VkCommandBufferAllocateInfo alloc_info;
    VkCommandBufferBeginInfo begin_info;
    VkBufferCreateInfo buffer_info;
    VkImageCreateInfo image_info;
    VkImageViewCreateInfo image_view_info;
    VkAttachmentDescription attachment;
    VkAttachmentReference color_attachment;
    VkSubpassDescription subpass;
    VkRenderPassCreateInfo render_pass_info;
    VkFramebufferCreateInfo framebuffer_info;
    VkPipelineLayoutCreateInfo pipeline_layout_info;
    VkGraphicsPipelineCreateInfo pipeline_info;
    VkViewport viewport;
    VkRect2D scissor;
    VkMemoryAllocateInfo memory_info;
    VkMemoryRequirements memory_requirements;
    VkBufferImageCopy copy_region;
    VkImageMemoryBarrier barriers[3];
    VkFenceCreateInfo fence_info;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphoreCreateInfo semaphore_info;
    VkSemaphore acquire_semaphore = VK_NULL_HANDLE;
    VkSemaphore present_semaphore = VK_NULL_HANDLE;
    VkSubmitInfo submit_info;
    VkPresentInfoKHR present_info;
    VkClearColorValue clear;
    VkDeviceSize vertex_offset = 0u;
    VkDeviceSize staging_alloc_size = 0u;
    VkDeviceSize vertex_alloc_size = 0u;
    VkDeviceSize index_alloc_size = 0u;
    uint32_t frame = 0u;
    uint32_t physical_count = 1u;
    uint32_t image_count = 1u;
    uint32_t image_index = 0u;
    uint32_t *mapped = 0;
    uint32_t *vertex_mapped = 0;
    uint32_t *index_mapped = 0;
    char line[160];
    size_t length = 0u;

    write_text("vk_test: begin\n");
    if (vkCreateInstance(0, 0, &instance) != VK_SUCCESS) {
        write_text("vk_test: instance failed\n");
        return 1;
    }
    if (vkEnumeratePhysicalDevices(instance, &physical_count, &physical_device) != VK_SUCCESS || physical_count == 0u) {
        write_text("vk_test: physical device failed\n");
        vkDestroyInstance(instance, 0);
        return 1;
    }
    vkGetPhysicalDeviceProperties(physical_device, &props);
    append_text(line, sizeof(line), &length, "vk_test: vendor=");
    append_u32(line, sizeof(line), &length, props.vendorID);
    append_text(line, sizeof(line), &length, " device=");
    append_u32(line, sizeof(line), &length, props.deviceID);
    append_text(line, sizeof(line), &length, "\n");
    write_text(line);

    if (vkCreateDevice(physical_device, 0, 0, &device) != VK_SUCCESS) {
        write_text("vk_test: device failed\n");
        vkDestroyInstance(instance, 0);
        return 1;
    }
    vkGetDeviceQueue(device, 0u, 0u, &queue);

    surface_info.sType = VK_STRUCTURE_TYPE_VEX_SURFACE_CREATE_INFO_KHR;
    surface_info.pNext = 0;
    surface_info.flags = 0u;
    surface_info.x = 72;
    surface_info.y = 120;
    surface_info.width = 420u;
    surface_info.height = 220u;
    if (vkCreateVexSurfaceKHR(instance, &surface_info, 0, &surface) != VK_SUCCESS) {
        write_text("vk_test: surface failed\n");
        vkDestroyDevice(device, 0);
        vkDestroyInstance(instance, 0);
        return 1;
    }

    swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_info.pNext = 0;
    swapchain_info.flags = 0u;
    swapchain_info.surface = surface;
    swapchain_info.minImageCount = 2u;
    swapchain_info.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    swapchain_info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchain_info.imageExtent.width = 420u;
    swapchain_info.imageExtent.height = 220u;
    swapchain_info.imageArrayLayers = 1u;
    swapchain_info.imageUsage = 0u;
    swapchain_info.imageSharingMode = 0u;
    swapchain_info.queueFamilyIndexCount = 0u;
    swapchain_info.pQueueFamilyIndices = 0;
    swapchain_info.preTransform = 0u;
    swapchain_info.compositeAlpha = 0u;
    swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchain_info.clipped = VK_TRUE;
    swapchain_info.oldSwapchain = VK_NULL_HANDLE;
    if (vkCreateSwapchainKHR(device, &swapchain_info, 0, &swapchain) != VK_SUCCESS) {
        write_text("vk_test: swapchain failed\n");
        vkDestroyDevice(device, 0);
        vkDestroyInstance(instance, 0);
        return 1;
    }

    if (vkGetSwapchainImagesKHR(device, swapchain, &image_count, 0) != VK_SUCCESS || image_count == 0u || image_count > 4u) {
        write_text("vk_test: images failed\n");
        goto cleanup;
    }
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphore_info.pNext = 0;
    semaphore_info.flags = 0u;
    if (vkCreateSemaphore(device, &semaphore_info, 0, &acquire_semaphore) != VK_SUCCESS ||
        vkCreateSemaphore(device, &semaphore_info, 0, &present_semaphore) != VK_SUCCESS) {
        write_text("vk_test: semaphore failed\n");
        goto cleanup;
    }
    if (vkAcquireNextImageKHR(device, swapchain, 0u, acquire_semaphore, VK_NULL_HANDLE, &image_index) != VK_SUCCESS) {
        write_text("vk_test: acquire failed\n");
        goto cleanup;
    }
    if (vkGetSwapchainImagesKHR(device, swapchain, &image_count, images) != VK_SUCCESS || image_index >= image_count) {
        write_text("vk_test: image fetch failed\n");
        goto cleanup;
    }
    image = images[image_index];
    image_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    image_view_info.pNext = 0;
    image_view_info.flags = 0u;
    image_view_info.image = image;
    image_view_info.viewType = 0u;
    image_view_info.format = VK_FORMAT_B8G8R8A8_UNORM;
    image_view_info.components.r = 0u;
    image_view_info.components.g = 0u;
    image_view_info.components.b = 0u;
    image_view_info.components.a = 0u;
    image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_view_info.subresourceRange.baseMipLevel = 0u;
    image_view_info.subresourceRange.levelCount = 1u;
    image_view_info.subresourceRange.baseArrayLayer = 0u;
    image_view_info.subresourceRange.layerCount = 1u;
    if (vkCreateImageView(device, &image_view_info, 0, &swapchain_view) != VK_SUCCESS) {
        write_text("vk_test: swapchain view failed\n");
        goto cleanup;
    }

    attachment.flags = 0u;
    attachment.format = VK_FORMAT_B8G8R8A8_UNORM;
    attachment.samples = 1u;
    attachment.loadOp = 0u;
    attachment.storeOp = 0u;
    attachment.stencilLoadOp = 0u;
    attachment.stencilStoreOp = 0u;
    attachment.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    color_attachment.attachment = 0u;
    color_attachment.layout = VK_IMAGE_LAYOUT_GENERAL;
    subpass.flags = 0u;
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.inputAttachmentCount = 0u;
    subpass.pInputAttachments = 0;
    subpass.colorAttachmentCount = 1u;
    subpass.pColorAttachments = &color_attachment;
    subpass.pResolveAttachments = 0;
    subpass.pDepthStencilAttachment = 0;
    subpass.preserveAttachmentCount = 0u;
    subpass.pPreserveAttachments = 0;
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.pNext = 0;
    render_pass_info.flags = 0u;
    render_pass_info.attachmentCount = 1u;
    render_pass_info.pAttachments = &attachment;
    render_pass_info.subpassCount = 1u;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 0u;
    render_pass_info.pDependencies = 0;
    if (vkCreateRenderPass(device, &render_pass_info, 0, &render_pass) != VK_SUCCESS) {
        write_text("vk_test: render pass failed\n");
        goto cleanup;
    }
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.pNext = 0;
    framebuffer_info.flags = 0u;
    framebuffer_info.renderPass = render_pass;
    framebuffer_info.attachmentCount = 1u;
    framebuffer_info.pAttachments = &swapchain_view;
    framebuffer_info.width = 420u;
    framebuffer_info.height = 220u;
    framebuffer_info.layers = 1u;
    if (vkCreateFramebuffer(device, &framebuffer_info, 0, &framebuffer) != VK_SUCCESS) {
        write_text("vk_test: framebuffer failed\n");
        goto cleanup;
    }
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.pNext = 0;
    pipeline_layout_info.flags = 0u;
    pipeline_layout_info.setLayoutCount = 0u;
    pipeline_layout_info.pSetLayouts = 0;
    pipeline_layout_info.pushConstantRangeCount = 0u;
    pipeline_layout_info.pPushConstantRanges = 0;
    if (vkCreatePipelineLayout(device, &pipeline_layout_info, 0, &pipeline_layout) != VK_SUCCESS) {
        write_text("vk_test: pipeline layout failed\n");
        goto cleanup;
    }
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = 0;
    pipeline_info.flags = 0u;
    pipeline_info.stageCount = 0u;
    pipeline_info.pStages = 0;
    pipeline_info.pVertexInputState = 0;
    pipeline_info.pInputAssemblyState = 0;
    pipeline_info.pTessellationState = 0;
    pipeline_info.pViewportState = 0;
    pipeline_info.pRasterizationState = 0;
    pipeline_info.pMultisampleState = 0;
    pipeline_info.pDepthStencilState = 0;
    pipeline_info.pColorBlendState = 0;
    pipeline_info.pDynamicState = 0;
    pipeline_info.layout = pipeline_layout;
    pipeline_info.renderPass = render_pass;
    pipeline_info.subpass = 0u;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex = -1;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u, &pipeline_info, 0, &pipeline) != VK_SUCCESS) {
        write_text("vk_test: pipeline failed\n");
        goto cleanup;
    }

    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.pNext = 0;
    image_info.flags = 0u;
    image_info.imageType = 0u;
    image_info.format = VK_FORMAT_B8G8R8A8_UNORM;
    image_info.extent.width = 420u;
    image_info.extent.height = 220u;
    image_info.extent.depth = 1u;
    image_info.mipLevels = 1u;
    image_info.arrayLayers = 1u;
    image_info.samples = 1u;
    image_info.tiling = 0u;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.sharingMode = 0u;
    image_info.queueFamilyIndexCount = 0u;
    image_info.pQueueFamilyIndices = 0;
    image_info.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
    if (vkCreateImage(device, &image_info, 0, &offscreen) != VK_SUCCESS) {
        write_text("vk_test: image failed\n");
        goto cleanup;
    }
    vkGetImageMemoryRequirements(device, offscreen, &memory_requirements);
    memory_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memory_info.pNext = 0;
    memory_info.allocationSize = memory_requirements.size;
    memory_info.memoryTypeIndex = 0u;
    if (vkAllocateMemory(device, &memory_info, 0, &image_memory) != VK_SUCCESS) {
        write_text("vk_test: image memory failed\n");
        goto cleanup;
    }
    if (vkBindImageMemory(device, offscreen, image_memory, 0u) != VK_SUCCESS) {
        write_text("vk_test: image bind failed\n");
        goto cleanup;
    }
    image_view_info.image = offscreen;
    if (vkCreateImageView(device, &image_view_info, 0, &offscreen_view) != VK_SUCCESS) {
        write_text("vk_test: image view failed\n");
        goto cleanup;
    }

    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.pNext = 0;
    pool_info.flags = 0u;
    pool_info.queueFamilyIndex = 0u;
    if (vkCreateCommandPool(device, &pool_info, 0, &pool) != VK_SUCCESS) {
        write_text("vk_test: pool failed\n");
        goto cleanup;
    }

    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.pNext = 0;
    buffer_info.flags = 0u;
    buffer_info.size = 420u * 220u * 4u;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_info.sharingMode = 0u;
    buffer_info.queueFamilyIndexCount = 0u;
    buffer_info.pQueueFamilyIndices = 0;
    if (vkCreateBuffer(device, &buffer_info, 0, &staging) != VK_SUCCESS) {
        write_text("vk_test: buffer failed\n");
        goto cleanup;
    }
    vkGetBufferMemoryRequirements(device, staging, &memory_requirements);
    memory_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memory_info.pNext = 0;
    memory_info.allocationSize = memory_requirements.size;
    memory_info.memoryTypeIndex = 0u;
    staging_alloc_size = memory_info.allocationSize;
    if (vkAllocateMemory(device, &memory_info, 0, &staging_memory) != VK_SUCCESS) {
        write_text("vk_test: memory failed\n");
        goto cleanup;
    }
    if (vkBindBufferMemory(device, staging, staging_memory, 0u) != VK_SUCCESS) {
        write_text("vk_test: bind failed\n");
        goto cleanup;
    }

    buffer_info.size = 8u * 3u * sizeof(uint32_t);
    buffer_info.usage = 0u;
    if (vkCreateBuffer(device, &buffer_info, 0, &vertex_buffer) != VK_SUCCESS) {
        write_text("vk_test: vertex buffer failed\n");
        goto cleanup;
    }
    vkGetBufferMemoryRequirements(device, vertex_buffer, &memory_requirements);
    memory_info.allocationSize = memory_requirements.size;
    vertex_alloc_size = memory_info.allocationSize;
    if (vkAllocateMemory(device, &memory_info, 0, &vertex_memory) != VK_SUCCESS) {
        write_text("vk_test: vertex memory failed\n");
        goto cleanup;
    }
    if (vkBindBufferMemory(device, vertex_buffer, vertex_memory, 0u) != VK_SUCCESS) {
        write_text("vk_test: vertex bind failed\n");
        goto cleanup;
    }
    buffer_info.size = 12u * sizeof(uint32_t);
    buffer_info.usage = 0u;
    if (vkCreateBuffer(device, &buffer_info, 0, &index_buffer) != VK_SUCCESS) {
        write_text("vk_test: index buffer failed\n");
        goto cleanup;
    }
    vkGetBufferMemoryRequirements(device, index_buffer, &memory_requirements);
    memory_info.allocationSize = memory_requirements.size;
    index_alloc_size = memory_info.allocationSize;
    if (vkAllocateMemory(device, &memory_info, 0, &index_memory) != VK_SUCCESS) {
        write_text("vk_test: index memory failed\n");
        goto cleanup;
    }
    if (vkBindBufferMemory(device, index_buffer, index_memory, 0u) != VK_SUCCESS) {
        write_text("vk_test: index bind failed\n");
        goto cleanup;
    }
    if (vkMapMemory(device, staging_memory, 0u, staging_alloc_size, 0u, (void **)&mapped) != VK_SUCCESS || mapped == 0) {
        write_text("vk_test: map failed\n");
        goto cleanup;
    }
    render_demo_scene(mapped, 420u, 220u, 0u);
    vkUnmapMemory(device, staging_memory);
    if (vkMapMemory(device, vertex_memory, 0u, vertex_alloc_size, 0u, (void **)&vertex_mapped) != VK_SUCCESS || vertex_mapped == 0) {
        write_text("vk_test: vertex map failed\n");
        goto cleanup;
    }
    vertex_mapped[0] = 56u;  vertex_mapped[1] = 76u;  vertex_mapped[2] = 0xff2563ebu;
    vertex_mapped[3] = 388u; vertex_mapped[4] = 76u;  vertex_mapped[5] = 0xff2563ebu;
    vertex_mapped[6] = 388u; vertex_mapped[7] = 120u; vertex_mapped[8] = 0xff60a5fau;
    vertex_mapped[9] = 56u;  vertex_mapped[10] = 120u; vertex_mapped[11] = 0xff60a5fau;
    vertex_mapped[12] = 74u; vertex_mapped[13] = 136u; vertex_mapped[14] = 0xfff97316u;
    vertex_mapped[15] = 350u; vertex_mapped[16] = 136u; vertex_mapped[17] = 0xfff97316u;
    vertex_mapped[18] = 350u; vertex_mapped[19] = 190u; vertex_mapped[20] = 0xfffb923cu;
    vertex_mapped[21] = 74u; vertex_mapped[22] = 190u; vertex_mapped[23] = 0xfffb923cu;
    vkUnmapMemory(device, vertex_memory);
    if (vkMapMemory(device, index_memory, 0u, index_alloc_size, 0u, (void **)&index_mapped) != VK_SUCCESS || index_mapped == 0) {
        write_text("vk_test: index map failed\n");
        goto cleanup;
    }
    index_mapped[0] = 0u;
    index_mapped[1] = 1u;
    index_mapped[2] = 2u;
    index_mapped[3] = 0u;
    index_mapped[4] = 2u;
    index_mapped[5] = 3u;
    index_mapped[6] = 4u;
    index_mapped[7] = 5u;
    index_mapped[8] = 6u;
    index_mapped[9] = 4u;
    index_mapped[10] = 6u;
    index_mapped[11] = 7u;
    vkUnmapMemory(device, index_memory);

    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.pNext = 0;
    alloc_info.commandPool = pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1u;
    if (vkAllocateCommandBuffers(device, &alloc_info, &cmd) != VK_SUCCESS) {
        write_text("vk_test: alloc failed\n");
        goto cleanup;
    }

    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.pNext = 0;
    begin_info.flags = 0u;
    begin_info.pInheritanceInfo = 0;
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.pNext = 0;
    fence_info.flags = 0u;
    if (vkCreateFence(device, &fence_info, 0, &fence) != VK_SUCCESS) {
        write_text("vk_test: fence failed\n");
        goto cleanup;
    }

    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = 420.0f;
    viewport.height = 220.0f;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    scissor.offset.x = 24;
    scissor.offset.y = 14;
    scissor.extent.width = 372u;
    scissor.extent.height = 182u;
    present_info.pNext = 0;
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = 0;
    submit_info.waitSemaphoreCount = 0u;
    submit_info.pWaitSemaphores = 0;
    submit_info.pWaitDstStageMask = 0;
    submit_info.commandBufferCount = 1u;
    submit_info.pCommandBuffers = &cmd;
    submit_info.signalSemaphoreCount = 1u;
    submit_info.pSignalSemaphores = &present_semaphore;
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1u;
    present_info.pWaitSemaphores = &present_semaphore;
    present_info.swapchainCount = 1u;
    present_info.pSwapchains = &swapchain;
    present_info.pImageIndices = &image_index;
    present_info.pResults = 0;

    for (frame = 0u; frame < 180u; ++frame) {
        if (vkMapMemory(device, staging_memory, 0u, staging_alloc_size, 0u, (void **)&mapped) != VK_SUCCESS || mapped == 0) {
            write_text("vk_test: remap failed\n");
            goto cleanup;
        }
        render_demo_scene(mapped, 420u, 220u, frame);
        vkUnmapMemory(device, staging_memory);

        if (vkResetFences(device, 1u, &fence) != VK_SUCCESS) {
            write_text("vk_test: reset fence failed\n");
            goto cleanup;
        }
        if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS) {
            write_text("vk_test: reset cmd failed\n");
            goto cleanup;
        }
        if (vkBeginCommandBuffer(cmd, &begin_info) != VK_SUCCESS) {
            write_text("vk_test: begin cmd failed\n");
            goto cleanup;
        }

        barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[0].pNext = 0;
        barriers[0].srcAccessMask = 0u;
        barriers[0].dstAccessMask = 0u;
        barriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[0].srcQueueFamilyIndex = 0u;
        barriers[0].dstQueueFamilyIndex = 0u;
        barriers[0].image = offscreen;
        barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barriers[0].subresourceRange.baseMipLevel = 0u;
        barriers[0].subresourceRange.levelCount = 1u;
        barriers[0].subresourceRange.baseArrayLayer = 0u;
        barriers[0].subresourceRange.layerCount = 1u;
        vkCmdPipelineBarrier(cmd, 0u, 0u, 0u, 0u, 0, 0u, 0, 1u, &barriers[0]);

        clear.float32[0] = 0.08f;
        clear.float32[1] = 0.10f;
        clear.float32[2] = 0.16f;
        clear.float32[3] = 1.0f;
        vkCmdClearColorImage(cmd, offscreen, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 0u, 0);
        copy_region.bufferOffset = 0u;
        copy_region.bufferRowLength = 420u;
        copy_region.bufferImageHeight = 220u;
        copy_region.imageSubresource.aspectMask = 0u;
        copy_region.imageSubresource.mipLevel = 0u;
        copy_region.imageSubresource.baseArrayLayer = 0u;
        copy_region.imageSubresource.layerCount = 1u;
        copy_region.imageOffset.x = 0;
        copy_region.imageOffset.y = 0;
        copy_region.imageOffset.z = 0;
        copy_region.imageExtent.width = 420u;
        copy_region.imageExtent.height = 220u;
        copy_region.imageExtent.depth = 1u;
        vkCmdCopyBufferToImage(cmd, staging, offscreen, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &copy_region);

        barriers[1] = barriers[0];
        barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        vkCmdPipelineBarrier(cmd, 0u, 0u, 0u, 0u, 0, 0u, 0, 1u, &barriers[1]);

        barriers[2] = barriers[0];
        barriers[2].image = image;
        barriers[2].oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barriers[2].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        vkCmdPipelineBarrier(cmd, 0u, 0u, 0u, 0u, 0, 0u, 0, 1u, &barriers[2]);

        vkCmdBlitImage(cmd, offscreen, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0u, 0, 0u);

        barriers[2].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[2].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(cmd, 0u, 0u, 0u, 0u, 0, 0u, 0, 1u, &barriers[2]);

        {
            VkRenderPassBeginInfo render_begin;
            render_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            render_begin.pNext = 0;
            render_begin.renderPass = render_pass;
            render_begin.framebuffer = framebuffer;
            render_begin.renderArea = scissor;
            render_begin.clearValueCount = 0u;
            render_begin.pClearValues = 0;
            vkCmdBeginRenderPass(cmd, &render_begin, 0u);
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindVertexBuffers(cmd, 0u, 1u, &vertex_buffer, &vertex_offset);
        vkCmdBindIndexBuffer(cmd, index_buffer, 0u, VK_INDEX_TYPE_UINT32);
        vkCmdSetViewport(cmd, 0u, 1u, &viewport);
        vkCmdSetScissor(cmd, 0u, 1u, &scissor);
        vkCmdDrawIndexed(cmd, 12u, 1u, 0u, 0, 0u);
        vkCmdEndRenderPass(cmd);

        if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
            write_text("vk_test: end cmd failed\n");
            goto cleanup;
        }
        if (vkQueueSubmit(queue, 1u, &submit_info, fence) != VK_SUCCESS) {
            write_text("vk_test: submit failed\n");
            goto cleanup;
        }
        if (vkWaitForFences(device, 1u, &fence, VK_TRUE, 0u) != VK_SUCCESS) {
            write_text("vk_test: wait failed\n");
            goto cleanup;
        }
        if (vkQueuePresentKHR(queue, &present_info) != VK_SUCCESS) {
            write_text("vk_test: present failed\n");
            goto cleanup;
        }
        usleep(16000u);
    }

    write_text("vk_test: ok\n");

cleanup:
    if (present_semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, present_semaphore, 0);
    }
    if (acquire_semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, acquire_semaphore, 0);
    }
    if (fence != VK_NULL_HANDLE) {
        vkDestroyFence(device, fence, 0);
    }
    if (cmd != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device, pool, 1u, &cmd);
    }
    if (staging_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, staging_memory, 0);
    }
    if (vertex_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, vertex_memory, 0);
    }
    if (index_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, index_memory, 0);
    }
    if (image_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, image_memory, 0);
    }
    if (staging != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, staging, 0);
    }
    if (vertex_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, vertex_buffer, 0);
    }
    if (index_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, index_buffer, 0);
    }
    if (offscreen_view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, offscreen_view, 0);
    }
    if (swapchain_view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, swapchain_view, 0);
    }
    if (offscreen != VK_NULL_HANDLE) {
        vkDestroyImage(device, offscreen, 0);
    }
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, 0);
    }
    if (pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipeline_layout, 0);
    }
    if (framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, framebuffer, 0);
    }
    if (render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, render_pass, 0);
    }
    if (pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, pool, 0);
    }
    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain, 0);
    }
    if (surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, 0);
    }
    if (device != VK_NULL_HANDLE) {
        vkDestroyDevice(device, 0);
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, 0);
    }
    return 0;
}
