/* Two independent runtime indices: arrays of UBO descriptors and sampled
   images. Values come from push constants, not a compiler-folded index. */
#define SPV_INST(op, words) ((((uint32_t)(words)) << 16u) | (uint32_t)(op))
static const uint32_t descriptor_arrays_fragment_spv[] = {
    0x07230203u, 0x00010000u, 0u, 41u, 0u,
    SPV_INST(17,2),1u, SPV_INST(17,2),28u, SPV_INST(17,2),29u,
    SPV_INST(14,3),0u,1u,
    SPV_INST(15,6),4u,25u,0x6e69616du,0u,20u,
    SPV_INST(16,3),25u,7u,
    SPV_INST(71,4),20u,30u,0u,
    SPV_INST(71,3),6u,2u, SPV_INST(72,5),6u,0u,35u,0u,
    SPV_INST(71,4),21u,34u,0u, SPV_INST(71,4),21u,33u,0u,
    SPV_INST(71,4),22u,34u,0u, SPV_INST(71,4),22u,33u,1u,
    SPV_INST(19,2),1u, SPV_INST(22,3),2u,32u,
    SPV_INST(23,4),3u,2u,4u, SPV_INST(23,4),4u,2u,2u,
    SPV_INST(21,4),5u,32u,0u,
    SPV_INST(30,3),6u,3u,
    SPV_INST(43,4),5u,7u,2u, SPV_INST(43,4),5u,8u,0u,
    SPV_INST(28,4),9u,6u,7u, SPV_INST(32,4),10u,2u,9u,
    SPV_INST(32,4),11u,2u,3u,
    SPV_INST(25,9),12u,2u,1u,0u,0u,0u,1u,0u,
    SPV_INST(27,3),13u,12u, SPV_INST(28,4),14u,13u,7u,
    SPV_INST(32,4),15u,0u,14u, SPV_INST(32,4),16u,0u,13u,
    SPV_INST(32,4),17u,9u,6u, SPV_INST(32,4),18u,9u,3u,
    SPV_INST(32,4),19u,3u,3u,
    SPV_INST(59,4),19u,20u,3u, SPV_INST(59,4),10u,21u,2u,
    SPV_INST(59,4),15u,22u,0u, SPV_INST(59,4),17u,23u,9u,
    SPV_INST(33,3),24u,1u,
    SPV_INST(43,4),2u,34u,0x3f000000u,
    SPV_INST(44,5),4u,35u,34u,34u,
    SPV_INST(54,5),1u,25u,0u,24u, SPV_INST(248,2),26u,
    SPV_INST(65,5),18u,27u,23u,8u, SPV_INST(61,4),3u,28u,27u,
    SPV_INST(81,5),2u,29u,28u,0u, SPV_INST(109,4),5u,30u,29u,
    SPV_INST(81,5),2u,31u,28u,1u, SPV_INST(109,4),5u,32u,31u,
    SPV_INST(65,6),11u,33u,21u,30u,8u,
    SPV_INST(61,4),3u,36u,33u,
    SPV_INST(65,5),16u,37u,22u,32u, SPV_INST(61,4),13u,38u,37u,
    SPV_INST(87,5),3u,39u,38u,35u,
    SPV_INST(129,5),3u,40u,36u,39u,
    SPV_INST(62,3),20u,40u, SPV_INST(253,1), SPV_INST(56,1)
};
static const uint32_t uniform_array_fragment_spv[] = {
    0x07230203u,0x00010000u,0u,34u,0u,
    SPV_INST(17,2),1u,SPV_INST(14,3),0u,1u,
    SPV_INST(15,6),4u,20u,0x6e69616du,0u,16u,SPV_INST(16,3),20u,7u,
    SPV_INST(71,3),7u,2u,SPV_INST(72,5),7u,0u,35u,0u,
    SPV_INST(72,5),7u,1u,35u,64u,SPV_INST(71,4),6u,6u,16u,
    SPV_INST(71,4),15u,34u,0u,SPV_INST(71,4),15u,33u,0u,
    SPV_INST(71,4),16u,30u,0u,
    SPV_INST(19,2),1u,SPV_INST(22,3),2u,32u,SPV_INST(23,4),3u,2u,4u,
    SPV_INST(21,4),4u,32u,0u,SPV_INST(43,4),4u,5u,4u,
    SPV_INST(28,4),6u,3u,5u,SPV_INST(30,4),7u,6u,4u,
    SPV_INST(32,4),8u,2u,7u,SPV_INST(32,4),9u,2u,4u,
    SPV_INST(32,4),10u,2u,3u,SPV_INST(32,4),11u,2u,6u,
    SPV_INST(32,4),12u,2u,2u,SPV_INST(32,4),13u,3u,3u,SPV_INST(33,3),14u,1u,
    SPV_INST(59,4),8u,15u,2u,SPV_INST(59,4),13u,16u,3u,
    SPV_INST(43,4),4u,17u,0u,SPV_INST(43,4),4u,18u,1u,
    SPV_INST(43,4),4u,19u,3u,SPV_INST(43,4),2u,32u,0x3f800000u,
    SPV_INST(54,5),1u,20u,0u,14u,SPV_INST(248,2),21u,
    SPV_INST(65,5),9u,22u,15u,18u,SPV_INST(61,4),4u,23u,22u,
    SPV_INST(65,5),11u,24u,15u,17u,SPV_INST(65,5),10u,25u,24u,23u,
    SPV_INST(61,4),3u,26u,25u,SPV_INST(65,5),12u,27u,25u,17u,
    SPV_INST(61,4),2u,28u,27u,SPV_INST(81,5),2u,30u,26u,1u,
    SPV_INST(81,5),2u,31u,26u,2u,SPV_INST(80,7),3u,29u,28u,30u,31u,32u,
    SPV_INST(62,3),16u,29u,SPV_INST(253,1),SPV_INST(56,1)
};
#undef SPV_INST

