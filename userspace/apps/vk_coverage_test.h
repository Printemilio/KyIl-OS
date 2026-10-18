/* Two triangles tile a 4096x4096 viewport; only its first row is stored.
   Pixel contents plus exact occlusion count distinguish both cracks and
   duplicate coverage along the shared diagonal, at the subpixel limit. */
static int vk_large_coverage_smoke(VkDevice device,VkQueue queue,const VkGraphicsPipelineCreateInfo *base){
    VkImage image=0;VkImageView view=0;VkDeviceMemory memory=0,vertices_memory=0;
    VkBuffer vertices=0;VkRenderPass pass=0;VkFramebuffer fb=0;VkPipeline pipeline=0;
    VkShaderModule vertex=0,fragment=0;VkCommandPool pool=0;VkCommandBuffer cmd=0;VkQueryPool query=0;
    uint32_t *pixels=0;float *positions=0;int ok=0;
    VkImageCreateInfo ii={0};ii.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;ii.imageType=VK_IMAGE_TYPE_2D;
    ii.format=VK_FORMAT_B8G8R8A8_UNORM;ii.extent=(VkExtent3D){4096u,1u,1u};ii.mipLevels=ii.arrayLayers=1u;
    ii.samples=VK_SAMPLE_COUNT_4_BIT;ii.tiling=VK_IMAGE_TILING_OPTIMAL;ii.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkMemoryAllocateInfo ma={0};ma.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;ma.allocationSize=4096u*4u*4u;
    if(vkCreateImage(device,&ii,NULL,&image)!=VK_SUCCESS||vkAllocateMemory(device,&ma,NULL,&memory)!=VK_SUCCESS||
       vkBindImageMemory(device,image,memory,0u)!=VK_SUCCESS||vkMapMemory(device,memory,0u,VK_WHOLE_SIZE,0u,(void**)&pixels)!=VK_SUCCESS)goto out;
    VkImageViewCreateInfo vi={0};vi.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;vi.image=image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=ii.format;
    vi.subresourceRange=(VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT,0u,1u,0u,1u};
    if(vkCreateImageView(device,&vi,NULL,&view)!=VK_SUCCESS)goto out;
    VkAttachmentDescription at={0};at.format=ii.format;at.samples=ii.samples;at.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;at.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    at.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;at.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;at.finalLayout=VK_IMAGE_LAYOUT_GENERAL;
    VkAttachmentReference color={0u,VK_IMAGE_LAYOUT_GENERAL};VkSubpassDescription sub={0};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1u;sub.pColorAttachments=&color;
    VkRenderPassCreateInfo rp={0};rp.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;rp.attachmentCount=rp.subpassCount=1u;rp.pAttachments=&at;rp.pSubpasses=&sub;
    if(vkCreateRenderPass(device,&rp,NULL,&pass)!=VK_SUCCESS)goto out;
    VkFramebufferCreateInfo fi={0};fi.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;fi.renderPass=pass;fi.attachmentCount=1u;fi.pAttachments=&view;fi.width=4096u;fi.height=fi.layers=1u;
    if(vkCreateFramebuffer(device,&fi,NULL,&fb)!=VK_SUCCESS)goto out;
    VkShaderModuleCreateInfo sm={0};sm.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;sm.pCode=vertex_attribute_spv;sm.codeSize=sizeof(vertex_attribute_spv);
    if(vkCreateShaderModule(device,&sm,NULL,&vertex)!=VK_SUCCESS)goto out;sm.pCode=solid_fragment_spv;sm.codeSize=sizeof(solid_fragment_spv);
    if(vkCreateShaderModule(device,&sm,NULL,&fragment)!=VK_SUCCESS)goto out;
    VkPipelineShaderStageCreateInfo stages[2];memcpy(stages,base->pStages,sizeof(stages));stages[0].module=vertex;stages[1].module=fragment;
    VkVertexInputBindingDescription binding={0u,16u,VK_VERTEX_INPUT_RATE_VERTEX};VkVertexInputAttributeDescription attribute={0u,0u,VK_FORMAT_R32G32B32A32_SFLOAT,0u};
    VkPipelineVertexInputStateCreateInfo input={0};input.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;input.vertexBindingDescriptionCount=input.vertexAttributeDescriptionCount=1u;
    input.pVertexBindingDescriptions=&binding;input.pVertexAttributeDescriptions=&attribute;
    VkViewport viewport={0.0f,0.0f,4096.0f,4096.0f,0.0f,1.0f};VkRect2D scissor={{0,0},{4096u,1u}};
    VkPipelineViewportStateCreateInfo vp=*base->pViewportState;vp.pViewports=&viewport;vp.pScissors=&scissor;
    VkPipelineMultisampleStateCreateInfo ms=*base->pMultisampleState;ms.rasterizationSamples=ii.samples;ms.pSampleMask=NULL;ms.alphaToCoverageEnable=VK_FALSE;
    VkPipelineRasterizationStateCreateInfo rs=*base->pRasterizationState;rs.cullMode=VK_CULL_MODE_NONE;rs.depthBiasEnable=VK_FALSE;
    VkGraphicsPipelineCreateInfo pi=*base;pi.renderPass=pass;pi.pStages=stages;pi.pVertexInputState=&input;pi.pViewportState=&vp;
    pi.pMultisampleState=&ms;pi.pRasterizationState=&rs;pi.pDepthStencilState=NULL;pi.pDynamicState=NULL;
    if(vkCreateGraphicsPipelines(device,0,1u,&pi,NULL,&pipeline)!=VK_SUCCESS)goto out;
    VkBufferCreateInfo bi={0};bi.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;bi.size=96u;bi.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;ma.allocationSize=96u;
    if(vkCreateBuffer(device,&bi,NULL,&vertices)!=VK_SUCCESS||vkAllocateMemory(device,&ma,NULL,&vertices_memory)!=VK_SUCCESS||
       vkBindBufferMemory(device,vertices,vertices_memory,0u)!=VK_SUCCESS||vkMapMemory(device,vertices_memory,0u,VK_WHOLE_SIZE,0u,(void**)&positions)!=VK_SUCCESS)goto out;
    const float corners[24]={-1,-1,0,1, 1,-1,0,1, -1,1,0,1, -1,1,0,1, 1,-1,0,1, 1,1,0,1};memcpy(positions,corners,sizeof(corners));
    VkCommandPoolCreateInfo cp={0};cp.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;cp.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandBufferAllocateInfo ca={0};ca.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;ca.commandBufferCount=1u;
    if(vkCreateCommandPool(device,&cp,NULL,&pool)!=VK_SUCCESS)goto out;ca.commandPool=pool;
    if(vkAllocateCommandBuffers(device,&ca,&cmd)!=VK_SUCCESS||!vk_subresource_begin(cmd))goto out;
    VkQueryPoolCreateInfo qc={0};qc.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;qc.queryType=VK_QUERY_TYPE_OCCLUSION;qc.queryCount=1u;
    if(vkCreateQueryPool(device,&qc,NULL,&query)!=VK_SUCCESS)goto out;vkCmdResetQueryPool(cmd,query,0u,1u);vkCmdBeginQuery(cmd,query,0u,VK_QUERY_CONTROL_PRECISE_BIT);
    VkClearValue clear={0};clear.color.float32[3]=1.0f;VkRenderPassBeginInfo begin={0};begin.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass=pass;begin.framebuffer=fb;begin.renderArea=scissor;begin.clearValueCount=1u;begin.pClearValues=&clear;
    vkCmdBeginRenderPass(cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
    VkDeviceSize offset=0u;vkCmdBindVertexBuffers(cmd,0u,1u,&vertices,&offset);vkCmdDraw(cmd,6u,1u,0u,0u);vkCmdEndRenderPass(cmd);vkCmdEndQuery(cmd,query,0u);
    if(!vk_subresource_complete(queue,cmd))goto out;
    uint64_t samples=0u;if(vkGetQueryPoolResults(device,query,0u,1u,sizeof(samples),&samples,8u,VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT)!=VK_SUCCESS||samples!=16384u)goto out;
    for(uint32_t i=0u;i<16384u;++i)if(pixels[i]!=0xffff0000u)goto out;
    ok=1;write_text("vk_test: 4096px MSAA shared-edge coverage has exactly 16384 samples, no cracks/overdraw and exact pixels ok\n");
out:
    if(!ok)write_text("vk_test: large subpixel coverage failed\n");(void)vkQueueWaitIdle(queue);
    if(pool)vkDestroyCommandPool(device,pool,NULL);if(pipeline)vkDestroyPipeline(device,pipeline,NULL);if(query)vkDestroyQueryPool(device,query,NULL);
    if(vertex)vkDestroyShaderModule(device,vertex,NULL);if(fragment)vkDestroyShaderModule(device,fragment,NULL);
    if(fb)vkDestroyFramebuffer(device,fb,NULL);if(pass)vkDestroyRenderPass(device,pass,NULL);if(view)vkDestroyImageView(device,view,NULL);
    if(image)vkDestroyImage(device,image,NULL);if(memory)vkFreeMemory(device,memory,NULL);if(vertices)vkDestroyBuffer(device,vertices,NULL);if(vertices_memory)vkFreeMemory(device,vertices_memory,NULL);
    return ok;
}
