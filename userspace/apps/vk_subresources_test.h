/* Deterministic backing/pixel tests. The expected offsets are calculated
   independently here for a 4x4 -> 2x2 -> 1x1, three-layer image. */
static int vk_subresource_begin(VkCommandBuffer cmd) {
    VkCommandBufferBeginInfo begin = {0};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    return vkResetCommandBuffer(cmd, 0u) == VK_SUCCESS &&
        vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS;
}

static int vk_subresource_complete(VkQueue queue, VkCommandBuffer cmd) {
    VkSubmitInfo submit = {0};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1u;
    submit.pCommandBuffers = &cmd;
    return vkEndCommandBuffer(cmd) == VK_SUCCESS &&
        vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) == VK_SUCCESS;
}

static void vk_subresource_barrier(VkCommandBuffer cmd, VkImage image,
        VkImageSubresourceRange range, VkImageLayout old, VkImageLayout next) {
    VkImageMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.oldLayout = old;
    barrier.newLayout = next;
    barrier.subresourceRange = range;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0u, 0u, NULL, 0u, NULL, 1u, &barrier);
}

static int vk_subresources_smoke(VkDevice device, VkQueue queue, VkRenderPass pass,
        VkPipeline pipeline, VkPipelineLayout pipeline_layout, VkDescriptorSet set,
        const VkDescriptorImageInfo *restore_descriptor) {
    VkImage image = VK_NULL_HANDLE, depth = VK_NULL_HANDLE, npot = VK_NULL_HANDLE, invalid_image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE, depth_memory = VK_NULL_HANDLE, buffer_memory = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkBuffer alias_buffer = VK_NULL_HANDLE;
    VkDeviceMemory alias_memory = VK_NULL_HANDLE;
    VkImageView target = VK_NULL_HANDLE, sampled = VK_NULL_HANDLE, invalid = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkImageCreateInfo info = {0};
    VkMemoryAllocateInfo alloc = {0};
    VkMemoryRequirements requirements = {0};
    VkCommandPoolCreateInfo pool_info = {0};
    VkCommandBufferAllocateInfo command_info = {0};
    VkBufferCreateInfo buffer_info = {0};
    VkImageSubresourceRange all = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, VK_REMAINING_MIP_LEVELS, 0u, VK_REMAINING_ARRAY_LAYERS };
    VkImageSubresourceRange one = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
    VkImageViewCreateInfo view_info = {0};
    VkDescriptorImageInfo descriptor = {0};
    VkWriteDescriptorSet write_info = {0};
    uint8_t *bytes = NULL, *buffer_bytes = NULL;
    float *depth_values = NULL;
    uint32_t expected[63], stage = 1u;
    int descriptor_changed = 0, ok = 0;
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_B8G8R8A8_UNORM;
    info.extent = (VkExtent3D){4u, 4u, 1u};
    info.mipLevels = 3u;
    info.arrayLayers = 3u;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_LINEAR;
    info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (vkCreateImage(device, &info, NULL, &image) != VK_SUCCESS) goto out;
    vkGetImageMemoryRequirements(device, image, &requirements);
    if (requirements.size != 252u || requirements.alignment != 4u) goto out;
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = requirements.size + 16u;
    if (vkAllocateMemory(device, &alloc, NULL, &memory) != VK_SUCCESS ||
        vkBindImageMemory(device, image, memory, 16u) != VK_SUCCESS ||
        vkMapMemory(device, memory, 0u, VK_WHOLE_SIZE, 0u, (void **)&bytes) != VK_SUCCESS) goto out;
    memset(bytes, 0xcc, 268u);
    stage = 2u;
    for (uint32_t layer = 0u; layer < 3u; ++layer) {
        const uint32_t offsets[3] = {0u, 64u, 80u};
        const uint32_t widths[3] = {4u, 2u, 1u};
        for (uint32_t mip = 0u; mip < 3u; ++mip) {
            VkImageSubresource sub = {VK_IMAGE_ASPECT_COLOR_BIT, mip, layer};
            VkSubresourceLayout layout = {0};
            vkGetImageSubresourceLayout(device, image, &sub, &layout);
            if (layout.offset != layer * 84u + offsets[mip] ||
                layout.rowPitch != widths[mip] * 4u ||
                layout.size != widths[mip] * widths[mip] * 4u || layout.arrayPitch != 84u) goto out;
        }
    }
    info.extent = (VkExtent3D){7u, 5u, 1u}; info.arrayLayers = 2u;
    if (vkCreateImage(device, &info, NULL, &npot) != VK_SUCCESS) goto out;
    {
        VkImageSubresource sub = {VK_IMAGE_ASPECT_COLOR_BIT, 1u, 1u};
        VkSubresourceLayout layout;
        vkGetImageSubresourceLayout(device, npot, &sub, &layout);
        if (layout.offset != 308u || layout.rowPitch != 12u || layout.size != 24u || layout.arrayPitch != 168u) goto out;
    }
    vkDestroyImage(device, npot, NULL); npot = VK_NULL_HANDLE;
    info.extent = (VkExtent3D){4u, 4u, 1u}; info.arrayLayers = 3u;
    info.mipLevels = 4u;
    if (vkCreateImage(device, &info, NULL, &invalid_image) == VK_SUCCESS) goto out;
    info.mipLevels = 3u;
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(device, &pool_info, NULL, &pool) != VK_SUCCESS) goto out;
    command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_info.commandPool = pool; command_info.commandBufferCount = 1u;
    if (vkAllocateCommandBuffers(device, &command_info, &cmd) != VK_SUCCESS) goto out;
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = 256u;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (vkCreateBuffer(device, &buffer_info, NULL, &buffer) != VK_SUCCESS) goto out;
    alloc.allocationSize = 256u;
    if (vkAllocateMemory(device, &alloc, NULL, &buffer_memory) != VK_SUCCESS ||
        vkBindBufferMemory(device, buffer, buffer_memory, 0u) != VK_SUCCESS ||
        vkMapMemory(device, buffer_memory, 0u, VK_WHOLE_SIZE, 0u, (void **)&buffer_bytes) != VK_SUCCESS) goto out;
    stage = 3u;
    if (!vk_subresource_begin(cmd)) goto out;
    vk_subresource_barrier(cmd, image, all, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    for (uint32_t layer = 0u; layer < 3u; ++layer) {
        for (uint32_t mip = 0u; mip < 3u; ++mip) {
            VkClearColorValue color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            uint32_t width = 4u >> mip;
            uint32_t offset = layer * 21u + (mip == 0u ? 0u : mip == 1u ? 16u : 20u);
            color.float32[layer] = (float)(mip + 1u) / 4.0f;
            one.baseMipLevel = mip; one.baseArrayLayer = layer;
            vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_GENERAL, &color, 1u, &one);
            uint32_t channel = (uint32_t)(color.float32[layer] * 255.0f + 0.5f);
            uint32_t value = 0xff000000u | channel << (16u - 8u * layer);
            for (uint32_t texel = 0u; texel < width * width; ++texel) expected[offset + texel] = value;
        }
    }
    if (!vk_subresource_complete(queue, cmd) || memcmp(bytes + 16u, expected, sizeof(expected)) != 0) goto out;
    for (uint32_t i = 0u; i < 16u; ++i) if (bytes[i] != 0xccu) goto out;
    write_text("vk_test: mip/layer offsets, NPOT extents, pitches and isolated clears ok\n");
    stage = 4u;
    memset(buffer_bytes, 0xaa, 256u);
    for (uint32_t layer = 0u; layer < 2u; ++layer)
        for (uint32_t y = 0u; y < 2u; ++y)
            for (uint32_t x = 0u; x < 2u; ++x) {
                uint32_t value = 0xff112200u + layer * 16u + y * 2u + x + 1u;
                memcpy(buffer_bytes + layer * 80u + y * 20u + x * 4u, &value, 4u);
                expected[(layer + 1u) * 21u + 16u + y * 2u + x] = value;
            }
    if (!vk_subresource_begin(cmd)) goto out;
    one = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 1u, 1u, 1u, 2u};
    vk_subresource_barrier(cmd, image, one, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    {
        VkBufferImageCopy copy = {0};
        copy.bufferRowLength = 5u; copy.bufferImageHeight = 4u;
        copy.imageSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 1u, 1u, 2u};
        copy.imageExtent = (VkExtent3D){2u, 2u, 1u};
        vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &copy);
    }
    vk_subresource_barrier(cmd, image, one, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    if (!vk_subresource_complete(queue, cmd) || memcmp(bytes + 16u, expected, sizeof(expected)) != 0) goto out;
    memset(buffer_bytes, 0xbb, 256u);
    if (!vk_subresource_begin(cmd)) goto out;
    {
        VkBufferImageCopy copy = {0};
        copy.bufferOffset = 16u; copy.bufferRowLength = 4u; copy.bufferImageHeight = 3u;
        copy.imageSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 1u, 1u, 2u};
        copy.imageExtent = (VkExtent3D){2u, 2u, 1u};
        vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1u, &copy);
    }
    if (!vk_subresource_complete(queue, cmd)) goto out;
    for (uint32_t i = 0u; i < 64u; ++i) {
        uint32_t actual; memcpy(&actual, buffer_bytes + i * 4u, 4u);
        uint32_t want = 0xbbbbbbbbu;
        for (uint32_t layer = 0u; layer < 2u; ++layer)
            for (uint32_t y = 0u; y < 2u; ++y)
                for (uint32_t x = 0u; x < 2u; ++x)
                    if (i == 4u + layer * 12u + y * 4u + x)
                        want = expected[(layer + 1u) * 21u + 16u + y * 2u + x];
        if (actual != want) goto out;
    }
    write_text("vk_test: padded buffer-image transfers across selected array layers ok\n");
    stage = 5u;
    if (!vk_subresource_begin(cmd)) goto out;
    {
        VkImageCopy copy = {0};
        copy.srcSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 1u, 1u, 1u};
        copy.dstSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 2u, 0u, 1u};
        copy.extent = (VkExtent3D){1u, 1u, 1u};
        vkCmdCopyImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image, VK_IMAGE_LAYOUT_GENERAL, 1u, &copy);
        expected[20u] = expected[37u];
        VkImageBlit blit = {0};
        blit.srcSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
        blit.dstSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 2u, 2u, 1u};
        blit.srcOffsets[1] = (VkOffset3D){4, 4, 1};
        blit.dstOffsets[1] = (VkOffset3D){1, 1, 1};
        vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_GENERAL, image, VK_IMAGE_LAYOUT_GENERAL, 1u, &blit, VK_FILTER_NEAREST);
        expected[62u] = expected[0u];
    }
    if (!vk_subresource_complete(queue, cmd) || memcmp(bytes + 16u, expected, sizeof(expected)) != 0) goto out;
    write_text("vk_test: same-image copies/blits between distinct mip/layer subresources ok\n");
    stage = 6u;
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image; view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = info.format;
    view_info.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 1u, 1u, 2u, 1u};
    if (vkCreateImageView(device, &view_info, NULL, &target) != VK_SUCCESS) goto out;
    view_info.subresourceRange.baseArrayLayer = 1u;
    if (vkCreateImageView(device, &view_info, NULL, &sampled) != VK_SUCCESS) goto out;
    view_info.subresourceRange.baseArrayLayer = 3u;
    if (vkCreateImageView(device, &view_info, NULL, &invalid) == VK_SUCCESS) goto out;
    {
        VkFramebufferCreateInfo fb = {0};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = pass; fb.attachmentCount = 1u; fb.pAttachments = &target;
        fb.width = fb.height = 3u; fb.layers = 1u;
        if (vkCreateFramebuffer(device, &fb, NULL, &framebuffer) == VK_SUCCESS) goto out;
        fb.width = fb.height = 2u;
        if (vkCreateFramebuffer(device, &fb, NULL, &framebuffer) != VK_SUCCESS) goto out;
    }
    descriptor = *restore_descriptor; descriptor.imageView = sampled;
    descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    write_info.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_info.dstSet = set; write_info.descriptorCount = 1u;
    write_info.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write_info.pImageInfo = &descriptor;
    vkUpdateDescriptorSets(device, 1u, &write_info, 0u, NULL); descriptor_changed = 1;
    if (!vk_subresource_begin(cmd)) goto out;
    one = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 1u, 1u, 1u, 1u};
    vk_subresource_barrier(cmd, image, one, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    {
        VkRenderPassBeginInfo begin = {0};
        VkClearValue clear = {.color = {{0.0f, 0.0f, 0.0f, 1.0f}}};
        begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin.renderPass = pass; begin.framebuffer = framebuffer;
        begin.renderArea.extent = (VkExtent2D){2u, 2u};
        begin.clearValueCount = 1u; begin.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0u, 1u, &set, 0u, NULL);
        vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
        vkCmdEndRenderPass(cmd);
    }
    if (!vk_subresource_complete(queue, cmd)) goto out;
    /* The existing 2x2 triangle covers the top row. Linear center sampling
       averages the four selected texels, rounding each channel to nearest. */
    expected[58u] = expected[59u] = 0xff112203u;
    expected[60u] = expected[61u] = 0xff000000u;
    if (memcmp(bytes + 16u, expected, sizeof(expected)) != 0) goto out;
    write_text("vk_test: partial mip/layer views drive real sampling and render targets ok\n");
    stage = 7u;
    info.format = VK_FORMAT_D32_SFLOAT; info.arrayLayers = 2u;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (vkCreateImage(device, &info, NULL, &depth) != VK_SUCCESS) goto out;
    vkGetImageMemoryRequirements(device, depth, &requirements); alloc.allocationSize = requirements.size;
    if (requirements.size != 168u || vkAllocateMemory(device, &alloc, NULL, &depth_memory) != VK_SUCCESS ||
        vkBindImageMemory(device, depth, depth_memory, 0u) != VK_SUCCESS ||
        vkMapMemory(device, depth_memory, 0u, VK_WHOLE_SIZE, 0u, (void **)&depth_values) != VK_SUCCESS) goto out;
    for (uint32_t i = 0u; i < 42u; ++i) depth_values[i] = 0.75f;
    if (!vk_subresource_begin(cmd)) goto out;
    all.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vk_subresource_barrier(cmd, depth, all, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    one = (VkImageSubresourceRange){VK_IMAGE_ASPECT_DEPTH_BIT, 1u, VK_REMAINING_MIP_LEVELS, 1u, 1u};
    VkClearDepthStencilValue clear_depth = {0.25f, 0u};
    vkCmdClearDepthStencilImage(cmd, depth, VK_IMAGE_LAYOUT_GENERAL, &clear_depth, 1u, &one);
    if (!vk_subresource_complete(queue, cmd)) goto out;
    for (uint32_t i = 0u; i < 42u; ++i) if (depth_values[i] != (i >= 37u ? 0.25f : 0.75f)) goto out;
    if (!vk_subresource_begin(cmd)) goto out;
    VkBufferImageCopy copy_depth = {0};
    copy_depth.bufferOffset = 240u;
    copy_depth.imageSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_DEPTH_BIT, 2u, 1u, 1u};
    copy_depth.imageExtent = (VkExtent3D){1u, 1u, 1u};
    vkCmdCopyImageToBuffer(cmd, depth, VK_IMAGE_LAYOUT_GENERAL, buffer, 1u, &copy_depth);
    if (!vk_subresource_complete(queue, cmd)) goto out;
    float read_depth; memcpy(&read_depth, buffer_bytes + 240u, 4u);
    if (read_depth != 0.25f) goto out;
    write_text("vk_test: D32 range clears and depth-aspect transfers preserve neighboring subresources ok\n");
    stage = 8u;
    /* Illegal aspects and overflowed padding are errors, never partial writes. */
    if (!vk_subresource_begin(cmd)) goto out;
    one.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
    vkCmdClearDepthStencilImage(cmd, depth, VK_IMAGE_LAYOUT_GENERAL, &clear_depth, 1u, &one);
    if (vkEndCommandBuffer(cmd) == VK_SUCCESS) goto out;
    if (!vk_subresource_begin(cmd)) goto out;
    copy_depth.bufferRowLength = UINT32_MAX; copy_depth.bufferImageHeight = UINT32_MAX;
    copy_depth.imageSubresource.layerCount = 2u; copy_depth.imageSubresource.baseArrayLayer = 0u;
    vkCmdCopyImageToBuffer(cmd, depth, VK_IMAGE_LAYOUT_GENERAL, buffer, 1u, &copy_depth);
    if (vkEndCommandBuffer(cmd) == VK_SUCCESS) goto out;
    if (!vk_subresource_begin(cmd)) goto out;
    /* Reject extreme signed endpoints before subtracting them into an extent. */
    VkImageBlit invalid_blit = {0};
    invalid_blit.srcSubresource = invalid_blit.dstSubresource =
        (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
    invalid_blit.srcOffsets[0].x = INT32_MIN;
    invalid_blit.srcOffsets[1] = invalid_blit.dstOffsets[1] = (VkOffset3D){1, 1, 1};
    vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_GENERAL, image,
        VK_IMAGE_LAYOUT_GENERAL, 1u, &invalid_blit, VK_FILTER_NEAREST);
    if (vkEndCommandBuffer(cmd) == VK_SUCCESS || !vk_subresource_begin(cmd)) goto out;
    invalid_blit.srcOffsets[0].x = 0;
    invalid_blit.dstOffsets[1].x = INT32_MAX;
    vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_GENERAL, image,
        VK_IMAGE_LAYOUT_GENERAL, 1u, &invalid_blit, VK_FILTER_NEAREST);
    if (vkEndCommandBuffer(cmd) == VK_SUCCESS || !vk_subresource_begin(cmd)) goto out;
    VkClearColorValue bad_color = {{1.0f, 1.0f, 1.0f, 1.0f}};
    one = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 1u, 1u, 1u, 1u};
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_GENERAL, &bad_color, 1u, &one);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) goto out;
    VkSubmitInfo bad_submit = {0}; bad_submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    bad_submit.commandBufferCount = 1u; bad_submit.pCommandBuffers = &cmd;
    if (vk_test_queue_submit_raw(queue, 1u, &bad_submit, VK_NULL_HANDLE) == VK_SUCCESS ||
        memcmp(bytes + 16u, expected, sizeof(expected)) != 0) goto out;
    write_text("vk_test: independent layouts, aspect/bounds/padding errors reject before mutation ok\n");
    stage = 9u;
    /* A different VkDeviceMemory handle must not conceal backing aliasing. */
    uint64_t alias_capability = 0u;
    VkVexMemoryImportInfoVEX import = {0};
    import.sType = VK_STRUCTURE_TYPE_VEX_MEMORY_IMPORT_INFO_EXPERIMENTAL;
    if (vkGetVexMemoryCapabilityVEX(device, memory, &alias_capability) != VK_SUCCESS) goto out;
    import.surfaceCapability = alias_capability;
    alloc.pNext = &import; alloc.allocationSize = 268u;
    VkBufferCreateInfo alias_info = {0}; alias_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    alias_info.size = 252u; alias_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (vkAllocateMemory(device, &alloc, NULL, &alias_memory) != VK_SUCCESS ||
        vkCreateBuffer(device, &alias_info, NULL, &alias_buffer) != VK_SUCCESS ||
        vkBindBufferMemory(device, alias_buffer, alias_memory, 16u) != VK_SUCCESS ||
        !vk_subresource_begin(cmd)) goto out;
    VkBufferImageCopy alias_copy = {0};
    alias_copy.imageSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
    alias_copy.imageExtent = (VkExtent3D){1u, 1u, 1u};
    vkCmdCopyBufferToImage(cmd, alias_buffer, image, VK_IMAGE_LAYOUT_GENERAL, 1u, &alias_copy);
    if (vkEndCommandBuffer(cmd) == VK_SUCCESS || !vk_subresource_begin(cmd)) goto out;
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_GENERAL, alias_buffer, 1u, &alias_copy);
    if (vkEndCommandBuffer(cmd) == VK_SUCCESS || !vk_subresource_begin(cmd)) goto out;
    /* Disjoint bytes of the very same backing are a legitimate copy. */
    alias_copy.bufferOffset = 248u;
    vkCmdCopyBufferToImage(cmd, alias_buffer, image, VK_IMAGE_LAYOUT_GENERAL, 1u, &alias_copy);
    if (!vk_subresource_complete(queue, cmd)) goto out;
    expected[0u] = expected[62u];
    if (memcmp(bytes + 16u, expected, sizeof(expected)) != 0) goto out;
    write_text("vk_test: imported backing aliases reject overlap and allow disjoint subresource transfers ok\n");
    ok = 1;
