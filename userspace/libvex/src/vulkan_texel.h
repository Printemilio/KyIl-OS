/* Private buffer views: the view owns a reference to the actual Vex backing
   through its buffer. Descriptors/immutable submissions retain the view. */
static void *vk_heap_alloc(size_t size);



static int vk_buffer_view_retain(vk_buffer_view_impl_t *view){
    return view&&vk_ref_retain_count(&view->references);
}
static void vk_buffer_view_release(vk_buffer_view_impl_t *view){
    if(view&&vk_ref_release_count(&view->references)){
        vk_buffer_release(view->buffer);vk_heap_free(view);
    }
}
VkResult vkCreateBufferView(VkDevice device,const VkBufferViewCreateInfo *info,
        const VkAllocationCallbacks *allocator,VkBufferView *out){
    vk_device_impl_t *d=(void *)(uintptr_t)device;
    if(!out)return VK_ERROR_INITIALIZATION_FAILED;*out=VK_NULL_HANDLE;
    if(!d||!info||info->sType!=VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO)return VK_ERROR_INITIALIZATION_FAILED;
    if(allocator||info->pNext||info->flags||!vk_texel_format(info->format,0,0))return VK_ERROR_FEATURE_NOT_PRESENT;
    vk_buffer_impl_t *b=(void *)(uintptr_t)info->buffer;
    if(!b||b->device!=d||b->destroy_requested||!b->memory||
       !(b->usage&(VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT))||
       (info->offset&3u)||info->offset>=b->size)return VK_ERROR_INITIALIZATION_FAILED;
    VkDeviceSize range=info->range==VK_WHOLE_SIZE?b->size-info->offset:info->range;
    if(!range||(range&3u)||range>b->size-info->offset||range/4u>65536u)return VK_ERROR_INITIALIZATION_FAILED;
    vk_buffer_view_impl_t *view=vk_heap_alloc(sizeof(*view));
    if(!view)return VK_ERROR_OUT_OF_HOST_MEMORY;
    if(!vk_buffer_retain(b)){vk_heap_free(view);return VK_ERROR_OUT_OF_HOST_MEMORY;}
    memset(view,0,sizeof(*view));view->device=d;view->buffer=b;view->offset=info->offset;
    view->range=range;view->format=info->format;view->references=1u;
    *out=(VkBufferView)(uintptr_t)view;return VK_SUCCESS;
}
void vkDestroyBufferView(VkDevice device,VkBufferView handle,const VkAllocationCallbacks *allocator){
    (void)allocator;vk_buffer_view_impl_t *view=(void *)(uintptr_t)handle;
    if(!view||view->device!=(void *)(uintptr_t)device||view->destroy_requested)return;
    view->destroy_requested=1u;vk_buffer_view_release(view);
}
