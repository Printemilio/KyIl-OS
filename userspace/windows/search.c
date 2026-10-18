#include "windows.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
typedef struct {uint32_t attributes,times[6],size_high,size_low,reserved[2];uint16_t name[260],alternate[14];} find_data;
_Static_assert(sizeof(find_data)==592,"AMD64 WIN32_FIND_DATAW");
typedef struct search {int root,fd;unsigned level,dirs,case_sensitive;uint16_t pattern[260];
    unsigned used,at;uint8_t entries[4096];} search;
static struct {search *object;uint32_t generation;} searches[256];
static wr_mutex gate;
static unsigned error(unsigned code){wr_current_teb()->last_error=code;return 0;}
static unsigned fold(unsigned c){return wr_unicode_case(c,1);}
/* Bounded DOS wildcard matcher, without exponential recursive backtracking.
 * The compatibility volume has no generated 8.3 aliases. Question marks may
 * collapse at dot/end, and a terminal . or .* also matches a missing suffix. */
int wr_search_match(const uint16_t *p,const uint16_t *name,int sensitive) {
    unsigned n=0,m=0;while(name[n]){if(n==259)return 0;n++;}while(p[m]){if(m==259)return 0;m++;}
    uint8_t a[260]={1},b[260];
    for(unsigned i=0;i<m;i++) {
        memset(b,0,sizeof(b));unsigned c=p[i];
        if(c=='*')for(unsigned j=0;j<=n;j++)b[j]=a[j] || (j && b[j-1]);
        else for(unsigned j=0;j<=n;j++) {
            if(c=='?') {if(a[j] && (j==n || name[j]=='.'))b[j]=1;
                if(j && name[j-1]!='.' && a[j-1])b[j]=1;}
            else {if(j && a[j-1] && (sensitive?c==name[j-1]:fold(c)==fold(name[j-1])))b[j]=1;
                if(c=='.' && j==n && a[j] && (i+1==m || (i+2==m && p[i+1]=='*')))b[j]=1;}
        }
        memcpy(a,b,sizeof(a));
    }
    return a[n];
}
static int root_call(search *s,unsigned op,const char *path,unsigned flags,void *out) {
    vex_fs_root_req_t r={.size=sizeof(r),.operation=op,.fd=s->root,.path=path,.flags=flags,.out=out};
    return (int)(int64_t)vex_syscall1(VEX_SYS_FS_ROOT,(uintptr_t)&r);
}
static uint64_t stamp(int64_t seconds,unsigned ns,unsigned valid) {
    if(!valid || seconds< -11644473600ll || ns>=1000000000u || seconds>(int64_t)(UINT64_MAX/10000000ull)-11644473601ll)return 0;
    return (uint64_t)(seconds+11644473600ll)*10000000ull+ns/100;
}
static int next(search *s,find_data *out) {
    for(;;) {
        if(s->at==s->used) {vex_getdents_req_t r={s->fd,0,s->entries,sizeof(s->entries)};
            int64_t n=(int64_t)vex_syscall1(VEX_SYS_GETDENTS,(uintptr_t)&r);
            if(n<0)return error(wr_file_error((int)-n));if(!n)return error(18);
            if(n>(int64_t)sizeof(s->entries))return error(1117);s->at=0;s->used=(unsigned)n;}
        vex_dirent_t *entry=(void *)(s->entries+s->at);unsigned remain=s->used-s->at;
        if(remain<20 || entry->reclen<20 || entry->reclen>remain || (entry->reclen&7))return error(1117);
        unsigned max=entry->reclen-(unsigned)offsetof(vex_dirent_t,name),n=0;
        while(n<max && entry->name[n])n++;if(n==max)return error(1117);s->at+=entry->reclen;
        uint16_t name[260];if(wr_utf16(entry->name,name,260)<0)return error(206);
        if(!wr_search_match(s->pattern,name,s->case_sensitive))continue;
        vex_stat_ex_t st;int result=root_call(s,VEX_FS_ROOT_STAT,entry->name,0,&st);
        if(result==-2)continue; /* Concurrent deletion is a legitimate enumeration race. */
        if(result)return error(wr_file_error(-result));if(s->dirs && (st.mode&0170000)!=0040000)continue;
        find_data value={0};value.attributes=(st.mode&0170000)==0040000?0x10:0;
        if(!(st.mode&0222))value.attributes|=1;if(!value.attributes)value.attributes=0x80;
        uint64_t at=stamp(st.atime_sec,st.atime_nsec,st.valid&VEX_STAT_EX_HAS_ATIME),mt=stamp(st.mtime_sec,st.mtime_nsec,st.valid&VEX_STAT_EX_HAS_MTIME);
        memcpy(value.times+2,&at,8);memcpy(value.times+4,&mt,8);
        uint64_t length=(st.mode&0170000)==0040000?0:(uint64_t)st.size;value.size_high=(unsigned)(length>>32);value.size_low=(unsigned)length;
        unsigned units=0;while(name[units])units++;memcpy(value.name,name,(units+1)*2);
        if(wr_memory_copy(out,&value,sizeof(value)))return error(998);return 1;
    }
}
static int lookup(void *handle) {
    uintptr_t value=(uintptr_t)handle;unsigned i=wr_handle_slot(value);
    return wr_handle_tag(value)==0x5b && wr_handle_generation(value)==searches[i].generation && searches[i].object?(int)i:-1;
}
static void dispose(search *s){if(s->fd>=0)vex_syscall1(VEX_SYS_CLOSE,s->fd);if(s->root>=0)vex_syscall1(VEX_SYS_CLOSE,s->root);wr_native_free(s);}
static void *WR_WIN FindFirstFileExW(const uint16_t *path,unsigned level,find_data *out,unsigned operation,void *filter,unsigned flags) {
    if(!path || !*path || !out || level>1 || operation>1 || filter || (flags&~3u)){error(87);return (void *)-1;}
    uint16_t full[1024];int n=wr_path_guest(path,full,1024);if(n<0){error(5);return (void *)-1;}
    unsigned slash=0;for(unsigned i=0;i<(unsigned)n;i++)if(full[i]=='\\')slash=i;
    unsigned length=(unsigned)n-slash-1;if(!length || length>=260){error(length?206:123);return (void *)-1;}
    search *s=wr_native_alloc(sizeof(*s));if(!s){error(8);return (void *)-1;}memset(s,0,sizeof(*s));s->root=s->fd=-1;
    memcpy(s->pattern,full+slash+1,(length+1)*2);if(slash==2)full[3]=0;else full[slash]=0;
    s->root=wr_namespace_directory(full);if(s->root<0){error(wr_file_error(-s->root));dispose(s);return (void *)-1;}
    s->fd=root_call(s,VEX_FS_ROOT_OPEN,"",O_RDONLY|0x10000u|O_CLOEXEC,0);s->level=level;s->dirs=operation;s->case_sensitive=flags&1;
    if(s->fd<0){error(wr_file_error(-s->fd));dispose(s);return (void *)-1;}
    wr_lock(&gate);unsigned i=0;while(i<256 && (searches[i].object || searches[i].generation>=WR_HANDLE_GENERATION_MAX))i++;
    if(i==256 || !next(s,out)){if(i==256)error(8);else if(wr_current_teb()->last_error==18)error(2);wr_unlock(&gate);dispose(s);return (void *)-1;}
    if(!++searches[i].generation)++searches[i].generation;searches[i].object=s;
    void *h=(void *)(wr_handle_pack(0x5b,searches[i].generation,i));wr_unlock(&gate);return h;
}
static void *WR_WIN FindFirstFileW(const uint16_t *p,find_data *out){return FindFirstFileExW(p,0,out,0,0,0);}
static int WR_WIN FindNextFileW(void *h,find_data *out){if(!out)return error(87);wr_lock(&gate);int i=lookup(h);int ok=i<0?error(6):next(searches[i].object,out);wr_unlock(&gate);return ok;}
static int WR_WIN FindClose(void *h){wr_lock(&gate);int i=lookup(h);search *s=i<0?0:searches[i].object;if(s)searches[i].object=0;wr_unlock(&gate);if(!s)return error(6);dispose(s);return 1;}
static unsigned WR_WIN GetFullPathNameW(const uint16_t *path,unsigned capacity,uint16_t *out,uint16_t **file) {
    uint16_t full[1024];int n=path && *path?wr_path_guest(path,full,1024):-1;if(n<0){error(123);return 0;}
    if((unsigned)n>=capacity)return (unsigned)n+1;if(!out){error(87);return 0;}
    if(wr_memory_copy(out,full,((size_t)n+1)*2)){error(998);return 0;}
    if(file){unsigned slash=0;for(unsigned i=0;i<(unsigned)n;i++)if(full[i]=='\\')slash=i;uint16_t *last=slash+1==(unsigned)n?0:out+slash+1;
        if(wr_memory_copy(file,&last,sizeof(last))){error(998);return 0;}}
    return (unsigned)n;
}
static unsigned WR_WIN GetLongPathNameW(const uint16_t *path,uint16_t *out,unsigned capacity) {
    vex_stat_ex_t st;int result=path?wr_namespace_call(VEX_FS_ROOT_STAT,path,0,0,&st,0):-22;
    if(result){error(wr_file_error(-result));return 0;}
    /* No 8.3 namespace is exported by the Vex-backed volume. Existing long
     * names are preserved, including the caller's relative/absolute spelling. */
    unsigned n=0;while(path[n]){if(n==32767){error(206);return 0;}n++;}
    if(n>=capacity)return n+1;if(!out){error(87);return 0;}
    if(wr_memory_copy(out,path,((size_t)n+1)*2)){error(998);return 0;}return n;
}
static unsigned WR_WIN GetDriveTypeW(const uint16_t *root) {
    uint16_t cwd[1024];if(!root){if(wr_path_guest((uint16_t[]){'.',0},cwd,1024)<0)return 1;cwd[3]=0;root=cwd;}
    if(!root[0] || root[1]!=':' || (root[2]!='\\' && root[2]!='/') || root[3])return 1;
    vex_stat_ex_t st;if(wr_namespace_call(VEX_FS_ROOT_STAT,root,0,0,&st,0))return 1;
    return 3; /* Explicit fixed compatibility volumes C: and package A:. */
}
void *wr_search_exports(const char *name) {
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(FindFirstFileExW);E(FindFirstFileW);E(FindNextFileW);E(FindClose);E(GetFullPathNameW);E(GetLongPathNameW);E(GetDriveTypeW);
#undef E
    return 0;
}