out:
    if (!ok) write_hex32("vk_test: subresource failure stage=", stage);
    if (cmd != VK_NULL_HANDLE) (void)vkResetCommandBuffer(cmd, 0u);
    if (descriptor_changed) { write_info.pImageInfo = restore_descriptor; vkUpdateDescriptorSets(device, 1u, &write_info, 0u, NULL); }
    if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(device, framebuffer, NULL);
    if (target != VK_NULL_HANDLE) vkDestroyImageView(device, target, NULL);
    if (sampled != VK_NULL_HANDLE) vkDestroyImageView(device, sampled, NULL);
    if (invalid != VK_NULL_HANDLE) vkDestroyImageView(device, invalid, NULL);
    if (pool != VK_NULL_HANDLE) vkDestroyCommandPool(device, pool, NULL);
    if (image != VK_NULL_HANDLE) vkDestroyImage(device, image, NULL);
    if (depth != VK_NULL_HANDLE) vkDestroyImage(device, depth, NULL);
    if (npot != VK_NULL_HANDLE) vkDestroyImage(device, npot, NULL);
    if (invalid_image != VK_NULL_HANDLE) vkDestroyImage(device, invalid_image, NULL);
    if (buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, buffer, NULL);
    if (alias_buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, alias_buffer, NULL);
    if (alias_memory != VK_NULL_HANDLE) vkFreeMemory(device, alias_memory, NULL);
    if (memory != VK_NULL_HANDLE) vkFreeMemory(device, memory, NULL);
    if (depth_memory != VK_NULL_HANDLE) vkFreeMemory(device, depth_memory, NULL);
    if (buffer_memory != VK_NULL_HANDLE) vkFreeMemory(device, buffer_memory, NULL);
    return ok;
}

