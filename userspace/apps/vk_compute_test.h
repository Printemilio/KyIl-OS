#include "vk_compute_shaders.h"

typedef struct vk_compute_buffer {VkBuffer buffer;VkDeviceMemory memory;uint32_t *words;} vk_compute_buffer_t;
static int vk_compute_buffer_make(VkDevice d,VkDeviceSize size,VkBufferUsageFlags usage,vk_compute_buffer_t *out){
    VkBufferCreateInfo info={0};info.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;info.size=size;info.usage=usage;
    VkMemoryAllocateInfo alloc={0};VkMemoryRequirements req={0};alloc.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    if(vkCreateBuffer(d,&info,0,&out->buffer)!=VK_SUCCESS)return 0;
    vkGetBufferMemoryRequirements(d,out->buffer,&req);alloc.allocationSize=req.size;
    return vkAllocateMemory(d,&alloc,0,&out->memory)==VK_SUCCESS&&
           vkBindBufferMemory(d,out->buffer,out->memory,0u)==VK_SUCCESS&&
           vkMapMemory(d,out->memory,0u,VK_WHOLE_SIZE,0u,(void **)&out->words)==VK_SUCCESS;
}
static void vk_compute_buffer_drop(VkDevice d,vk_compute_buffer_t *b){
    if(b->words&&b->memory)vkUnmapMemory(d,b->memory);
    if(b->buffer)vkDestroyBuffer(d,b->buffer,0);
    if(b->memory)vkFreeMemory(d,b->memory,0);memset(b,0,sizeof(*b));
}
static int vk_compute_smoke(VkDevice d,VkQueue queue){
    write_text("vk_test: compute memory begin\n");
    vk_compute_buffer_t data={0},readback={0};VkDescriptorSetLayout set_layout=0;VkPipelineLayout layout=0;
    VkDescriptorPool descriptors=0;VkDescriptorSet set=0;VkShaderModule shader=0;VkPipeline pipeline=0;
    VkCommandPool pool=0;VkCommandBuffer cmd=0;VkFence fence=0;VkEvent gate=0;uint32_t stage=1u;int ok=0;
    const uint32_t words=6u*32u+8u;
    if(!vk_compute_buffer_make(d,words*4u,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT,&data)||
       !vk_compute_buffer_make(d,words*4u,VK_BUFFER_USAGE_TRANSFER_DST_BIT,&readback))goto done;
    memset(data.words,0,words*4u);memset(readback.words,0x99,words*4u);
    for(uint32_t i=0u;i<4u;++i)data.words[i]=data.words[words-4u+i]=0xabcdef12u;
    VkDescriptorSetLayoutBinding binding={0u,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1u,VK_SHADER_STAGE_COMPUTE_BIT,0};
    VkDescriptorSetLayoutCreateInfo si={0};si.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;si.bindingCount=1u;si.pBindings=&binding;
    VkPushConstantRange range={VK_SHADER_STAGE_COMPUTE_BIT,0u,4u};
    VkPipelineLayoutCreateInfo li={0};li.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if(vkCreateDescriptorSetLayout(d,&si,0,&set_layout)!=VK_SUCCESS)goto done;
    li.setLayoutCount=1u;li.pSetLayouts=&set_layout;li.pushConstantRangeCount=1u;li.pPushConstantRanges=&range;
    if(vkCreatePipelineLayout(d,&li,0,&layout)!=VK_SUCCESS)goto done;
    VkDescriptorPoolSize ds={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1u};VkDescriptorPoolCreateInfo di={0};
    di.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;di.maxSets=1u;di.poolSizeCount=1u;di.pPoolSizes=&ds;
    VkDescriptorSetAllocateInfo ai={0};ai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;ai.descriptorSetCount=1u;ai.pSetLayouts=&set_layout;
    if(vkCreateDescriptorPool(d,&di,0,&descriptors)!=VK_SUCCESS)goto done;ai.descriptorPool=descriptors;
    if(vkAllocateDescriptorSets(d,&ai,&set)!=VK_SUCCESS)goto done;
    VkDescriptorBufferInfo bi={data.buffer,16u,6u*32u*4u};VkWriteDescriptorSet write={0};
    write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=set;write.descriptorCount=1u;
    write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&bi;vkUpdateDescriptorSets(d,1u,&write,0u,0);
    VkShaderModuleCreateInfo mi={0};mi.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;mi.pCode=compute_memory_spv;mi.codeSize=sizeof(compute_memory_spv);
    if(vkCreateShaderModule(d,&mi,0,&shader)!=VK_SUCCESS)goto done;
    VkComputePipelineCreateInfo pi={0};pi.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;pi.layout=layout;
    pi.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pi.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pi.stage.module=shader;pi.stage.pName="main";
    stage=2u;VkResult result=vkCreateComputePipelines(d,0,1u,&pi,0,&pipeline);
    if(result!=VK_SUCCESS){write_hex32("vk_test: compute pipeline result=",(uint32_t)result);goto done;}
    write_text("vk_test: compute pipeline created\n");
    VkCommandPoolCreateInfo ci={0};ci.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;ci.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandBufferAllocateInfo ca={0};ca.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1u;
    VkFenceCreateInfo fi={0};fi.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;VkEventCreateInfo ei={0};ei.sType=VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;
    if(vkCreateCommandPool(d,&ci,0,&pool)!=VK_SUCCESS)goto done;ca.commandPool=pool;
    if(vkAllocateCommandBuffers(d,&ca,&cmd)!=VK_SUCCESS||vkCreateFence(d,&fi,0,&fence)!=VK_SUCCESS||vkCreateEvent(d,&ei,0,&gate)!=VK_SUCCESS)goto done;
    stage=3u;
    write_text("vk_test: compute resources created\n");
    if(!vk_subresource_begin(cmd))goto done;
    vkCmdWaitEvents(cmd,1u,&gate,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0u,0,0u,0,0u,0);
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0u,1u,&set,0u,0);
    uint32_t bias=7u;vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_COMPUTE_BIT,0u,4u,&bias);
    vkCmdDispatch(cmd,3u,2u,1u);
    write_text("vk_test: compute dispatch recorded\n");
    VkMemoryBarrier barrier={0};barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0u,1u,&barrier,0u,0,0u,0);
    VkBufferCopy copy={0u,0u,words*4u};vkCmdCopyBuffer(cmd,data.buffer,readback.buffer,1u,&copy);
    if(vkEndCommandBuffer(cmd)!=VK_SUCCESS)goto done;
    write_text("vk_test: compute recording ended\n");
    VkSubmitInfo submit={0};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1u;submit.pCommandBuffers=&cmd;
    write_text("vk_test: compute entering submit\n");
    result=vk_test_queue_submit_raw(queue,1u,&submit,fence);
    write_hex32("vk_test: compute actual submit result=",(uint32_t)result);
    if(result!=VK_SUCCESS)goto done;
    result=vkGetFenceStatus(d,fence);
    write_hex32("vk_test: compute actual fence result=",(uint32_t)result);
    if(result!=VK_NOT_READY||readback.words[4]!=0x99999999u)goto done;
    write_text("vk_test: compute submit returned behind unsignaled event\n");
    /* Public owners disappear while the event-gated immutable record retains
       descriptors, backing, pipeline and specialized module. */
    vkDestroyPipeline(d,pipeline,0);pipeline=0;vkDestroyShaderModule(d,shader,0);shader=0;
    vkDestroyDescriptorPool(d,descriptors,0);descriptors=0;
    vkDestroyBuffer(d,data.buffer,0);data.buffer=0;
    write_text("vk_test: compute public owners closed\n");
    stage=4u;
    if(vkSetEvent(d,gate)!=VK_SUCCESS)goto done;
    write_text("vk_test: compute gate signaled, waiting completion\n");
    if(vkWaitForFences(d,1u,&fence,VK_TRUE,UINT64_MAX)!=VK_SUCCESS)goto done;
    for(uint32_t g=0u;g<6u;++g){
        uint32_t gx=g%3u,gy=g/3u;
        for(uint32_t lane=0u;lane<16u;++lane){uint32_t n=(lane+1u)%16u;
            uint32_t expected=gx*8u+n%8u+100u*(gy*2u+n/8u)+7u;
            if(readback.words[4u+g*32u+lane]!=expected){write_hex32("vk_test: compute wrong word=",4u+g*32u+lane);goto done;}
        }
        if(readback.words[4u+g*32u+16u]!=16u||readback.words[4u+g*32u+17u]!=16u)goto done;
        for(uint32_t i=18u;i<32u;++i)if(readback.words[4u+g*32u+i]!=0u)goto done;
    }
    for(uint32_t i=0u;i<4u;++i)if(readback.words[i]!=0xabcdef12u||readback.words[words-4u+i]!=0xabcdef12u)goto done;
    write_text("vk_test: real GLSL compute 3x2 workgroups, local/global IDs, std430 runtime array, shared rendezvous, atomicAdd, push constants, compute-transfer visibility and in-flight teardown ok\n");ok=1;
