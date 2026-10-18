/* Mandatory ranges are native Process/open-description resources, distinct
 * from advisory POSIX ranges. No Windows structures or handles enter here.
 * Lock order: memory transaction -> file data -> range inventory -> FD
 * inventory. Close detaches its FD before taking the range inventory lock.
 * Neither a memory transaction nor a file data gate survives a wait. */
static int description_owned(uint32_t owner,ladybird_fd_object_t *object) {
    uint64_t flags=fd_inventory_lock();
    ladybird_process_fd_table_t *table=ladybird_fd_find_table(owner);
    int found=0;
    if(table)for(unsigned i=0;i<LADYBIRD_MAX_FDS;i++)
        if(table->slots[i].object==object){found=1;break;}
    fd_inventory_unlock(flags);return found;
}
static void description_range_closed(uint32_t owner,ladybird_fd_object_t *object,int all) {
    if(!object || object->ops!=&file_ops || !object->ctx)return;
    uint64_t flags=ladybird_fcntl_lock_acquire();
    if(!all && description_owned(owner,object)){ladybird_fcntl_lock_release(flags);return;}
    for(unsigned i=0;i<LADYBIRD_FCNTL_LOCK_MAX;i++) {
        ladybird_fcntl_lock_t *held=&ladybird_fcntl_locks[i];
        if(held->used && held->mandatory && held->description==object->ctx && (all || held->pid==owner)) {
            --mandatory_lock_count;memset(held,0,sizeof(*held));
        }
    }
    ladybird_fcntl_wake_path_locked("");ladybird_fcntl_lock_release(flags);
}
static int description_range_access(ladybird_file_ctx_t *file,uint64_t start,size_t size,int writing) {
    if(!size)return 0;
    if(size>UINT64_MAX-start)return -EINVAL;
    uint32_t owner=ladybird_fd_owner_pid();int denied=0;
    uint64_t flags=ladybird_fcntl_lock_acquire();
    if(mandatory_lock_count)for(unsigned i=0;i<LADYBIRD_FCNTL_LOCK_MAX;i++) {
        ladybird_fcntl_lock_t *h=&ladybird_fcntl_locks[i];
        if(!h->used || !h->mandatory || h->start==h->end || h->identity!=file->identity ||
           !ladybird_fcntl_ranges_overlap(h->start,h->end,0,start,start+size,0))continue;
        if(h->type==LADYBIRD_F_RDLCK ? writing : (h->pid!=owner || h->description!=file)) {denied=1;break;}
    }
    ladybird_fcntl_lock_release(flags);return denied;
}
void vex_file_range_thread_stopped(uint32_t tid) {
    uint64_t flags=ladybird_fcntl_lock_acquire();
    for(unsigned i=0;i<LADYBIRD_FCNTL_WAITER_MAX;i++)
        if(ladybird_fcntl_waiters[i].used && ladybird_fcntl_waiters[i].tid==tid)
            memset(&ladybird_fcntl_waiters[i],0,sizeof(ladybird_fcntl_waiters[i]));
    ladybird_fcntl_lock_release(flags);
}
int vex_fd_description_range(int fd,unsigned mode,uint64_t start,uint64_t size,int wait) {
    if(mode>2 || start>INT64_MAX || size>INT64_MAX-start)return -EINVAL;
    VEX_FD_BORROW(ref);ladybird_fd_object_t *object=vex_fd_borrow_acquire(&ref,fd);
    if(!object || object->ops!=&file_ops || !object->ctx)return -EBADF;
    ladybird_file_ctx_t *file=object->ctx;if(!file->identity)return -ENOTSUP;
    uint32_t owner=ladybird_fd_owner_pid(),tid=scheduler_current_tid();
    ladybird_fcntl_waiter_t *waiter=NULL;
    for(;;) {
        int result=0,conflict=0,slot=-1;
        {
            kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
            uint64_t flags=ladybird_fcntl_lock_acquire();
            if(!description_owned(owner,object))result=-EBADF;
            else if(!mode) {
                for(unsigned i=0;i<LADYBIRD_FCNTL_LOCK_MAX;i++) {
                    ladybird_fcntl_lock_t *h=&ladybird_fcntl_locks[i];
                    if(h->used && h->mandatory && h->pid==owner && h->description==file && h->start==start && h->end==start+size)
                        if(slot<0 || h->type==LADYBIRD_F_WRLCK)slot=(int)i;
                }
                if(slot<0)result=-ENOENT;
                else {memset(&ladybird_fcntl_locks[slot],0,sizeof(ladybird_fcntl_locks[slot]));--mandatory_lock_count;ladybird_fcntl_wake_path_locked("");}
            } else {
                for(unsigned i=0;i<LADYBIRD_FCNTL_LOCK_MAX;i++) {
                    ladybird_fcntl_lock_t *h=&ladybird_fcntl_locks[i];
                    if(!h->used){if(slot<0)slot=(int)i;continue;}
                    if(!h->mandatory || h->identity!=file->identity || !size || h->start==h->end ||
                       !ladybird_fcntl_ranges_overlap(h->start,h->end,0,start,start+size,0))continue;
                    if(mode==2 || (h->type==LADYBIRD_F_WRLCK && (h->pid!=owner || h->description!=file)))conflict=1;
                }
                if(!conflict) {
                    if(slot<0)result=-ENOLCK;
                    else {
                        ladybird_fcntl_lock_t *h=&ladybird_fcntl_locks[slot];memset(h,0,sizeof(*h));
                        h->used=h->mandatory=1;h->type=mode==2?LADYBIRD_F_WRLCK:LADYBIRD_F_RDLCK;
                        h->pid=owner;h->description=file;h->identity=file->identity;h->start=start;h->end=start+size;++mandatory_lock_count;
                    }
                } else if(!wait)result=-EAGAIN;
                else {
                    if(!waiter)for(unsigned i=0;i<LADYBIRD_FCNTL_WAITER_MAX;i++)if(!ladybird_fcntl_waiters[i].used) {
                        waiter=&ladybird_fcntl_waiters[i];memset(waiter,0,sizeof(*waiter));
                        waiter->used=waiter->mandatory=1;waiter->pid=owner;waiter->tid=tid;break;
                    }
                    if(!waiter)result=-ENOLCK;
                    else __atomic_store_n(&waiter->woken,0,__ATOMIC_RELEASE);
                }
            }
            if((!conflict || result) && waiter){memset(waiter,0,sizeof(*waiter));waiter=NULL;}
            ladybird_fcntl_lock_release(flags);
        }
        if(result || !conflict)return result;
        process_wait_event(&waiter->woken);
    }
}