static int vk_subresources_inflight_smoke(VkDevice device, VkQueue queue, VkSwapchainKHR swapchain) {
    VkImage image = VK_NULL_HANDLE, images[4] = {0};
    VkDeviceMemory owner = VK_NULL_HANDLE, observer = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkSemaphore gate = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkImageCreateInfo info = {0};
    VkMemoryAllocateInfo alloc = {0};
    VkVexMemoryImportInfoVEX import = {0};
    VkCommandPoolCreateInfo pool_info = {0};
    VkCommandBufferAllocateInfo command_info = {0};
    VkSemaphoreCreateInfo semaphore_info = {0};
    VkFenceCreateInfo fence_info = {0};
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    vk_async_submit_probe_t probe = {0};
    uint64_t capability = 0u, event_value = 0u;
    uint32_t *bytes = NULL, tid = 0u, acquired = UINT32_MAX, count = 4u;
    uint32_t stage = 1u;
    int ok = 0, signal_sent = 0, submitted = 0;
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D; info.format = VK_FORMAT_B8G8R8A8_UNORM;
    info.extent = (VkExtent3D){4u, 4u, 1u}; info.mipLevels = info.arrayLayers = 3u;
    info.samples = VK_SAMPLE_COUNT_1_BIT; info.tiling = VK_IMAGE_TILING_LINEAR;
    info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; alloc.allocationSize = 252u;
    if (vkCreateImage(device, &info, NULL, &image) != VK_SUCCESS ||
        vkAllocateMemory(device, &alloc, NULL, &owner) != VK_SUCCESS ||
        vkBindImageMemory(device, image, owner, 0u) != VK_SUCCESS ||
        vkGetVexMemoryCapabilityVEX(device, owner, &capability) != VK_SUCCESS) goto out;
    import.sType = VK_STRUCTURE_TYPE_VEX_MEMORY_IMPORT_INFO_EXPERIMENTAL;
    import.surfaceCapability = capability; alloc.pNext = &import;
    if (vkAllocateMemory(device, &alloc, NULL, &observer) != VK_SUCCESS ||
        vkMapMemory(device, observer, 0u, VK_WHOLE_SIZE, 0u, (void **)&bytes) != VK_SUCCESS) goto out;
    for (uint32_t i = 0u; i < 63u; ++i) bytes[i] = 0xccccccccu;
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_info.commandBufferCount = 1u;
    if (vkCreateCommandPool(device, &pool_info, NULL, &pool) != VK_SUCCESS) goto out;
    command_info.commandPool = pool;
    if (vkAllocateCommandBuffers(device, &command_info, &cmd) != VK_SUCCESS || !vk_subresource_begin(cmd)) goto out;
    VkImageSubresourceRange all = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, VK_REMAINING_MIP_LEVELS, 0u, VK_REMAINING_ARRAY_LAYERS};
    VkImageSubresourceRange one = {VK_IMAGE_ASPECT_COLOR_BIT, 2u, 1u, 2u, 1u};
    VkClearColorValue color = {{0.0f, 1.0f, 0.0f, 1.0f}};
    vk_subresource_barrier(cmd, image, all, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_GENERAL, &color, 1u, &one);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) goto out;
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateSemaphore(device, &semaphore_info, NULL, &gate) != VK_SUCCESS ||
        vkCreateFence(device, &fence_info, NULL, &fence) != VK_SUCCESS) goto out;
    probe.queue = queue; probe.fence = fence;
    probe.submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    probe.submit.waitSemaphoreCount = 1u; probe.submit.pWaitSemaphores = &gate;
    probe.submit.pWaitDstStageMask = &wait_stage;
    probe.submit.commandBufferCount = 1u; probe.submit.pCommandBuffers = &cmd;
    probe.started_event = vex_event_counter_create(0u, 0u);
    probe.returned_event = vex_event_counter_create(0u, 0u);
    if ((int64_t)probe.started_event <= 0 || (int64_t)probe.returned_event <= 0 ||
        vex_thread_create(vk_async_submit_probe_thread, &probe, 0u, &tid) != 0) goto out;
    stage = 2u;
    if (vex_event_counter_read(probe.started_event, &event_value, 0u, 100u) != 0) goto release;
    submitted = vex_event_counter_read(probe.returned_event, &event_value, 0u, 100u) == 0 &&
        __atomic_load_n(&probe.returned, __ATOMIC_ACQUIRE) && probe.result == VK_SUCCESS;
    if (!submitted || vkGetFenceStatus(device, fence) != VK_NOT_READY) goto release;
    for (uint32_t i = 0u; i < 63u; ++i) if (bytes[i] != 0xccccccccu) goto release;
    /* Drop the owner and every recorded handle while their work is pending.
       The observer is a different local import of the same backing. */
    vkDestroyCommandPool(device, pool, NULL); pool = VK_NULL_HANDLE; cmd = VK_NULL_HANDLE;
    vkDestroyImage(device, image, NULL); image = VK_NULL_HANDLE;
    vkFreeMemory(device, owner, NULL); owner = VK_NULL_HANDLE;
    stage = 3u;
