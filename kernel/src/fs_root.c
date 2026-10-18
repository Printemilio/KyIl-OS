#include <unos/fs_root.h>
#include <unos/fs.h>
#include <unos/heap.h>
#include <string.h>
#include <unos/scheduler.h>
struct fs_root {struct fs_root *next;uint64_t id;uint32_t refs;char path[512];};
static fs_root_t *roots;
static uint64_t next_identity;
typedef struct {fs_root_t *root;unsigned rights;uint32_t uid,caps;} authority;
static int beneath(const char *path,const char *root) {
    size_t n=strlen(root);
    return !strncmp(path,root,n) && (n==1 || !path[n] || path[n]=='/');
}
/* Only relative components enter a capability operation. Dot-dot is processed
 * before any link resolution and cannot cross the root. No silent truncation. */
static int join(fs_root_t *root,const char *relative,char out[512]) {
    if(!relative || relative[0]=='/')return -1;
    size_t floor=strlen(root->path),n=floor;memcpy(out,root->path,n+1);
    while(*relative) {
        while(*relative=='/')relative++;if(!*relative)break;
        const char *part=relative;size_t count=0;while(relative[count] && relative[count]!='/')count++;
        relative+=count;
        if(count==1 && part[0]=='.')continue;
        if(count==2 && part[0]=='.' && part[1]=='.') {
            if(n==floor)return -1;while(n>floor && out[n-1]!='/')n--;
            if(n>floor)n--;out[n]=0;continue;
        }
        if(n+1+count>=512)return -1;
        if(n>1)out[n++]='/';memcpy(out+n,part,count);n+=count;out[n]=0;
    }
    return 0;
}
fs_root_t *fs_root_create(const char *path,unsigned rights,int *error) {
    kernel_guard_t namespace __attribute__((cleanup(fs_namespace_end)))=fs_namespace_begin();
    char canonical[512];
    if(error)*error=-22;
    if(!path || !rights || (rights&~FS_ROOT_ALL) || strlen(path)>=sizeof(canonical) || fs_resolve_path(path,canonical,sizeof(canonical)))return NULL;
    if(error)*error=-13;if(!fs_root_scope_allows(canonical,rights))return NULL;
    if(error)*error=-20;if(!fs_directory_exists(canonical))return NULL;
    if(error)*error=-13;
    if(fs_access_path(canonical,(rights&(FS_ROOT_READ|FS_ROOT_ENUM|FS_ROOT_EXEC))!=0,
        (rights&(FS_ROOT_WRITE|FS_ROOT_CREATE|FS_ROOT_REMOVE|FS_ROOT_RENAME))!=0,0,1))return NULL;
    for(fs_root_t *r=roots;r;r=r->next)if(!strcmp(r->path,canonical)) {
        fs_root_t *result=fs_root_retain(r);if(error)*error=result?0:-24;return result;
    }
    if(error)*error=-12;
    fs_root_t *r=kmalloc(sizeof(*r));if(!r || next_identity==UINT64_MAX){kfree(r);return NULL;}
    memset(r,0,sizeof(*r));r->refs=1;r->id=++next_identity;memcpy(r->path,canonical,strlen(canonical)+1);
    r->next=roots;roots=r;if(error)*error=0;return r;
}
fs_root_t *fs_root_retain(fs_root_t *r) {
    kernel_guard_t namespace __attribute__((cleanup(fs_namespace_end)))=fs_namespace_begin();
    if(!r || r->refs==UINT32_MAX)return NULL;++r->refs;return r;
}
void fs_root_release(fs_root_t *r) {
    if(!r)return;
    kernel_guard_t namespace __attribute__((cleanup(fs_namespace_end)))=fs_namespace_begin();
    if(--r->refs)return;
    fs_root_t **p=&roots;while(*p && *p!=r)p=&(*p)->next;if(*p)*p=r->next;kfree(r);
}
uint64_t fs_root_identity(fs_root_t *r) {return r?r->id:0;}
int fs_root_path(fs_root_t *r,char *out,size_t size) {
    kernel_guard_t namespace __attribute__((cleanup(fs_namespace_end)))=fs_namespace_begin();
    if(!r || !out || strlen(r->path)>=size)return -1;
    memcpy(out,r->path,strlen(r->path)+1);return 0;
}
int fs_root_path_pinned(const char *path) {
    kernel_guard_t namespace __attribute__((cleanup(fs_namespace_end)))=fs_namespace_begin();
    char directory[512];size_t n=strlen(path);
    const char *marker="/.ladybird_dir";size_t suffix=strlen(marker);
    if(n>=suffix && !strcmp(path+n-suffix,marker)) {
        if(n-suffix>=sizeof(directory))return 1;
        memcpy(directory,path,n-suffix);directory[n-suffix]=0;path=directory;
    }
    for(fs_root_t *r=roots;r;r=r->next)
        if(beneath(r->path,path))return 1;
    return 0;
}
uint32_t fs_root_credential_uid(uint32_t fallback) {
    process_t *p=scheduler_current_process();authority *a=p?p->fs_authority:NULL;return a?a->uid:fallback;
}
uint32_t fs_root_credential_caps(uint32_t fallback) {
    process_t *p=scheduler_current_process();authority *a=p?p->fs_authority:NULL;return a?a->caps:fallback;
}
int fs_root_active(fs_root_t *root) {
    process_t *p=scheduler_current_process();authority *a=p?p->fs_authority:NULL;return a && a->root==root;
}
fs_root_t *fs_root_current(void) {
    process_t *p=scheduler_current_process();authority *a=p?p->fs_authority:NULL;return a?a->root:NULL;
}
unsigned fs_root_current_rights(void) {
    process_t *p=scheduler_current_process();authority *a=p?p->fs_authority:NULL;return a?a->rights:0;
}
int fs_root_relative(fs_root_t *r,const char *path,char *out,size_t size) {
    if(!r || !path || !out || !beneath(path,r->path))return -1;
    const char *s=path+strlen(r->path);if(*s=='/')s++;
    if(strlen(s)>=size)return -1;memcpy(out,s,strlen(s)+1);return 0;
}
int fs_root_scope_allows(const char *path,unsigned rights) {
    process_t *p=scheduler_current_process();
    if(!p || !p->user_mode)return 1;
    authority *a=p->fs_authority;
    if(a) {
        if(!beneath(path,a->root->path))return 0;
        /* A write-only descriptor needs internal reads to preserve untouched
         * file bytes. Only trusted operation callbacks see this authority. */
        unsigned allowed=a->rights;
        if(allowed&FS_ROOT_WRITE)allowed|=FS_ROOT_READ;
        return !(rights&~allowed);
    }
    return !p->group || !__atomic_load_n(&p->group->fs_sealed,__ATOMIC_ACQUIRE);
}
int fs_root_invoke(fs_root_t *r,unsigned rights,uint32_t uid,uint32_t caps,const char *relative,fs_root_operation_t operation,void *context) {
    if(!r || !operation || (rights&~FS_ROOT_ALL))return -1;
    kernel_guard_t namespace __attribute__((cleanup(fs_namespace_end)))=fs_namespace_begin();
    char joined[512],resolved[512];
    if(join(r,relative,joined) || fs_resolve_path(joined,resolved,sizeof(resolved)) || !beneath(resolved,r->path))return -1;
    process_t *p=scheduler_current_process();authority a={r,rights,uid,caps};
    void *saved=p?p->fs_authority:NULL;if(p)p->fs_authority=&a;
    int result=operation(resolved,context);
    if(p)p->fs_authority=saved;
    return result;
}
int fs_root_resolve_relative(fs_root_t *r,const char *relative,char *out,size_t capacity,int reject_link) {
    if(!fs_root_active(r))return -13;
    kernel_guard_t namespace __attribute__((cleanup(fs_namespace_end)))=fs_namespace_begin();
    char joined[512],resolved[512];fs_metadata_t metadata;
    if(join(r,relative,joined) || fs_resolve_path(joined,resolved,sizeof(resolved)) || !beneath(resolved,r->path))return -13;
    if(reject_link && !fs_get_metadata(joined,&metadata,0) && metadata.type==FS_ENTRY_SYMLINK)return -95;
    if(strlen(resolved)>=capacity)return -36;memcpy(out,resolved,strlen(resolved)+1);return 0;
}