done:
    if(!ok)write_hex32("vk_test: compute FAILED stage=",stage);
    if(gate){(void)vkSetEvent(d,gate);(void)vkQueueWaitIdle(queue);vkDestroyEvent(d,gate,0);}
    if(fence)vkDestroyFence(d,fence,0);if(pool)vkDestroyCommandPool(d,pool,0);
    if(pipeline)vkDestroyPipeline(d,pipeline,0);if(shader)vkDestroyShaderModule(d,shader,0);
    if(descriptors)vkDestroyDescriptorPool(d,descriptors,0);if(layout)vkDestroyPipelineLayout(d,layout,0);
    if(set_layout)vkDestroyDescriptorSetLayout(d,set_layout,0);
    vk_compute_buffer_drop(d,&data);vk_compute_buffer_drop(d,&readback);return ok;
}
/* Storage-image oracle uses a partial mip/layer view. Adjacent subresources
   retain sentinel bytes; expected UNORM values are computed on the host. */
static int vk_compute_image_smoke(VkDevice d,VkQueue queue){
    VkImage image=0;VkImageView view=0;VkDeviceMemory memory=0;uint32_t *pixels=0;
    VkDescriptorSetLayout sl=0;VkPipelineLayout layout=0;VkDescriptorPool dp=0;VkDescriptorSet set=0;
    VkShaderModule shader=0;VkPipeline pipeline=0;VkCommandPool pool=0;VkCommandBuffer cmd=0;
    vk_compute_buffer_t output={0};int ok=0;uint32_t line=__LINE__;
#define IMAGE_CHECK(x) do{if(!(x)){line=__LINE__;goto out;}}while(0)
    VkImageCreateInfo ii={0};ii.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;ii.imageType=VK_IMAGE_TYPE_2D;
    ii.format=VK_FORMAT_R8G8B8A8_UNORM;ii.extent=(VkExtent3D){16,8,1};ii.mipLevels=2;ii.arrayLayers=3;
    ii.samples=VK_SAMPLE_COUNT_1_BIT;ii.tiling=VK_IMAGE_TILING_LINEAR;
    ii.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    IMAGE_CHECK(vkCreateImage(d,&ii,0,&image)==VK_SUCCESS);
    VkMemoryRequirements req;vkGetImageMemoryRequirements(d,image,&req);VkMemoryAllocateInfo ma={0};
    ma.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;ma.allocationSize=req.size;
    IMAGE_CHECK(vkAllocateMemory(d,&ma,0,&memory)==VK_SUCCESS&&vkBindImageMemory(d,image,memory,0)==VK_SUCCESS);
    IMAGE_CHECK(vkMapMemory(d,memory,0,VK_WHOLE_SIZE,0,(void**)&pixels)==VK_SUCCESS);
    for(uint32_t i=0;i<3u*160u;++i)pixels[i]=0x23456789u;
    VkImageViewCreateInfo vi={0};vi.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;vi.image=image;
    vi.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY;vi.format=ii.format;vi.subresourceRange=(VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT,1,1,1,2};
    IMAGE_CHECK(vkCreateImageView(d,&vi,0,&view)==VK_SUCCESS);
    IMAGE_CHECK(vk_compute_buffer_make(d,64u*4u,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&output));memset(output.words,0,256u);
    VkDescriptorSetLayoutBinding bindings[2]={{0,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,0},{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,0}};
    VkDescriptorSetLayoutCreateInfo si={0};si.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;si.bindingCount=2;si.pBindings=bindings;
    IMAGE_CHECK(vkCreateDescriptorSetLayout(d,&si,0,&sl)==VK_SUCCESS);
    VkPipelineLayoutCreateInfo li={0};li.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;li.setLayoutCount=1;li.pSetLayouts=&sl;
    IMAGE_CHECK(vkCreatePipelineLayout(d,&li,0,&layout)==VK_SUCCESS);
    VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};
    VkDescriptorPoolCreateInfo di={0};di.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;di.maxSets=1;di.poolSizeCount=2;di.pPoolSizes=sizes;
    IMAGE_CHECK(vkCreateDescriptorPool(d,&di,0,&dp)==VK_SUCCESS);
    VkDescriptorSetAllocateInfo ai={0};ai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;ai.descriptorPool=dp;ai.descriptorSetCount=1;ai.pSetLayouts=&sl;
    IMAGE_CHECK(vkAllocateDescriptorSets(d,&ai,&set)==VK_SUCCESS);
    VkDescriptorImageInfo im={0,view,VK_IMAGE_LAYOUT_GENERAL};VkDescriptorBufferInfo bi={output.buffer,0,256};VkWriteDescriptorSet writes[2]={{0}};
    for(uint32_t i=0;i<2;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=set;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=bindings[i].descriptorType;}
    writes[0].pImageInfo=&im;writes[1].pBufferInfo=&bi;vkUpdateDescriptorSets(d,2,writes,0,0);
    VkShaderModuleCreateInfo mi={0};mi.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;mi.pCode=compute_image_spv;mi.codeSize=sizeof(compute_image_spv);
    IMAGE_CHECK(vkCreateShaderModule(d,&mi,0,&shader)==VK_SUCCESS);
    VkComputePipelineCreateInfo pi={0};pi.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;pi.layout=layout;
    pi.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pi.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pi.stage.module=shader;pi.stage.pName="main";
    IMAGE_CHECK(vkCreateComputePipelines(d,0,1,&pi,0,&pipeline)==VK_SUCCESS);
    VkCommandPoolCreateInfo cp={0};cp.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;cp.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandBufferAllocateInfo ca={0};ca.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;ca.commandBufferCount=1;
    IMAGE_CHECK(vkCreateCommandPool(d,&cp,0,&pool)==VK_SUCCESS);ca.commandPool=pool;
    IMAGE_CHECK(vkAllocateCommandBuffers(d,&ca,&cmd)==VK_SUCCESS&&vk_subresource_begin(cmd));
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,0);
    vkCmdDispatch(cmd,2,2,2);IMAGE_CHECK(vkEndCommandBuffer(cmd)==VK_SUCCESS);
    VkSubmitInfo submit={0};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
    /* A storage image in UNDEFINED cannot execute: rejection precedes any write. */
    IMAGE_CHECK(vk_test_queue_submit_raw(queue,1,&submit,0)!=VK_SUCCESS&&pixels[288]==0x23456789u);
    IMAGE_CHECK(vk_subresource_begin(cmd));
    vk_subresource_barrier(cmd,image,vi.subresourceRange,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL);
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,0);
    vkCmdDispatch(cmd,2,2,2);
    VkMemoryBarrier barrier={0};barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,0,0,0);
    IMAGE_CHECK(vk_subresource_complete(queue,cmd));
    for(uint32_t layer=0;layer<3;++layer)for(uint32_t i=0;i<160;++i){
        uint32_t expected=0x23456789u;
        if(layer&&i>=128u){uint32_t x=(i-128u)%8u,y=(i-128u)/8u;
            uint32_t red=(uint32_t)((float)x/7.0f*255.0f+0.5f),green=(uint32_t)((float)y/3.0f*255.0f+0.5f);
            expected=0xff000000u|((layer-1u)*255u<<16u)|(green<<8u)|red;
            IMAGE_CHECK(output.words[x+8u*(y+4u*(layer-1u))]==red);
        }IMAGE_CHECK(pixels[layer*160u+i]==expected);
    }
    write_text("vk_test: storage-image compute RGBA8 image2DArray partial mip/layers, imageLoad/Store, image memory barrier, 64 exact pixels, layout rejection and untouched neighbours ok\n");ok=1;
