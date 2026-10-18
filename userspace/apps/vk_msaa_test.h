/* Two calls through a nested helper mutate a Function-storage struct/array.
   Constants and logical aggregates have no std140 padding. Pixel red=.75. */
static const uint32_t function_local_fragment_spv[]={
0x07230203u,0x00010000u,0u,90u,0u,
0x20011u,1u,0x3000eu,0u,1u,
0x6000fu,4u,50u,0x6e69616du,0u,21u,0x30010u,50u,7u,
0x40047u,21u,30u,0u,
0x20013u,1u,0x30021u,2u,1u,
0x30016u,3u,32u,0x40015u,4u,32u,0u,
0x40017u,5u,3u,4u,0x40017u,6u,3u,2u,
0x4002bu,4u,18u,0u,0x4002bu,4u,19u,1u,0x4002bu,4u,20u,2u,
0x4001cu,7u,3u,20u,0x4001eu,8u,7u,6u,
0x40020u,9u,7u,8u,0x40020u,10u,7u,3u,0x40020u,11u,3u,5u,
0x40021u,12u,3u,10u,
0x4002bu,3u,13u,0u,0x4002bu,3u,14u,0x3f800000u,
0x4002bu,3u,15u,0x3e800000u,0x4002bu,3u,16u,0x3f000000u,
0x4002bu,3u,17u,0x3f400000u,
0x5002cu,7u,22u,15u,16u,0x5002cu,6u,23u,17u,14u,
0x5002cu,8u,24u,22u,23u,0x4003bu,11u,21u,3u,
/* Rectangular 2x3 / 3x2 matrices with immutable column constants. */
0x40017u,64u,3u,3u,0x40018u,65u,64u,2u,0x40018u,66u,6u,3u,0x40018u,67u,64u,3u,
0x6002cu,64u,69u,14u,13u,13u,0x6002cu,64u,70u,13u,14u,13u,
0x5002cu,65u,71u,69u,70u,0x5002cu,6u,72u,15u,16u,0x20014u,80u,
0x50036u,3u,30u,0u,12u,0x30037u,10u,31u,0x200f8u,32u,
0x4003du,3u,33u,31u,0x50081u,3u,34u,33u,15u,
0x3003eu,31u,34u,0x200feu,34u,0x10038u,
0x50036u,3u,40u,0u,12u,0x30037u,10u,41u,0x200f8u,42u,
0x50039u,3u,43u,30u,41u,0x200feu,43u,0x10038u,
0x50036u,1u,50u,0u,2u,0x200f8u,51u,0x5003bu,9u,52u,7u,24u,
0x60041u,10u,53u,52u,18u,18u,
0x50039u,3u,54u,40u,53u,0x50039u,3u,55u,40u,53u,
0x4003du,8u,56u,52u,0x70052u,8u,57u,13u,56u,1u,0u,
0x60051u,3u,58u,57u,0u,0u,0x40053u,3u,59u,58u,
0x70050u,5u,60u,59u,13u,13u,14u,
0x6004eu,5u,61u,60u,13u,19u,0x5004du,3u,62u,61u,18u,
0x70050u,5u,63u,62u,13u,13u,14u,
0x50091u,64u,73u,71u,72u,0x50090u,6u,74u,73u,71u,
0x40054u,66u,75u,71u,0x50092u,67u,76u,71u,75u,
0x50093u,65u,77u,73u,72u,0x5008fu,65u,78u,77u,14u,
0x60051u,3u,79u,78u,1u,1u,0x500b4u,80u,81u,79u,15u,
0x60051u,3u,82u,76u,0u,0u,0x500b4u,80u,83u,82u,14u,
0x500a7u,80u,84u,81u,83u,0x50051u,3u,85u,74u,1u,
0x500b4u,80u,86u,85u,16u,0x500a7u,80u,87u,84u,86u,
0x600a9u,3u,88u,87u,62u,13u,0x70050u,5u,89u,88u,13u,13u,14u,0x3003eu,21u,89u,
0x100fdu,0x10038u};
static const uint32_t discard_fragment_spv[]={
0x07230203u,0x00010000u,0u,9u,0u,
0x20011u,1u,0x3000eu,0u,1u,
0x6000fu,4u,6u,0x6e69616du,0u,5u,0x30010u,6u,7u,
0x40047u,5u,30u,0u,0x20013u,1u,0x30016u,2u,32u,
0x40017u,3u,2u,4u,0x40020u,4u,3u,3u,
0x30021u,7u,1u,0x4003bu,4u,5u,3u,
0x50036u,1u,6u,0u,7u,0x200f8u,8u,
0x100fcu,0x10038u};
/* Shader reads a std430 scalar array at byte 112; SpecId 42 selects its
   element. The entry's ID exceeds the previous 512-ID resource ceiling. */
static const uint32_t wide_push_fragment_spv[]={
0x07230203u,0x00010000u,0u,1025u,0u,
0x00020011u,1u,0x0003000eu,0u,1u,
0x0006000fu,4u,1024u,0x6e69616du,0u,20u,0x00030010u,1024u,7u,
0x00030047u,7u,2u,0x00050048u,7u,0u,35u,112u,
0x00040047u,6u,6u,4u,0x00040047u,20u,30u,0u,0x00040047u,15u,1u,42u,
0x00020013u,1u,0x00030021u,2u,1u,0x00030016u,3u,32u,
0x00040015u,4u,32u,0u,0x00040017u,5u,3u,4u,
0x0004002bu,4u,11u,4u,0x0004001cu,6u,3u,11u,0x0003001eu,7u,6u,
0x00040020u,8u,9u,7u,0x00040020u,9u,9u,3u,0x00040020u,10u,3u,5u,
0x0004002bu,3u,12u,0u,0x0004002bu,3u,13u,0x3f800000u,
0x0004002bu,4u,14u,0u,0x00040032u,4u,15u,0u,
0x0004003bu,8u,19u,9u,0x0004003bu,10u,20u,3u,
0x00050036u,1u,1024u,0u,2u,0x000200f8u,21u,
0x00060041u,9u,22u,19u,14u,15u,0x0004003du,3u,23u,22u,
0x00070050u,5u,24u,23u,12u,12u,13u,0x0003003eu,20u,24u,
0x000100fdu,0x00010038u};
/* Coverage oracle is independent of the renderer: the supplied vertex shader
   defines the screen triangle (0,0),(2,0),(1,2). Inspect both resolve pixels and
   per-sample depth/stencil, with untouched layer zero as a guard. */
