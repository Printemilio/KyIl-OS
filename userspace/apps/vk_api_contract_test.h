/* Exceeds the fixed 256 capability slots without increasing their quota.
   Returning an extra reference is legal while mapped; dropping the last one
   must fail until unmap. Query then proves that the last close was final. */
static int vk_native_mapped_capability_smoke(void){
    uint64_t last_identity=0u;
    for(uint32_t i=0u;i<300u;++i){
        vex_gpu_buffer_create_req_t buffer={0};buffer.width=buffer.height=buffer.stride=1u;
        buffer.format=VEX_GPU_FORMAT_XRGB8888;buffer.flags=VEX_GPU_MEMORY_CPU_VISIBLE;buffer.size=4u;
        if(vex_gpu_buffer_create(&buffer)!=0)return 0;
        vex_gpu_surface_create_req_t surface={0};surface.buffer_handle=buffer.handle;
        surface.alpha_mode=VEX_GPU_SURFACE_ALPHA_OPAQUE;surface.usage=VEX_GPU_SURFACE_USAGE_SAMPLED;
        if(vex_gpu_surface_create(&surface)!=0){(void)vex_gpu_buffer_destroy(buffer.handle);return 0;}
        if(vex_gpu_buffer_destroy(buffer.handle)!=0)return 0;
        uint64_t cap=surface.surface_handle;
        vex_surface_query_req_t query={0};query.surface_handle=cap;
        if(vex_surface_query(&query)!=0||query.backing_identity==last_identity)return 0;
        last_identity=query.backing_identity;
        if(vex_surface_retain(cap)!=0)return 0;
        vex_surface_map_req_t map={0};map.surface_handle=cap;map.length=query.backing_size;
        map.access=VEX_SURFACE_MAP_READ|VEX_SURFACE_MAP_WRITE;
        if(vex_surface_map(&map)!=0)return 0;
        if(vex_gpu_surface_destroy(cap)!=0||vex_gpu_surface_destroy(cap)==0)return 0;
        uint32_t *word=(void*)(uintptr_t)map.mapped_addr;*word=0x5a000000u+i;
        if(vex_surface_query(&query)!=0||*word!=0x5a000000u+i)return 0;
        vex_surface_unmap_req_t unmap={0};unmap.surface_handle=cap;unmap.mapped_addr=map.mapped_addr;
        unmap.length=query.backing_size;unmap.access=map.access;
        if(vex_surface_unmap(&unmap)!=0||vex_gpu_surface_destroy(cap)!=0||vex_surface_query(&query)==0)return 0;
    }
    write_text("vk_test: 300 mapped capability retain/release cycles exceed slot capacity; extra close allowed, mapped last close refused, final close destroys backing ok\n");
    return 1;
}

