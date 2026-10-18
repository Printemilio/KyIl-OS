/* Native typed resources built on the existing process-owned FD inventory.
 * Native paths/rights only: no Windows drive names, structures or policy here. */
typedef struct {fs_root_t *root;uint32_t rights,uid,caps;} root_context;
static int root_close(void *context) {
    root_context *r=context;fs_root_release(r->root);kfree(r);return 0;
}
static const ladybird_fd_ops_t root_ops={.close=root_close};
unsigned vex_fs_root_borrow_rights(void *opaque){vex_fd_borrow_t *borrow=opaque;
    vex_web_fd_object_t *o=borrow->object;return o && o->ops==&root_ops?((root_context *)o->ctx)->rights:0;
}
int vex_fd_transfer_allowed(void *opaque) {
    ladybird_fd_object_t *o=opaque;
    if(!o)return 0;
    if(!vex_message_fd_transfer_allowed(o) || !vex_rendezvous_transfer_allowed(o) || !vex_process_resource_transfer_allowed(o))return 0;
    if(o->ops==&root_ops)return (((root_context *)o->ctx)->rights&FS_ROOT_TRANSFER)!=0;
    if(o->ops==&file_ops && ((ladybird_file_ctx_t *)o->ctx)->root)
        return (((ladybird_file_ctx_t *)o->ctx)->root_rights&FS_ROOT_TRANSFER)!=0;
    if(o->ops==&dir_ops && ((ladybird_dir_ctx_t *)o->ctx)->root)
        return (((ladybird_dir_ctx_t *)o->ctx)->root_rights&FS_ROOT_TRANSFER)!=0;
    return 1;
}
static int root_install(fs_root_t *root,unsigned rights,uint32_t uid,uint32_t caps) {
    root_context *r=kmalloc(sizeof(*r));
    if(!r){fs_root_release(root);return -ENOMEM;}
    *r=(root_context){root,rights,uid,caps&VEX_CAP_FS_ADMIN};
    ladybird_fd_object_t *o=ladybird_fd_object_create(r,&root_ops);
    if(!o){root_close(r);return -ENOMEM;}
    int fd=ladybird_fd_install(o,-1);return fd<0?-*ladybird_errno_location():fd;
}
int vex_fs_root_create_fd(const char *path,unsigned rights) {
    int error;fs_root_t *root=fs_root_create(path,rights,&error);
    return root?root_install(root,rights,fs_root_credential_uid(user_current_uid()),
        fs_root_credential_caps(scheduler_current_capabilities())):error;
}
int vex_fs_root_query(int fd,vex_fs_root_info_t *info) {
    ladybird_fd_object_t *o=ladybird_fd_acquire(fd);
    int result=-EBADF;
    if(o && o->ops==&root_ops) {
        root_context *r=o->ctx;memset(info,0,sizeof(*info));
        info->identity=fs_root_identity(r->root);info->rights=r->rights;info->grant_uid=r->uid;
        result=fs_root_path(r->root,info->path,sizeof(info->path));
    }
    if(o)ladybird_fd_release(o);return result;
}
fs_root_t *vex_fs_root_borrow(vex_fd_borrow_t *borrow,int fd,unsigned needed,int *error) {
    vex_web_fd_object_t *o=vex_fd_borrow_acquire(borrow,fd);*error=-EBADF;
    if(!o || o->ops!=&root_ops)return NULL;
    root_context *r=o->ctx;*error=-EACCES;if(needed&~r->rights)return NULL;
    *error=0;return r->root;
}
typedef struct {root_context *root;unsigned rights,flags,mode;vex_stat_ex_t *stat;} root_call;
typedef struct {root_call call;const char *relative;} remove_call;
static int root_remove_call(const char *path,void *opaque) {
    remove_call *r=opaque;root_call *c=&r->call;
    if(fs_root_path_pinned(path))return -EBUSY;
    /* fs_root_invoke follows the terminal link. Refuse that case explicitly:
     * removing its target is not a correct substitute for unlinking the link. */
    char anchor[512],original[1024];fs_metadata_t metadata;
    if(fs_root_path(c->root->root,anchor,sizeof(anchor)))return -EACCES;
    if(snprintf(original,sizeof(original),"%s/%s",anchor,r->relative)>=(int)sizeof(original))return -ENAMETOOLONG;
    if(!fs_get_metadata(original,&metadata,0u) && metadata.type==FS_ENTRY_SYMLINK)return -ENOTSUP;
    if(c->flags&VEX_FS_REMOVE_DIRECTORY)return rmdir(path)<0?-*ladybird_errno_location():0;
    if(path_is_directory(path))return -EISDIR;
    if(!path_exists_any(path))return -ENOENT;
    if(fs_access_path(path,0,1,0,0))return -EACCES;
    uint64_t flags=fd_inventory_lock();int busy=0;
    for(ladybird_file_ctx_t *f=live_file_contexts;f;f=f->live_next)
        if(!strcmp(f->path,path)){busy=1;break;}
    fd_inventory_unlock(flags);
    if(busy)return -EBUSY;
    return unlink(path)<0?-*ladybird_errno_location():0;
}
static int root_derive_call(const char *path,void *opaque) {
    root_call *c=opaque;return vex_fs_root_create_fd(path,c->rights);
}
static int root_open_call(const char *path,void *opaque) {
    root_call *c=opaque;
    if((c->flags&VEX_FS_OPEN_REGULAR) && fs_directory_exists(path))return -EISDIR;
    /* A nonexistent image must reach open's ENOENT path, not become EACCES.
       The root namespace transaction still spans access checking and open. */
    if((c->flags&VEX_FS_OPEN_EXECUTABLE) && path_exists_any(path) && fs_access_path(path,1,0,1,0))return -EACCES;
    int fd=vex_open_mode(path,(int)(c->flags&~(VEX_FS_OPEN_REGULAR|VEX_FS_OPEN_EXECUTABLE)),c->mode);
    if(fd>=0 && (c->flags&VEX_FS_OPEN_EXECUTABLE)) {
        ladybird_fd_object_t *o=ladybird_fd_acquire(fd);
        if(!o || o->ops!=&file_ops) {if(o)ladybird_fd_release(o);close(fd);return -ENOTSUP;}
        ((ladybird_file_ctx_t *)o->ctx)->root_rights|=FS_ROOT_EXEC;
        ladybird_fd_release(o);
    }
    return fd<0?-*ladybird_errno_location():fd;
}
static int root_mkdir_call(const char *path,void *opaque) {
    root_call *c=opaque;return mkdir(path,c->mode)<0?-*ladybird_errno_location():0;
}
static int root_stat_call(const char *path,void *opaque) {
    root_call *c=opaque;return vex_stat_ex_path(path,1,c->stat);
}
static int root_call_fd(int fd,const char *path,unsigned needed,fs_root_operation_t operation,root_call *c) {
    ladybird_fd_object_t *o=ladybird_fd_acquire(fd);
    if(!o)return -EBADF;
    int result=-EBADF;
    if(o->ops==&root_ops) {
        root_context *r=o->ctx;c->root=r;
        if(needed&~r->rights)result=-EACCES;
        else {result=fs_root_invoke(r->root,needed|(r->rights&FS_ROOT_TRANSFER),r->uid,r->caps,path,operation,c);if(result==-1)result=-EACCES;}
    }
    ladybird_fd_release(o);return result;
}
int vex_fs_root_derive(int fd,const char *path,unsigned rights) {
    if(!rights || (rights&~FS_ROOT_ALL))return -EINVAL;
    root_call c={.rights=rights};return root_call_fd(fd,path,rights,root_derive_call,&c);
}
int vex_fs_root_open(int fd,const char *path,unsigned flags,unsigned mode) {
    const unsigned allowed=O_ACCMODE|O_CREAT|LADYBIRD_O_EXCL|O_TRUNC|O_APPEND|O_NONBLOCK|O_CLOEXEC|LINUX_O_DIRECTORY|VEX_FS_OPEN_REGULAR|VEX_FS_OPEN_EXECUTABLE;
    if((flags&~allowed) || (flags&O_ACCMODE)==O_ACCMODE || mode&~0777u ||
        (flags&(LINUX_O_DIRECTORY|VEX_FS_OPEN_REGULAR))==(LINUX_O_DIRECTORY|VEX_FS_OPEN_REGULAR))return -EINVAL;
    if((flags&VEX_FS_OPEN_EXECUTABLE) && (flags&(O_ACCMODE|O_CREAT|O_TRUNC|O_APPEND|LINUX_O_DIRECTORY)))return -EINVAL;
    unsigned needed=(flags&O_ACCMODE)==O_WRONLY?FS_ROOT_WRITE:FS_ROOT_READ;
    if((flags&O_ACCMODE)==O_RDWR || (flags&(O_TRUNC|O_APPEND)))needed|=FS_ROOT_WRITE;
    if(flags&O_CREAT)needed|=FS_ROOT_CREATE|FS_ROOT_WRITE;
    if(flags&LINUX_O_DIRECTORY)needed|=FS_ROOT_ENUM;
    if(flags&VEX_FS_OPEN_EXECUTABLE)needed|=FS_ROOT_EXEC;
    root_call c={.flags=flags,.mode=mode};return root_call_fd(fd,path,needed,root_open_call,&c);
}
int vex_fs_root_mkdir(int fd,const char *path,unsigned mode) {
    if(mode&~0777u)return -EINVAL;
    root_call c={.mode=mode};return root_call_fd(fd,path,FS_ROOT_WRITE|FS_ROOT_CREATE,root_mkdir_call,&c);
}
int vex_fs_root_stat(int fd,const char *path,vex_stat_ex_t *out) {
    root_call c={.stat=out};return root_call_fd(fd,path,FS_ROOT_READ,root_stat_call,&c);
}
int vex_fs_root_remove(int fd,const char *path,unsigned flags) {
    if(flags&~VEX_FS_REMOVE_DIRECTORY)return -EINVAL;
    remove_call c={.call={.flags=flags},.relative=path};
    return root_call_fd(fd,path,FS_ROOT_READ|FS_ROOT_WRITE|FS_ROOT_REMOVE|
        ((flags&VEX_FS_REMOVE_DIRECTORY)?FS_ROOT_ENUM:0),root_remove_call,&c.call);
}
typedef struct {root_call call;const char *source,*destination,*backup;int replace;} move_call;
static int root_move_call(const char *ignored,void *opaque) {
    (void)ignored;move_call *m=opaque;char source[512],destination[512],backup[512];int result;
    fs_root_t *root=m->call.root->root;
    if((result=fs_root_resolve_relative(root,m->source,source,sizeof(source),1)) ||
       (result=fs_root_resolve_relative(root,m->destination,destination,sizeof(destination),1)) ||
       (m->backup && (result=fs_root_resolve_relative(root,m->backup,backup,sizeof(backup),1))))return result;
    if(!strcmp(source,destination) || (m->backup && (!strcmp(source,backup) || !strcmp(destination,backup))))return -EINVAL;
    const char *names[3]={source,destination,m->backup?backup:NULL};
    for(unsigned i=0;i<3;i++)if(names[i]) {
        if(fs_root_path_pinned(names[i]))return -EBUSY;
        if(path_is_directory(names[i]))return -ENOTSUP;
        char parent[512];memcpy(parent,names[i],strlen(names[i])+1);char *last=NULL;
        for(char *c=parent;*c;c++)if(*c=='/')last=c;
        if(!last)return -EINVAL;if(last==parent)last[1]=0;else *last=0;
        if(!path_is_directory(parent))return -ENOENT;
        if(!fs_directory_can_modify(parent))return -EACCES;
    }
    /* Open descriptions carry immutable paths. Until they support atomic
       retargeting, do not rename a name whose final flush can recreate it.
       Publication of new descriptions shares this namespace transaction. */
    uint64_t inventory=fd_inventory_lock();int busy=0;
    for(ladybird_file_ctx_t *f=live_file_contexts;f && !busy;f=f->live_next)
        for(unsigned i=0;i<3;i++)if(names[i] && !strcmp(f->path,names[i])){busy=1;break;}
    fd_inventory_unlock(inventory);if(busy)return -EBUSY;
    if(!path_exists_any(source))return -ENOENT;
    if(m->replace) {
        return fs_replace_preserve(source,destination,m->backup?backup:NULL);
    }
    if(path_exists_any(destination))return -EEXIST;
    return fs_move(source,destination)?-EIO:0;
}
int vex_fs_root_move(int fd,const char *source,const char *destination,const char *backup,int replace) {
    move_call c={.source=source,.destination=destination,.backup=backup,.replace=replace};
    return root_call_fd(fd,source,FS_ROOT_READ|FS_ROOT_WRITE|FS_ROOT_CREATE|FS_ROOT_REMOVE|FS_ROOT_RENAME,root_move_call,&c.call);
}
static int root_mode_call(const char *path,void *opaque){root_call *c=opaque;return fs_mode_mask(path,c->flags,c->mode);}
int vex_fs_root_mode(int fd,const char *path,unsigned mask,unsigned value) {
    if((mask&~0777u) || (value&~mask))return -EINVAL;
    root_call c={.flags=mask,.mode=value};return root_call_fd(fd,path,FS_ROOT_WRITE,root_mode_call,&c);
}
int vex_fd_executable_snapshot(void *opaque,ora_view_t *out) {
    ladybird_fd_object_t *o=opaque;
    if(!o || o->ops!=&file_ops)return -EBADF;
    ladybird_file_ctx_t *f=o->ctx;KERNEL_GUARD(f->data_gate);
    if(!f->root || !(f->root_rights&FS_ROOT_EXEC) || f->write_allowed || f->dirty || f->volatile_shm)return -EACCES;
    if(f->content.object)return ora_view_acquire(f->content.object,f->content.offset,f->content.length,out)?-ENOMEM:0;
    ora_ref_t content=ora_create(f->buffer,f->size,ORA_UNIQUE);if(!content)return -ENOMEM;
    int r=ora_view_acquire(content,0,f->size,out);ora_release(content);return r?-ENOMEM:0;
}
