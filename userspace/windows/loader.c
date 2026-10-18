#include "windows.h"
#include "process.h"
#include "job.h"
#include "wer.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
#include "generated_export_census.h"
static int main_source_fd=-1;
static module modules[WR_MAX_MODULES];
static unsigned module_count, init_order[WR_MAX_MODULES], init_count;
static unsigned finishing;
static int wer_host;
/* Userspace DLL policy; known native providers remain protected. The optional
   USER_DIRS list is empty until AddDllDirectory is implemented. */
static unsigned default_search=0xa00u,active_search;
static const char *error;
unsigned wr_modules_count(void) {return module_count;}
module *wr_module_at(unsigned i) {return i<module_count?&modules[i]:0;}
static const char *fail(const char *e) { error=e;return e; }
static void log_hex(uintptr_t value) {
    char text[]="0x0000000000000000";
    for(unsigned i=0;i<16;i++)text[2+i]="0123456789abcdef"[(value>>(60-4*i))&15];wr_log(text);
}
static const char *image_string(module *m,uint64_t at) {
    if(at>=m->image.size) return 0;
    for(uint64_t end=at;end<m->image.size && end-at<4096;end++)
        if(!m->base[end]) return (const char *)m->base+at;
    return 0;
}
static int dll_name(const char *name) {
    size_t n=wr_len(name);
    if(!n || n>=128 || name[0]=='.') return 0;
    for(size_t i=0;i<n;i++) {
        unsigned c=(unsigned char)name[i];
        if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_' || c=='-' || c=='.')) return 0;
    }
    return 1; /* A PE DLL can have a non-.dll extension. */
}
const char *wr_api_set_host(const char *name){
    /* Contract catalog, not a catch-all api-ms prefix heuristic. The existing
       FLS exports come from KernelBase; absent fiber-execution exports still
       fail lookup rather than being advertised as implemented. */
    return wr_equal(name,"api-ms-win-core-fibers-l1-1-2.dll",1)?"kernelbase.dll":NULL;
}
static int library_name(const char *input,char out[1024]){
    if(!input)return -1;size_t n=0;int dot=0;
    while(input[n]){if(n>=1019)return -1;char c=input[n];out[n++]=c;
        if(c=='/' || c=='\\' || c==':')dot=0;else if(c=='.')dot=1;}
    if(!n)return -1;
    if(out[n-1]=='.')--n;else if(!dot){memcpy(out+n,".dll",4);n+=4;}
    out[n]=0;return n?0:-1;
}
static module *load(const char *path,int expect_dll);
static module *load_source(const char *,int,int);
static module *load_opened(const char *path,int fd){module *m=load_source(path,1,fd);vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)fd);return m;}
static int open_image(const char *path){return wr_namespace_open_native(path,O_RDONLY|O_CLOEXEC|VEX_FS_OPEN_REGULAR|VEX_FS_OPEN_EXECUTABLE);}
int wr_native_module_name(const char *name) {
    return wr_api_set_host(name) || wr_equal(name,"shell32.dll",1) || wr_equal(name,"advapi32.dll",1) || wr_equal(name,"winmm.dll",1) || wr_equal(name,"bcryptprimitives.dll",1) || wr_equal(name,"kernel32.dll",1) || wr_equal(name,"kernelbase.dll",1) || wr_equal(name,"ntdll.dll",1) || wr_equal(name,"version.dll",1);
}
static module *dependency(module *parent,const char *name) {
    const char *host=wr_api_set_host(name);if(host)name=host;
    if(!dll_name(name)) {fail("invalid DLL basename");return 0;}
    for(unsigned i=0;i<module_count;i++)
        if(wr_equal(modules[i].name,name,1)){if(!modules[i].state){fail("previous DLL initialization failed");return 0;}return &modules[i];}
    if(wr_native_module_name(name)) {
        char system[512]="/usr/lib/kyil-windows/";size_t at=wr_len(system),len=wr_len(name);
        if(at+len>=sizeof(system)){fail("provider path too long");return 0;}
        for(size_t i=0;i<len;i++){unsigned c=(unsigned char)name[i];system[at+i]=(char)(c>='A'&&c<='Z'?c+32:c);}system[at+len]=0;
        return load(system,1);
    }
    unsigned search=active_search?active_search:default_search;
    if(search&0x1000u)search|=0xe00u;
    if(search&0x200u){
        module *exe=module_count?&modules[0]:parent;
        char path[512];size_t slash=0,n=wr_len(exe->path),len=wr_len(name);
        for(size_t i=0;i<n;i++)if(exe->path[i]=='/')slash=i+1;
        if(slash+len>=sizeof(path)){fail("dependency path too long");return 0;}
        memcpy(path,exe->path,slash);memcpy(path+slash,name,len+1);
        int fd=open_image(path);if(fd>=0)return load_opened(path,fd);
        if(fd!=-2){fail("application DLL open denied/failed");return 0;}
    }
    if(search&0x800u){
        char guest[512]="C:\\Windows\\System32\\",path[1024];uint16_t wide[512];size_t at=wr_len(guest),n=wr_len(name);
        if(at+n>=sizeof(guest)){fail("system DLL path too long");return 0;}memcpy(guest+at,name,n+1);
        if(wr_utf16(guest,wide,512)<0 || wr_path_native(wide,path,sizeof(path),0)){fail("system DLL path invalid");return 0;}
        int fd=wr_namespace_call(VEX_FS_ROOT_OPEN,wide,O_RDONLY|O_CLOEXEC|VEX_FS_OPEN_REGULAR|VEX_FS_OPEN_EXECUTABLE,0,0,0);
        if(fd>=0)return load_opened(path,fd);
        if(fd!=-2){fail("system DLL open denied/failed");return 0;}
    }
    fail("DLL absent from selected search directories");return 0;
}
static void *symbol(module *m,const char *name,uint32_t ordinal,unsigned depth) {
    if(depth>16) {fail("export forwarder cycle/depth");return 0;}
    uint32_t r=m->image.dirs[0][0], n=m->image.dirs[0][1];
    if(!r || n<40) {fail("missing DLL export directory");return 0;}
    const uint8_t *e=m->base+r;
    uint32_t first=pe_u32(e+16), nf=pe_u32(e+20), nn=pe_u32(e+24);
    uint32_t funcs=pe_u32(e+28), names=pe_u32(e+32), ords=pe_u32(e+36);
    if(!pe_span(funcs,(uint64_t)nf*4,m->image.size) || !pe_span(names,(uint64_t)nn*4,m->image.size) ||
       !pe_span(ords,(uint64_t)nn*2,m->image.size)) {fail("invalid export arrays");return 0;}
    uint32_t index=UINT32_MAX;
    if(!name) {if(ordinal>=first && ordinal-first<nf) index=ordinal-first;}
    else for(uint32_t k=0;k<nn;k++) {
        const char *s=image_string(m,pe_u32(m->base+names+4ull*k));
        if(!s) {fail("invalid export name");return 0;}
        if(wr_equal(s,name,0)) {index=pe_u16(m->base+ords+2ull*k);break;}
    }
    if(index>=nf) {fail("export not found");return 0;}
    uint32_t target=pe_u32(m->base+funcs+4ull*index);
    if(!target || target>=m->image.size) {fail("invalid export RVA");return 0;}
    if(target>=r && target<(uint64_t)r+n) {
        const char *f=image_string(m,target); if(!f) {fail("invalid export forwarder");return 0;}
        size_t dot=0;while(f[dot] && f[dot]!='.')dot++;
        if(!dot || dot+5>=128 || !f[dot] || !f[dot+1]) {fail("invalid export forwarder");return 0;}
        char dll[128];memcpy(dll,f,dot);memcpy(dll+dot,".dll",5);
        const char *s=f+dot+1;void *b=wr_builtin(dll,s);if(b)return b;
        module *d=dependency(m,dll);if(!d)return 0;
        if(s[0]=='#') {
            uint64_t o=0;size_t i=1;
            for(;s[i];i++) {if(s[i]<'0'||s[i]>'9'||o>(65535u-(s[i]-'0'))/10u) {fail("invalid forward ordinal");return 0;} o=o*10+s[i]-'0';}
            if(i==1) {fail("empty forward ordinal");return 0;}
            return symbol(d,0,(uint32_t)o,depth+1);
        }
        return symbol(d,s,0,depth+1);
    }
    return m->base+target;
}
static int imports(module *m) {
    uint32_t at=m->image.dirs[1][0], n=m->image.dirs[1][1];
    if(!n)return 0;
    uint64_t end=(uint64_t)at+n;unsigned missing=0;
    for(;pe_span(at,20,end);at+=20) {
        uint8_t *d=m->base+at;
        uint32_t src=pe_u32(d), name_rva=pe_u32(d+12), dst=pe_u32(d+16);
        if(!src && !name_rva && !dst && !pe_u32(d+4) && !pe_u32(d+8)) {
            if(missing){wr_log("windows: unresolved static contracts in ");wr_log(m->name);wr_log(" count=");log_hex(missing);wr_log("\n");fail("Windows APIs not implemented");return -1;}
            return 0;
        }
        const char *dll=image_string(m,name_rva);
        if(!dll || !dll_name(dll) || !dst || (dst&7) || (src&7)) {fail("invalid import descriptor");return -1;}
        if(!src)src=dst;
        for(uint64_t k=0;;k+=8) {
            if(!pe_span((uint64_t)src+k,8,m->image.size) || !pe_span((uint64_t)dst+k,8,m->image.size)) {fail("unterminated import thunks");return -1;}
            uint64_t v=pe_u64(m->base+src+k);if(!v)break;
            const char *name=0;uint32_t ord=0;
            if(v>>63) {
                if(v&0x7fffffffffff0000ull) {fail("invalid import ordinal");return -1;}ord=(uint16_t)v;
            } else {
                if(!pe_span(v,3,m->image.size) || !(name=image_string(m,v+2))) {fail("invalid import name");return -1;}
            }
            void *address=name?wr_builtin(dll,name):0;
            if(!address) {
                /* Builtin modules fail by exact symbol instead of guessing a stub. */
                if(wr_native_module_name(dll)) {
                    if(missing++<128){wr_log("windows: missing import ");wr_log(dll);wr_log("!");
                        if(name)wr_log(name);else log_hex(ord);wr_log(" caller=");wr_log(m->name);wr_log("\n");}
                    /* Continue validating the complete table, but no failed
                     * image is executable or given a substitute IAT target. */
                    continue;
                }
                module *dep=dependency(m,dll);
                if(!dep || !(address=symbol(dep,name,ord,0)))return -1;
            }
            pe_put64(m->base+dst+k,(uintptr_t)address);
        }
    }
    fail("unterminated import descriptors");return -1;
}
static const char *validate_delay_imports(module *m) {
    uint32_t at=m->image.dirs[13][0],n=m->image.dirs[13][1];if(!n)return 0;
    uint64_t end=(uint64_t)at+n;
    /* The Microsoft linker supplies its real delay helper/thunks in the PE.
       Validate and preserve them; never eagerly initialize unused DLLs or
       replace unresolved calls by success stubs. Actual first use goes through
       LoadLibrary/GetProcAddress and fails unless those contracts are present. */
    for(;pe_span(at,32,end);at+=32) {
        const uint8_t *d=m->base+at;unsigned nonzero=0;
        for(unsigned i=0;i<32;i++)nonzero|=d[i];if(!nonzero)return 0;
        if(pe_u32(d)!=1)return "unsupported delay-import VA/attributes";
        const char *name=image_string(m,pe_u32(d+4));if(!name || !dll_name(name))return "invalid delay DLL name";
        uint32_t handle=pe_u32(d+8),iat=pe_u32(d+12),names=pe_u32(d+16);
        if(!handle || (handle&7) || !pe_span(handle,8,m->image.size) ||
            !(pe_page_access(&m->image,handle&~4095u)&PE_W) || !iat || !names || ((iat|names)&7))return "invalid delay-import tables";
        for(uint64_t k=0;;k+=8) {
            if(!pe_span((uint64_t)iat+k,8,m->image.size) || !pe_span((uint64_t)names+k,8,m->image.size))return "unterminated delay thunks";
            uint64_t v=pe_u64(m->base+names+k);if(!v)break;
            if(v>>63) {if(v&0x7fffffffffff0000ull)return "invalid delay ordinal";}
            else if(!pe_span(v,3,m->image.size) || !image_string(m,v+2))return "invalid delay import name";
            uint64_t target=pe_u64(m->base+iat+k);
            if(target<(uintptr_t)m->base || !pe_span(target-(uintptr_t)m->base,1,m->image.size) ||
                !(pe_page_access(&m->image,(uint32_t)(target-(uintptr_t)m->base)&~4095u)&PE_X))return "invalid linker delay thunk";
            for(unsigned j=20;j<=24;j+=4) {
                uint32_t optional=pe_u32(d+j);
                if(optional && !pe_span((uint64_t)optional+k,8,m->image.size))return "invalid delay bound/unload table";
            }
        }
    }
    return "unterminated delay descriptors";
}
static module *load(const char *path,int expect_dll){return load_source(path,expect_dll,-1);}
static module *load_source(const char *path,int expect_dll,int source_fd) {
    for(unsigned i=0;i<module_count;i++)if(modules[i].base && wr_equal(modules[i].path,path,1)){
        if(modules[i].state<2 || modules[i].references==UINT32_MAX){fail("module unavailable/refs exhausted");return 0;}
        modules[i].references++;return &modules[i];
    }
    if(module_count>=WR_MAX_MODULES || wr_len(path)>=512) {fail("module/path budget exceeded");return 0;}
    uint8_t *file=0;size_t size=0;
    int read=source_fd>=0?wr_read_fd(source_fd,&file,&size):module_count==0 && main_source_fd>=0?wr_read_fd(main_source_fd,&file,&size):wr_read_file(path,&file,&size);
    if(read) {wr_log("windows: cannot read ");wr_log(path);wr_log("\n");fail("image open/read failed");return 0;}
    module *m=&modules[module_count];memset(m,0,sizeof(*m));
    const char *why=pe_parse(file,size,&m->image);
    if(why) {fail(why);goto out;}
    if(m->image.dll!=(unsigned)expect_dll) {fail("EXE/DLL kind mismatch");goto out;}
    if(m->image.size>WR_MAX_IMAGE) {fail("image exceeds runtime budget");goto out;}
    if(m->image.dirs[14][1]) {fail("managed PE runtime not implemented");goto out;}
    memcpy(m->path,path,wr_len(path)+1);
    const char *name=path;for(const char *p=path;*p;p++)if(*p=='/')name=p+1;
    if(wr_len(name)>=sizeof(m->name)) {fail("module basename too long");goto out;}
    memcpy(m->name,name,wr_len(name)+1);
    m->base=wr_map_image((void *)(uintptr_t)m->image.preferred,m->image.size);
    if(!m->base) {fail("image reservation failed");goto out;}
    memcpy(m->base,file,m->image.headers);
    for(unsigned i=0;i<m->image.count;i++) {
        pe_section *s=&m->image.sections[i];memcpy(m->base+s->rva,file+s->raw,s->raw_size);
    }
    why=pe_relocate(m->base,&m->image,(uintptr_t)m->base);
    if(why) {fail(why);goto out;}
    if(wr_tls_describe(m)) {fail("invalid PE static TLS directory");goto out;}
    why=wr_load_config(m);if(why) {fail(why);goto out;}
    why=validate_delay_imports(m);if(why) {fail(why);goto out;}
    wr_unmap(file,size);file=0;
    m->state=1;m->references=1;m->pinned=wr_native_module_name(m->name);unsigned index=module_count++;
    if(imports(m)) return 0;
    m->state=2;init_order[init_count++]=index;
    return m;
out:
    if(file)wr_unmap(file,size);
    if(m->base)wr_unmap(m->base,m->image.size);
    m->base=0;return 0;
}
static void cleanup(void) {
    for(unsigned i=init_count;i>0;i--) {
        module *m=&modules[init_order[i-1]];
        if(m->initialized) {
            m->initialized=0;
            wr_tls_callbacks(m,0);
            if(m->image.dll && m->image.entry) {
                int (WR_WIN *entry)(void *,uint32_t,void *)=(void *)(m->base+m->image.entry);
                entry(m->base,0,(void *)1); /* Process termination, not FreeLibrary. */
            }
        }
    }
    for(unsigned i=module_count;i>0;i--) {
        module *m=&modules[i-1];if(m->base)wr_unmap(m->base,m->image.size);
    }
}
static int protect_module(module *m) {
    for(uint32_t p=0;p<m->image.size;) {
        unsigned prot=pe_page_access(&m->image,p);uint32_t end=p+PE_PAGE;
        while(end<m->image.size && pe_page_access(&m->image,end)==prot)end+=PE_PAGE;
        if((prot&6)==6 || wr_protect(m->base+p,end-p,prot))return -1;p=end;
    }return 0;
}
static module *module_handle(void *handle) {
    for(unsigned i=0;i<module_count;i++)if(modules[i].base==handle && modules[i].state>=2)return &modules[i];return 0;
}
static module *module_named(const char *name) {
    const char *host=wr_api_set_host(name);if(host)name=host;
    for(unsigned i=0;i<module_count;i++)if(modules[i].base && modules[i].state>=2 && wr_equal(modules[i].name,name,1))return &modules[i];return 0;
}
static module *load_dynamic_flags(const char *name,unsigned flags) {
    module *existing=module_named(name);if(existing){if(existing->references==UINT32_MAX)return 0;existing->references++;return existing;}
    unsigned before=module_count,order=init_count,entered=0;module *m;unsigned saved_search=active_search;
    active_search=flags?flags:default_search;
    if(dll_name(name))m=dependency(&modules[0],name);
    else {
        uint16_t wide[1024];char path[1024];m=0;
        if(wr_utf16(name,wide,1024)>=0){
            int fd=wr_namespace_image(wide,0,0,path,sizeof(path));
            if(fd>=0)m=load_opened(path,fd);
        }
    }
    active_search=saved_search;
    if(!m)goto rollback;
    /* Static TLS for dynamically loaded modules needs all-thread initialization.
       Refuse it until that protocol exists; don't publish half-valid TLS. */
    for(unsigned i=before;i<module_count;i++)if(modules[i].tls.index!=UINT32_MAX)goto rollback;
    for(unsigned i=before;i<module_count;i++)if(protect_module(&modules[i]) || wr_module_ldr(&modules[i]))goto rollback;
    for(unsigned i=order;i<init_count;i++) {
        module *new_module=&modules[init_order[i]];if(new_module->initialized)continue;
        if(new_module->image.entry) {
            entered=1;
            int (WR_WIN *entry)(void *,unsigned,void *)=(void *)(new_module->base+new_module->image.entry);
            if(!entry(new_module->base,1,0))goto rollback;
        }
        new_module->initialized=1;
    }
    return m;
rollback:
    if(!entered) {
        for(unsigned i=module_count;i>before;i--){module *dead=&modules[i-1];wr_module_unlink(dead);if(dead->base)wr_unmap(dead->base,dead->image.size);memset(dead,0,sizeof(*dead));}
        module_count=before;init_count=order;return 0;
    }
    /* If a constructor entered the graph, its loader links/callback state are
       observable. Keep an explicit failed module alive rather than free code
       that a constructor might have referenced. Never return its handle. */
    for(unsigned i=before;i<module_count;i++)modules[i].state=0;
    return 0;
}
static module *load_dynamic(const char *name){return load_dynamic_flags(name,0);}
static int WR_WIN SetDefaultDllDirectories(unsigned flags){
    if(!flags || (flags&~0x1e00u)){wr_current_teb()->last_error=87;return 0;}
    wr_loader_lock();default_search=flags;wr_loader_unlock();return 1;
}
static void *WR_WIN GetModuleHandleA(const char *name) {
    char normalized[1024];if(name){if(library_name(name,normalized)){wr_current_teb()->last_error=87;return 0;}name=normalized;}
    wr_loader_lock();module *m=name?module_named(name):&modules[0];
    if(!m && name && wr_native_module_name(name)){m=load_dynamic(name);if(m)m->pinned=1;}
    wr_loader_unlock();if(!m)wr_current_teb()->last_error=126;return m?m->base:0;
}
static void *WR_WIN GetModuleHandleW(const uint16_t *name) {
    char utf8[512];if(name && wr_utf8(name,utf8,sizeof(utf8))){wr_current_teb()->last_error=87;return 0;}
    return GetModuleHandleA(name?utf8:0);
}
static void *WR_WIN GetProcAddress(void *handle,const char *name) {
    wr_loader_lock();module *m=module_handle(handle);void *result=0;
    if(m)result=symbol(m,(uintptr_t)name>65535?name:0,(uintptr_t)name<=65535?(uint32_t)(uintptr_t)name:0,0);
    if(result)wr_cfg_register(result);
    static unsigned misses;
    if(!result && misses++<64) {
        wr_log("windows-dynamic: GetProcAddress ");wr_log(m?m->name:"invalid-module");wr_log("!");
        if((uintptr_t)name>65535)wr_log(name);else log_hex((uintptr_t)name);
        wr_log(" caller=");log_hex((uintptr_t)__builtin_return_address(0));wr_log(" error=");log_hex(m?127:6);wr_log("\n");
    }
    wr_loader_unlock();if(!result)wr_current_teb()->last_error=m?127:6;return result;
}
static void *WR_WIN LoadLibraryExA(const char *name,void *file,unsigned flags) {
    static unsigned calls;unsigned trace=__atomic_fetch_add(&calls,1,__ATOMIC_RELAXED)<64;
    if(trace){wr_log("windows-dynamic: LoadLibraryEx name=");wr_log(name?name:"NULL");wr_log(" flags=");log_hex(flags);wr_log(" caller=");log_hex((uintptr_t)__builtin_return_address(0));wr_log("\n");}
    if(!name || file || (flags&~0x1e00u)){wr_current_teb()->last_error=flags?50:87;
        if(trace)wr_log("windows-dynamic: LoadLibraryEx rejected arguments/flags\n");return 0;}
    char normalized[1024];if(library_name(name,normalized)){wr_current_teb()->last_error=87;return 0;}name=normalized;
    wr_loader_lock();module *m=load_dynamic_flags(name,flags);wr_loader_unlock();if(!m)wr_current_teb()->last_error=126;
    if(trace){wr_log("windows-dynamic: LoadLibraryEx result=");log_hex(m?(uintptr_t)m->base:0);wr_log(" error=");log_hex(m?0:wr_current_teb()->last_error);wr_log("\n");}
    return m?m->base:0;
}
static void *WR_WIN LoadLibraryExW(const uint16_t *name,void *file,unsigned flags) {
    char utf8[1024];if(!name || wr_utf8(name,utf8,sizeof(utf8))){wr_current_teb()->last_error=87;return 0;}return LoadLibraryExA(utf8,file,flags);
}
static void *WR_WIN LoadLibraryA(const char *name){return LoadLibraryExA(name,0,0);}
static void *WR_WIN LoadLibraryW(const uint16_t *name){return LoadLibraryExW(name,0,0);}
static int WR_WIN FreeLibrary(void *handle) {
    wr_loader_lock();module *m=module_handle(handle);unsigned why=0;
    if(!m)why=6;
    else if(m->pinned || m->references>1){if(m->references>1)m->references--;}
    else why=50; /* Graph/TLS-safe dynamic unloading is not a no-op success. */
    wr_loader_unlock();if(why)wr_current_teb()->last_error=why;return !why;
}
static int guest_module_path(module *m,uint16_t *out,size_t cap) {
    if(m==&modules[0])return wr_utf16(wr_guest_image(),out,cap);
    if(wr_native_module_name(m->name)) {
        char system[512]="C:\\Windows\\System32\\";size_t n=wr_len(system),len=wr_len(m->name);if(n+len>=sizeof(system))return -1;memcpy(system+n,m->name,len+1);return wr_utf16(system,out,cap);
    }
    char path[512];size_t n=wr_len(wr_guest_image()),slash=0;for(size_t i=0;i<n;i++)if(wr_guest_image()[i]=='\\' || wr_guest_image()[i]=='/')slash=i+1;
    size_t name=wr_len(m->name);if(slash+name>=sizeof(path))return -1;memcpy(path,wr_guest_image(),slash);memcpy(path+slash,m->name,name+1);return wr_utf16(path,out,cap);
}
static unsigned WR_WIN GetModuleFileNameW(void *handle,uint16_t *out,unsigned capacity) {
    if(!out || !capacity){wr_current_teb()->last_error=87;return 0;}
    wr_loader_lock();module *m=handle?module_handle(handle):&modules[0];uint16_t name[512];int n=m?guest_module_path(m,name,512):-1;
    wr_loader_unlock();if(n<0){wr_current_teb()->last_error=6;return 0;}
    unsigned copy=(unsigned)n<capacity?(unsigned)n:capacity-1;memcpy(out,name,copy*2);out[copy]=0;
    if((unsigned)n>=capacity){wr_current_teb()->last_error=122;return capacity;}return (unsigned)n;
}
static unsigned WR_WIN GetModuleFileNameA(void *handle,char *out,unsigned capacity) {
    uint16_t wide[512];char bytes[2048];unsigned n=GetModuleFileNameW(handle,wide,512);if(!n || n>=512 || wr_utf8(wide,bytes,sizeof(bytes)))return 0;
    n=(unsigned)wr_len(bytes);if(!out || !capacity){wr_current_teb()->last_error=87;return 0;}
    unsigned copy=n<capacity?n:capacity-1;memcpy(out,bytes,copy);out[copy]=0;if(n>=capacity){wr_current_teb()->last_error=122;return capacity;}return n;
}
static int WR_WIN GetModuleHandleExW(unsigned flags,const uint16_t *name,void **out) {
    if(!out || (flags&~7u) || ((flags&1) && (flags&2))){wr_current_teb()->last_error=87;return 0;}*out=0;wr_loader_lock();module *m=0;
    if(flags&4){uintptr_t address=(uintptr_t)name;for(unsigned i=0;i<module_count;i++)if(address>=(uintptr_t)modules[i].base && pe_span(address-(uintptr_t)modules[i].base,1,modules[i].image.size)){m=&modules[i];break;}}
    else {char text[512];if(!name)m=&modules[0];else if(!wr_utf8(name,text,sizeof(text)))m=module_named(text);}
    if(m){if(flags&1)m->pinned=1;if(!(flags&2))m->references++;*out=m->base;}
    wr_loader_unlock();if(!m)wr_current_teb()->last_error=126;return m!=0;
}
typedef struct {void *base;uint32_t size,pad;void *entry;} module_info;
_Static_assert(sizeof(module_info)==24,"AMD64 MODULEINFO");
static int self_process(void *process) {
    if(process==(void *)-1)return 1;
    unsigned (WR_WIN *pid)(void *)=wr_process_exports("GetProcessId");unsigned id=pid(process);
    if(!id)return 0;if(id!=wr_pid()){wr_current_teb()->last_error=50;return 0;}return 1;
}
static int WR_WIN K32GetModuleInformation(void *process,void *handle,module_info *out,unsigned length) {
    if(!self_process(process))return 0;if(!out || length<sizeof(*out)){wr_current_teb()->last_error=87;return 0;}
    wr_loader_lock();module *m=module_handle(handle);module_info value={0};
    if(m)value=(module_info){m->base,m->image.size,0,m->image.entry?m->base+m->image.entry:0};
    wr_loader_unlock();if(!m){wr_current_teb()->last_error=6;return 0;}
    if(wr_memory_copy(out,&value,sizeof(value))){wr_current_teb()->last_error=998;return 0;}return 1;
}
void *wr_module_exports(const char *name) {
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(GetModuleHandleA);E(GetModuleHandleW);E(GetModuleHandleExW);E(GetProcAddress);
    E(LoadLibraryA);E(LoadLibraryW);E(LoadLibraryExA);E(LoadLibraryExW);E(FreeLibrary);E(GetModuleFileNameW);E(GetModuleFileNameA);E(K32GetModuleInformation);
    E(SetDefaultDllDirectories);
#undef E
    return 0;
}
_Noreturn void wr_finish(unsigned code) {
    unsigned tid=wr_tid(),owner=0;
    if(!__atomic_compare_exchange_n(&finishing,&owner,tid,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
        if(owner==tid)wr_exit(code); /* Recursive ExitProcess during detach. */
        /* Do not let a second caller terminate the group underneath the first
           caller's DLL detach/cleanup. Its stop request wakes this native wait. */
        for(;;)wr_futex(&finishing,0,owner,0);
    }
    /* Lifecycle diagnostics must not append bytes to a program's stdout. */
    char exit_note[]="windows-exit: code=0x00000000\n";
    for(unsigned i=0;i<8;i++)exit_note[sizeof("windows-exit: code=0x")-1+i]="0123456789abcdef"[(code>>(28-4*i))&15];
    vex_syscall1(VEX_SYS_DEBUG_LOG,(uintptr_t)exit_note);
    wr_env_stop_peers(code);wr_files_shutdown();wr_loader_lock();cleanup();wr_exit(code);
}
void wr_loader_thread_notify(unsigned reason) {
    for(unsigned k=0;k<init_count;k++) {
        unsigned i=reason==3?init_count-1-k:k;module *m=&modules[init_order[i]];
        if(m->image.dll && m->initialized) {
            wr_tls_callbacks(m,reason);
            if(m->image.entry) {int (WR_WIN *entry)(void *,unsigned,void *)=(void *)(m->base+m->image.entry);entry(m->base,reason,0);}
        }
    }
    if(!modules[0].image.dll)wr_tls_callbacks(&modules[0],reason);
}
static int run_image(void *arg) {
    module *exe=arg;
    wr_loader_lock();
    for(unsigned i=0;i<init_count;i++) {
        module *m=&modules[init_order[i]];
        if(!m->image.dll)continue;
        wr_tls_callbacks(m,1);
        if(m->image.entry) {
            wr_log("windows-dll-entry: ");wr_log(m->name);wr_log(" attach rip=");log_hex((uintptr_t)m->base+m->image.entry);wr_log("\n");
            int (WR_WIN *entry)(void *,uint32_t,void *)=(void *)(m->base+m->image.entry);
            int accepted=entry(m->base,1,(void *)1);
            wr_log("windows-dll-return: ");wr_log(m->name);wr_log(" attach result=");log_hex((uint32_t)accepted);wr_log("\n");
            if(!accepted) {wr_loader_unlock();fail("DLL_PROCESS_ATTACH refused");return -1;}
        }
        m->initialized=1;
    }
    if(!exe->image.dll)wr_tls_callbacks(exe,1);exe->initialized=1;wr_loader_unlock();
    if(wer_host)return wr_wer_execute(exe);
    wr_log("windows: imports bound, TEB/PEB/TLS initialized, entering PE (Microsoft x64)\n");
    wr_log("windows-entry: ");wr_log(exe->name);wr_log(" rip=");log_hex((uintptr_t)exe->base+exe->image.entry);wr_log("\n");
    void (WR_WIN *entry)(void)=(void *)(exe->base+exe->image.entry);entry();wr_finish(0);
}
int wr_main(uint64_t argc,char **argv) {
    if(argc==2 && wr_equal(argv[1],"--job-service",0))return wr_job_service();
    if(argc==2 && wr_equal(argv[1],"--export-census",0)) {
        for(unsigned i=0;i<sizeof(export_candidates)/sizeof(export_candidates[0]);i++) {
            const char *dll=export_candidates[i].dll,*name=export_candidates[i].name;
            wr_log("windows-export|");wr_log(dll);wr_log("|");wr_log(name);wr_log(wr_builtin(dll,name)?"|resolved\n":"|absent\n");
        }
        wr_log("windows-export-census: compiled resolver PASS\n");return 0;
    }
    if(argc<2) {wr_log("usage: kyil-windows <PE32+ EXE> [arguments]\n");return 2;}
    wr_log("windows: userspace direct PE loader / native ABI\n");
    char child_path[1024];char *child_argv[3]={argv[0],child_path,0};
    wer_host=argc==2 && wr_equal(argv[1],"--wer-service",0);
    if(wer_host || (argc==3 && wr_equal(argv[1],"--native-child",0))) {
        if((!wer_host && !wr_equal(argv[2],"8",0)) || wr_child_bootstrap(8) ||
           (wer_host && wr_wer_prepare()) || wr_path_native(wr_boot->image,child_path,sizeof(child_path),0))return 126;
        argc=2;argv=child_argv;main_source_fd=7;
    }
    /* Static dependencies use the same confined image-open path as later DLL
       loads. Establish capabilities before decoding that dependency graph;
       do not fall back to unscoped opens before the PEB happens to exist. */
    if(wr_process_init() || (!wr_boot && wr_namespace_init(argv[1]))){fail("native process namespace setup failed");goto fail;}
    module *exe=load(argv[1],wer_host);
    if(main_source_fd>=0){vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)main_source_fd);main_source_fd=-1;}
    if(!exe)goto fail;
    if(wr_env_main(argc,argv,exe)) {fail("PE process environment setup failed");goto fail;}
    /* Whole dependency graph is relocated/bound before any code is executable.
       Final page permissions include PROT_NONE for image holes. */
    for(unsigned i=0;i<module_count;i++) {
        module *m=&modules[i];
        for(uint32_t p=0;p<m->image.size;) {
            unsigned prot=pe_page_access(&m->image,p);uint32_t end=p+PE_PAGE;
            while(end<m->image.size && pe_page_access(&m->image,end)==prot)end+=PE_PAGE;
            if((prot&6)==6 || wr_protect(m->base+p,end-p,prot)) {fail("final image protections failed");goto fail;}
            p=end;
        }
    }
    if(wr_env_enter(run_image,exe)) {fail(error?error:"PE stack setup failed");goto fail;}
    wr_finish(0);
fail:
    wr_log("windows: load failed: ");wr_log(error?error:"unknown failure");wr_log("\n");
    cleanup();return 126;
}