out:
    if(!ok)write_hex32("vk_test: compute image FAILED line=",line);
    if(pool)vkDestroyCommandPool(d,pool,0);if(pipeline)vkDestroyPipeline(d,pipeline,0);if(shader)vkDestroyShaderModule(d,shader,0);
    if(dp)vkDestroyDescriptorPool(d,dp,0);if(layout)vkDestroyPipelineLayout(d,layout,0);if(sl)vkDestroyDescriptorSetLayout(d,sl,0);
    if(view)vkDestroyImageView(d,view,0);if(image)vkDestroyImage(d,image,0);if(memory)vkFreeMemory(d,memory,0);
    vk_compute_buffer_drop(d,&output);return ok;
#undef IMAGE_CHECK
}
/* Independent oracle for exchange/CAS/min/max/bitwise/add plus functions,
   structured switch/loop, private initializer and runtime-array length. */
static int vk_compute_ops_smoke(VkDevice d,VkQueue queue){
    vk_compute_buffer_t b={0},indirect={0};VkCommandBuffer secondary=0;VkCommandPool secondary_pool=0;VkEvent gate=0;VkFence fence=0;VkDescriptorSetLayout sl=0;VkPipelineLayout layout=0;VkDescriptorPool dp=0;VkDescriptorSet set=0;
    VkShaderModule shader=0;VkPipeline pipeline=0;VkCommandPool pool=0;VkCommandBuffer cmd=0;int ok=0;uint32_t line=__LINE__;
#define OPS_CHECK(x) do{if(!(x)){line=__LINE__;goto out;}}while(0)
    OPS_CHECK(vk_compute_buffer_make(d,192u*4u,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&b));memset(b.words,0,192u*4u);
    VkDescriptorSetLayoutBinding binding={0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,0};
    VkDescriptorSetLayoutCreateInfo si={0};si.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;si.bindingCount=1;si.pBindings=&binding;
    OPS_CHECK(vkCreateDescriptorSetLayout(d,&si,0,&sl)==VK_SUCCESS);
    VkPipelineLayoutCreateInfo li={0};li.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;li.setLayoutCount=1;li.pSetLayouts=&sl;
    OPS_CHECK(vkCreatePipelineLayout(d,&li,0,&layout)==VK_SUCCESS);
    VkDescriptorPoolSize ds={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};VkDescriptorPoolCreateInfo di={0};di.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;di.maxSets=1;di.poolSizeCount=1;di.pPoolSizes=&ds;
    OPS_CHECK(vkCreateDescriptorPool(d,&di,0,&dp)==VK_SUCCESS);VkDescriptorSetAllocateInfo ai={0};ai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;ai.descriptorPool=dp;ai.descriptorSetCount=1;ai.pSetLayouts=&sl;
    OPS_CHECK(vkAllocateDescriptorSets(d,&ai,&set)==VK_SUCCESS);VkDescriptorBufferInfo bi={b.buffer,0,192u*4u};VkWriteDescriptorSet write={0};write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=set;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&bi;vkUpdateDescriptorSets(d,1,&write,0,0);
    VkShaderModuleCreateInfo mi={0};mi.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;mi.pCode=compute_ops_spv;mi.codeSize=sizeof(compute_ops_spv);
    OPS_CHECK(vkCreateShaderModule(d,&mi,0,&shader)==VK_SUCCESS);
    VkComputePipelineCreateInfo pi={0};pi.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;pi.layout=layout;pi.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pi.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pi.stage.module=shader;pi.stage.pName="main";
    OPS_CHECK(vkCreateComputePipelines(d,0,1,&pi,0,&pipeline)==VK_SUCCESS);
    VkCommandPoolCreateInfo cp={0};cp.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;cp.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandBufferAllocateInfo ca={0};ca.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;ca.commandBufferCount=1;
    OPS_CHECK(vkCreateCommandPool(d,&cp,0,&pool)==VK_SUCCESS);ca.commandPool=pool;
    OPS_CHECK(vkAllocateCommandBuffers(d,&ca,&cmd)==VK_SUCCESS&&vk_subresource_begin(cmd));
    OPS_CHECK(vk_compute_buffer_make(d,12u,VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,&indirect));memset(indirect.words,0,12u);
    OPS_CHECK(vkCreateCommandPool(d,&cp,0,&secondary_pool)==VK_SUCCESS);ca.commandPool=secondary_pool;ca.level=VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    OPS_CHECK(vkAllocateCommandBuffers(d,&ca,&secondary)==VK_SUCCESS);
    VkCommandBufferInheritanceInfo inheritance={0};inheritance.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
    VkCommandBufferBeginInfo begin={0};begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;begin.pInheritanceInfo=&inheritance;
    for(uint32_t epoch=0;epoch<2;++epoch){
        OPS_CHECK(vkBeginCommandBuffer(secondary,&begin)==VK_SUCCESS);
        vkCmdBindPipeline(secondary,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);vkCmdBindDescriptorSets(secondary,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,0);
        vkCmdDispatchIndirect(secondary,indirect.buffer,0);
        OPS_CHECK(vkEndCommandBuffer(secondary)==VK_SUCCESS);
        if(!epoch){vkCmdExecuteCommands(cmd,1,&secondary);OPS_CHECK(vkEndCommandBuffer(cmd)==VK_SUCCESS);
            OPS_CHECK(vkResetCommandBuffer(secondary,0)==VK_SUCCESS);
        }
    }
    VkSubmitInfo submit={0};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
    OPS_CHECK(vk_test_queue_submit_raw(queue,1,&submit,0)!=VK_SUCCESS&&b.words[0]==0u);
    /* Secondary cannot be directly submitted to a queue. */
    submit.pCommandBuffers=&secondary;OPS_CHECK(vk_test_queue_submit_raw(queue,1,&submit,0)!=VK_SUCCESS);submit.pCommandBuffers=&cmd;
    OPS_CHECK(vk_subresource_begin(cmd));
    VkEventCreateInfo ei={0};ei.sType=VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;VkFenceCreateInfo fi={0};fi.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    OPS_CHECK(vkCreateEvent(d,&ei,0,&gate)==VK_SUCCESS&&vkCreateFence(d,&fi,0,&fence)==VK_SUCCESS);
    vkCmdWaitEvents(cmd,1,&gate,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,0,0,0,0);
    uint32_t groups[3]={3,1,1};vkCmdUpdateBuffer(cmd,indirect.buffer,0,12u,groups);
    VkMemoryBarrier barrier={0};barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,0,1,&barrier,0,0,0,0);
    vkCmdExecuteCommands(cmd,1,&secondary);OPS_CHECK(vkEndCommandBuffer(cmd)==VK_SUCCESS);
    OPS_CHECK(vk_test_queue_submit_raw(queue,1,&submit,fence)==VK_SUCCESS&&vkGetFenceStatus(d,fence)==VK_NOT_READY);
    OPS_CHECK(vkResetCommandBuffer(secondary,0)!=VK_SUCCESS&&vkResetCommandPool(d,secondary_pool,0)!=VK_SUCCESS);
    vkDestroyCommandPool(d,secondary_pool,0);secondary_pool=0;secondary=0;
    vkDestroyBuffer(d,indirect.buffer,0);indirect.buffer=0;
    OPS_CHECK(vkSetEvent(d,gate)==VK_SUCCESS&&vkWaitForFences(d,1,&fence,VK_TRUE,UINT64_MAX)==VK_SUCCESS);
    write_text("vk_test: secondary compute/indirect execution, rerecord invalidation, direct-submit rejection, pending reset rejection, deferred counts and closed pool/backing lifetime ok\n");
    const uint32_t expected[10]={15,9,5,12,12,7,15,6,14,13};
    for(uint32_t lane=0;lane<12;++lane){for(uint32_t i=0;i<10;++i)OPS_CHECK(b.words[lane*16u+i]==expected[i]);
        uint32_t v=lane%3u==0u?lane+17u:lane%3u==1u?lane*3u:lane^85u;
        OPS_CHECK(b.words[lane*16u+10u]==v+14u&&b.words[lane*16u+11u]==192u);
        for(uint32_t i=12;i<16;++i)OPS_CHECK(b.words[lane*16u+i]==0u);
    }
    write_text("vk_test: compute 9 atomic operations, CAS success/failure, Function calls, Switch/loop, private initializer and OpArrayLength exact oracle ok\n");ok=1;
