/* Generic, capability-scoped ephemeral userspace services. Included in the
 * executable-resource launcher to share its transactional construction path. */
typedef struct ultra_domain {struct ultra_domain *next;fs_root_t *scope;ora_view_t image;
    process_backing *process;vex_web_fd_object_t *ingress,*grants[GRANTS_MAX];
    vex_process_grant_t slots[GRANTS_MAX];unsigned count,clients;
    uint64_t identity,created,opens;} ultra_domain;
static ultra_domain *ultra_domains;static uint64_t ultra_next;
static kernel_gate_t ultra_gate=KERNEL_GATE_INIT;
static void ultra_destroy(ultra_domain *d){
    if(d->process){
        (void)scheduler_resource_terminate(d->process->observer.info.pid,0);backing_release(d->process);}
    if(d->ingress)ladybird_fd_release(d->ingress);
    for(unsigned i=0;i<d->count;i++)if(d->grants[i])ladybird_fd_release(d->grants[i]);
    fs_root_release(d->scope);ora_view_release(&d->image);kfree(d);
}
static void ultra_release(void *opaque){ultra_domain *d=opaque;unsigned dead;
    {KERNEL_GUARD(ultra_gate);dead=!--d->clients;if(dead){ultra_domain **p=&ultra_domains;while(*p && *p!=d)p=&(*p)->next;if(*p)*p=d->next;}}
    if(dead)ultra_destroy(d);
}
/* Exported service endpoints are leases too. Merely transferring a control
 * endpoint, without the discovery ingress, must retain its live service.
 * Internal service views are not leases, which avoids an ownership cycle. */
