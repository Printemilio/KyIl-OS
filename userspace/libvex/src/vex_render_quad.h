/* Software fragment helpers execute actual neighboring shader invocations.
 * Continuations are the same interpreter states used by compute, not a second
 * shader engine. Uncovered helper lanes never mutate framebuffer/depth/stencil. */
/* Fragment programs with a native implementation. */
enum { VEX_FAST_NONE = 0, VEX_FAST_SOLID_VARYING, VEX_FAST_TEXTURE, VEX_FAST_ATLAS_COVERAGE };
static int vex_render_fast_paths_enabled = 1;

/* The four lanes of a 2x2 quad, evaluated like the interpreter does: the texture
 * footprint comes from the neighbouring lanes' coordinates (GL's LOD rule with the
 * program's bias) and selects the magnification or minification filter; the
 * sample itself is the same vk_spv_sample_plane(). */
static int vex_fast_quad(const vk_recorded_command_t *record,const vk_spv_profile_t *profile,
        const uint32_t values[4][VK_SPV_MAX_INTERFACE_LOCATIONS][4],uint32_t colors[4][4]){
    vk_image_view_impl_t *view;vk_sampler_impl_t *sampler;VkImageLayout layout;
    const unsigned coordinate=profile->fast_location[0];
    if(!vk_spv_get_combined_image(record->args.draw.descriptor_sets,0u,profile->fast_binding,0u,&view,&sampler,&layout))return 0;
    for(unsigned lane=0;lane<4;++lane){
        const unsigned left=lane&2u,right=left+1u,bottom=lane&1u,top=bottom+2u;
        float dx[2],dy[2];
        for(unsigned axis=0;axis<2;++axis){
            const float scale=(float)(axis?view->image->height:view->image->width);
            dx[axis]=(vk_spv_word_float(values[right][coordinate][axis])-vk_spv_word_float(values[left][coordinate][axis]))*scale;
            dy[axis]=(vk_spv_word_float(values[top][coordinate][axis])-vk_spv_word_float(values[bottom][coordinate][axis]))*scale;
        }
        const float dx2=dx[0]*dx[0]+dx[1]*dx[1],dy2=dy[0]*dy[0]+dy[1]*dy[1];
#ifdef VEX_RENDER_GL_LOD_MAGNIFICATION
        const int magnify=VEX_RENDER_GL_LOD_MAGNIFICATION(dx2>dy2?dx2:dy2,profile->fast_bias);
        if(magnify<0)return 0;
#else
        (void)dx2;(void)dy2;return 0;   /* only the hosted GL frontend has fast programs */
        const int magnify=0;
#endif
        vk_sampler_impl_t selected=*sampler;
        selected.min_filter=selected.mag_filter=magnify?sampler->mag_filter:sampler->min_filter;
        uint32_t texel[4];
        if(!vk_spv_sample_plane(view,&selected,layout,0,values[lane][coordinate],texel))return 0;
        if(profile->fast_kind==VEX_FAST_ATLAS_COVERAGE){
            const float coverage=vk_spv_word_float(texel[3]);
            for(unsigned c=0;c<4;++c)
                colors[lane][c]=vk_spv_float_word(vk_spv_word_float(values[lane][profile->fast_location[1]][c])*coverage);
        }else memcpy(colors[lane],texel,16u);
    }
    return 1;
}