out:
    if(!ok)write_hex32("vk_test: compute operations FAILED line=",line);
    if(gate){(void)vkSetEvent(d,gate);(void)vkQueueWaitIdle(queue);vkDestroyEvent(d,gate,0);}if(fence)vkDestroyFence(d,fence,0);
    if(secondary_pool)vkDestroyCommandPool(d,secondary_pool,0);vk_compute_buffer_drop(d,&indirect);
    if(pool)vkDestroyCommandPool(d,pool,0);if(pipeline)vkDestroyPipeline(d,pipeline,0);if(shader)vkDestroyShaderModule(d,shader,0);
    if(dp)vkDestroyDescriptorPool(d,dp,0);if(layout)vkDestroyPipelineLayout(d,layout,0);if(sl)vkDestroyDescriptorSetLayout(d,sl,0);vk_compute_buffer_drop(d,&b);return ok;
#undef OPS_CHECK
}
static int vk_compute_arrays_smoke(VkDevice d,VkQueue queue){
    vk_compute_buffer_t buffers[2]={{0}};VkImage images[2]={0};VkImageView views[2]={0};VkDeviceMemory memories[2]={0};uint32_t *pixels[2]={0};
    VkDescriptorSetLayout sl=0;VkPipelineLayout layout=0;VkDescriptorPool dp=0;VkDescriptorSet set=0;VkShaderModule shader=0;VkPipeline pipeline=0;VkCommandPool pool=0;VkCommandBuffer cmd=0;
    int ok=0;uint32_t line=__LINE__;
#define ARRAY_CHECK(x) do{if(!(x)){line=__LINE__;goto out;}}while(0)
    VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};VkDescriptorBufferInfo bi[2];VkDescriptorImageInfo im[2];
    for(uint32_t e=0;e<2;++e){
        ARRAY_CHECK(vk_compute_buffer_make(d,32u,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&buffers[e]));memset(buffers[e].words,0,32u);
        VkImageCreateInfo ii={0};ii.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;ii.imageType=VK_IMAGE_TYPE_2D;ii.format=VK_FORMAT_R8G8B8A8_UNORM;ii.extent=(VkExtent3D){4,1,1};ii.mipLevels=ii.arrayLayers=1;ii.samples=VK_SAMPLE_COUNT_1_BIT;ii.tiling=VK_IMAGE_TILING_LINEAR;ii.usage=VK_IMAGE_USAGE_STORAGE_BIT;
        ARRAY_CHECK(vkCreateImage(d,&ii,0,&images[e])==VK_SUCCESS);VkMemoryRequirements req;vkGetImageMemoryRequirements(d,images[e],&req);
        VkMemoryAllocateInfo ma={0};ma.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;ma.allocationSize=req.size;
        ARRAY_CHECK(vkAllocateMemory(d,&ma,0,&memories[e])==VK_SUCCESS&&vkBindImageMemory(d,images[e],memories[e],0)==VK_SUCCESS&&vkMapMemory(d,memories[e],0,VK_WHOLE_SIZE,0,(void**)&pixels[e])==VK_SUCCESS);
        VkImageViewCreateInfo vi={0};vi.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;vi.image=images[e];vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=ii.format;vi.subresourceRange=range;
        ARRAY_CHECK(vkCreateImageView(d,&vi,0,&views[e])==VK_SUCCESS);
        bi[e]=(VkDescriptorBufferInfo){buffers[e].buffer,0,32u};im[e]=(VkDescriptorImageInfo){0,views[e],VK_IMAGE_LAYOUT_GENERAL};
    }
    VkDescriptorSetLayoutBinding bindings[2]={{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2,VK_SHADER_STAGE_COMPUTE_BIT,0},{1,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,2,VK_SHADER_STAGE_COMPUTE_BIT,0}};
    VkDescriptorSetLayoutCreateInfo si={0};si.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;si.bindingCount=2;si.pBindings=bindings;
    ARRAY_CHECK(vkCreateDescriptorSetLayout(d,&si,0,&sl)==VK_SUCCESS);VkPipelineLayoutCreateInfo li={0};li.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;li.setLayoutCount=1;li.pSetLayouts=&sl;
    ARRAY_CHECK(vkCreatePipelineLayout(d,&li,0,&layout)==VK_SUCCESS);
    VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,2}};VkDescriptorPoolCreateInfo di={0};di.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;di.maxSets=1;di.poolSizeCount=2;di.pPoolSizes=sizes;
    ARRAY_CHECK(vkCreateDescriptorPool(d,&di,0,&dp)==VK_SUCCESS);VkDescriptorSetAllocateInfo ai={0};ai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;ai.descriptorPool=dp;ai.descriptorSetCount=1;ai.pSetLayouts=&sl;
    ARRAY_CHECK(vkAllocateDescriptorSets(d,&ai,&set)==VK_SUCCESS);
    VkWriteDescriptorSet writes[2]={{0}};for(uint32_t i=0;i<2;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=set;writes[i].dstBinding=i;writes[i].descriptorCount=2;writes[i].descriptorType=bindings[i].descriptorType;}
    writes[0].pBufferInfo=bi;writes[1].pImageInfo=im;vkUpdateDescriptorSets(d,2,writes,0,0);
    VkShaderModuleCreateInfo mi={0};mi.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;mi.pCode=compute_arrays_spv;mi.codeSize=sizeof(compute_arrays_spv);
    ARRAY_CHECK(vkCreateShaderModule(d,&mi,0,&shader)==VK_SUCCESS);VkComputePipelineCreateInfo pi={0};pi.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;pi.layout=layout;pi.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pi.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pi.stage.module=shader;pi.stage.pName="main";
    ARRAY_CHECK(vkCreateComputePipelines(d,0,1,&pi,0,&pipeline)==VK_SUCCESS);
    VkCommandPoolCreateInfo cp={0};cp.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;cp.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;VkCommandBufferAllocateInfo ca={0};ca.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;ca.commandBufferCount=1;
    ARRAY_CHECK(vkCreateCommandPool(d,&cp,0,&pool)==VK_SUCCESS);ca.commandPool=pool;ARRAY_CHECK(vkAllocateCommandBuffers(d,&ca,&cmd)==VK_SUCCESS&&vk_subresource_begin(cmd));
    for(uint32_t e=0;e<2;++e)vk_subresource_barrier(cmd,images[e],range,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL);
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,0);vkCmdDispatch(cmd,2,1,1);
    VkMemoryBarrier barrier={0};barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,0,0,0);vkCmdDispatch(cmd,2,1,1);
    ARRAY_CHECK(vk_subresource_complete(queue,cmd));
    for(uint32_t e=0;e<2;++e){for(uint32_t i=0;i<4;++i){ARRAY_CHECK(buffers[e].words[i]==100u+4u*e+i);
        uint32_t red=(uint32_t)((float)(4u*e+i)*255.0f/7.0f+0.5f);ARRAY_CHECK(pixels[e][i]==(0xff000000u|red));}
        ARRAY_CHECK(buffers[e].words[4]==8u&&buffers[e].words[5]==0u&&buffers[e].words[6]==0u&&buffers[e].words[7]==0u);
    }
    write_text("vk_test: dynamic storage-buffer/image descriptor arrays select distinct objects, atomics and exact pixels ok\n");ok=1;