/* FrontFacing is a real bool input, not a location/varying alias. */
static const uint32_t front_facing_fragment_spv[]={
0x07230203u,0x10000u,0u,22u,0u,
0x20011u,1u,0x3000eu,0u,1u,0x7000fu,4u,14u,0x6e69616du,0u,8u,9u,0x30010u,14u,7u,
0x40047u,8u,11u,17u,0x40047u,9u,30u,0u,
0x20013u,1u,0x30021u,2u,1u,0x20014u,3u,0x30016u,4u,32u,0x40017u,5u,4u,4u,
0x40020u,6u,1u,3u,0x40020u,7u,3u,5u,0x4003bu,6u,8u,1u,0x4003bu,7u,9u,3u,
0x4002bu,4u,10u,0u,0x4002bu,4u,11u,0x3f800000u,
0x7002cu,5u,12u,11u,10u,10u,11u,0x7002cu,5u,13u,10u,11u,10u,11u,
0x50036u,1u,14u,0u,2u,0x200f8u,15u,0x4003du,3u,16u,8u,
0x600a9u,4u,17u,16u,11u,10u,0x600a9u,4u,18u,16u,10u,11u,
0x70050u,5u,19u,17u,18u,10u,11u,0x3003eu,9u,19u,0x100fdu,0x10038u};

/* Fragment-center x/y, interpolated reciprocal clip-W, and real depth. */
static const uint32_t frag_coord_fragment_spv[]={
0x07230203u,0x10000u,0u,25u,0u,
0x20011u,1u,0x3000eu,0u,1u,0x7000fu,4u,14u,0x6e69616du,0u,8u,9u,0x30010u,14u,7u,
0x40047u,8u,11u,15u,0x40047u,9u,30u,0u,
0x20013u,1u,0x30021u,2u,1u,0x30016u,3u,32u,0x40017u,4u,3u,4u,
0x40020u,5u,1u,4u,0x40020u,6u,3u,4u,0x4003bu,5u,8u,1u,0x4003bu,6u,9u,3u,
0x4002bu,3u,11u,0x3f800000u,0x4002bu,3u,12u,0x3f000000u,
0x50036u,1u,14u,0u,2u,0x200f8u,13u,0x4003du,4u,15u,8u,
0x50051u,3u,16u,15u,0u,0x50051u,3u,17u,15u,1u,
0x50051u,3u,18u,15u,2u,0x50051u,3u,19u,15u,3u,
0x50085u,3u,20u,16u,12u,0x50085u,3u,21u,17u,12u,0x50085u,3u,22u,19u,12u,
0x70050u,4u,23u,20u,21u,22u,11u,0x3003eu,9u,23u,0x100fdu,0x10038u};