static int vex_spv_run_quad(const vk_recorded_command_t *record,vk_pipeline_impl_t *pipeline,
                           int32_t x,int32_t y,int front,const float center[4],
                           const uint32_t inputs[VK_SPV_MAX_INTERFACE_LOCATIONS][4],uint32_t output[4]){
    const vk_spv_profile_t *profile=pipeline->fragment_profile;
    vk_spv_value_t *workspace=pipeline->quad_workspace_override?pipeline->quad_workspace_override:profile->quad_workspace;
    if(!workspace)return 0;
    const int32_t quad_x=x&~1,quad_y=y&~1;
    unsigned selected=((unsigned)y&1u)*2u+((unsigned)x&1u);
    if(pipeline->quad_cache_valid&&pipeline->quad_cache_x==quad_x&&
       pipeline->quad_cache_y==quad_y){
        if(pipeline->quad_cache_discard[selected])return 2;
        memcpy(output,pipeline->quad_cache_color[selected],16u);
        return 1;
    }
    int state[4]={0};uint32_t colors[4][4]={{0}};
    uint32_t values[4][VK_SPV_MAX_INTERFACE_LOCATIONS][4];
    for(unsigned lane=0;lane<4;++lane){
        if(pipeline->quad_inputs)memcpy(values[lane],pipeline->quad_inputs[lane],sizeof(values[lane]));
        else if(inputs)memcpy(values[lane],inputs,sizeof(values[lane]));
        else memset(values[lane],0,sizeof(values[lane]));
    }
    if(profile->fast_kind>=VEX_FAST_TEXTURE&&vex_render_fast_paths_enabled){
        if(!vex_fast_quad(record,profile,values,colors))return 0;
        pipeline->quad_cache_valid=1u;
        pipeline->quad_cache_x=quad_x;pipeline->quad_cache_y=quad_y;
        for(unsigned lane=0;lane<4u;++lane){
            pipeline->quad_cache_discard[lane]=0u;
            memcpy(pipeline->quad_cache_color[lane],colors[lane],16u);
        }
        memcpy(output,colors[selected],16u);return 1;
    }
    vk_spv_invocation_t lanes[4]={{0}};float coords[4][4];
    for(unsigned lane=0;lane<4;++lane){
        lanes[lane].values=workspace+(size_t)lane*profile->bound;lanes[lane].graphics_quad=1;
        if(pipeline->quad_coords)memcpy(coords[lane],pipeline->quad_coords[lane],sizeof(coords[lane]));
        else {memcpy(coords[lane],center,sizeof(coords[lane]));coords[lane][0]=(float)(x&~1)+(lane&1u)+.5f;coords[lane][1]=(float)(y&~1)+(lane>>1u)+.5f;}
    }
    for(unsigned epoch=0;epoch<VK_SPV_MAX_EXECUTION_INSTRUCTIONS;++epoch){
        unsigned remaining=0;
        for(unsigned lane=0;lane<4;++lane)if(state[lane]!=1&&state[lane]!=2){
            state[lane]=vk_spv_run(pipeline->fragment_shader,profile,VK_SHADER_STAGE_FRAGMENT_BIT,0,0,front,NULL,
                record->args.draw.descriptor_sets,record->args.draw.push_constants[1],values[lane],coords[lane],colors[lane],NULL,&lanes[lane]);
            if(state[lane]!=1&&state[lane]!=2&&state[lane]!=4)return 0;
            if(state[lane]==4)++remaining;
        }
        if(!remaining){
            pipeline->quad_cache_valid=1u;
            pipeline->quad_cache_x=quad_x;pipeline->quad_cache_y=quad_y;
            for(unsigned lane=0;lane<4u;++lane){
                pipeline->quad_cache_discard[lane]=(uint8_t)(state[lane]==2);
                if(state[lane]==1)memcpy(pipeline->quad_cache_color[lane],colors[lane],16u);
            }
            if(state[selected]==2)return 2;
            memcpy(output,colors[selected],16u);return 1;
        }
        for(unsigned lane=0;lane<4;++lane)if(state[lane]==4){
            const uint32_t *w=pipeline->fragment_shader->code+lanes[lane].pc;uint32_t op=w[0]&65535u;
            uint32_t type=w[1],result=w[2];
            if(type>=profile->bound||result>=profile->bound)return 0;
            unsigned left=lane&2u,right=left+1u,bottom=lane&1u,top=bottom+2u;
            /* Derivatives in nonuniform control flow are undefined by GL.
             * Refuse that case rather than fabricate coherent neighbors. */
            int coherent=1;for(unsigned other=0;other<4;++other)
                if(state[other]!=4||lanes[other].pc!=lanes[lane].pc)coherent=0;
            if(!coherent)return 0;
            if(op==87u){
                uint32_t sampled=w[3],coordinate=w[4];
                if(sampled>=profile->bound||coordinate>=profile->bound||!lanes[lane].values[sampled].initialized||!lanes[lane].values[coordinate].initialized)return 0;
                uint32_t coordinate_type=lanes[lane].values[coordinate].type;
                unsigned wc=w[0]>>16;float bias=0;
                if(wc==7u){
                    uint32_t id=w[6];
                    if(w[5]!=1u||id>=profile->bound||!lanes[lane].values[id].initialized)return 0;
                    uint32_t bias_type=lanes[lane].values[id].type;
                    if(bias_type>=profile->bound||profile->kind[bias_type]!=VK_SPV_KIND_FLOAT||profile->width[bias_type]!=32u)return 0;
                    bias=vk_spv_word_float(lanes[lane].values[id].word[0]);
                }
                if((wc!=5u&&wc!=7u)||profile->kind[type]!=VK_SPV_KIND_VECTOR||profile->count[type]!=4u||
                    profile->element[type]>=profile->bound||profile->kind[profile->element[type]]!=VK_SPV_KIND_FLOAT||profile->width[profile->element[type]]!=32u||
                    coordinate_type>=profile->bound||profile->kind[coordinate_type]!=VK_SPV_KIND_VECTOR||profile->count[coordinate_type]!=2u||
                    profile->element[coordinate_type]>=profile->bound||profile->kind[profile->element[coordinate_type]]!=VK_SPV_KIND_FLOAT||profile->width[profile->element[coordinate_type]]!=32u)return 0;
                vk_spv_value_t *image=&lanes[lane].values[sampled];
                vk_image_view_impl_t *view;vk_sampler_impl_t *sampler;VkImageLayout layout;
                if(image->descriptor_pointer!=2u||!vk_spv_get_combined_image(record->args.draw.descriptor_sets,image->descriptor_set,image->descriptor_binding,image->descriptor_index,&view,&sampler,&layout)||view->range.levelCount!=1)return 0;
                float dx[2],dy[2];
                for(unsigned axis=0;axis<2;++axis){
                    for(unsigned other=0;other<4;++other)if(!lanes[other].values[coordinate].initialized||lanes[other].values[coordinate].type!=coordinate_type)return 0;
                    float scale=(float)(axis?view->image->height:view->image->width);
                    dx[axis]=(vk_spv_word_float(lanes[right].values[coordinate].word[axis])-vk_spv_word_float(lanes[left].values[coordinate].word[axis]))*scale;
                    dy[axis]=(vk_spv_word_float(lanes[top].values[coordinate].word[axis])-vk_spv_word_float(lanes[bottom].values[coordinate].word[axis]))*scale;
                }
                float dx2=dx[0]*dx[0]+dx[1]*dx[1],dy2=dy[0]*dy[0]+dy[1]*dy[1];
                vk_sampler_impl_t selected_sampler=*sampler;
#ifdef VEX_RENDER_GL_LOD_MAGNIFICATION
                int magnify=VEX_RENDER_GL_LOD_MAGNIFICATION(dx2>dy2?dx2:dy2,bias);
                if(magnify<0)return 0;
#else
                if(wc!=5u)return 0;
                int magnify=dx2<=1.f&&dy2<=1.f;
#endif
                selected_sampler.min_filter=selected_sampler.mag_filter=magnify?sampler->mag_filter:sampler->min_filter;
                if(!vk_spv_sample_plane(view,&selected_sampler,layout,0,lanes[lane].values[coordinate].word,lanes[lane].values[result].word))return 0;
            }else if(op>=207u&&op<=209u){
                uint32_t operand=w[3],scalar,count;
                if(operand>=profile->bound||!vk_spv_scalar_and_components(profile,type,&scalar,&count)||profile->kind[scalar]!=VK_SPV_KIND_FLOAT)return 0;
                for(unsigned other=0;other<4;++other)if(!lanes[other].values[operand].initialized||lanes[other].values[operand].type!=type)return 0;
                for(unsigned component=0;component<count;++component){
                    float dx=vk_spv_word_float(lanes[right].values[operand].word[component])-vk_spv_word_float(lanes[left].values[operand].word[component]);
                    float dy=vk_spv_word_float(lanes[top].values[operand].word[component])-vk_spv_word_float(lanes[bottom].values[operand].word[component]);
                    if(profile->gl_top_left_height)dy=-dy;
                    float value=op==207u?dx:op==208u?dy:(dx<0?-dx:dx)+(dy<0?-dy:dy);
                    lanes[lane].values[result].word[component]=vk_spv_float_word(value);
                }
            }else return 0;
            lanes[lane].values[result].type=type;lanes[lane].values[result].initialized=1;
        }
        for(unsigned lane=0;lane<4;++lane)if(state[lane]==4)lanes[lane].pc+=pipeline->fragment_shader->code[lanes[lane].pc]>>16;
    }
    return 0;
}
