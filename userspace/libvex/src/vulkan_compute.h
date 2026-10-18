/* Compute shares pipelines/descriptors, immutable records and the existing
   asynchronous queue worker. Workgroups use resumable interpreter lanes. */
static int vk_compute_descriptors(vk_pipeline_impl_t *pipeline,vk_descriptor_set_impl_t *const sets[8]){
    return pipeline&&vk_spv_profile_bound_set_supported(pipeline->compute_profile,(vk_pipeline_impl_t *)pipeline,sets)&&vk_spv_resources_supported(pipeline->compute_profile,sets);
}

VkResult vkCreateComputePipelines(VkDevice device,VkPipelineCache cache,uint32_t count,
        const VkComputePipelineCreateInfo *infos,const VkAllocationCallbacks *allocator,VkPipeline *out){
    vk_device_impl_t *d=(void *)(uintptr_t)device;
    vk_pipeline_cache_impl_t *c=(void *)(uintptr_t)cache;
    if(!d||!count||!infos||!out||(c&&c->device!=d))return VK_ERROR_INITIALIZATION_FAILED;
    for(uint32_t i=0u;i<count;++i)out[i]=VK_NULL_HANDLE;
    if(allocator)return VK_ERROR_FEATURE_NOT_PRESENT;
    VkResult aggregate=VK_SUCCESS;
    for(uint32_t i=0u;i<count;++i){
        const VkComputePipelineCreateInfo *ci=infos+i;
        const VkPipelineShaderStageCreateInfo *stage=&ci->stage;
        vk_pipeline_layout_impl_t *layout=(void *)(uintptr_t)ci->layout;
        VkShaderModule module=VK_NULL_HANDLE;
        vk_pipeline_impl_t *p=0;
        VkResult result=VK_ERROR_FEATURE_NOT_PRESENT;
        if(ci->sType!=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO||ci->pNext||
           (ci->flags&~VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT)||
           stage->sType!=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO||stage->pNext||stage->flags||
           stage->stage!=VK_SHADER_STAGE_COMPUTE_BIT||!string_equals_local(stage->pName,"main")||
           !layout||layout->device!=d||layout->destroy_requested)goto next;
        result=vk_specialize_module(device,stage->module,stage->pSpecializationInfo,&module);
        if(result!=VK_SUCCESS)goto next;
        p=vk_heap_alloc(sizeof(*p));if(!p){result=VK_ERROR_OUT_OF_HOST_MEMORY;goto next;}
        memset(p,0,sizeof(*p));p->references=1u;p->device=d;p->bind_point=VK_PIPELINE_BIND_POINT_COMPUTE;
        p->compute_profile=vk_heap_alloc(sizeof(*p->compute_profile));
        if(!p->compute_profile){result=VK_ERROR_OUT_OF_HOST_MEMORY;goto next;}
        if(!vk_cached_profile(c,(void *)(uintptr_t)module,VK_SHADER_STAGE_COMPUTE_BIT,p->compute_profile)||
           !vk_spv_profile_layout_supported(p->compute_profile,layout,VK_SHADER_STAGE_COMPUTE_BIT)){
            result=VK_ERROR_FEATURE_NOT_PRESENT;goto next;
        }
        if(!vk_ref_retain_count(&layout->references)){
            result=VK_ERROR_OUT_OF_HOST_MEMORY;goto next;
        }
        p->layout=layout;
        if(!vk_shader_module_retain((void *)(uintptr_t)module)){result=VK_ERROR_OUT_OF_HOST_MEMORY;goto next;}
        p->compute_shader=(void *)(uintptr_t)module;
        out[i]=(VkPipeline)(uintptr_t)p;p=0;result=VK_SUCCESS;
    next:
        if(p)vk_pipeline_release(p);
        if(module)vkDestroyShaderModule(device,module,0);
        if(result!=VK_SUCCESS)aggregate=result;
    }
    return aggregate;
}

static void vk_record_dispatch(vk_command_buffer_impl_t *cmd,const uint32_t groups[3],
        vk_buffer_impl_t *indirect,VkDeviceSize offset){
    if(!cmd||!cmd->recording)return;
    vk_pipeline_impl_t *p=cmd->compute_pipeline;
    if(cmd->active_render_pass||!p||!p->compute_profile||!vk_compute_descriptors(p,cmd->compute_descriptor_sets)||
       !vk_spv_profile_push_initialized(p->compute_profile,VK_SHADER_STAGE_COMPUTE_BIT,cmd->push_constant_initialized)){
        vk_record_unsupported(cmd);return;
    }
    if(indirect){
        if(indirect->device!=cmd->device||indirect->destroy_requested||!indirect->memory||
           !(indirect->usage&VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)||(offset&3u)||offset>indirect->size||12u>indirect->size-offset){
            vk_record_unsupported(cmd);return;
        }
    }else for(uint32_t axis=0u;axis<3u;++axis)if(groups[axis]>65535u){vk_record_unsupported(cmd);return;}
    vk_recorded_command_t *r=vk_append_record(cmd,VK_RECORD_DISPATCH);if(!r)return;
    if(!vk_pipeline_retain(p)){cmd->record_error=VK_ERROR_OUT_OF_HOST_MEMORY;return;}r->args.dispatch.pipeline=p;
    if(indirect){if(!vk_buffer_retain(indirect)){cmd->record_error=VK_ERROR_OUT_OF_HOST_MEMORY;return;}
        r->args.dispatch.indirect=indirect;r->args.dispatch.offset=offset;
    }else memcpy(r->args.dispatch.groups,groups,12u);
    memcpy(r->args.dispatch.push,cmd->push_constants[2],128u);
    for(uint32_t i=0u;i<8u;++i)if(cmd->compute_descriptor_sets[i]){
        if(!vk_ref_retain_count(&cmd->compute_descriptor_sets[i]->references)){cmd->record_error=VK_ERROR_OUT_OF_HOST_MEMORY;return;}
        r->args.dispatch.sets[i]=cmd->compute_descriptor_sets[i];
    }
}
void vkCmdDispatch(VkCommandBuffer cb,uint32_t x,uint32_t y,uint32_t z){
    uint32_t groups[3]={x,y,z};vk_record_dispatch((void *)(uintptr_t)cb,groups,0,0u);
}
void vkCmdDispatchIndirect(VkCommandBuffer cb,VkBuffer buffer,VkDeviceSize offset){
    vk_record_dispatch((void *)(uintptr_t)cb,0,(void *)(uintptr_t)buffer,offset);
}

