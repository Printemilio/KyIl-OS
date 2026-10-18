static int vk_stencil_smoke(VkDevice device,VkQueue queue,
        const VkGraphicsPipelineCreateInfo *template_info,VkImageView color_view,uint32_t *color_pixels){
    VkImage image=VK_NULL_HANDLE;
    VkDeviceMemory memory=VK_NULL_HANDLE,bm=VK_NULL_HANDLE;
    VkBuffer buffer=VK_NULL_HANDLE;
    VkImageView view=VK_NULL_HANDLE;
    VkRenderPass pass=VK_NULL_HANDLE;
    VkFramebuffer fb=VK_NULL_HANDLE;
    VkPipeline pipeline=VK_NULL_HANDLE;
    VkShaderModule vertex=VK_NULL_HANDLE,fragment=VK_NULL_HANDLE;
    VkCommandPool pool=VK_NULL_HANDLE;
    VkCommandBuffer cmd=VK_NULL_HANDLE;
    uint8_t *stencil=NULL,*bytes=NULL;
    uint32_t stage=1u;
    int ok=0;
    VkImageCreateInfo ii={0};ii.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType=VK_IMAGE_TYPE_2D;ii.format=VK_FORMAT_S8_UINT;ii.extent=(VkExtent3D){4u,4u,1u};
    ii.mipLevels=3u;ii.arrayLayers=2u;ii.samples=VK_SAMPLE_COUNT_1_BIT;ii.tiling=VK_IMAGE_TILING_OPTIMAL;
    ii.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkMemoryAllocateInfo ma={0};ma.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;ma.allocationSize=42u;
    VkMemoryRequirements req={0};
    if(vkCreateImage(device,&ii,NULL,&image)!=VK_SUCCESS)goto out;
    vkGetImageMemoryRequirements(device,image,&req);
    if(req.size!=42u || vkAllocateMemory(device,&ma,NULL,&memory)!=VK_SUCCESS ||
        vkBindImageMemory(device,image,memory,0u)!=VK_SUCCESS ||
        vkMapMemory(device,memory,0u,VK_WHOLE_SIZE,0u,(void**)&stencil)!=VK_SUCCESS)goto out;
    memset(stencil,5,42u);
    VkBufferCreateInfo bi={0};bi.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;bi.size=32u;
    bi.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;ma.allocationSize=32u;
    if(vkCreateBuffer(device,&bi,NULL,&buffer)!=VK_SUCCESS || vkAllocateMemory(device,&ma,NULL,&bm)!=VK_SUCCESS ||
        vkBindBufferMemory(device,buffer,bm,0u)!=VK_SUCCESS ||
        vkMapMemory(device,bm,0u,VK_WHOLE_SIZE,0u,(void**)&bytes)!=VK_SUCCESS)goto out;
    VkCommandPoolCreateInfo cp={0};cp.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;cp.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandBufferAllocateInfo ca={0};ca.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;ca.commandBufferCount=1u;
    if(vkCreateCommandPool(device,&cp,NULL,&pool)!=VK_SUCCESS)goto out;ca.commandPool=pool;
    if(vkAllocateCommandBuffers(device,&ca,&cmd)!=VK_SUCCESS||!vk_subresource_begin(cmd))goto out;
    VkImageSubresourceRange all={VK_IMAGE_ASPECT_STENCIL_BIT,0u,3u,0u,2u};
    VkImageSubresourceRange one={VK_IMAGE_ASPECT_STENCIL_BIT,1u,1u,1u,1u};
    VkClearDepthStencilValue clear={0.0f,0xabu};
    vk_subresource_barrier(cmd,image,all,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL);
    vkCmdClearDepthStencilImage(cmd,image,VK_IMAGE_LAYOUT_GENERAL,&clear,1u,&one);
    memset(bytes,0xcc,32u);
    VkBufferImageCopy copy={0};copy.bufferOffset=4u;copy.bufferRowLength=4u;copy.bufferImageHeight=3u;
    copy.imageSubresource=(VkImageSubresourceLayers){VK_IMAGE_ASPECT_STENCIL_BIT,1u,1u,1u};copy.imageExtent=(VkExtent3D){2u,2u,1u};
    vkCmdCopyImageToBuffer(cmd,image,VK_IMAGE_LAYOUT_GENERAL,buffer,1u,&copy);
    if(!vk_subresource_complete(queue,cmd))goto out;
    for(uint32_t i=0u;i<42u;++i)if(stencil[i]!=(i>=37u&&i<41u?0xabu:5u))goto out;
    for(uint32_t i=0u;i<32u;++i)if(bytes[i]!=((i==4u||i==5u||i==8u||i==9u)?0xabu:0xccu))goto out;
    bytes[4u]=0x12u;bytes[5u]=0x34u;bytes[8u]=0x56u;bytes[9u]=0x78u;
    if(!vk_subresource_begin(cmd))goto out;
    vkCmdCopyBufferToImage(cmd,buffer,image,VK_IMAGE_LAYOUT_GENERAL,1u,&copy);
    if(!vk_subresource_complete(queue,cmd)||stencil[37u]!=0x12u||stencil[38u]!=0x34u||stencil[39u]!=0x56u||stencil[40u]!=0x78u)goto out;
    write_text("vk_test: S8 mip/layer clear and padded byte transfers preserve neighbors ok\n");
    stage=2u;
    VkImageViewCreateInfo vi={0};vi.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image=image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=ii.format;vi.subresourceRange=one;
    if(vkCreateImageView(device,&vi,NULL,&view)!=VK_SUCCESS)goto out;
    VkAttachmentDescription at[2]={0};
    at[0].format=VK_FORMAT_B8G8R8A8_UNORM;at[0].samples=VK_SAMPLE_COUNT_1_BIT;
    at[0].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;at[0].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    at[0].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;at[0].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    at[0].initialLayout=at[0].finalLayout=VK_IMAGE_LAYOUT_GENERAL;
    at[1].format=ii.format;at[1].samples=VK_SAMPLE_COUNT_1_BIT;
    at[1].loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;at[1].storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    at[1].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_LOAD;at[1].stencilStoreOp=VK_ATTACHMENT_STORE_OP_STORE;
    at[1].initialLayout=at[1].finalLayout=VK_IMAGE_LAYOUT_GENERAL;
    VkAttachmentReference color={0u,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},depth={1u,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub={0};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount=1u;sub.pColorAttachments=&color;sub.pDepthStencilAttachment=&depth;
    VkRenderPassCreateInfo rp={0};rp.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;rp.attachmentCount=2u;rp.pAttachments=at;
    rp.subpassCount=1u;rp.pSubpasses=&sub;
    if(vkCreateRenderPass(device,&rp,NULL,&pass)!=VK_SUCCESS)goto out;
    VkImageView views[2]={color_view,view};VkFramebufferCreateInfo fi={0};fi.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass=pass;fi.attachmentCount=2u;fi.pAttachments=views;fi.width=fi.height=2u;fi.layers=1u;
    if(vkCreateFramebuffer(device,&fi,NULL,&fb)!=VK_SUCCESS)goto out;
    VkShaderModuleCreateInfo sm={0};sm.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    sm.codeSize=sizeof(triangle_vertex_spv);sm.pCode=triangle_vertex_spv;
    if(vkCreateShaderModule(device,&sm,NULL,&vertex)!=VK_SUCCESS)goto out;
    sm.codeSize=sizeof(solid_fragment_spv);sm.pCode=solid_fragment_spv;
    if(vkCreateShaderModule(device,&sm,NULL,&fragment)!=VK_SUCCESS)goto out;
    VkPipelineShaderStageCreateInfo stages[2];memcpy(stages,template_info->pStages,sizeof(stages));stages[0].module=vertex;stages[1].module=fragment;
    VkPipelineRasterizationStateCreateInfo raster=*template_info->pRasterizationState;
    VkPipelineDepthStencilStateCreateInfo state={0};state.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    state.stencilTestEnable=VK_TRUE;
    VkGraphicsPipelineCreateInfo pi=*template_info;pi.renderPass=pass;pi.pStages=stages;pi.pRasterizationState=&raster;pi.pDepthStencilState=&state;
    VkClearValue cv[2]={0};cv[0].color.float32[3]=1.0f;
    VkRenderPassBeginInfo begin={0};begin.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass=pass;begin.framebuffer=fb;begin.renderArea.extent=(VkExtent2D){2u,2u};begin.clearValueCount=2u;begin.pClearValues=cv;
    const uint8_t initial[12]={0xab,0xab,0xab,255,0,0xab,255,0,0xab,0xab,0xab,0xab};
    const uint8_t expected[12]={0xab,0,0xa5,255,0,0x54,0,255,0x27,0xac,0x11,0x22};
    for(uint32_t i=0u;i<12u;++i){
        stage=3u+i;
        memset(stencil+37u,initial[i],4u);
        state.front=(VkStencilOpState){VK_STENCIL_OP_KEEP,(VkStencilOp)i,VK_STENCIL_OP_KEEP,VK_COMPARE_OP_ALWAYS,0xffu,0xffu,0x12345u};
        if(i==2u)state.front.writeMask=0xfu;
        if(i==8u){state.front.failOp=VK_STENCIL_OP_REPLACE;state.front.passOp=VK_STENCIL_OP_KEEP;state.front.reference=0x27u;state.front.compareOp=VK_COMPARE_OP_NEVER;}
        if(i==9u){state.front.passOp=VK_STENCIL_OP_INCREMENT_AND_WRAP;state.front.compareOp=VK_COMPARE_OP_EQUAL;state.front.compareMask=0xfu;state.front.reference=0xbu;}
        if(i>=10u){state.front.passOp=VK_STENCIL_OP_REPLACE;state.front.reference=0x11u;}
        state.back=state.front;
        if(i>=10u)state.back.reference=0x22u;
        raster.frontFace=i==11u?VK_FRONT_FACE_CLOCKWISE:VK_FRONT_FACE_COUNTER_CLOCKWISE;
        if(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1u,&pi,NULL,&pipeline)!=VK_SUCCESS||!vk_subresource_begin(cmd))goto out;
        vkCmdBeginRenderPass(cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        vkCmdDraw(cmd,3u,1u,0u,0u);vkCmdEndRenderPass(cmd);
        if(!vk_subresource_complete(queue,cmd))goto out;
        uint32_t want=i==8u?0xff000000u:0xffff0000u;
        if(color_pixels[0]!=want||color_pixels[1]!=want||color_pixels[2]!=0xff000000u||color_pixels[3]!=0xff000000u ||
            stencil[37u]!=expected[i]||stencil[38u]!=expected[i]||stencil[39u]!=initial[i]||stencil[40u]!=initial[i])goto out;
        for(uint32_t j=0u;j<42u;++j)if((j<37u||j>=41u)&&stencil[j]!=5u)goto out;
        vkDestroyPipeline(device,pipeline,NULL);pipeline=VK_NULL_HANDLE;
    }
    write_text("vk_test: stencil compare/masks, eight operations, front/back and partial-view rasterization ok\n");
    stage=16u;
    state.front.passOp=(VkStencilOp)-1;
    if(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1u,&pi,NULL,&pipeline)==VK_SUCCESS||pipeline!=VK_NULL_HANDLE)goto out;
    if(vkResetCommandBuffer(cmd,0u)!=VK_SUCCESS)goto out;
    vkDestroyFramebuffer(device,fb,NULL);fb=VK_NULL_HANDLE;
    vkDestroyRenderPass(device,pass,NULL);pass=VK_NULL_HANDLE;
    at[1].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    if(vkCreateRenderPass(device,&rp,NULL,&pass)!=VK_SUCCESS)goto out;
    fi.renderPass=pass;
    if(vkCreateFramebuffer(device,&fi,NULL,&fb)!=VK_SUCCESS||!vk_subresource_begin(cmd))goto out;
    begin.renderPass=pass;begin.framebuffer=fb;cv[1].depthStencil.stencil=0x123u;
    vkCmdBeginRenderPass(cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);vkCmdEndRenderPass(cmd);
    if(!vk_subresource_complete(queue,cmd))goto out;
    for(uint32_t i=0u;i<42u;++i)if(stencil[i]!=((i>=37u&&i<41u)?0x23u:5u))goto out;
    write_text("vk_test: stencil attachment load-clear selects only its view and invalid operations reject ok\n");
    ok=1;
out:
    if(!ok)write_hex32("vk_test: stencil failure stage=",stage);
    (void)vkQueueWaitIdle(queue);
    if(pool)vkDestroyCommandPool(device,pool,NULL);
    if(pipeline)vkDestroyPipeline(device,pipeline,NULL);
    if(vertex)vkDestroyShaderModule(device,vertex,NULL);
    if(fragment)vkDestroyShaderModule(device,fragment,NULL);
    if(fb)vkDestroyFramebuffer(device,fb,NULL);
    if(pass)vkDestroyRenderPass(device,pass,NULL);
    if(view)vkDestroyImageView(device,view,NULL);
    if(image)vkDestroyImage(device,image,NULL);
    if(memory)vkFreeMemory(device,memory,NULL);
    if(buffer)vkDestroyBuffer(device,buffer,NULL);
    if(bm)vkFreeMemory(device,bm,NULL);
    return ok;
}
