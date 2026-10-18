/* The Windows runtime uses ONLY the tagged structured KyIlOS ABI. No Linux
 * syscalls, host libc, PE conversion, or kernel Win32 dispatch. */
#include <unos/vex_abi.h>
#include "vex_syscall.h"
#include "runtime.h"
void *memcpy(void *d,const void *s,size_t n) { uint8_t *a=d; const uint8_t *b=s; while(n--) *a++=*b++; return d; }
void *memset(void *d,int c,size_t n) { uint8_t *a=d; while(n--) *a++=(uint8_t)c; return d; }
size_t wr_len(const char *s) { size_t n=0; while(s[n]) n++; return n; }
int wr_equal(const char *a,const char *b,int fold) {
    for(;;a++,b++) { unsigned x=(unsigned char)*a,y=(unsigned char)*b;
        if(fold) { if(x>='A'&&x<='Z') x+=32; if(y>='A'&&y<='Z') y+=32; }
        if(x!=y) return 0; if(!x) return 1;
    }
}
void wr_log(const char *s) {
    vex_syscall1(VEX_SYS_DEBUG_LOG,(uintptr_t)s);
    vex_fd_io_req_t r={1,(void *)s,wr_len(s)};
    (void)vex_syscall1(VEX_SYS_WRITEFD,(uintptr_t)&r);
}
void wr_trace_contract(const char *name,uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t site){
    char line[192];size_t n=0;const char *prefix="windows-contract: ";
    while(*prefix)line[n++]=*prefix++;
    for(unsigned i=0;name[i] && i<56;i++)line[n++]=name[i];
    uintptr_t values[]={a,b,c,site};
    for(unsigned j=0;j<4;j++){line[n++]=' ';line[n++]='0';line[n++]='x';
        for(unsigned i=0;i<16;i++)line[n++]="0123456789abcdef"[(values[j]>>(60-i*4))&15];}
    line[n++]='\n';line[n]=0;vex_syscall1(VEX_SYS_DEBUG_LOG,(uintptr_t)line);
}
_Noreturn void wr_exit(unsigned code) {
    vex_syscall1(VEX_SYS_PROCESS_EXIT_GROUP,code);
    __builtin_trap();
}
void *wr_map(void *hint,size_t n,int prot) {
    if(!n || n>SIZE_MAX-4095 || (prot&6)==6) return 0;
    vex_mmap_req_t r={hint,n,prot,MAP_PRIVATE|MAP_ANONYMOUS,-1,0};
    uint64_t p=vex_syscall1(VEX_SYS_MMAP,(uintptr_t)&r);
    return p==(uint64_t)-1?0:(void *)(uintptr_t)p;
}
int wr_protect(void *p,size_t n,int prot) {
    if(!n || (prot&6)==6) return -1;
    vex_memory_req_t r={p,n,prot};
    return (int)vex_syscall1(VEX_SYS_MPROTECT,(uintptr_t)&r);
}
int wr_unmap(void *p,size_t n) {
    vex_memory_req_t r={p,n,0}; return (int)vex_syscall1(VEX_SYS_MUNMAP,(uintptr_t)&r);
}
void *wr_map_image(void *hint,size_t n) {
    if(!n || n>SIZE_MAX-65536)return 0;
    n=(n+4095)&~(size_t)4095;
    void *p=wr_map(hint,n,PE_R|PE_W);
    if(!p || !((uintptr_t)p&65535))return p;
    if(wr_unmap(p,n))return 0;
    uint8_t *b=wr_map(0,n+65536,PE_R|PE_W);if(!b)return 0;
    uint8_t *a=(uint8_t *)(((uintptr_t)b+65535)&~(uintptr_t)65535);
    size_t head=(size_t)(a-b),tail=65536-head;
    if(head && wr_unmap(b,head)) {wr_unmap(b,n+65536);return 0;}
    if(tail && wr_unmap(a+n,tail)) {wr_unmap(a,n+tail);return 0;}
    return a;
}
int64_t wr_write(int fd,const void *p,size_t n) {
    vex_fd_io_req_t r={fd,(void *)p,n}; return (int64_t)vex_syscall1(VEX_SYS_WRITEFD,(uintptr_t)&r);
}
uint64_t wr_pid(void) { return vex_syscall0(VEX_SYS_GETPID); }
uint64_t wr_ticks(void) {
    struct timespec ts={0}; vex_clock_gettime_req_t r={1,&ts};
    if(vex_syscall1(VEX_SYS_CLOCK_GETTIME,(uintptr_t)&r)) wr_exit(125);
    return (uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;
}
int wr_read_file(const char *path,uint8_t **data,size_t *size) {
    vex_open_ex_req_t o={VEX_AT_FDCWD,O_RDONLY|O_CLOEXEC,0,0,path};
    extern int wr_namespace_open_native(const char *,unsigned) __attribute__((weak));
    vex_fs_root_req_t policy={.size=sizeof(policy),.operation=VEX_FS_ROOT_POLICY};
    int64_t fd=vex_syscall1(VEX_SYS_FS_ROOT,(uintptr_t)&policy)==1?
        (wr_namespace_open_native?wr_namespace_open_native(path,O_RDONLY|O_CLOEXEC|VEX_FS_OPEN_REGULAR):-13):
        (int64_t)vex_syscall1(VEX_SYS_OPEN_EX,(uintptr_t)&o);
    if(fd<0) return -1;
    int result=wr_read_fd((int)fd,data,size);
    if(vex_syscall1(VEX_SYS_CLOSE,(uint64_t)fd))return -1;return result;
}
int wr_read_fd(int fd,uint8_t **data,size_t *size) {
    vex_stat_ex_t st={0}; vex_stat_ex_req_t sr={VEX_STAT_EX_FD,(int)fd,0,&st};
    int ok=-1; uint8_t *p=0;
    if(vex_syscall1(VEX_SYS_STAT_EX,(uintptr_t)&sr) || st.size<64 ||
       (uint64_t)st.size>WR_MAX_IMAGE || (st.mode&0170000)!=0100000) goto done;
    /* Existing native private, lazy file backing: retain its lifetime after
       close. The loader never writes to this view or to filesystem bytes. */
    vex_mmap_req_t map={0,(size_t)st.size,PE_R,MAP_PRIVATE,(int)fd,0};
    uint64_t address=vex_syscall1(VEX_SYS_MMAP,(uintptr_t)&map);
    if(address==(uint64_t)-1)goto done;
    p=(void *)(uintptr_t)address;
    *data=p; *size=(size_t)st.size; ok=0;
done:
    if(ok && p) wr_unmap(p,(size_t)st.size);
    return ok;
}
int wr_futex(uint32_t *p,unsigned op,unsigned value,uint64_t timeout) {
    vex_futex_req_t r={(uintptr_t)p,op,value,0,0,timeout};
    return (int)vex_syscall1(VEX_SYS_FUTEX,(uintptr_t)&r);
}
void wr_lock(wr_mutex *m) {
    while(__atomic_exchange_n(&m->held,1,__ATOMIC_ACQUIRE))wr_futex(&m->held,0,1,0);
}
void wr_unlock(wr_mutex *m) {
    __atomic_store_n(&m->held,0,__ATOMIC_RELEASE);wr_futex(&m->held,1,1,0);
}
uint32_t wr_tid(void) {return (uint32_t)vex_syscall0(VEX_SYS_THREAD_SELF);}
int wr_set_gs(void *p) {
    vex_thread_context_req_t r={.size=sizeof(r),.operation=1,.mask=2,.gs_base=(uintptr_t)p};
    return (int)vex_syscall1(VEX_SYS_THREAD_CONTEXT,(uintptr_t)&r);
}
int wr_context(uint64_t *low,uint64_t *high,uint32_t *cpu) {
    vex_thread_context_req_t r={.size=sizeof(r)};
    int rc=(int)vex_syscall1(VEX_SYS_THREAD_CONTEXT,(uintptr_t)&r);
    if(!rc) {if(low)*low=r.stack_low;if(high)*high=r.stack_high;if(cpu)*cpu=r.cpu;}return rc;
}
int wr_start_thread(void (*entry)(void *),void *arg,void *top,uint32_t *tid,uint32_t *alive) {
    return wr_start_thread_flags(entry,arg,top,tid,alive,0);
}
int wr_start_thread_flags(void (*entry)(void *),void *arg,void *top,uint32_t *tid,uint32_t *alive,unsigned flags) {
    vex_thread_start_req_t r={.size=sizeof(r),.flags=flags,.entry=(uintptr_t)entry,.arg=(uintptr_t)arg,
        .stack_top=(uintptr_t)top,.tid_out=(uintptr_t)tid,.clear_on_exit=(uintptr_t)alive};
    return (int)vex_syscall1(VEX_SYS_THREAD_START,(uintptr_t)&r);
}
int wr_stop_thread(uint32_t tid,unsigned status) {
    vex_thread_stop_req_t r={tid,(int32_t)status};return (int)vex_syscall1(VEX_SYS_THREAD_STOP,(uintptr_t)&r);
}
_Noreturn void wr_end_thread(void *stack,size_t size,unsigned code) {
    vex_thread_exit_unmap_req_t r={(uintptr_t)stack,size,(int32_t)code,0};
    vex_syscall1(VEX_SYS_THREAD_EXIT_UNMAP,(uintptr_t)&r);wr_exit(125);
}
void wr_yield(void) {vex_syscall0(VEX_SYS_THREAD_YIELD);}
int wr_random(void *p,size_t n) {
    vex_getrandom_req_t r={p,n,0};return vex_syscall1(VEX_SYS_GETRANDOM,(uintptr_t)&r)==n?0:-1;
}
void *wr_native_alloc(size_t n) {return (void *)(uintptr_t)vex_syscall1(VEX_SYS_HEAP_ALLOC,n);}
int wr_native_free(void *p) {
    vex_heap_req_t r={.ptr=p};return (int)vex_syscall1(VEX_SYS_HEAP_FREE,(uintptr_t)&r);
}