out:
    if(!ok)write_hex32("vk_test: compute arrays FAILED line=",line);
    if(pool)vkDestroyCommandPool(d,pool,0);if(pipeline)vkDestroyPipeline(d,pipeline,0);if(shader)vkDestroyShaderModule(d,shader,0);if(dp)vkDestroyDescriptorPool(d,dp,0);if(layout)vkDestroyPipelineLayout(d,layout,0);if(sl)vkDestroyDescriptorSetLayout(d,sl,0);
    for(uint32_t e=0;e<2;++e){if(views[e])vkDestroyImageView(d,views[e],0);if(images[e])vkDestroyImage(d,images[e],0);if(memories[e])vkFreeMemory(d,memories[e],0);vk_compute_buffer_drop(d,&buffers[e]);}return ok;
#undef ARRAY_CHECK
}
typedef struct vk_compute_fixture {VkDescriptorSetLayout sl;VkPipelineLayout layout;VkDescriptorPool pool;VkDescriptorSet set;VkShaderModule shader;VkPipeline pipeline;} vk_compute_fixture_t;
static void vk_compute_fixture_drop(VkDevice d,vk_compute_fixture_t *f){
    if(f->pipeline)vkDestroyPipeline(d,f->pipeline,0);if(f->shader)vkDestroyShaderModule(d,f->shader,0);if(f->pool)vkDestroyDescriptorPool(d,f->pool,0);if(f->layout)vkDestroyPipelineLayout(d,f->layout,0);if(f->sl)vkDestroyDescriptorSetLayout(d,f->sl,0);memset(f,0,sizeof(*f));
}
static int vk_compute_fixture_make(VkDevice d,const uint32_t *code,size_t bytes,const VkDescriptorBufferInfo *bi,vk_compute_fixture_t *f){
    VkDescriptorSetLayoutBinding binding={0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,0};VkDescriptorSetLayoutCreateInfo si={0};si.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;si.bindingCount=1;si.pBindings=&binding;
    if(vkCreateDescriptorSetLayout(d,&si,0,&f->sl)!=VK_SUCCESS)return 0;
    VkPipelineLayoutCreateInfo li={0};li.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;li.setLayoutCount=1;li.pSetLayouts=&f->sl;
    if(vkCreatePipelineLayout(d,&li,0,&f->layout)!=VK_SUCCESS)return 0;
    VkDescriptorPoolSize ds={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};VkDescriptorPoolCreateInfo di={0};di.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;di.maxSets=1;di.poolSizeCount=1;di.pPoolSizes=&ds;
    if(vkCreateDescriptorPool(d,&di,0,&f->pool)!=VK_SUCCESS)return 0;
    VkDescriptorSetAllocateInfo ai={0};ai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;ai.descriptorPool=f->pool;ai.descriptorSetCount=1;ai.pSetLayouts=&f->sl;
    if(vkAllocateDescriptorSets(d,&ai,&f->set)!=VK_SUCCESS)return 0;
    VkWriteDescriptorSet write={0};write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=f->set;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=bi;vkUpdateDescriptorSets(d,1,&write,0,0);
    VkShaderModuleCreateInfo mi={0};mi.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;mi.pCode=code;mi.codeSize=bytes;
    if(vkCreateShaderModule(d,&mi,0,&f->shader)!=VK_SUCCESS)return 0;
    VkComputePipelineCreateInfo pi={0};pi.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;pi.layout=f->layout;pi.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pi.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pi.stage.module=f->shader;pi.stage.pName="main";
    return vkCreateComputePipelines(d,0,1,&pi,0,&f->pipeline)==VK_SUCCESS;
}
/* Invalid execution is confined to a separate logical device. Bounds checks
   must report DEVICE_LOST without touching the adjacent mapped bytes. */