static int vk_descriptor_arrays_smoke(VkInstance instance, VkDevice device, VkQueue queue,
        const VkGraphicsPipelineCreateInfo *template_info,
        const VkRenderPassBeginInfo *render_begin, VkImage target_image, uint32_t *target_pixels) {
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    vex_gpu_buffer_create_req_t wsi_backing = {0};
    vex_gpu_surface_create_req_t wsi_surface = {0};
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE, incomplete = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkShaderModule vertex = VK_NULL_HANDLE, fragment = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE, image_memory = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImageView views[2] = {VK_NULL_HANDLE,VK_NULL_HANDLE};
    VkSampler sampler = VK_NULL_HANDLE;
    VkSampler mip_sampler = VK_NULL_HANDLE;
    VkPipeline mip_pipeline = VK_NULL_HANDLE;
    VkShaderModule mip_fragment = VK_NULL_HANDLE;
    VkShaderModule array_fragment = VK_NULL_HANDLE;
    VkPipeline array_pipeline = VK_NULL_HANDLE;
    VkBuffer array_buffer = VK_NULL_HANDLE;
    VkDeviceMemory array_memory = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    float *uniforms = NULL;
    uint32_t *texture = NULL;
    uint32_t stage = 1u;
    VkSemaphore gate=VK_NULL_HANDLE;
    VkFence fence=VK_NULL_HANDLE;
    vk_async_submit_probe_t probe={0};
    uint32_t tid=0u,acquired=UINT32_MAX;
    uint64_t event_value=0u;
    int signaled=0;
    int ok = 0;
    VkDescriptorSetLayoutBinding bindings[2] = {
        {0u,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,3u,VK_SHADER_STAGE_FRAGMENT_BIT,NULL},
        {1u,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2u,VK_SHADER_STAGE_FRAGMENT_BIT,NULL}
    };
    VkDescriptorSetLayoutCreateInfo sl = {0}; sl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    sl.bindingCount=2u; sl.pBindings=bindings;
    VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,6u},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,4u}};
    VkDescriptorPoolCreateInfo dp={0}; dp.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dp.maxSets=2u; dp.poolSizeCount=2u; dp.pPoolSizes=sizes;
    VkDescriptorSetAllocateInfo ds={0}; ds.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ds.descriptorSetCount=1u; ds.pSetLayouts=&set_layout;
    if (vkCreateDescriptorSetLayout(device,&sl,NULL,&set_layout)!=VK_SUCCESS ||
        vkCreateDescriptorPool(device,&dp,NULL,&descriptor_pool)!=VK_SUCCESS) goto out;
    ds.descriptorPool=descriptor_pool;
    if (vkAllocateDescriptorSets(device,&ds,&set)!=VK_SUCCESS ||
        vkAllocateDescriptorSets(device,&ds,&incomplete)!=VK_SUCCESS) goto out;
    VkPushConstantRange push={VK_SHADER_STAGE_FRAGMENT_BIT,0u,16u};
    VkPipelineLayoutCreateInfo pl={0}; pl.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount=1u; pl.pSetLayouts=&set_layout; pl.pushConstantRangeCount=1u; pl.pPushConstantRanges=&push;
    if (vkCreatePipelineLayout(device,&pl,NULL,&layout)!=VK_SUCCESS) goto out;
    VkShaderModuleCreateInfo sm={0}; sm.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    sm.codeSize=sizeof(triangle_vertex_spv); sm.pCode=triangle_vertex_spv;
    if (vkCreateShaderModule(device,&sm,NULL,&vertex)!=VK_SUCCESS) goto out;
    sm.codeSize=sizeof(descriptor_arrays_fragment_spv); sm.pCode=descriptor_arrays_fragment_spv;
    if (vkCreateShaderModule(device,&sm,NULL,&fragment)!=VK_SUCCESS) goto out;
    VkPipelineShaderStageCreateInfo stages[2]; memcpy(stages,template_info->pStages,sizeof(stages));
    stages[0].module=vertex; stages[1].module=fragment;
    VkGraphicsPipelineCreateInfo pi=*template_info; pi.layout=layout; pi.pStages=stages;
    if (vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1u,&pi,NULL,&pipeline)!=VK_SUCCESS) goto out;
    stage=2u;
    VkBufferCreateInfo bi={0}; bi.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size=32u; bi.usage=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    VkMemoryAllocateInfo ma={0}; ma.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; ma.allocationSize=32u;
    if (vkCreateBuffer(device,&bi,NULL,&buffer)!=VK_SUCCESS ||
        vkAllocateMemory(device,&ma,NULL,&memory)!=VK_SUCCESS ||
        vkBindBufferMemory(device,buffer,memory,0u)!=VK_SUCCESS ||
        vkMapMemory(device,memory,0u,VK_WHOLE_SIZE,0u,(void**)&uniforms)!=VK_SUCCESS) goto out;
    const float colors[8]={0.25f,0.0f,0.0f,0.0f, 0.0f,0.0f,0.25f,0.0f};
    memcpy(uniforms,colors,sizeof(colors));
    VkImageCreateInfo ii={0}; ii.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType=VK_IMAGE_TYPE_2D; ii.format=VK_FORMAT_B8G8R8A8_UNORM;
    ii.extent=(VkExtent3D){4u,4u,1u}; ii.mipLevels=3u; ii.arrayLayers=2u;
    ii.samples=VK_SAMPLE_COUNT_1_BIT; ii.tiling=VK_IMAGE_TILING_LINEAR;
    ii.usage=VK_IMAGE_USAGE_SAMPLED_BIT;
    ma.allocationSize=168u;
    if (vkCreateImage(device,&ii,NULL,&image)!=VK_SUCCESS ||
        vkAllocateMemory(device,&ma,NULL,&image_memory)!=VK_SUCCESS ||
        vkBindImageMemory(device,image,image_memory,0u)!=VK_SUCCESS ||
        vkMapMemory(device,image_memory,0u,VK_WHOLE_SIZE,0u,(void**)&texture)!=VK_SUCCESS) goto out;
    for(uint32_t layer=0u;layer<2u;++layer){
        for(uint32_t i=0u;i<16u;++i)texture[layer*21u+i]=layer?0xff800000u:0xff008000u;
        for(uint32_t i=16u;i<20u;++i)texture[layer*21u+i]=layer?0xff000080u:0xff800000u;
        texture[layer*21u+20u]=layer?0xff008000u:0xff000080u;
    }
    VkImageViewCreateInfo vi={0}; vi.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image=image; vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=ii.format;
    vi.subresourceRange=(VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT,0u,3u,0u,1u};
    for(uint32_t i=0u;i<2u;++i){vi.subresourceRange.baseArrayLayer=i;
        if(vkCreateImageView(device,&vi,NULL,&views[i])!=VK_SUCCESS)goto out;}
    VkSamplerCreateInfo si={0}; si.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if(vkCreateSampler(device,&si,NULL,&sampler)!=VK_SUCCESS)goto out;
    VkDescriptorBufferInfo buffers[2]={{buffer,0u,16u},{buffer,16u,16u}};
    VkDescriptorImageInfo images[2]={{sampler,views[0],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {sampler,views[1],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    VkWriteDescriptorSet writes[2]={0};
    for(uint32_t i=0u;i<2u;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet=set;writes[i].dstBinding=i;writes[i].descriptorCount=2u;writes[i].descriptorType=bindings[i].descriptorType;}
    writes[0].pBufferInfo=buffers; writes[1].pImageInfo=images;
    vkUpdateDescriptorSets(device,2u,writes,0u,NULL);
    /* The second UBO array slot of this set is intentionally absent. */
    writes[0].dstSet=incomplete; writes[0].descriptorCount=1u; writes[1].dstSet=incomplete;
    vkUpdateDescriptorSets(device,2u,writes,0u,NULL);
    VkCommandPoolCreateInfo cp={0};cp.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cp.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandBufferAllocateInfo ca={0};ca.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;ca.commandBufferCount=1u;
    if(vkCreateCommandPool(device,&cp,NULL,&pool)!=VK_SUCCESS)goto out;
    ca.commandPool=pool;
    if(vkAllocateCommandBuffers(device,&ca,&cmd)!=VK_SUCCESS || !vk_subresource_begin(cmd))goto out;
    VkImageSubresourceRange all={VK_IMAGE_ASPECT_COLOR_BIT,0u,3u,0u,2u};
    vk_subresource_barrier(cmd,image,all,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if(!vk_subresource_complete(queue,cmd))goto out;
    stage=3u;
    for(uint32_t i=0u;i<3u;++i){
        if(!vk_subresource_begin(cmd))goto out;
        VkImageSubresourceRange target_range={VK_IMAGE_ASPECT_COLOR_BIT,0u,1u,0u,1u};
        vk_subresource_barrier(cmd,target_image,target_range,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        float indices[4]={(float)(i&1u),(float)(i&1u),0.0f,0.0f};
        vkCmdBeginRenderPass(cmd,render_begin,VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0u,1u,&set,0u,NULL);
        vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_FRAGMENT_BIT,0u,16u,indices);
        vkCmdDraw(cmd,3u,1u,0u,0u);vkCmdEndRenderPass(cmd);
        if(!vk_subresource_complete(queue,cmd))goto out;
        uint32_t want=i==1u?0xff800040u:0xff408000u;
        if(target_pixels[0]!=want || target_pixels[1]!=want ||
            target_pixels[2]!=0xff000000u || target_pixels[3]!=0xff000000u)goto out;
    }
    write_text("vk_test: runtime descriptor-array indexing selects independent UBOs and image layers ok\n");
    stage=4u;
    /* Copy only the second element, not its neighbors, then select it. */
    VkCopyDescriptorSet copy={0};copy.sType=VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;
    copy.srcSet=set;copy.srcBinding=0u;copy.dstSet=set;copy.dstBinding=0u;
    copy.dstArrayElement=1u;copy.descriptorCount=1u;
    vkUpdateDescriptorSets(device,0u,NULL,1u,&copy);
    if(!vk_subresource_begin(cmd))goto out;
    VkImageSubresourceRange target_range={VK_IMAGE_ASPECT_COLOR_BIT,0u,1u,0u,1u};
    vk_subresource_barrier(cmd,target_image,target_range,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    float indices[4]={1.0f,1.0f,0.0f,0.0f};
    vkCmdBeginRenderPass(cmd,render_begin,VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0u,1u,&set,0u,NULL);
    vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_FRAGMENT_BIT,0u,16u,indices);
    vkCmdDraw(cmd,3u,1u,0u,0u);vkCmdEndRenderPass(cmd);
    if(!vk_subresource_complete(queue,cmd)||target_pixels[0]!=0xffc00000u)goto out;
    stage=5u;
    uint32_t before[4];memcpy(before,target_pixels,sizeof(before));
    if(!vk_subresource_begin(cmd))goto out;
    vk_subresource_barrier(cmd,target_image,target_range,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkCmdBeginRenderPass(cmd,render_begin,VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0u,1u,&incomplete,0u,NULL);
    vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_FRAGMENT_BIT,0u,16u,indices);
    vkCmdDraw(cmd,3u,1u,0u,0u);vkCmdEndRenderPass(cmd);
    VkSubmitInfo submit={0};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1u;submit.pCommandBuffers=&cmd;
    if(vkEndCommandBuffer(cmd)!=VK_SUCCESS || vkQueueSubmit(queue,1u,&submit,VK_NULL_HANDLE)==VK_SUCCESS ||
        memcmp(before,target_pixels,sizeof(before))!=0)goto out;
    write_text("vk_test: descriptor array element copy and missing-element preflight preserve neighbors ok\n");
    stage=6u;
    /* Preserve the same shader, replacing implicit sampling by a Lod operand
       read from push.z. This also checks fractional trilinear interpolation. */
    uint32_t mip_code[sizeof(descriptor_arrays_fragment_spv)/4u+7u];
    uint32_t used=5u;
    memcpy(mip_code,descriptor_arrays_fragment_spv,5u*4u);mip_code[3]=42u;
    for(uint32_t offset=5u;offset<sizeof(descriptor_arrays_fragment_spv)/4u;){
        uint32_t wc=descriptor_arrays_fragment_spv[offset]>>16u;
        if((descriptor_arrays_fragment_spv[offset]&0xffffu)==87u){
            mip_code[used++]=(5u<<16u)|81u; mip_code[used++]=2u;mip_code[used++]=41u;mip_code[used++]=28u;mip_code[used++]=2u;
            memcpy(mip_code+used,descriptor_arrays_fragment_spv+offset,5u*4u);
            mip_code[used]=(7u<<16u)|88u;used+=5u;mip_code[used++]=2u;mip_code[used++]=41u;
        }else{memcpy(mip_code+used,descriptor_arrays_fragment_spv+offset,wc*4u);used+=wc;}
        offset+=wc;
    }
    sm.codeSize=used*4u;sm.pCode=mip_code;
    if(vkCreateShaderModule(device,&sm,NULL,&mip_fragment)!=VK_SUCCESS)goto out;
    stages[1].module=mip_fragment;
    if(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1u,&pi,NULL,&mip_pipeline)!=VK_SUCCESS)goto out;
    for(uint32_t mode=0u;mode<2u;++mode){
        si.maxLod=2.0f;si.mipmapMode=mode?VK_SAMPLER_MIPMAP_MODE_LINEAR:VK_SAMPLER_MIPMAP_MODE_NEAREST;
        if(vkCreateSampler(device,&si,NULL,&mip_sampler)!=VK_SUCCESS)goto out;
        images[0].sampler=images[1].sampler=mip_sampler;
        writes[1].dstSet=set;vkUpdateDescriptorSets(device,1u,&writes[1],0u,NULL);
        const float lods[7]={0.0f,1.0f,2.0f,0.5f,1.5f,-99.0f,99.0f};
        for(uint32_t i=0u;i<7u;++i){
            if(!vk_subresource_begin(cmd))goto out;
            vk_subresource_barrier(cmd,target_image,target_range,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            float params[4]={0.0f,1.0f,lods[i],0.0f};
            vkCmdBeginRenderPass(cmd,render_begin,VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,mip_pipeline);
            vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0u,1u,&set,0u,NULL);
            vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_FRAGMENT_BIT,0u,16u,params);
            vkCmdDraw(cmd,3u,1u,0u,0u);vkCmdEndRenderPass(cmd);
            if(!vk_subresource_complete(queue,cmd))goto out;
            uint32_t want;
            if(i==0u || i==5u)want=0xffc00000u;
            else if(i==1u)want=0xff400080u;
            else if(i==2u || i==6u)want=0xff408000u;
            else if(i==3u)want=mode?0xff800040u:0xff400080u;
            else want=mode?0xff404040u:0xff408000u;
            if(target_pixels[0]!=want || target_pixels[1]!=want)goto out;
        }
        vkDestroySampler(device,mip_sampler,NULL);mip_sampler=VK_NULL_HANDLE;
    }
    write_text("vk_test: explicit shader LOD selects mips with nearest/trilinear filtering and clamps ok\n");
    stage=7u;
    sm.codeSize=sizeof(uniform_array_fragment_spv);sm.pCode=uniform_array_fragment_spv;
    if(vkCreateShaderModule(device,&sm,NULL,&array_fragment)!=VK_SUCCESS)goto out;
    stages[1].module=array_fragment;
    if(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1u,&pi,NULL,&array_pipeline)!=VK_SUCCESS)goto out;
    bi.size=80u;ma.allocationSize=80u;
    void *array_bytes=NULL;
    if(vkCreateBuffer(device,&bi,NULL,&array_buffer)!=VK_SUCCESS ||
        vkAllocateMemory(device,&ma,NULL,&array_memory)!=VK_SUCCESS ||
        vkBindBufferMemory(device,array_buffer,array_memory,0u)!=VK_SUCCESS ||
        vkMapMemory(device,array_memory,0u,VK_WHOLE_SIZE,0u,&array_bytes)!=VK_SUCCESS)goto out;
    float values[16]={0.25f,0.0f,0.0f,0.0f, 0.0f,0.5f,0.0f,0.0f,
        0.0f,0.0f,1.0f,0.0f, 0.25f,0.5f,1.0f,0.0f};
    memcpy(array_bytes,values,sizeof(values));
    VkDescriptorBufferInfo array_info={array_buffer,0u,80u};
    writes[0].dstSet=set;writes[0].descriptorCount=1u;writes[0].pBufferInfo=&array_info;
    vkUpdateDescriptorSets(device,1u,&writes[0],0u,NULL);
    const uint32_t expected[4]={0xff400000u,0xff008000u,0xff0000ffu,0xff4080ffu};
    for(uint32_t i=0u;i<4u;++i){
        memcpy((uint8_t*)array_bytes+64u,&i,4u);
        if(!vk_subresource_begin(cmd))goto out;
        vk_subresource_barrier(cmd,target_image,target_range,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        vkCmdBeginRenderPass(cmd,render_begin,VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,array_pipeline);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0u,1u,&set,0u,NULL);
        vkCmdDraw(cmd,3u,1u,0u,0u);vkCmdEndRenderPass(cmd);
        if(!vk_subresource_complete(queue,cmd)||target_pixels[0]!=expected[i]||target_pixels[1]!=expected[i])goto out;
    }
    write_text("vk_test: std140 arrays, integer UBO loads and chained dynamic/component AccessChain ok\n");
    stage=8u;
    /* This test precedes the main WSI smoke. Its own acquire is the external
       semaphore signal, avoiding a same-queue signal behind the waiting draw. */
    wsi_backing.width=2u;wsi_backing.height=2u;wsi_backing.stride=2u;
    wsi_backing.format=VEX_GPU_FORMAT_XRGB8888;
    wsi_backing.flags=VEX_GPU_MEMORY_CPU_VISIBLE;wsi_backing.size=16u;
    if(vex_gpu_buffer_create(&wsi_backing)!=0)goto out;
    wsi_surface.buffer_handle=wsi_backing.handle;
    wsi_surface.alpha_mode=VEX_GPU_SURFACE_ALPHA_OPAQUE;
    wsi_surface.usage=VEX_GPU_SURFACE_USAGE_SAMPLED;
    if(vex_gpu_surface_create(&wsi_surface)!=0)goto out;
    VkVexSurfaceCreateInfoVEX surface_info={0};
    surface_info.sType=VK_STRUCTURE_TYPE_VEX_SURFACE_CREATE_INFO_EXPERIMENTAL;
    surface_info.surfaceCapability=wsi_surface.surface_handle;
    if(vkCreateVexSurfaceVEX(instance,&surface_info,NULL,&surface)!=VK_SUCCESS)goto out;
    VkSwapchainCreateInfoKHR swap_info={0};
    swap_info.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;swap_info.surface=surface;
    swap_info.minImageCount=2u;swap_info.imageFormat=VK_FORMAT_B8G8R8A8_UNORM;
    swap_info.imageColorSpace=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swap_info.imageExtent=(VkExtent2D){2u,2u};swap_info.imageArrayLayers=1u;
    swap_info.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swap_info.preTransform=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swap_info.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swap_info.presentMode=VK_PRESENT_MODE_FIFO_KHR;swap_info.clipped=VK_TRUE;
    if(vkCreateSwapchainKHR(device,&swap_info,NULL,&swapchain)!=VK_SUCCESS)goto out;
    si.maxLod=0.0f;si.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
    if(vkCreateSampler(device,&si,NULL,&mip_sampler)!=VK_SUCCESS)goto out;
    images[0].sampler=images[1].sampler=mip_sampler;
    writes[0].pBufferInfo=buffers;writes[0].descriptorCount=2u;
    vkUpdateDescriptorSets(device,2u,writes,0u,NULL);
    if(!vk_subresource_begin(cmd))goto out;
    vk_subresource_barrier(cmd,target_image,target_range,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    indices[0]=0.0f;indices[1]=1.0f;
    vkCmdBeginRenderPass(cmd,render_begin,VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0u,1u,&set,0u,NULL);
    vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_FRAGMENT_BIT,0u,16u,indices);
    vkCmdDraw(cmd,3u,1u,0u,0u);vkCmdEndRenderPass(cmd);
    if(vkEndCommandBuffer(cmd)!=VK_SUCCESS)goto out;
    stage=9u;
    VkSemaphoreCreateInfo se={0};se.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fe={0};fe.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if(vkCreateSemaphore(device,&se,NULL,&gate)!=VK_SUCCESS||vkCreateFence(device,&fe,NULL,&fence)!=VK_SUCCESS)goto out;
    VkPipelineStageFlags wait_stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    probe.queue=queue;probe.fence=fence;probe.submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
    probe.submit.waitSemaphoreCount=1u;probe.submit.pWaitSemaphores=&gate;probe.submit.pWaitDstStageMask=&wait_stage;
    probe.submit.commandBufferCount=1u;probe.submit.pCommandBuffers=&cmd;
    probe.started_event=vex_event_counter_create(0u,0u);probe.returned_event=vex_event_counter_create(0u,0u);
    if((int64_t)probe.started_event<=0||(int64_t)probe.returned_event<=0||vex_thread_create(vk_async_submit_probe_thread,&probe,0u,&tid)!=0)goto out;
    if(vex_event_counter_read(probe.started_event,&event_value,0u,100u)!=0 ||
        vex_event_counter_read(probe.returned_event,&event_value,0u,100u)!=0 ||
        probe.result!=VK_SUCCESS || vkGetFenceStatus(device,fence)!=VK_NOT_READY){
        write_hex32("vk_test: pending array submit result=",(uint32_t)probe.result);
        goto out;
    }
    stage=10u;
    /* The queued descriptor snapshot, not the now-closed local owners, must
       keep both array elements and their backings usable by the worker. */
    vkDestroyCommandPool(device,pool,NULL);pool=VK_NULL_HANDLE;cmd=VK_NULL_HANDLE;
    vkDestroyDescriptorPool(device,descriptor_pool,NULL);descriptor_pool=VK_NULL_HANDLE;
    vkDestroySampler(device,mip_sampler,NULL);mip_sampler=VK_NULL_HANDLE;
    for(uint32_t i=0u;i<2u;++i){vkDestroyImageView(device,views[i],NULL);views[i]=VK_NULL_HANDLE;}
    vkDestroyImage(device,image,NULL);image=VK_NULL_HANDLE;
    vkFreeMemory(device,image_memory,NULL);image_memory=VK_NULL_HANDLE;
    vkDestroyBuffer(device,buffer,NULL);buffer=VK_NULL_HANDLE;
    vkFreeMemory(device,memory,NULL);memory=VK_NULL_HANDLE;
    stage=11u;
    if(vkAcquireNextImageKHR(device,swapchain,UINT64_MAX,gate,VK_NULL_HANDLE,&acquired)!=VK_SUCCESS)goto out;
    signaled=1;
    if(vex_thread_join(tid,NULL)!=0)goto out;tid=0u;
    stage=12u;
    if(vkWaitForFences(device,1u,&fence,VK_TRUE,UINT64_MAX)!=VK_SUCCESS||
        vkQueueWaitIdle(queue)!=VK_SUCCESS)goto out;
    stage=13u;
    if(target_pixels[0]!=0xffc00000u||target_pixels[1]!=0xffc00000u){
        write_hex32("vk_test: pending array pixel0=",target_pixels[0]);
        write_hex32("vk_test: pending array pixel1=",target_pixels[1]);
        goto out;
    }
    write_text("vk_test: pending descriptor arrays retain views, samplers, buffers and backings after owner closure ok\n");
    ok=1;
out:
    if(!ok)write_hex32("vk_test: descriptor array failure stage=",stage);
    if(tid){
        if(!signaled && gate && vkAcquireNextImageKHR(device,swapchain,UINT64_MAX,gate,VK_NULL_HANDLE,&acquired)==VK_SUCCESS)signaled=1;
        (void)vex_thread_join(tid,NULL);
    }
    (void)vkQueueWaitIdle(queue);
    if(acquired!=UINT32_MAX){
        VkImage swap_images[4];uint32_t count=4u;
        if(vkGetSwapchainImagesKHR(device,swapchain,&count,swap_images)!=VK_SUCCESS || acquired>=count ||
            (pool==VK_NULL_HANDLE && vkCreateCommandPool(device,&cp,NULL,&pool)!=VK_SUCCESS)){ok=0;}
        else {
            ca.commandPool=pool;
            if(cmd==VK_NULL_HANDLE && vkAllocateCommandBuffers(device,&ca,&cmd)!=VK_SUCCESS)ok=0;
            else if(!vk_subresource_begin(cmd))ok=0;
            else {
                VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0u,1u,0u,1u};
                vk_subresource_barrier(cmd,swap_images[acquired],range,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
                if(!vk_subresource_complete(queue,cmd))ok=0;
                else {
                    VkPresentInfoKHR present={0};present.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
                    present.swapchainCount=1u;present.pSwapchains=&swapchain;present.pImageIndices=&acquired;
                    if(vkQueuePresentKHR(queue,&present)!=VK_SUCCESS || vkQueueWaitIdle(queue)!=VK_SUCCESS)ok=0;
                }
            }
        }
    }
    if(pool)vkDestroyCommandPool(device,pool,NULL);
    if(pipeline)vkDestroyPipeline(device,pipeline,NULL);
    if(mip_pipeline)vkDestroyPipeline(device,mip_pipeline,NULL);
    if(array_pipeline)vkDestroyPipeline(device,array_pipeline,NULL);
    if(vertex)vkDestroyShaderModule(device,vertex,NULL);
    if(fragment)vkDestroyShaderModule(device,fragment,NULL);
    if(mip_fragment)vkDestroyShaderModule(device,mip_fragment,NULL);
    if(array_fragment)vkDestroyShaderModule(device,array_fragment,NULL);
    if(layout)vkDestroyPipelineLayout(device,layout,NULL);
    if(descriptor_pool)vkDestroyDescriptorPool(device,descriptor_pool,NULL);
    if(set_layout)vkDestroyDescriptorSetLayout(device,set_layout,NULL);
    for(uint32_t i=0u;i<2u;++i)if(views[i])vkDestroyImageView(device,views[i],NULL);
    if(sampler)vkDestroySampler(device,sampler,NULL);
    if(mip_sampler)vkDestroySampler(device,mip_sampler,NULL);
    if(image)vkDestroyImage(device,image,NULL);
    if(image_memory)vkFreeMemory(device,image_memory,NULL);
    if(buffer)vkDestroyBuffer(device,buffer,NULL);
    if(memory)vkFreeMemory(device,memory,NULL);
    if(array_buffer)vkDestroyBuffer(device,array_buffer,NULL);
    if(array_memory)vkFreeMemory(device,array_memory,NULL);
    if(gate)vkDestroySemaphore(device,gate,NULL);
    if(fence)vkDestroyFence(device,fence,NULL);
    if(swapchain)vkDestroySwapchainKHR(device,swapchain,NULL);
    if(surface)vkDestroySurfaceKHR(instance,surface,NULL);
    if(wsi_surface.surface_handle)(void)vex_gpu_surface_destroy(wsi_surface.surface_handle);
    if(wsi_backing.handle)(void)vex_gpu_buffer_destroy(wsi_backing.handle);
    if((int64_t)probe.started_event>0)(void)vex_event_counter_release(probe.started_event);
    if((int64_t)probe.returned_event>0)(void)vex_event_counter_release(probe.returned_event);
    return ok;
}
