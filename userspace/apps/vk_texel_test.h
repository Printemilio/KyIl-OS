/* Independent GLSL fixture and exact host oracle. No interpreter-specific SPIR-V. */
static int vk_texel_smoke(VkDevice d,VkQueue q){
    vk_compute_buffer_t buffers[6]={{0}},readback[3]={{0}},result={0};
    VkBufferView views[6]={0},bad=0;vk_compute_fixture_t f={0};
    VkDescriptorSet source=0;VkCommandPool pool=0;VkCommandBuffer cmd=0;VkEvent gate=0;VkFence fence=0;
    int ok=0;uint32_t line=0;
#define TEX_CHECK(x) do{line=__LINE__;if(!(x))goto done;}while(0)
    write_text("vk_test: texel buffer views begin\n");
    VkFormat formats[3]={VK_FORMAT_R32_UINT,VK_FORMAT_R32_SINT,VK_FORMAT_R32_SFLOAT};
    for(uint32_t i=0;i<6u;++i){
        VkBufferUsageFlags usage=i<3u?VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT:VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        TEX_CHECK(vk_compute_buffer_make(d,64u,usage,&buffers[i]));
        for(uint32_t word=0;word<16u;++word)buffers[i].words[word]=0xabcddcbau;
        VkBufferViewCreateInfo vi={0};vi.sType=VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;vi.buffer=buffers[i].buffer;vi.format=formats[i%3u];vi.offset=16u;vi.range=32u;
        TEX_CHECK(vkCreateBufferView(d,&vi,0,&views[i])==VK_SUCCESS);
        vi.offset=2u;TEX_CHECK(vkCreateBufferView(d,&vi,0,&bad)==VK_ERROR_INITIALIZATION_FAILED&&!bad);
        vi.offset=16u;vi.range=UINT64_MAX-1u;TEX_CHECK(vkCreateBufferView(d,&vi,0,&bad)==VK_ERROR_INITIALIZATION_FAILED&&!bad);
        vi.range=32u;vi.format=VK_FORMAT_D32_SFLOAT;TEX_CHECK(vkCreateBufferView(d,&vi,0,&bad)==VK_ERROR_FEATURE_NOT_PRESENT&&!bad);
        if(i<3u){vi.format=formats[i];vi.range=VK_WHOLE_SIZE;TEX_CHECK(vkCreateBufferView(d,&vi,0,&bad)==VK_SUCCESS);vkDestroyBufferView(d,bad,0);bad=0;}
    }
    for(uint32_t i=0;i<8u;++i){
        buffers[0].words[4u+i]=i*100u+3u;buffers[1].words[4u+i]=(uint32_t)(-(int32_t)i*7-1);
        float value=(float)i+0.5f;memcpy(&buffers[2].words[4u+i],&value,4u);
    }
    for(uint32_t i=0;i<3u;++i)TEX_CHECK(vk_compute_buffer_make(d,64u,VK_BUFFER_USAGE_TRANSFER_DST_BIT,&readback[i]));
    TEX_CHECK(vk_compute_buffer_make(d,128u,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&result));
    for(uint32_t i=0;i<32u;++i)result.words[i]=0xfeedc0deu;
    VkDescriptorSetLayoutBinding bindings[7]={{0}};for(uint32_t i=0;i<7u;++i){bindings[i].binding=i;bindings[i].descriptorCount=1u;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[i].descriptorType=i<3u?VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:i<6u?VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;}
    VkDescriptorSetLayoutCreateInfo si={0};si.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;si.bindingCount=7u;si.pBindings=bindings;
    TEX_CHECK(vkCreateDescriptorSetLayout(d,&si,0,&f.sl)==VK_SUCCESS);
    VkPipelineLayoutCreateInfo li={0};li.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;li.setLayoutCount=1u;li.pSetLayouts=&f.sl;
    TEX_CHECK(vkCreatePipelineLayout(d,&li,0,&f.layout)==VK_SUCCESS);
    VkDescriptorPoolSize sizes[3]={{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,6},{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,6},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2}};
    VkDescriptorPoolCreateInfo di={0};di.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;di.maxSets=2u;di.poolSizeCount=3u;di.pPoolSizes=sizes;
    TEX_CHECK(vkCreateDescriptorPool(d,&di,0,&f.pool)==VK_SUCCESS);
    VkDescriptorSetAllocateInfo ai={0};ai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;ai.descriptorPool=f.pool;ai.descriptorSetCount=1u;ai.pSetLayouts=&f.sl;
    TEX_CHECK(vkAllocateDescriptorSets(d,&ai,&source)==VK_SUCCESS&&vkAllocateDescriptorSets(d,&ai,&f.set)==VK_SUCCESS);
    VkDescriptorBufferInfo bi={result.buffer,16u,96u};VkWriteDescriptorSet writes[7]={{0}};VkCopyDescriptorSet copies[7]={{0}};
    for(uint32_t i=0;i<7u;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=source;writes[i].dstBinding=i;writes[i].descriptorType=bindings[i].descriptorType;writes[i].descriptorCount=1;
        if(i<6u)writes[i].pTexelBufferView=&views[i];else writes[i].pBufferInfo=&bi;
        copies[i].sType=VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;copies[i].srcSet=source;copies[i].dstSet=f.set;copies[i].srcBinding=copies[i].dstBinding=i;copies[i].descriptorCount=1;
    }
    vkUpdateDescriptorSets(d,7u,writes,7u,copies);
    VkShaderModuleCreateInfo mi={0};mi.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;mi.pCode=compute_texel_spv;mi.codeSize=sizeof(compute_texel_spv);
    TEX_CHECK(vkCreateShaderModule(d,&mi,0,&f.shader)==VK_SUCCESS);
    VkComputePipelineCreateInfo pi={0};pi.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;pi.layout=f.layout;pi.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pi.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pi.stage.module=f.shader;pi.stage.pName="main";
    TEX_CHECK(vkCreateComputePipelines(d,0,1u,&pi,0,&f.pipeline)==VK_SUCCESS);
    VkCommandPoolCreateInfo ci={0};ci.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;ci.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;TEX_CHECK(vkCreateCommandPool(d,&ci,0,&pool)==VK_SUCCESS);
    VkCommandBufferAllocateInfo ca={0};ca.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandPool=pool;ca.commandBufferCount=1;
    TEX_CHECK(vkAllocateCommandBuffers(d,&ca,&cmd)==VK_SUCCESS);
    VkEventCreateInfo ei={0};ei.sType=VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;VkFenceCreateInfo fi={0};fi.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkResult event_result=vkCreateEvent(d,&ei,0,&gate);
    if(event_result!=VK_SUCCESS)write_hex32("vk_test: texel create event result=",(uint32_t)event_result);
    TEX_CHECK(event_result==VK_SUCCESS);
    VkResult fence_result=vkCreateFence(d,&fi,0,&fence);
    if(fence_result!=VK_SUCCESS)write_hex32("vk_test: texel create fence result=",(uint32_t)fence_result);
    TEX_CHECK(fence_result==VK_SUCCESS);
    TEX_CHECK(vk_subresource_begin(cmd));vkCmdWaitEvents(cmd,1,&gate,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,0,0,0,0);
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,f.pipeline);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,f.layout,0,1,&f.set,0,0);vkCmdDispatch(cmd,2u,1u,1u);
    VkMemoryBarrier barrier={0};barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,0,0,0);
    VkBufferCopy copy={0u,0u,64u};for(uint32_t i=0;i<3u;++i)vkCmdCopyBuffer(cmd,buffers[3u+i].buffer,readback[i].buffer,1,&copy);
    TEX_CHECK(vkEndCommandBuffer(cmd)==VK_SUCCESS);VkSubmitInfo submit={0};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
    TEX_CHECK(vk_test_queue_submit_raw(q,1,&submit,fence)==VK_SUCCESS&&vkGetFenceStatus(d,fence)==VK_NOT_READY);
    for(uint32_t i=0;i<6u;++i){vkDestroyBufferView(d,views[i],0);views[i]=0;vk_compute_buffer_drop(d,&buffers[i]);}
    vkDestroyDescriptorPool(d,f.pool,0);f.pool=0;f.set=0;source=0;
    TEX_CHECK(vkSetEvent(d,gate)==VK_SUCCESS&&vkWaitForFences(d,1,&fence,VK_TRUE,UINT64_MAX)==VK_SUCCESS);
    for(uint32_t i=0;i<8u;++i){
        TEX_CHECK(readback[0].words[4u+i]==i*100u+10u&&readback[1].words[4u+i]==(uint32_t)(-(int32_t)i*7-4));
        float expected=(float)(2u*i+1u);uint32_t bits;memcpy(&bits,&expected,4u);TEX_CHECK(readback[2].words[4u+i]==bits);
        TEX_CHECK(result.words[4u+3u*i]==1u&&result.words[5u+3u*i]==1u&&result.words[6u+3u*i]==0x3f800000u);
    }
    for(uint32_t i=0;i<4u;++i){TEX_CHECK(result.words[i]==0xfeedc0deu&&result.words[28u+i]==0xfeedc0deu);
        for(uint32_t b=0;b<3u;++b)TEX_CHECK(readback[b].words[i]==0xabcddcbau&&readback[b].words[12u+i]==0xabcddcbau);}
    ok=1;write_text("vk_test: R32 uint/sint/float texel fetch/write, default components, partial views, descriptor copies, guards and pending owner destruction ok\n");
done:
    if(!ok)write_hex32("vk_test: texel FAILED line=",line);
    if(gate)(void)vkSetEvent(d,gate);if(q)(void)vkQueueWaitIdle(q);
    if(pool)vkDestroyCommandPool(d,pool,0);if(fence)vkDestroyFence(d,fence,0);if(gate)vkDestroyEvent(d,gate,0);
    for(uint32_t i=0;i<6u;++i){if(views[i])vkDestroyBufferView(d,views[i],0);vk_compute_buffer_drop(d,&buffers[i]);}
    for(uint32_t i=0;i<3u;++i)vk_compute_buffer_drop(d,&readback[i]);vk_compute_fixture_drop(d,&f);vk_compute_buffer_drop(d,&result);return ok;
#undef TEX_CHECK
}