/* Completion gates make these assertions deterministic; no timing delays. */
static int vk_api_contract_smoke(VkInstance instance,VkDevice device,VkQueue queue){
    VkEvent gate=0,done=0;VkBuffer buffer=0;VkDeviceMemory memory=0,observer=0;
    VkCommandPool pool=0;VkCommandBuffer cmd=0;VkFence fence=0;uint32_t *pixels=0;
    int ok=0,submitted=0;uint32_t stage=1u;
    if(!vkGetInstanceProcAddr(instance,"vkCmdFillBuffer")||!vkGetDeviceProcAddr(device,"vkCreateEvent")||
       vkGetDeviceProcAddr(device,"vkGetPhysicalDeviceFeatures")||vkGetInstanceProcAddr(VK_NULL_HANDLE,"vkCmdFillBuffer"))goto out;
    VkPhysicalDevice physical=0;uint32_t count=1u;
    count=0u;physical=(VkPhysicalDevice)(uintptr_t)0xdeadbeef;
    if(vkEnumeratePhysicalDevices(instance,&count,&physical)!=VK_INCOMPLETE||count!=0u||
       physical!=(VkPhysicalDevice)(uintptr_t)0xdeadbeef)goto out;
    count=1u;
    if(vkEnumeratePhysicalDevices(instance,&count,&physical)!=VK_SUCCESS||count!=1u)goto out;
    VkPhysicalDeviceFeatures features={0};vkGetPhysicalDeviceFeatures(physical,&features);
    if(features.sparseBinding||features.sparseResidencyImage2D||!features.logicOp)goto out;
    count=7u;vkGetPhysicalDeviceSparseImageFormatProperties(physical,VK_FORMAT_B8G8R8A8_UNORM,
        VK_IMAGE_TYPE_2D,VK_SAMPLE_COUNT_1_BIT,VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_TILING_OPTIMAL,&count,NULL);
    if(count||vkQueueBindSparse(queue,0u,NULL,0)!=VK_ERROR_FEATURE_NOT_PRESENT)goto out;
    VkEventCreateInfo ec={0};ec.sType=VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;
    if(vkCreateEvent(device,&ec,NULL,&gate)!=VK_SUCCESS||vkCreateEvent(device,&ec,NULL,&done)!=VK_SUCCESS||
       vkGetEventStatus(device,gate)!=VK_EVENT_RESET||vkSetEvent(device,gate)!=VK_SUCCESS||
       vkGetEventStatus(device,gate)!=VK_EVENT_SET||vkResetEvent(device,gate)!=VK_SUCCESS)goto out;
    VkBufferCreateInfo bc={0};bc.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;bc.size=64u;bc.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkMemoryAllocateInfo ma={0};ma.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;ma.allocationSize=64u;
    if(vkCreateBuffer(device,&bc,NULL,&buffer)!=VK_SUCCESS||vkAllocateMemory(device,&ma,NULL,&memory)!=VK_SUCCESS||vkBindBufferMemory(device,buffer,memory,0u)!=VK_SUCCESS)goto out;
    void *mapped=0,*second=0;
    if(vkMapMemory(device,memory,16u,32u,0u,&mapped)!=VK_SUCCESS||vkMapMemory(device,memory,0u,4u,0u,&second)!=VK_ERROR_MEMORY_MAP_FAILED)goto out;
    VkMappedMemoryRange range={0};range.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;range.memory=memory;range.offset=16u;range.size=32u;
    if(vkFlushMappedMemoryRanges(device,1u,&range)!=VK_SUCCESS||vkInvalidateMappedMemoryRanges(device,1u,&range)!=VK_SUCCESS)goto out;
    range.offset=12u;if(vkFlushMappedMemoryRanges(device,1u,&range)!=VK_ERROR_MEMORY_MAP_FAILED)goto out;
    vkUnmapMemory(device,memory);
    if(vkInvalidateMappedMemoryRanges(device,1u,&range)!=VK_ERROR_MEMORY_MAP_FAILED)goto out;
    uint64_t capability=0u;if(vkGetVexMemoryCapabilityVEX(device,memory,&capability)!=VK_SUCCESS)goto out;
    VkVexMemoryImportInfoVEX import={0};import.sType=VK_STRUCTURE_TYPE_VEX_MEMORY_IMPORT_INFO_EXPERIMENTAL;import.surfaceCapability=capability;
    ma.pNext=&import;if(vkAllocateMemory(device,&ma,NULL,&observer)!=VK_SUCCESS||vkMapMemory(device,observer,0u,VK_WHOLE_SIZE,0u,(void**)&pixels)!=VK_SUCCESS)goto out;
    for(uint32_t i=0u;i<16u;++i)pixels[i]=0xccccccccu;
    VkCommandPoolCreateInfo cp={0};cp.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;cp.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if(vkCreateCommandPool(device,&cp,NULL,&pool)!=VK_SUCCESS)goto out;
    VkCommandBufferAllocateInfo ca={0};ca.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;ca.commandPool=pool;ca.commandBufferCount=1u;
    if(vkAllocateCommandBuffers(device,&ca,&cmd)!=VK_SUCCESS||!vk_subresource_begin(cmd))goto out;
    vkCmdWaitEvents(cmd,1u,&gate,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0u,NULL,0u,NULL,0u,NULL);
    vkCmdFillBuffer(cmd,buffer,0u,VK_WHOLE_SIZE,0x12345678u);
    uint32_t captured[4]={1u,2u,3u,4u};vkCmdUpdateBuffer(cmd,buffer,16u,sizeof(captured),captured);
    memset(captured,0xff,sizeof(captured));
    VkBufferMemoryBarrier barrier={0};barrier.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;barrier.buffer=buffer;barrier.size=VK_WHOLE_SIZE;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0u,0u,NULL,1u,&barrier,0u,NULL);
    vkCmdSetEvent(cmd,done,VK_PIPELINE_STAGE_TRANSFER_BIT);vkCmdResetEvent(cmd,done,VK_PIPELINE_STAGE_TRANSFER_BIT);vkCmdSetEvent(cmd,done,VK_PIPELINE_STAGE_TRANSFER_BIT);
    if(vkEndCommandBuffer(cmd)!=VK_SUCCESS)goto out;
    VkFenceCreateInfo fc={0};fc.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;if(vkCreateFence(device,&fc,NULL,&fence)!=VK_SUCCESS)goto out;
    VkSubmitInfo submit={0};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1u;submit.pCommandBuffers=&cmd;
    stage=2u;if(vk_test_queue_submit_raw(queue,1u,&submit,fence)!=VK_SUCCESS)goto out;submitted=1;
    if(vkGetFenceStatus(device,fence)!=VK_NOT_READY||vkGetEventStatus(device,done)!=VK_EVENT_RESET)goto out;
    for(uint32_t i=0u;i<16u;++i)if(pixels[i]!=0xccccccccu)goto out;
    /* Destroy producers and command pool while the retained work is blocked. */
    vkDestroyBuffer(device,buffer,NULL);buffer=0;vkFreeMemory(device,memory,NULL);memory=0;
    vkDestroyCommandPool(device,pool,NULL);pool=0;cmd=0;
    if(vkSetEvent(device,gate)!=VK_SUCCESS||vkWaitForFences(device,1u,&fence,VK_TRUE,UINT64_MAX)!=VK_SUCCESS||vkGetEventStatus(device,done)!=VK_EVENT_SET)goto out;
    stage=3u;
    for(uint32_t i=0u;i<16u;++i)if(pixels[i]!=(i>=4u&&i<8u?i-3u:0x12345678u))goto out;
    ok=1;write_text("vk_test: host mapping/range validation, deferred fill/update capture, event wait/wake and in-flight producer teardown ok\n");