int vex_ultra_export_endpoint(vex_web_fd_object_t **object){
    process_t *p=scheduler_current_process();if(!p || !p->group || !p->group->ultra_space)return 0;
    unsigned rights=vex_message_fd_rights(*object);if(!rights)return 0;
    vex_web_fd_object_t *copy=NULL;int error=-ECANCELED;
    {KERNEL_GUARD(ultra_gate);ultra_domain *d=ultra_domains;
        while(d && d->process->observer.info.pid!=p->group->process_id)d=d->next;
        if(!d || d->clients==UINT32_MAX)return -ECANCELED;
        copy=vex_message_fd_clone(*object,rights,&error);if(!copy)return error;
        ++d->clients;vex_message_fd_admission(copy,0);vex_message_fd_lifetime_hook(copy,ultra_release,d);
    }
    ladybird_fd_release(*object);*object=copy;return 0;
}
static int ultra_client(ultra_domain *d,unsigned authority){if(d->clients==UINT32_MAX)return -ENOMEM;int error;vex_web_fd_object_t *o=vex_message_fd_clone(d->ingress,VEX_MESSAGE_RIGHT_SEND|VEX_MESSAGE_RIGHT_TRANSFER,&error);
    if(!o)return error;vex_message_fd_admission(o,authority);d->clients++;d->opens++;vex_message_fd_lifetime_hook(o,ultra_release,d);
    return ladybird_fd_install_flags(o,-1,1);
}
static int64_t ultra_call(const vex_process_resource_req_t *r){
    process_t *caller=scheduler_current_process();if(caller && caller->group && caller->group->ultra_space)return -EACCES;
    if(r->operation==VEX_PROCESS_ULTRA_QUERY || r->operation==VEX_PROCESS_ULTRA_OBSERVE){
        if(r->argv || r->envp || r->grants || r->grant_count || r->rights || r->timeout_ns || r->status || r->flags || ((r->operation==VEX_PROCESS_ULTRA_QUERY)!=!!r->out))return -EINVAL;
        VEX_FD_BORROW(ref);vex_web_fd_object_t *o=vex_fd_borrow_acquire(&ref,r->fd);ultra_domain *d=vex_message_fd_hook_context(o,ultra_release);
        if(!d)return -EBADF;
        if(r->operation==VEX_PROCESS_ULTRA_OBSERVE)return install(d->process,VEX_PROCESS_RIGHT_QUERY|VEX_PROCESS_RIGHT_WAIT);
        vex_ultra_info_t info;
        {KERNEL_GUARD(ultra_gate);info=(vex_ultra_info_t){d->identity,d->created,d->process->observer.info.pid,d->clients,d->opens};}
        return vex_copy_to_user((uintptr_t)r->out,&info,sizeof(info))?-EFAULT:0;
    }
    if(r->argv || r->envp || r->flags || r->rights || r->timeout_ns || r->out || r->grant_count>=GRANTS_MAX)return -EINVAL;
    launch l __attribute__((cleanup(cleanup)))={.count=r->grant_count,.fd=-1,.ultra=1};
    VEX_FD_BORROW(scope_ref);int error;fs_root_t *scope=vex_fs_root_borrow(&scope_ref,r->status,FS_ROOT_READ|FS_ROOT_ENUM,&error);if(!scope)return error;
    unsigned authority=vex_fs_root_borrow_rights(&scope_ref);
    vex_web_fd_object_t *image=vex_fd_borrow_acquire(&l.source,r->fd);if(!image)return -EBADF;
    if((error=vex_fd_executable_snapshot(image,&l.image)))return error;
    if(l.count && vex_copy_from_user(l.slots,(uintptr_t)r->grants,l.count*sizeof(l.slots[0])))return -EFAULT;
    for(unsigned i=0;i<l.count;i++){
        if(l.slots[i].reserved || l.slots[i].flags>1 || l.slots[i].slot<16 || l.slots[i].slot>=128)return -EINVAL;
        for(unsigned j=0;j<i;j++)if(l.slots[i].slot==l.slots[j].slot)return -EINVAL;
        if(!vex_fd_borrow_acquire(&l.grants[i],l.slots[i].fd))return -EBADF;
        if(!vex_fd_transfer_allowed(l.grants[i].object))return -EACCES;
    }
    KERNEL_GUARD(ultra_gate);
    for(ultra_domain *d=ultra_domains;d;d=d->next){
        /* Root authority, not the first client's UID, identifies discovery.
           Explicitly delegated roots must reach the same policy service;
           each message still carries its authenticated sender identity. */
        if(fs_root_identity(d->scope)!=fs_root_identity(scope) || d->count!=l.count || d->image.length!=l.image.length)continue;
        unsigned same=1;for(unsigned i=0;i<l.count;i++)if(d->grants[i]!=l.grants[i].object || d->slots[i].slot!=l.slots[i].slot || d->slots[i].flags!=l.slots[i].flags)same=0;
        vex_process_resource_info_t state;if(scheduler_resource_info(&d->process->observer,&state) || state.exited)continue;
        if(same && !memcmp(ora_view_data(&d->image),ora_view_data(&l.image),l.image.length))return ultra_client(d,authority);
    }
    if(ultra_next==UINT64_MAX)return -ENOMEM;
    ultra_domain *d=kmalloc(sizeof(*d));if(!d)return -ENOMEM;memset(d,0,sizeof(*d));d->count=l.count;
    d->scope=fs_root_retain(scope);if(!d->scope){kfree(d);return -ENOMEM;}
    for(unsigned i=0;i<l.count;i++){d->slots[i]=l.slots[i];d->grants[i]=ladybird_fd_acquire(l.slots[i].fd);
        if(d->grants[i]!=l.grants[i].object){ultra_destroy(d);return -EBADF;}}
    vex_web_fd_object_t *receive=NULL;
    if((error=vex_message_fd_pair(&d->ingress,&receive))){ultra_destroy(d);return error;}
    vex_message_fd_admission(d->ingress,0);
    /* Reuse the exact launch transaction. Slot 8 is private service ingress;
       the scope root is a cache/discovery authority, never an implicit grant. */
    l.slots[l.count]=(vex_process_grant_t){0,8,1,0};
    int temp=ladybird_fd_install_flags(receive,-1,1);if(temp<0){ultra_destroy(d);return -ENOMEM;}
    if(!vex_fd_borrow_acquire(&l.grants[l.count],temp)){close(temp);ultra_destroy(d);return -EBADF;}close(temp);l.count++;
    l.backing=kmalloc(sizeof(*l.backing));if(!l.backing){ultra_destroy(d);return -ENOMEM;}memset(l.backing,0,sizeof(*l.backing));l.backing->refs=1;
    const char *args[]={"ultra-space",NULL};uint32_t pid;
    if(elf_spawn_native_snapshot(ora_view_data(&l.image),l.image.length,args,1,NULL,0,prepare,&l,&pid)){
        if(l.fd>=0)close(l.fd);ultra_destroy(d);return l.error?l.error:-ENOEXEC;}
    d->process=l.backing;l.backing=NULL;close(l.fd);l.fd=-1;d->image=l.image;l.image=(ora_view_t){0};
    d->identity=++ultra_next;d->created=scheduler_time_ns();d->next=ultra_domains;ultra_domains=d;
    /* Temporary construction ownership makes FD installation rollback safe. */
    d->clients=1;int fd=ultra_client(d,authority);ultra_release(d);return fd;
}