release:
    if (vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, gate, VK_NULL_HANDLE, &acquired) != VK_SUCCESS) goto out;
    signal_sent = 1;
    if (tid != 0u) { (void)vex_thread_join(tid, NULL); tid = 0u; }
    if (!submitted || stage != 3u || vkWaitForFences(device, 1u, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS ||
        vkQueueWaitIdle(queue) != VK_SUCCESS) goto out;
    for (uint32_t i = 0u; i < 63u; ++i) if (bytes[i] != (i == 62u ? 0xff00ff00u : 0xccccccccu)) goto out;
    write_text("vk_test: mip/layer metadata and shared backing survive in-flight owner/pool destruction ok\n");
    stage = 4u;
    if (vkGetSwapchainImagesKHR(device, swapchain, &count, images) != VK_SUCCESS || acquired >= count ||
        vkCreateCommandPool(device, &pool_info, NULL, &pool) != VK_SUCCESS) goto out;
    command_info.commandPool = pool;
    if (vkAllocateCommandBuffers(device, &command_info, &cmd) != VK_SUCCESS || !vk_subresource_begin(cmd)) goto out;
    one = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
    vk_subresource_barrier(cmd, images[acquired], one, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    if (!vk_subresource_complete(queue, cmd)) goto out;
    VkPresentInfoKHR present = {0}; present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.swapchainCount = 1u; present.pSwapchains = &swapchain; present.pImageIndices = &acquired;
    if (vkQueuePresentKHR(queue, &present) != VK_SUCCESS || vkQueueWaitIdle(queue) != VK_SUCCESS) goto out;
    ok = 1;
out:
    if (!ok) write_hex32("vk_test: in-flight subresource failure stage=", stage);
    if (tid != 0u) {
        if (!signal_sent && gate != VK_NULL_HANDLE)
            (void)vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, gate, VK_NULL_HANDLE, &acquired);
        (void)vex_thread_join(tid, NULL);
    }
    if (queue != VK_NULL_HANDLE) (void)vkQueueWaitIdle(queue);
    if (pool != VK_NULL_HANDLE) vkDestroyCommandPool(device, pool, NULL);
    if (image != VK_NULL_HANDLE) vkDestroyImage(device, image, NULL);
    if (owner != VK_NULL_HANDLE) vkFreeMemory(device, owner, NULL);
    if (observer != VK_NULL_HANDLE) vkFreeMemory(device, observer, NULL);
    if (gate != VK_NULL_HANDLE) vkDestroySemaphore(device, gate, NULL);
    if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, NULL);
    if ((int64_t)probe.started_event > 0) (void)vex_event_counter_release(probe.started_event);
    if ((int64_t)probe.returned_event > 0) (void)vex_event_counter_release(probe.returned_event);
    return ok;
}
