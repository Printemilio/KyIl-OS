#include <unos/usercopy.h>
#include <unos/process_memory.h>
#include <unos/vex_syscall.h>
#include <unos/scheduler.h>
#include <unos/heap.h>
#include <string.h>
static kernel_gate_t scratch_gate=KERNEL_GATE_INIT;
void *vex_scratch_allocate(vex_scratch_t *ref,size_t length) {
    if(!ref || ref->data)return NULL;
    void *data=kmalloc(length?length:1u);
    if(!data)return NULL;
    KERNEL_GUARD(scratch_gate);
    process_t *p=scheduler_current_process();
    ref->data=data;ref->owner=p;
    if(p){ref->next=p->scratch_buffers;if(ref->next)ref->next->previous=ref;p->scratch_buffers=ref;}
    return data;
}
void vex_scratch_release(vex_scratch_t *ref) {
    if(!ref)return;
    void *data;
    {KERNEL_GUARD(scratch_gate);data=ref->data;
     if(data){if(ref->previous)ref->previous->next=ref->next;
       else if(ref->owner)ref->owner->scratch_buffers=ref->next;
       if(ref->next)ref->next->previous=ref->previous;
       *ref=(vex_scratch_t){0};}}
    kfree(data);
}
void *vex_scratch_exchange(vex_scratch_t *ref,void *replacement){
    if(!ref || !ref->data)return NULL;
    KERNEL_GUARD(scratch_gate);void *old=ref->data;
    if(replacement)ref->data=replacement;
    else{
        if(ref->previous)ref->previous->next=ref->next;else if(ref->owner)ref->owner->scratch_buffers=ref->next;
        if(ref->next)ref->next->previous=ref->previous;*ref=(vex_scratch_t){0};
    }return old;
}
void vex_scratch_thread_stopped(struct process *thread) {
    while(thread && thread->scratch_buffers)vex_scratch_release(thread->scratch_buffers);
}
int vex_copy_from_user(void *destination,uint64_t source,size_t length) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    if(!vex_syscall_user_range_valid(source,length,0))return -1;
    if(length)memcpy(destination,(const void *)(uintptr_t)source,length);
    return 0;
}
int vex_copy_to_user(uint64_t destination,const void *source,size_t length) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    if(!vex_syscall_user_range_valid(destination,length,1))return -1;
    if(length)memcpy((void *)(uintptr_t)destination,source,length);
    return 0;
}
int vex_user_string_copy(char *destination,size_t capacity,uint64_t source) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    if(!destination || !capacity)return -1;
    size_t copied=0;
    while(copied<capacity) {
        if(source>UINT64_MAX-copied)return -1;
        uint64_t address=source+copied;
        size_t chunk=4096u-(address&4095u);
        if(chunk>capacity-copied)chunk=capacity-copied;
        if(!vex_syscall_user_range_valid(address,chunk,0))return -1;
        const char *bytes=(const char *)(uintptr_t)address;
        const char *end=NULL;
        for(size_t i=0;i<chunk;++i)if(!bytes[i]){end=bytes+i;break;}
        size_t n=end?(size_t)(end-bytes)+1u:chunk;
        memcpy(destination+copied,bytes,n);copied+=n;
        /* User bytes can change during a copy; our bounded snapshot still
           needs its own terminator at the position that was observed. */
        if(end){destination[copied-1u]=0;return 0;}
    }
    return -1;
}