static int vk_compute_negative_smoke(VkPhysicalDevice physical){
    VkDevice d=0;VkQueue queue=0;VkCommandPool pool=0;VkCommandBuffer cmd=0;VkFence fence=0;
    vk_compute_buffer_t b={0};vk_compute_fixture_t f={0};VkShaderModule bad=0;VkPipeline rejected=0;
    VkDescriptorSetLayout dynamic_sl=0;VkPipelineLayout dynamic_layout=0;int ok=0;uint32_t line=__LINE__;
#define NEG_CHECK(x) do{if(!(x)){line=__LINE__;goto out;}}while(0)
    float priority=1.0f;VkDeviceQueueCreateInfo qi={0};qi.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;qi.queueCount=1;qi.pQueuePriorities=&priority;
    VkDeviceCreateInfo di={0};di.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;
    NEG_CHECK(vkCreateDevice(physical,&di,0,&d)==VK_SUCCESS);vkGetDeviceQueue(d,0,0,&queue);
    NEG_CHECK(vk_compute_buffer_make(d,192u*4u,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&b));for(uint32_t i=0;i<192u;++i)b.words[i]=0xabcd1234u;
    VkDescriptorBufferInfo bi={b.buffer,0,4u};NEG_CHECK(vk_compute_fixture_make(d,compute_ops_spv,sizeof(compute_ops_spv),&bi,&f));
    VkComputePipelineCreateInfo pi={0};pi.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;pi.layout=f.layout;pi.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pi.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pi.stage.pName="main";
    uint32_t altered[sizeof(compute_ops_spv)/4u];VkShaderModuleCreateInfo mi={0};mi.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;mi.codeSize=sizeof(altered);mi.pCode=altered;
    for(uint32_t mode=0;mode<2;++mode){memcpy(altered,compute_ops_spv,sizeof(altered));int changed=0;
        for(uint32_t off=5;off<sizeof(altered)/4u;off+=altered[off]>>16u){uint32_t op=altered[off]&65535u;
            if(!mode&&op==16u&&altered[off+2u]==17u){altered[off+3u]=129u;changed=1;break;}
            if(mode&&op==234u){altered[off+4u]=altered[off+5u];changed=1;break;}
        }
        NEG_CHECK(changed&&vkCreateShaderModule(d,&mi,0,&bad)==VK_SUCCESS);pi.stage.module=bad;
        NEG_CHECK(vkCreateComputePipelines(d,0,1,&pi,0,&rejected)==VK_ERROR_FEATURE_NOT_PRESENT&&rejected==0);
        vkDestroyShaderModule(d,bad,0);bad=0;
    }
    VkDescriptorSetLayoutBinding bindings[2]={{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2,VK_SHADER_STAGE_COMPUTE_BIT,0},{1,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,2,VK_SHADER_STAGE_COMPUTE_BIT,0}};
    VkDescriptorSetLayoutCreateInfo sl={0};sl.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;sl.bindingCount=2;sl.pBindings=bindings;
    NEG_CHECK(vkCreateDescriptorSetLayout(d,&sl,0,&dynamic_sl)==VK_SUCCESS);VkPipelineLayoutCreateInfo li={0};li.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;li.setLayoutCount=1;li.pSetLayouts=&dynamic_sl;
    NEG_CHECK(vkCreatePipelineLayout(d,&li,0,&dynamic_layout)==VK_SUCCESS);mi.codeSize=sizeof(compute_arrays_spv);mi.pCode=compute_arrays_spv;
    NEG_CHECK(vkCreateShaderModule(d,&mi,0,&bad)==VK_SUCCESS);pi.stage.module=bad;pi.layout=dynamic_layout;
    NEG_CHECK(vkCreateComputePipelines(d,0,1,&pi,0,&rejected)==VK_ERROR_FEATURE_NOT_PRESENT&&rejected==0);
    vkDestroyShaderModule(d,bad,0);bad=0;
    VkCommandPoolCreateInfo cp={0};cp.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;cp.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandBufferAllocateInfo ca={0};ca.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;ca.commandBufferCount=1;
    NEG_CHECK(vkCreateCommandPool(d,&cp,0,&pool)==VK_SUCCESS);ca.commandPool=pool;NEG_CHECK(vkAllocateCommandBuffers(d,&ca,&cmd)==VK_SUCCESS&&vk_subresource_begin(cmd));
    vkCmdDispatch(cmd,1,1,1);NEG_CHECK(vkEndCommandBuffer(cmd)!=VK_SUCCESS&&b.words[0]==0xabcd1234u);
    NEG_CHECK(vk_subresource_begin(cmd));vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,f.pipeline);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,f.layout,0,1,&f.set,0,0);
    vkCmdDispatch(cmd,65536u,1,1);NEG_CHECK(vkEndCommandBuffer(cmd)!=VK_SUCCESS&&b.words[0]==0xabcd1234u);
    NEG_CHECK(vk_subresource_begin(cmd));vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,f.pipeline);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,f.layout,0,1,&f.set,0,0);
    vkCmdDispatch(cmd,1,1,1);NEG_CHECK(vkEndCommandBuffer(cmd)==VK_SUCCESS);VkFenceCreateInfo fi={0};fi.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    NEG_CHECK(vkCreateFence(d,&fi,0,&fence)==VK_SUCCESS);VkSubmitInfo submit={0};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
    write_text("vk_test: intentional compute descriptor-bounds violation on isolated logical device begins\n");
    NEG_CHECK(vk_test_queue_submit_raw(queue,1,&submit,fence)==VK_SUCCESS&&vkWaitForFences(d,1,&fence,VK_TRUE,UINT64_MAX)==VK_ERROR_DEVICE_LOST);
    for(uint32_t i=1;i<192u;++i)NEG_CHECK(b.words[i]==0xabcd1234u);
    NEG_CHECK(vkGetFenceStatus(d,fence)==VK_ERROR_DEVICE_LOST&&vk_test_queue_submit_raw(queue,1,&submit,0)==VK_ERROR_DEVICE_LOST);
    write_text("vk_test: compute scope/local-size/disabled-feature/unbound/dispatch-limit negatives and descriptor bounds guard with real DEVICE_LOST completion ok\n");ok=1;