static int vk_msaa_case(VkDevice device,VkQueue queue,const VkGraphicsPipelineCreateInfo *base,
        uint32_t samples,uint32_t mask,VkFormat depth_format,int half_alpha,int implicit){
    uint32_t test_mode=(uint32_t)implicit;
    int function_case=implicit==4,discard_case=implicit==5;
    int dynamic_case=implicit==6||implicit==7,depth_fail=implicit==7,logic_case=implicit>=8&&implicit<24;
    int coords=implicit>=32&&implicit<=34,coord_depth=implicit==34;
    int gradient=implicit==24||implicit==25||coords,perspective=implicit==25||implicit==33||implicit==34,attachment_clear=implicit==26,instancing=implicit>=27&&implicit<30,instance_fetch=implicit>=28&&implicit<30;
    int zero_stride=implicit==29;
    int facing_case=implicit==30||implicit==31,reverse_face=implicit==31;
    uint32_t logic_index=logic_case?(uint32_t)implicit-8u:0u;
    int wide=implicit==2||implicit==3;int specialized=implicit==2;implicit=implicit==1;
    VkImage color=0,depth=0,result=0;VkDeviceMemory cm=0,dm=0,rm=0;
    VkImageView cv=0,dv=0,rv=0;VkRenderPass pass=0;VkFramebuffer fb=0;
    VkShaderModule vs=0,fs=0;VkPipelineLayout own_layout=0;VkPipeline pipeline=0;VkCommandPool pool=0;VkCommandBuffer cmd=0;VkQueryPool queries=0;VkBuffer qb=0,instance_buffer=0;VkDeviceMemory qm=0,instance_memory=0;uint64_t *query_pixels=0;VkEvent instance_gate=0;VkFence instance_fence=0;
    uint32_t *colors=NULL,*resolved=NULL;void *depths=NULL;int ok=0;uint32_t stage=1u,failed_line=0u;
    VkImageCreateInfo ii={0};ii.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;ii.imageType=VK_IMAGE_TYPE_2D;
    ii.format=VK_FORMAT_B8G8R8A8_UNORM;ii.extent=(VkExtent3D){2u,2u,1u};ii.mipLevels=1u;ii.arrayLayers=2u;
    ii.samples=(VkSampleCountFlagBits)samples;ii.tiling=VK_IMAGE_TILING_OPTIMAL;
    ii.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkMemoryAllocateInfo ma={0};ma.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;ma.allocationSize=32u*samples;
    VkMemoryRequirements requirements;
    if(vkCreateImage(device,&ii,NULL,&color)!=VK_SUCCESS){failed_line=__LINE__;goto out;}
    vkGetImageMemoryRequirements(device,color,&requirements);
    if(requirements.size!=ma.allocationSize||vkAllocateMemory(device,&ma,NULL,&cm)!=VK_SUCCESS||
       vkBindImageMemory(device,color,cm,0u)!=VK_SUCCESS||vkMapMemory(device,cm,0u,VK_WHOLE_SIZE,0u,(void**)&colors)!=VK_SUCCESS){failed_line=__LINE__;goto out;}
    memset(colors,0xcc,(size_t)ma.allocationSize);
    ii.format=depth_format;ii.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ma.allocationSize=(depth_format==VK_FORMAT_S8_UINT?8u:depth_format==VK_FORMAT_D32_SFLOAT_S8_UINT?64u:32u)*samples;
    if(vkCreateImage(device,&ii,NULL,&depth)!=VK_SUCCESS||vkAllocateMemory(device,&ma,NULL,&dm)!=VK_SUCCESS||
       vkBindImageMemory(device,depth,dm,0u)!=VK_SUCCESS||vkMapMemory(device,dm,0u,VK_WHOLE_SIZE,0u,&depths)!=VK_SUCCESS){failed_line=__LINE__;goto out;}
    memset(depths,0xcc,(size_t)ma.allocationSize);
    ii.format=VK_FORMAT_B8G8R8A8_UNORM;ii.samples=VK_SAMPLE_COUNT_1_BIT;ii.tiling=VK_IMAGE_TILING_LINEAR;
    ii.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;ma.allocationSize=32u;
    if(vkCreateImage(device,&ii,NULL,&result)!=VK_SUCCESS||vkAllocateMemory(device,&ma,NULL,&rm)!=VK_SUCCESS||
       vkBindImageMemory(device,result,rm,0u)!=VK_SUCCESS||vkMapMemory(device,rm,0u,VK_WHOLE_SIZE,0u,(void**)&resolved)!=VK_SUCCESS){failed_line=__LINE__;goto out;}
    for(uint32_t i=0u;i<8u;++i)resolved[i]=0xccccccccu;
    VkImageViewCreateInfo vi={0};vi.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;
    vi.format=VK_FORMAT_B8G8R8A8_UNORM;vi.image=color;vi.subresourceRange=(VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT,0u,1u,1u,1u};
    if(vkCreateImageView(device,&vi,NULL,&cv)!=VK_SUCCESS){failed_line=__LINE__;goto out;}
    VkImageAspectFlags aspect=depth_format==VK_FORMAT_S8_UINT?VK_IMAGE_ASPECT_STENCIL_BIT:depth_format==VK_FORMAT_D32_SFLOAT_S8_UINT?(VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT):VK_IMAGE_ASPECT_DEPTH_BIT;
    vi.format=depth_format;vi.image=depth;vi.subresourceRange.aspectMask=aspect;
    if(vkCreateImageView(device,&vi,NULL,&dv)!=VK_SUCCESS){failed_line=__LINE__;goto out;}
    vi.image=result;vi.format=VK_FORMAT_B8G8R8A8_UNORM;vi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    if(implicit&&vkCreateImageView(device,&vi,NULL,&rv)!=VK_SUCCESS){failed_line=__LINE__;goto out;}
    VkAttachmentDescription attachments[3]={0};
    for(uint32_t i=0u;i<2u;++i){attachments[i].samples=(VkSampleCountFlagBits)samples;
        attachments[i].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachments[i].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        attachments[i].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachments[i].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[i].initialLayout=attachments[i].finalLayout=VK_IMAGE_LAYOUT_GENERAL;}
    attachments[0].format=VK_FORMAT_B8G8R8A8_UNORM;attachments[1].format=depth_format;
    if(depth_format!=VK_FORMAT_D32_SFLOAT){attachments[1].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachments[1].stencilStoreOp=VK_ATTACHMENT_STORE_OP_STORE;}
    attachments[2]=attachments[0];attachments[2].samples=VK_SAMPLE_COUNT_1_BIT;attachments[2].loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    VkAttachmentReference cr={0u,VK_IMAGE_LAYOUT_GENERAL},dr={1u,VK_IMAGE_LAYOUT_GENERAL},rr={2u,VK_IMAGE_LAYOUT_GENERAL};
    /* Permute attachment numbering to exercise the public reference contract. */
    if(implicit){VkAttachmentDescription swap=attachments[0];attachments[0]=attachments[2];attachments[2]=swap;cr.attachment=2u;rr.attachment=0u;}
    VkSubpassDescription sub={0};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1u;sub.pColorAttachments=&cr;sub.pDepthStencilAttachment=&dr;sub.pResolveAttachments=implicit?&rr:NULL;
    VkRenderPassCreateInfo rp={0};rp.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;rp.attachmentCount=implicit?3u:2u;rp.pAttachments=attachments;rp.subpassCount=1u;rp.pSubpasses=&sub;
    VkSubpassDependency dependencies[2]={0};
    dependencies[0].srcSubpass=VK_SUBPASS_EXTERNAL;dependencies[0].dstSubpass=0u;
    dependencies[0].srcStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;dependencies[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;dependencies[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags=VK_DEPENDENCY_BY_REGION_BIT;
    dependencies[1]=dependencies[0];dependencies[1].srcSubpass=0u;dependencies[1].dstSubpass=VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependencies[1].dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependencies[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;dependencies[1].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    rp.dependencyCount=2u;rp.pDependencies=dependencies;
    if(vkCreateRenderPass(device,&rp,NULL,&pass)!=VK_SUCCESS){failed_line=__LINE__;goto out;}
    VkExtent2D granularity={0};vkGetRenderAreaGranularity(device,pass,&granularity);if(granularity.width!=1u||granularity.height!=1u){failed_line=__LINE__;goto out;}
    VkImageView views[3]={implicit?rv:cv,dv,cv};VkFramebufferCreateInfo fi={0};fi.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass=pass;fi.attachmentCount=implicit?3u:2u;fi.pAttachments=views;fi.width=fi.height=2u;fi.layers=1u;
    if(vkCreateFramebuffer(device,&fi,NULL,&fb)!=VK_SUCCESS){failed_line=__LINE__;goto out;}
    VkShaderModuleCreateInfo sm={0};sm.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;uint32_t vertex[sizeof(triangle_vertex_spv)/4u+4u];
    memcpy(vertex,triangle_vertex_spv,sizeof(triangle_vertex_spv));
    if(dynamic_case){
        uint32_t off=5u;while((vertex[off]&65535u)!=54u)off+=vertex[off]>>16u;
        memmove(vertex+off+4u,vertex+off,sizeof(triangle_vertex_spv)-off*4u);
        vertex[3]=27u;vertex[off]=0x4002bu;vertex[off+1u]=4u;vertex[off+2u]=26u;vertex[off+3u]=0x3f000000u;
        for(off=5u;off<sizeof(vertex)/4u;){uint32_t wc=vertex[off]>>16u;
            if((vertex[off]&65535u)==80u&&vertex[off+2u]==25u)vertex[off+5u]=26u;off+=wc;}
    }
    sm.codeSize=dynamic_case?sizeof(vertex):sizeof(triangle_vertex_spv);sm.pCode=vertex;
    uint32_t varying[sizeof(varying_vertex_spv)/4u+19u];
    if(gradient){
        memcpy(varying,varying_vertex_spv,sizeof(varying_vertex_spv));
        uint32_t words=sizeof(varying_vertex_spv)/4u;
        if(perspective){
            uint32_t off=5u;while((varying[off]&65535u)!=54u)off+=varying[off]>>16u;
            memmove(varying+off+4u,varying+off,(words-off)*4u);words+=4u;
            varying[off]=0x4002bu;varying[off+1u]=3u;varying[off+2u]=33u;varying[off+3u]=0x40000000u;
            off=5u;while(!((varying[off]&65535u)==62u&&varying[off+1u]==8u))off+=varying[off]>>16u;
            memmove(varying+off+11u,varying+off,(words-off)*4u);words+=11u;
            uint32_t operations[]={0x600a9u,3u,32u,23u,33u,15u,0x5008eu,4u,34u,28u,32u};
            memcpy(varying+off,operations,sizeof(operations));varying[off+13u]=34u;varying[3]=35u;
        }
        if(coord_depth){
            uint32_t off=5u;while((varying[off]&65535u)!=54u)off+=varying[off]>>16u;
            memmove(varying+off+4u,varying+off,(words-off)*4u);words+=4u;
            uint32_t c[]={0x4002bu,3u,35u,0x3e800000u};memcpy(varying+off,c,sizeof(c));varying[3]=36u;
            for(off=5u;off<words;){if((varying[off]&65535u)==80u&&varying[off+2u]==28u)varying[off+5u]=35u;off+=varying[off]>>16u;}
        }
        sm.pCode=varying;sm.codeSize=words*4u;
    }
    uint32_t instances[sizeof(varying_vertex_spv)/4u+40u];
    if(instancing){
        memcpy(instances,varying_vertex_spv,sizeof(varying_vertex_spv));uint32_t words=sizeof(varying_vertex_spv)/4u;
        uint32_t off=5u;while((instances[off]&65535u)!=15u)off+=instances[off]>>16u;
        uint32_t wc=instances[off]>>16u;memmove(instances+off+wc+1u,instances+off+wc,(words-off-wc)*4u);words++;
        instances[off]+=1u<<16u;instances[off+wc]=32u;
        off=5u;while((instances[off]&65535u)!=19u)off+=instances[off]>>16u;
        memmove(instances+off+4u,instances+off,(words-off)*4u);words+=4u;
        instances[off]=0x40047u;instances[off+1u]=32u;instances[off+2u]=instance_fetch?30u:11u;instances[off+3u]=instance_fetch?0u:43u;
        off=5u;while((instances[off]&65535u)!=54u)off+=instances[off]>>16u;
        uint32_t declarations[]={0x4002bu,3u,33u,0x3e800000u,0x40020u,38u,1u,3u,0x4003bu,instance_fetch?38u:5u,32u,1u};
        memmove(instances+off+12u,instances+off,(words-off)*4u);words+=12u;memcpy(instances+off,declarations,sizeof(declarations));
        off=5u;while(!((instances[off]&65535u)==62u&&instances[off+1u]==9u))off+=instances[off]>>16u;
        uint32_t operations[]={0x4003du,2u,34u,32u,0x40070u,3u,35u,34u,0x50085u,3u,36u,35u,33u,0x70050u,4u,37u,36u,14u,14u,15u};
        if(instance_fetch){operations[1]=3u;operations[4]=0x40053u;operations[12]=15u;}
        memmove(instances+off+20u,instances+off,(words-off)*4u);words+=20u;memcpy(instances+off,operations,sizeof(operations));
        instances[off+22u]=37u;instances[off+25u]=37u;instances[3]=39u;
        sm.pCode=instances;sm.codeSize=words*4u;
    }
    if(vkCreateShaderModule(device,&sm,NULL,&vs)!=VK_SUCCESS){failed_line=__LINE__;goto out;}
    uint32_t fragment[sizeof(solid_fragment_spv)/4u];memcpy(fragment,solid_fragment_spv,sizeof(fragment));
    if(half_alpha)for(uint32_t off=5u;off<sizeof(fragment)/4u;){uint32_t wc=fragment[off]>>16u;
        if((fragment[off]&0xffffu)==43u&&fragment[off+2u]==9u)fragment[off+3u]=0x3f000000u;off+=wc;}
    sm.codeSize=function_case?sizeof(function_local_fragment_spv):discard_case?sizeof(discard_fragment_spv):wide?sizeof(wide_push_fragment_spv):sizeof(fragment);
    sm.pCode=function_case?function_local_fragment_spv:discard_case?discard_fragment_spv:wide?wide_push_fragment_spv:fragment;if(gradient||instancing){sm.pCode=varying_fragment_spv;sm.codeSize=sizeof(varying_fragment_spv);}
    if(facing_case){sm.pCode=front_facing_fragment_spv;sm.codeSize=sizeof(front_facing_fragment_spv);}
    uint32_t coord_shader[sizeof(frag_coord_fragment_spv)/4u];
    if(coords){
        memcpy(coord_shader,frag_coord_fragment_spv,sizeof(coord_shader));
        if(coord_depth)for(uint32_t off=5u;off<sizeof(coord_shader)/4u;){if((coord_shader[off]&65535u)==80u)coord_shader[off+6u]=18u;off+=coord_shader[off]>>16u;}
        sm.pCode=coord_shader;sm.codeSize=sizeof(coord_shader);
    }
    if(vkCreateShaderModule(device,&sm,NULL,&fs)!=VK_SUCCESS){failed_line=__LINE__;goto out;}
    VkPipelineShaderStageCreateInfo stages[2];memcpy(stages,base->pStages,sizeof(stages));stages[0].module=vs;stages[1].module=fs;
    uint32_t index=3u;VkSpecializationMapEntry map={42u,0u,4u};VkSpecializationInfo spec={1u,&map,4u,&index};
    if(specialized)stages[1].pSpecializationInfo=&spec;
    if(wide){VkPushConstantRange push={VK_SHADER_STAGE_FRAGMENT_BIT,112u,16u};VkPipelineLayoutCreateInfo layout={0};
        layout.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout.pushConstantRangeCount=1u;layout.pPushConstantRanges=&push;
        if(vkCreatePipelineLayout(device,&layout,NULL,&own_layout)!=VK_SUCCESS){failed_line=__LINE__;goto out;}
    }
    VkPipelineMultisampleStateCreateInfo multisample=*base->pMultisampleState;
    multisample.rasterizationSamples=(VkSampleCountFlagBits)samples;multisample.pSampleMask=&mask;multisample.alphaToCoverageEnable=half_alpha?VK_TRUE:VK_FALSE;
    VkPipelineDepthStencilStateCreateInfo ds={0};ds.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable=ds.depthWriteEnable=depth_format!=VK_FORMAT_S8_UINT;ds.depthCompareOp=VK_COMPARE_OP_LESS;
    ds.stencilTestEnable=depth_format!=VK_FORMAT_D32_SFLOAT;ds.front.compareOp=ds.back.compareOp=VK_COMPARE_OP_ALWAYS;
    ds.front.passOp=ds.back.passOp=VK_STENCIL_OP_REPLACE;ds.front.reference=ds.back.reference=7u;
    ds.front.writeMask=ds.back.writeMask=255u;
    VkGraphicsPipelineCreateInfo pi=*base;pi.renderPass=pass;pi.pStages=stages;pi.pMultisampleState=&multisample;pi.pDepthStencilState=&ds;if(wide)pi.layout=own_layout;
    if(function_case){
        uint32_t malformed[sizeof(function_local_fragment_spv)/4u];
        for(uint32_t mode=0u;mode<2u;++mode){
            memcpy(malformed,function_local_fragment_spv,sizeof(malformed));
            for(uint32_t off=5u;off<sizeof(malformed)/4u;){uint32_t wc=malformed[off]>>16u;
                if((malformed[off]&65535u)==57u&&malformed[off+2u]==43u){
                    if(mode==0u)malformed[off+3u]=40u;else malformed[off+4u]=15u;
                }off+=wc;
            }
            VkShaderModule bad=0;VkPipeline rejected=0;sm.pCode=malformed;sm.codeSize=sizeof(malformed);
            if(vkCreateShaderModule(device,&sm,NULL,&bad)!=VK_SUCCESS){failed_line=__LINE__;goto out;}
            stages[1].module=bad;VkResult r=vkCreateGraphicsPipelines(device,0,1u,&pi,NULL,&rejected);
            vkDestroyShaderModule(device,bad,NULL);stages[1].module=fs;
            if(r==VK_SUCCESS||rejected){if(rejected)vkDestroyPipeline(device,rejected,NULL);{failed_line=__LINE__;goto out;}}
        }
    }
    VkPipelineRasterizationStateCreateInfo raster=*pi.pRasterizationState;
    if(facing_case){raster.cullMode=VK_CULL_MODE_NONE;raster.frontFace=reverse_face?VK_FRONT_FACE_CLOCKWISE:VK_FRONT_FACE_COUNTER_CLOCKWISE;pi.pRasterizationState=&raster;}
    VkPipelineColorBlendStateCreateInfo blend=*pi.pColorBlendState;
    VkPipelineColorBlendAttachmentState ba=*blend.pAttachments;
    VkDynamicState dynamic_states[]={VK_DYNAMIC_STATE_BLEND_CONSTANTS,VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,VK_DYNAMIC_STATE_STENCIL_REFERENCE,VK_DYNAMIC_STATE_DEPTH_BIAS,VK_DYNAMIC_STATE_LINE_WIDTH};
    VkPipelineDynamicStateCreateInfo dynamic={0};dynamic.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount=6u;dynamic.pDynamicStates=dynamic_states;
    if(dynamic_case){
        raster.depthBiasEnable=VK_TRUE;raster.depthBiasConstantFactor=0.0f;raster.depthBiasSlopeFactor=0.0f;
        pi.pRasterizationState=&raster;pi.pDynamicState=&dynamic;
        ba.blendEnable=VK_TRUE;ba.srcColorBlendFactor=VK_BLEND_FACTOR_CONSTANT_COLOR;ba.dstColorBlendFactor=VK_BLEND_FACTOR_ZERO;
        ba.srcAlphaBlendFactor=VK_BLEND_FACTOR_CONSTANT_ALPHA;ba.dstAlphaBlendFactor=VK_BLEND_FACTOR_ZERO;
        ba.colorBlendOp=ba.alphaBlendOp=VK_BLEND_OP_ADD;blend.pAttachments=&ba;
        memset(blend.blendConstants,0,sizeof(blend.blendConstants));pi.pColorBlendState=&blend;
        ds.front.reference=ds.back.reference=99u;ds.front.writeMask=ds.back.writeMask=0u;
        ds.front.depthFailOp=ds.back.depthFailOp=VK_STENCIL_OP_INVERT;
    }
    if(logic_case){blend.logicOpEnable=VK_TRUE;blend.logicOp=(VkLogicOp)logic_index;
        /* Vulkan ignores blendEnable when logical operations are enabled. */
        ba.blendEnable=VK_TRUE;blend.pAttachments=&ba;pi.pColorBlendState=&blend;}
    VkPipelineVertexInputStateCreateInfo vertex_input=*pi.pVertexInputState;
    VkVertexInputBindingDescription binding={0u,zero_stride?0u:4u,VK_VERTEX_INPUT_RATE_INSTANCE};
    VkVertexInputAttributeDescription attribute={0u,0u,VK_FORMAT_R32_SFLOAT,0u};
    if(instancing)ds.depthCompareOp=VK_COMPARE_OP_ALWAYS;
    if(instance_fetch){vertex_input.vertexBindingDescriptionCount=vertex_input.vertexAttributeDescriptionCount=1u;
        vertex_input.pVertexBindingDescriptions=&binding;vertex_input.pVertexAttributeDescriptions=&attribute;pi.pVertexInputState=&vertex_input;}
    VkPipelineCache cache=0;VkPipelineCacheCreateInfo cc={0};cc.sType=VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    if(wide&&vkCreatePipelineCache(device,&cc,NULL,&cache)!=VK_SUCCESS){failed_line=__LINE__;goto out;}
    VkResult pipeline_result=vkCreateGraphicsPipelines(device,cache,1u,&pi,NULL,&pipeline);
    if(pipeline_result!=VK_SUCCESS){write_hex32("vk_test: MSAA pipeline create return=",(uint32_t)pipeline_result);if(cache)vkDestroyPipelineCache(device,cache,NULL);{failed_line=__LINE__;goto out;}}
    if(wide){
        size_t bytes=0u;uint8_t data[4096];VkPipelineCache imported=0,merged=0;
        if(vkGetPipelineCacheData(device,cache,&bytes,NULL)!=VK_SUCCESS||bytes<=sizeof(VkPipelineCacheHeaderVersionOne)||bytes>sizeof(data)){
            vkDestroyPipelineCache(device,cache,NULL);{failed_line=__LINE__;goto out;}}
        size_t short_size=sizeof(VkPipelineCacheHeaderVersionOne);
        if(vkGetPipelineCacheData(device,cache,&short_size,data)!=VK_INCOMPLETE||short_size!=sizeof(VkPipelineCacheHeaderVersionOne)){
            vkDestroyPipelineCache(device,cache,NULL);{failed_line=__LINE__;goto out;}}
        size_t capacity=sizeof(data);
        if(vkGetPipelineCacheData(device,cache,&capacity,data)!=VK_SUCCESS||capacity!=bytes){vkDestroyPipelineCache(device,cache,NULL);{failed_line=__LINE__;goto out;}}
        cc.initialDataSize=bytes;cc.pInitialData=data;
        if(vkCreatePipelineCache(device,&cc,NULL,&imported)!=VK_SUCCESS){vkDestroyPipelineCache(device,cache,NULL);{failed_line=__LINE__;goto out;}}
        cc.initialDataSize=0u;cc.pInitialData=NULL;
        if(vkCreatePipelineCache(device,&cc,NULL,&merged)!=VK_SUCCESS){vkDestroyPipelineCache(device,cache,NULL);vkDestroyPipelineCache(device,imported,NULL);{failed_line=__LINE__;goto out;}}
        VkGraphicsPipelineCreateInfo batch[2]={pi,pi};VkPipeline pipelines[2]={0};
        VkResult merge=vkMergePipelineCaches(device,merged,1u,&imported);
        VkResult create=vkCreateGraphicsPipelines(device,merged,2u,batch,NULL,pipelines);
        vkDestroyPipelineCache(device,cache,NULL);vkDestroyPipelineCache(device,imported,NULL);vkDestroyPipelineCache(device,merged,NULL);
        if(merge!=VK_SUCCESS||create!=VK_SUCCESS){if(pipelines[0])vkDestroyPipeline(device,pipelines[0],NULL);if(pipelines[1])vkDestroyPipeline(device,pipelines[1],NULL);{failed_line=__LINE__;goto out;}}
        vkDestroyPipeline(device,pipelines[0],NULL);vkDestroyPipeline(device,pipeline,NULL);pipeline=pipelines[1];
    }
    VkCommandPoolCreateInfo cp={0};cp.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;cp.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandBufferAllocateInfo ca={0};ca.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;ca.commandBufferCount=1u;
    if(vkCreateCommandPool(device,&cp,NULL,&pool)!=VK_SUCCESS){failed_line=__LINE__;goto out;}ca.commandPool=pool;
    if(vkAllocateCommandBuffers(device,&ca,&cmd)!=VK_SUCCESS||!vk_subresource_begin(cmd)){failed_line=__LINE__;goto out;}
    VkQueryPoolCreateInfo qc={0};qc.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;qc.queryType=VK_QUERY_TYPE_OCCLUSION;qc.queryCount=3u;
    if(vkCreateQueryPool(device,&qc,NULL,&queries)!=VK_SUCCESS){failed_line=__LINE__;goto out;}
    uint64_t unavailable[2]={0xdeadbeefull,9u};
    if(vkGetQueryPoolResults(device,queries,1u,1u,sizeof(unavailable),unavailable,16u,VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)!=VK_NOT_READY||unavailable[0]!=0xdeadbeefull||unavailable[1]!=0u){failed_line=__LINE__;goto out;}
    VkBufferCreateInfo bc={0};bc.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;bc.size=32u;bc.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    ma.allocationSize=32u;
    if(vkCreateBuffer(device,&bc,NULL,&qb)!=VK_SUCCESS||vkAllocateMemory(device,&ma,NULL,&qm)!=VK_SUCCESS||vkBindBufferMemory(device,qb,qm,0u)!=VK_SUCCESS||vkMapMemory(device,qm,0u,VK_WHOLE_SIZE,0u,(void**)&query_pixels)!=VK_SUCCESS){failed_line=__LINE__;goto out;}
    for(uint32_t i=0u;i<4u;++i)query_pixels[i]=0xccccccccccccccccull;
    vkCmdResetQueryPool(cmd,queries,0u,3u);vkCmdBeginQuery(cmd,queries,1u,VK_QUERY_CONTROL_PRECISE_BIT);
    VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0u,1u,0u,2u};
    vk_subresource_barrier(cmd,color,range,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL);
    vk_subresource_barrier(cmd,result,range,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL);
    range.aspectMask=aspect;vk_subresource_barrier(cmd,depth,range,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL);
    if(instancing){VkEventCreateInfo ec={0};ec.sType=VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;
        if(vkCreateEvent(device,&ec,NULL,&instance_gate)!=VK_SUCCESS){failed_line=__LINE__;goto out;}
        vkCmdWaitEvents(cmd,1u,&instance_gate,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0u,NULL,0u,NULL,0u,NULL);
    }
    if(instance_fetch){VkBufferCreateInfo bi={0};bi.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;bi.size=16u;bi.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        ma.allocationSize=16u;ma.pNext=NULL;float *values=0;
        if(vkCreateBuffer(device,&bi,NULL,&instance_buffer)!=VK_SUCCESS||vkAllocateMemory(device,&ma,NULL,&instance_memory)!=VK_SUCCESS||
           vkBindBufferMemory(device,instance_buffer,instance_memory,0u)!=VK_SUCCESS||vkMapMemory(device,instance_memory,0u,VK_WHOLE_SIZE,0u,(void**)&values)!=VK_SUCCESS){failed_line=__LINE__;goto out;}
        values[0]=.75f;values[1]=.25f;values[2]=.5f;values[3]=.75f;
        VkDeviceSize offset=0u;vkCmdBindVertexBuffers(cmd,0u,1u,&instance_buffer,&offset);
    }
    VkClearValue clear[3]={0};clear[0].color.float32[3]=1.0f;clear[2]=clear[0];clear[1].depthStencil=(VkClearDepthStencilValue){.75f,3u};
    if(logic_case){clear[0].color.float32[0]=.25f;clear[0].color.float32[1]=.5f;clear[0].color.float32[2]=.75f;}
    VkRenderPassBeginInfo begin={0};begin.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;begin.renderPass=pass;begin.framebuffer=fb;
    begin.renderArea.extent=(VkExtent2D){2u,2u};begin.clearValueCount=implicit?3u:2u;begin.pClearValues=clear;
    vkCmdBeginRenderPass(cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
    if(wide){float values[4]={.25f,.5f,.75f,1.0f};vkCmdPushConstants(cmd,own_layout,VK_SHADER_STAGE_FRAGMENT_BIT,112u,16u,values);}
    if(dynamic_case){float constants[4]={.5f,.25f,.75f,1.0f};vkCmdSetBlendConstants(cmd,constants);
        vkCmdSetStencilCompareMask(cmd,VK_STENCIL_FACE_FRONT_BIT,255u);vkCmdSetStencilCompareMask(cmd,VK_STENCIL_FACE_BACK_BIT,255u);
        vkCmdSetStencilWriteMask(cmd,VK_STENCIL_FACE_FRONT_AND_BACK,255u);vkCmdSetStencilReference(cmd,VK_STENCIL_FACE_FRONT_AND_BACK,7u);
        vkCmdSetDepthBias(cmd,depth_fail?4194304.0f:2097152.0f,0.0f,0.0f);vkCmdSetLineWidth(cmd,1.0f);}
    vkCmdDraw(cmd,3u,instancing?2u:1u,0u,instancing?2u:0u);
    if(dynamic_case){float zero[4]={0};vkCmdSetBlendConstants(cmd,zero);vkCmdSetStencilReference(cmd,VK_STENCIL_FACE_FRONT_AND_BACK,0u);vkCmdSetDepthBias(cmd,0.0f,0.0f,0.0f);}
    if(attachment_clear){VkClearAttachment clear_attachment[2]={0};VkClearRect rect={{{0,0},{1u,1u}},0u,1u};
        clear_attachment[0].aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;clear_attachment[0].clearValue.color=(VkClearColorValue){{0.0f,.25f,.5f,1.0f}};
        clear_attachment[1].aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT;clear_attachment[1].clearValue.depthStencil=(VkClearDepthStencilValue){.25f,31u};
        vkCmdClearAttachments(cmd,2u,clear_attachment,1u,&rect);
        memset(clear_attachment,0,sizeof(clear_attachment));rect.rect.extent=(VkExtent2D){2u,2u};
    }
    vkCmdEndRenderPass(cmd);vkCmdEndQuery(cmd,queries,1u);
    vkCmdCopyQueryPoolResults(cmd,queries,1u,1u,qb,8u,16u,VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT|VK_QUERY_RESULT_WAIT_BIT);
    VkImageResolve resolve={0};resolve.srcSubresource=resolve.dstSubresource=(VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT,0u,1u,1u};resolve.extent=(VkExtent3D){2u,2u,1u};
    if(!implicit)vkCmdResolveImage(cmd,color,VK_IMAGE_LAYOUT_GENERAL,result,VK_IMAGE_LAYOUT_GENERAL,1u,&resolve);
    stage=2u;
    if(instancing){
        VkFenceCreateInfo fi={0};fi.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if(vkCreateFence(device,&fi,NULL,&instance_fence)!=VK_SUCCESS||vkEndCommandBuffer(cmd)!=VK_SUCCESS){failed_line=__LINE__;goto out;}
        VkSubmitInfo submit={0};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1u;submit.pCommandBuffers=&cmd;
        if(vk_test_queue_submit_raw(queue,1u,&submit,instance_fence)!=VK_SUCCESS||vkGetFenceStatus(device,instance_fence)!=VK_NOT_READY){failed_line=__LINE__;goto out;}
        if(instance_buffer){vkDestroyBuffer(device,instance_buffer,NULL);instance_buffer=0;vkFreeMemory(device,instance_memory,NULL);instance_memory=0;}
        if(vkSetEvent(device,instance_gate)!=VK_SUCCESS||vkQueueWaitIdle(queue)!=VK_SUCCESS||vkGetFenceStatus(device,instance_fence)!=VK_SUCCESS){failed_line=__LINE__;goto out;}
    }else if(!vk_subresource_complete(queue,cmd)){failed_line=__LINE__;goto out;}
    static const float two[2][2]={{.75f,.75f},{.25f,.25f}};
    static const float four[4][2]={{.375f,.125f},{.875f,.375f},{.125f,.625f},{.625f,.875f}};
    static const float eight[8][2]={{.5625f,.3125f},{.4375f,.6875f},{.8125f,.5625f},{.3125f,.1875f},{.1875f,.8125f},{.0625f,.4375f},{.6875f,.9375f},{.9375f,.0625f}};
    uint32_t background=logic_case?0xff4080bfu:0xff000000u;
    uint32_t foreground=(function_case||instancing)?0xffbf0000u:dynamic_case?0xff800000u:wide&&!specialized?0xff400000u:half_alpha?0x80ff0000u:0xffff0000u;
    if(logic_case){
        /* Independent truth-table oracle, four minterms for each bit. */
        static const uint8_t truth[16]={0,8,4,12,2,10,6,14,1,9,5,13,3,11,7,15};foreground=0u;
        for(uint32_t bit=0u;bit<32u;++bit){uint32_t input=(((0xffff0000u>>bit)&1u)<<1u)|((background>>bit)&1u);
            foreground|=((truth[logic_index]>>input)&1u)<<bit;}
    }
    if(facing_case)foreground=reverse_face?0xff00ff00u:0xffff0000u;
    uint64_t total=0u;
    for(uint32_t y=0u;y<2u;++y)for(uint32_t x=0u;x<2u;++x){uint32_t hits=0u;uint32_t gradient_sum[4]={0};
        for(uint32_t sample=0u;sample<samples;++sample){const float *pos=samples==2u?two[sample]:samples==4u?four[sample]:eight[sample];
            float px=(float)x+pos[0],py=(float)y+pos[1];
            int hit=!discard_case&&px>=.5f*py&&px<2.0f-.5f*py&&(mask&(1u<<sample))&&(!half_alpha||sample<samples/2u);
            uint32_t index=(4u+y*2u+x)*samples+sample;hits+=(uint32_t)(hit&&!depth_fail);
            uint32_t expected_color=hit&&!depth_fail?foreground:background;
            if(gradient&&hit){
                /* Direct barycentric oracle for (right,top,left) clip vertices. */
                float right=px*.5f-py*.25f,top=py*.5f,left=1.0f-px*.5f-py*.25f;
                if(perspective)right*=.5f;float sum=right+top+left;
                expected_color=0xff000000u|((uint32_t)(right/sum*255.0f+.5f)<<16u)|((uint32_t)(top/sum*255.0f+.5f)<<8u)|(uint32_t)(left/sum*255.0f+.5f);
            }
            if(attachment_clear&&x==0u&&y==0u)expected_color=0xff004080u;
            if(coords&&hit){
                float cx=(float)x+.5f,cy=(float)y+.5f;
                float reciprocal=perspective?1.0f-.5f*(cx*.5f-cy*.25f):1.0f;
                expected_color=((coord_depth?64u:255u)<<24u)|
                    ((uint32_t)(cx*.5f*255.0f+.5f)<<16u)|
                    ((uint32_t)(cy*.5f*255.0f+.5f)<<8u)|
                    (uint32_t)(reciprocal*.5f*255.0f+.5f);
            }
            for(uint32_t c=0u;c<4u;++c)gradient_sum[c]+=(expected_color>>(c*8u))&255u;
            if(colors[index]!=expected_color){failed_line=__LINE__;goto out;}
            if(depth_format!=VK_FORMAT_S8_UINT){if(((float*)depths)[index*(depth_format==VK_FORMAT_D32_SFLOAT_S8_UINT?2u:1u)]!=(attachment_clear&&x==0u&&y==0u?.25f:hit&&!depth_fail?(dynamic_case?.625f:coord_depth?.25f:0.0f):.75f)){failed_line=__LINE__;goto out;}}
            if(depth_format!=VK_FORMAT_D32_SFLOAT){if(((uint8_t*)depths)[depth_format==VK_FORMAT_S8_UINT?index:index*8u+4u]!=(attachment_clear&&x==0u&&y==0u?31u:hit?(depth_fail?252u:7u):3u)){failed_line=__LINE__;goto out;}}
        }
        total+=hits;
        uint32_t expected=0u;
        for(uint32_t shift=0u;shift<32u;shift+=8u){uint32_t src=(foreground>>shift)&255u,dst=(background>>shift)&255u;
            expected|=((gradient||attachment_clear)?(gradient_sum[shift/8u]+samples/2u)/samples:(hits*src+(samples-hits)*dst+samples/2u)/samples)<<shift;}
        if(resolved[4u+y*2u+x]!=expected){failed_line=__LINE__;goto out;}
    }
    for(uint32_t i=0u;i<4u*samples;++i){if(colors[i]!=0xccccccccu){failed_line=__LINE__;goto out;}}
    for(uint32_t i=0u;i<4u;++i)if(resolved[i]!=0xccccccccu){failed_line=__LINE__;goto out;}
    if(instancing)total*=2u;
    uint64_t values[2]={0};
    if(vkGetQueryPoolResults(device,queries,1u,1u,sizeof(values),values,16u,VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT|VK_QUERY_RESULT_WAIT_BIT)!=VK_SUCCESS||values[0]!=total||values[1]!=1u||
       query_pixels[0]!=0xccccccccccccccccull||query_pixels[1]!=total||query_pixels[2]!=1u||query_pixels[3]!=0xccccccccccccccccull){failed_line=__LINE__;goto out;}
    uint32_t narrow[2]={0};if(vkGetQueryPoolResults(device,queries,1u,1u,sizeof(narrow),narrow,8u,VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)!=VK_SUCCESS||narrow[0]!=total||narrow[1]!=1u){failed_line=__LINE__;goto out;}
    if(vkGetQueryPoolResults(device,queries,0u,1u,sizeof(values),values,16u,VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT|VK_QUERY_RESULT_PARTIAL_BIT)!=VK_SUCCESS||values[0]!=0u||values[1]!=0u){failed_line=__LINE__;goto out;}
    if(depth_format==VK_FORMAT_D32_SFLOAT_S8_UINT&&!dynamic_case&&!logic_case&&!attachment_clear){
        if(!vk_subresource_begin(cmd)){failed_line=__LINE__;goto out;}
        VkImageSubresourceRange selected={VK_IMAGE_ASPECT_DEPTH_BIT,0u,1u,1u,1u};VkClearDepthStencilValue value={.375f,91u};
        vkCmdClearDepthStencilImage(cmd,depth,VK_IMAGE_LAYOUT_GENERAL,&value,1u,&selected);
        if(!vk_subresource_complete(queue,cmd)){failed_line=__LINE__;goto out;}
        for(uint32_t i=0u;i<4u*samples;++i){uint32_t n=4u*samples+i;uint8_t expected_stencil=((uint8_t*)depths)[n*8u+4u];
            if(((float*)depths)[n*2u]!=.375f||expected_stencil!=(discard_case?3u:colors[n]==foreground?7u:3u)){failed_line=__LINE__;goto out;}}
        if(!vk_subresource_begin(cmd)){failed_line=__LINE__;goto out;}selected.aspectMask=VK_IMAGE_ASPECT_STENCIL_BIT;value.depth=-1.0f;
        vkCmdClearDepthStencilImage(cmd,depth,VK_IMAGE_LAYOUT_GENERAL,&value,1u,&selected);
        if(!vk_subresource_complete(queue,cmd)){failed_line=__LINE__;goto out;}
        for(uint32_t i=0u;i<4u*samples;++i){uint32_t n=4u*samples+i;if(((float*)depths)[n*2u]!=.375f||((uint8_t*)depths)[n*8u+4u]!=91u){failed_line=__LINE__;goto out;}}
        for(uint32_t i=0u;i<4u*samples*8u;++i)if(((uint8_t*)depths)[i]!=0xccu){failed_line=__LINE__;goto out;}
    }
    ok=1;
out:
    if(!ok){write_hex32("vk_test: MSAA failure source line=",failed_line);write_hex32("vk_test: MSAA failure mode=",test_mode);write_hex32("vk_test: MSAA failure samples=",samples);write_hex32("vk_test: MSAA failure mask=",mask);write_hex32("vk_test: MSAA failure stage=",stage);}
    if(instance_gate)(void)vkSetEvent(device,instance_gate);
    (void)vkQueueWaitIdle(queue);
    if(instance_gate)vkDestroyEvent(device,instance_gate,NULL);if(instance_fence)vkDestroyFence(device,instance_fence,NULL);
    if(instance_buffer)vkDestroyBuffer(device,instance_buffer,NULL);if(instance_memory)vkFreeMemory(device,instance_memory,NULL);
    if(queries)vkDestroyQueryPool(device,queries,NULL);if(qb)vkDestroyBuffer(device,qb,NULL);if(qm)vkFreeMemory(device,qm,NULL);
    if(own_layout)vkDestroyPipelineLayout(device,own_layout,NULL);if(pool)vkDestroyCommandPool(device,pool,NULL);if(pipeline)vkDestroyPipeline(device,pipeline,NULL);
    if(vs)vkDestroyShaderModule(device,vs,NULL);if(fs)vkDestroyShaderModule(device,fs,NULL);
    if(fb)vkDestroyFramebuffer(device,fb,NULL);if(pass)vkDestroyRenderPass(device,pass,NULL);
    if(rv)vkDestroyImageView(device,rv,NULL);if(cv)vkDestroyImageView(device,cv,NULL);if(dv)vkDestroyImageView(device,dv,NULL);
    if(color)vkDestroyImage(device,color,NULL);if(depth)vkDestroyImage(device,depth,NULL);if(result)vkDestroyImage(device,result,NULL);
    if(cm)vkFreeMemory(device,cm,NULL);if(dm)vkFreeMemory(device,dm,NULL);if(rm)vkFreeMemory(device,rm,NULL);
    return ok;
}
static int vk_msaa_smoke(VkDevice device,VkQueue queue,const VkGraphicsPipelineCreateInfo *base){
    for(uint32_t count=2u;count<=8u;count*=2u)for(uint32_t mode=0u;mode<4u;++mode){
        uint32_t mask=mode==1u?0x55u:mode==2u?0u:UINT32_MAX;
        for(uint32_t aspect=0u;aspect<2u;++aspect)
            if(!vk_msaa_case(device,queue,base,count,mask,aspect?VK_FORMAT_S8_UINT:VK_FORMAT_D32_SFLOAT,mode==3u,0))return 0;
    }
    for(uint32_t count=2u;count<=8u;count*=2u)for(uint32_t implicit=0u;implicit<2u;++implicit)
        if(!vk_msaa_case(device,queue,base,count,UINT32_MAX,VK_FORMAT_D32_SFLOAT_S8_UINT,0,implicit))return 0;
    for(uint32_t mode=2u;mode<4u;++mode)if(!vk_msaa_case(device,queue,base,2u,UINT32_MAX,VK_FORMAT_D32_SFLOAT,0,mode))return 0;
    for(uint32_t mode=4u;mode<6u;++mode)if(!vk_msaa_case(device,queue,base,4u,UINT32_MAX,VK_FORMAT_D32_SFLOAT_S8_UINT,0,mode))return 0;
    for(uint32_t mode=6u;mode<24u;++mode)if(!vk_msaa_case(device,queue,base,2u,UINT32_MAX,VK_FORMAT_D32_SFLOAT_S8_UINT,0,mode))return 0;
    for(uint32_t mode=24u;mode<26u;++mode)if(!vk_msaa_case(device,queue,base,4u,UINT32_MAX,VK_FORMAT_D32_SFLOAT,0,mode))return 0;
    if(!vk_msaa_case(device,queue,base,4u,UINT32_MAX,VK_FORMAT_D32_SFLOAT_S8_UINT,0,26))return 0;
    for(uint32_t mode=27u;mode<32u;++mode)if(!vk_msaa_case(device,queue,base,4u,UINT32_MAX,VK_FORMAT_D32_SFLOAT,0,mode))return 0;
    for(uint32_t count=2u;count<=8u;count*=2u)for(uint32_t mode=32u;mode<=34u;++mode)
        if(!vk_msaa_case(device,queue,base,count,UINT32_MAX,VK_FORMAT_D32_SFLOAT,0,mode))return 0;
    write_text("vk_test: FragCoord pixel centers, unequal clip-W reciprocal, interpolated depth and MSAA 2/4/8 pixel/resolve oracles ok\n");
    write_text("vk_test: real FrontFacing bool follows both pipeline orientations through MSAA shader pixels ok\n");
    write_text("vk_test: multi-instance firstInstance/InstanceIndex, instance-rate and zero-stride fetch, event-gated asynchronous backing lifetime and actual query counts ok\n");
    write_text("vk_test: external subpass dependencies, unit render granularity, deferred partial attachment clears and ignored depth for stencil-only clear ok\n");
    write_text("vk_test: asymmetric RGB barycentric oracle and unequal clip-W perspective gradients match each sample and resolve ok\n");
    write_text("vk_test: immutable dynamic blend/stencil/depth-bias state, D32 bias and depth-fail stencil interaction ok\n");
    write_text("vk_test: all 16 logical color operations use independent bit truth-table oracle, MSAA samples and resolve ok\n");
    write_text("vk_test: combined depth/stencil aspect clears preserve other component and neighboring layer ok\n");
    write_text("vk_test: rectangular matrix/transpose/outer-product operations, nested SPIR-V functions, mutable local struct/array pointers, composites, vector indexing and recursion/type rejection ok\n");
    write_text("vk_test: fragment discard preserves color/depth/stencil and contributes zero occlusion samples ok\n");
    write_text("vk_test: precise occlusion counts actual passed samples; host/copy 32/64-bit availability, partial and neighbor guards ok\n");
    write_text("vk_test: real shader reflection cache serialization/import/merge, multi-create and post-cache-destruction draws ok\n");
    write_text("vk_test: std430 push array at byte 112, high SPIR-V IDs and immutable specialization/default values ok\n");
    write_text("vk_test: combined D32/S8 per-sample tests and attachment-indexed render-pass resolves ok\n");
    write_text("vk_test: MSAA 2/4/8 coverage, sample masks, alpha coverage, per-sample D32/S8 and resolves preserve neighbor layers ok\n");
    return 1;
}