static VkResult vk_execute_dispatch(const vk_recorded_command_t *record){
    vk_pipeline_impl_t *pipeline=record->args.dispatch.pipeline;
    if(!pipeline||!pipeline->compute_profile||!vk_compute_descriptors(pipeline,record->args.dispatch.sets))return VK_ERROR_DEVICE_LOST;
    const vk_spv_profile_t *p=pipeline->compute_profile;
    uint32_t groups[3];memcpy(groups,record->args.dispatch.groups,12u);
    if(record->args.dispatch.indirect){
        vk_buffer_impl_t *b=record->args.dispatch.indirect;
        if(!b->memory||!b->memory->bytes||b->memory_offset>b->memory->size||b->size>b->memory->size-b->memory_offset)return VK_ERROR_DEVICE_LOST;
        memcpy(groups,b->memory->bytes+(size_t)(b->memory_offset+record->args.dispatch.offset),12u);
    }
    for(uint32_t a=0u;a<3u;++a){if(groups[a]>65535u)return VK_ERROR_DEVICE_LOST;if(!groups[a])return VK_SUCCESS;}
    uint32_t lanes=p->local_size[0]*p->local_size[1]*p->local_size[2];
    if(!lanes||lanes>128u||!p->bound||p->bound>VK_SPV_MAX_IDS)return VK_ERROR_DEVICE_LOST;
    size_t per_lane=p->bound*sizeof(vk_spv_value_t);
    if(per_lane>SIZE_MAX/lanes)return VK_ERROR_OUT_OF_HOST_MEMORY;
    vk_spv_value_t *values=vk_heap_alloc(per_lane*lanes);
    vk_spv_invocation_t *states=vk_heap_alloc(lanes*sizeof(*states));
    uint8_t *shared=p->workgroup_size?vk_heap_alloc(p->workgroup_size):0;
    VkResult result=VK_ERROR_OUT_OF_HOST_MEMORY;
    if(!values||!states||(p->workgroup_size&&!shared))goto done;
    for(uint32_t z=0u;z<groups[2];++z)for(uint32_t y=0u;y<groups[1];++y)for(uint32_t x=0u;x<groups[0];++x){
        memset(states,0,lanes*sizeof(*states));
        if(shared)memset(shared,0,p->workgroup_size); /* undefined initial bytes: deterministic choice */
        for(uint32_t lane=0u;lane<lanes;++lane){vk_spv_invocation_t *s=states+lane;
            s->values=(vk_spv_value_t *)((uint8_t *)values+lane*per_lane);s->shared=shared;
            memcpy(s->groups,groups,12u);s->group[0]=x;s->group[1]=y;s->group[2]=z;
            s->local[0]=lane%p->local_size[0];s->local[1]=(lane/p->local_size[0])%p->local_size[1];s->local[2]=lane/(p->local_size[0]*p->local_size[1]);
            for(uint32_t a=0u;a<3u;++a)s->global[a]=s->group[a]*p->local_size[a]+s->local[a];
        }
        for(;;){uint32_t finished=0u,waiting=0u,first_wait=UINT32_MAX;
            for(uint32_t lane=0u;lane<lanes;++lane){vk_spv_invocation_t *s=states+lane;uint32_t dummy[4];
                int status=vk_spv_run(pipeline->compute_shader,p,VK_SHADER_STAGE_COMPUTE_BIT,0u,0u,0,0,
                    record->args.dispatch.sets,record->args.dispatch.push,0,0,dummy,0,s);
                if(status==1)++finished;
                else if(status==3){++waiting;if(first_wait==UINT32_MAX)first_wait=lane;}
                else{result=VK_ERROR_DEVICE_LOST;goto done;}
            }
            if(finished==lanes)break;
            /* Every lane must reach the same dynamic barrier instance. Reject
               divergence instead of executing past it or polling forever. */
            if(waiting!=lanes){result=VK_ERROR_DEVICE_LOST;goto done;}
            for(uint32_t lane=0u;lane<lanes;++lane){const vk_spv_invocation_t *a=states+first_wait,*b=states+lane;
                if(a->barrier!=b->barrier||a->call_depth!=b->call_depth){result=VK_ERROR_DEVICE_LOST;goto done;}
                for(uint32_t c=0u;c<a->call_depth;++c)if(a->calls[c].offset!=b->calls[c].offset){result=VK_ERROR_DEVICE_LOST;goto done;}
            }
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
        }
    }
    __atomic_thread_fence(__ATOMIC_SEQ_CST);result=VK_SUCCESS;
done:
    vk_heap_free(shared);vk_heap_free(states);vk_heap_free(values);return result;
}