out:
    if(!ok)write_hex32("vk_test: compute negative FAILED line=",line);
    if(queue)(void)vkQueueWaitIdle(queue);if(pool)vkDestroyCommandPool(d,pool,0);if(fence)vkDestroyFence(d,fence,0);if(rejected)vkDestroyPipeline(d,rejected,0);if(bad)vkDestroyShaderModule(d,bad,0);
    if(dynamic_layout)vkDestroyPipelineLayout(d,dynamic_layout,0);if(dynamic_sl)vkDestroyDescriptorSetLayout(d,dynamic_sl,0);vk_compute_fixture_drop(d,&f);vk_compute_buffer_drop(d,&b);if(d)vkDestroyDevice(d,0);return ok;
#undef NEG_CHECK
}
static int vk_compute_readframe_fixture(VkDevice d,VkImageView view,VkBuffer buffer,vk_compute_fixture_t *f){
    VkDescriptorSetLayoutBinding bindings[2]={{0,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,0},{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,0}};
    VkDescriptorSetLayoutCreateInfo si={0};si.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;si.bindingCount=2;si.pBindings=bindings;
    if(vkCreateDescriptorSetLayout(d,&si,0,&f->sl)!=VK_SUCCESS)return 0;VkPipelineLayoutCreateInfo li={0};li.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;li.setLayoutCount=1;li.pSetLayouts=&f->sl;
    if(vkCreatePipelineLayout(d,&li,0,&f->layout)!=VK_SUCCESS)return 0;VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};VkDescriptorPoolCreateInfo di={0};di.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;di.maxSets=1;di.poolSizeCount=2;di.pPoolSizes=sizes;
    if(vkCreateDescriptorPool(d,&di,0,&f->pool)!=VK_SUCCESS)return 0;VkDescriptorSetAllocateInfo ai={0};ai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;ai.descriptorPool=f->pool;ai.descriptorSetCount=1;ai.pSetLayouts=&f->sl;
    if(vkAllocateDescriptorSets(d,&ai,&f->set)!=VK_SUCCESS)return 0;
    VkDescriptorImageInfo im={0,view,VK_IMAGE_LAYOUT_GENERAL};VkDescriptorBufferInfo bi={buffer,16u,256u};VkWriteDescriptorSet writes[2]={{0}};
    for(uint32_t i=0;i<2;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=f->set;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=bindings[i].descriptorType;}
    writes[0].pImageInfo=&im;writes[1].pBufferInfo=&bi;vkUpdateDescriptorSets(d,2,writes,0,0);
    VkShaderModuleCreateInfo mi={0};mi.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;mi.pCode=compute_readframe_spv;mi.codeSize=sizeof(compute_readframe_spv);
    if(vkCreateShaderModule(d,&mi,0,&f->shader)!=VK_SUCCESS)return 0;VkComputePipelineCreateInfo pi={0};pi.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;pi.layout=f->layout;pi.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pi.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pi.stage.module=f->shader;pi.stage.pName="main";
    return vkCreateComputePipelines(d,0,1,&pi,0,&f->pipeline)==VK_SUCCESS;
}