out:
    if(!ok)write_hex32("vk_test: API contract failure stage=",stage);
    if(submitted){if(gate)(void)vkSetEvent(device,gate);(void)vkQueueWaitIdle(queue);}
    if(pool)vkDestroyCommandPool(device,pool,NULL);if(fence)vkDestroyFence(device,fence,NULL);
    if(gate)vkDestroyEvent(device,gate,NULL);if(done)vkDestroyEvent(device,done,NULL);
    if(buffer)vkDestroyBuffer(device,buffer,NULL);if(memory)vkFreeMemory(device,memory,NULL);if(observer)vkFreeMemory(device,observer,NULL);
    return ok;
}

/* Query the real native domain; pixel tests cannot substitute for accounting. */
static int vk_allocation_accounting_smoke(VkInstance instance,VkDevice device){
    VkEventCreateInfo ec={0};ec.sType=VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;
    /* Measure real PMM accounting, allowing only a small slab/page-table tail.
       The former unknown-size HEAP_FREE leaked at least one page per event. */
    vex_bulk_result_t before={0},after={0};vex_bulk_req_t bulk={VEX_BULK_MEMORY,&before};
    int64_t before_result=(int64_t)vex_syscall1(VEX_SYS_BULK,(uint64_t)(uintptr_t)&bulk);
    if(before_result!=0||!(before.valid_mask&VEX_BULK_MEMORY)){
        write_hex32("vk_test: PMM initial query return=",(uint32_t)before_result);
        write_hex32("vk_test: PMM initial query valid mask=",(uint32_t)before.valid_mask);
        write_hex32("vk_test: PMM request stack address=",(uint32_t)(uintptr_t)&bulk);
        return 0;
    }
    VkPhysicalDevice physical=0;uint32_t count=1u;VkPhysicalDeviceMemoryProperties heaps={0};
    if(vkEnumeratePhysicalDevices(instance,&count,&physical)!=VK_SUCCESS)return 0;
    vkGetPhysicalDeviceMemoryProperties(physical,&heaps);
    if(heaps.memoryHeapCount!=1u||heaps.memoryHeaps[0].size!=before.total_pages*4096u)return 0;
    write_text("vk_test: Vulkan heap size equals native PMM total pages ok\n");
    uint64_t root_versions=vex_syscall1(VEX_SYS_MRAC_VERSION_COUNT,0x726f6f742d6e7300ull);
    uint64_t initial_versions=root_versions;int within_bound=1;
    write_hex32("vk_test: PMM before events=",(uint32_t)before.used_pages);
    write_hex32("vk_test: inventory root owner versions before=",(uint32_t)root_versions);
    for(uint32_t round=0u;round<3u;++round){
        for(uint32_t i=0u;i<128u;++i){VkEvent event=0;if(vkCreateEvent(device,&ec,NULL,&event)!=VK_SUCCESS)return 0;vkDestroyEvent(device,event,NULL);}
        bulk.result=&after;
        int64_t bulk_result=(int64_t)vex_syscall1(VEX_SYS_BULK,(uint64_t)(uintptr_t)&bulk);
        root_versions=vex_syscall1(VEX_SYS_MRAC_VERSION_COUNT,0x726f6f742d6e7300ull);
        write_hex32("vk_test: PMM after event epoch=",(uint32_t)after.used_pages);
        write_hex32("vk_test: inventory root owner versions after=",(uint32_t)root_versions);
        if(bulk_result!=0||!(after.valid_mask&VEX_BULK_MEMORY)||
           (after.used_pages>before.used_pages&&after.used_pages-before.used_pages>8u))within_bound=0;
    }
    if(!within_bound||root_versions!=initial_versions)return 0;
    write_hex32("vk_test: 128 event allocation/free cycles PMM delta pages=",(uint32_t)(after.used_pages>=before.used_pages?after.used_pages-before.used_pages:0u));
    return 1;
}
