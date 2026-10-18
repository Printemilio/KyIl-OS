#define _GNU_SOURCE
#define VEX_HOSTED_BUILD 1
#include <unos/vex_abi.h>
#include <vex_syscall.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>

static volatile uintptr_t resume_ip, expected_address;
static volatile sig_atomic_t faults, nx_fault;
static void require(int yes, const char *what) {
    if (!yes) { fprintf(stderr,"JIT alias regression failed: %s\n",what); exit(1); }
}
static void fault(int sig, siginfo_t *info, void *context) {
    if (sig != SIGSEGV || !resume_ip || (uintptr_t)info->si_addr != expected_address)
        _exit(2);
    ucontext_t *uc = context;
    uc->uc_mcontext.gregs[REG_RIP] = resume_ip;
    if (nx_fault) uc->uc_mcontext.gregs[REG_RSP] += 8;
    ++faults;
}
static uint64_t free_pages(void) {
    vex_bulk_result_t result = {0};
    vex_bulk_req_t req = { VEX_BULK_MEMORY, &result };
    require(!vex_syscall1(VEX_SYS_BULK,(uintptr_t)&req),"bulk PMM");
    return result.free_pages;
}
static void *patch_thread(void *arg) {
    unsigned char *rw = arg;
    /* Exercise a compiler thread writing code subsequently fetched by another
       thread. Both addresses remain stable, with no RWX protection interval. */
    const unsigned char code[] = {0xb8,73,0,0,0,0xc3};
    for (unsigned i=0;i<sizeof(code);++i) rw[i]=code[i];
    return NULL;
}
int main(void) {
    struct sigaction action = {0};
    action.sa_sigaction = fault;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    require(!sigaction(SIGSEGV,&action,NULL),"fault handler");
    for (unsigned cycle=0; cycle<16; ++cycle) {
        uint64_t before = free_pages();
        vex_jit_arena_create_req_t arena = {0};
        arena.size = (size_t)1<<30;
        arena.quota_bytes = 8192;
        require(!vex_syscall1(VEX_SYS_JIT_ARENA_CREATE,(uintptr_t)&arena),"lazy 1 GiB arena");
        require(free_pages()+256>before,"reservation must not commit 1 GiB");
        unsigned char *rw=arena.rw_base,*rx=arena.rx_base;
        pthread_t thread;
        require(!pthread_create(&thread,NULL,patch_thread,rw),"compiler thread");
        require(!pthread_join(thread,NULL),"compiler completion");
        require(((int(*)(void))rx)()==73,"RW generation / RX execution");
        *(uint32_t*)(rw+1)=91;
        require(((int(*)(void))rx)()==91,"repatch through RW");
        expected_address=(uintptr_t)rx; nx_fault=0; resume_ip=0;
        __asm__ volatile("leaq 1f(%%rip),%%rax; movq %%rax,%1; movb $0x90,(%0); 1:"
            : : "r"(rx),"m"(resume_ip) : "rax","memory");
        require(resume_ip && (unsigned)faults==cycle*2+1,"RX write must fault");
        expected_address=(uintptr_t)rw; nx_fault=1;
        __asm__ volatile("leaq 1f(%%rip),%%rax; movq %%rax,%1; call *%0; 1:"
            : : "r"(rw),"m"(resume_ip) : "rax","rcx","rdx","rsi","rdi","r8","r9","r10","r11","memory");
        require((unsigned)faults==cycle*2+2,"RW execution must fault");
        resume_ip=0; nx_fault=0;
        vex_memory_req_t protection = {rw,4096,PROT_READ|PROT_WRITE|PROT_EXEC};
        require(vex_syscall1(VEX_SYS_MPROTECT,(uintptr_t)&protection)!=0,"native W+X denied");
        require(mmap(NULL,4096,PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0)==MAP_FAILED
            && errno==EACCES,"Linux RWX still denied");
        vex_memory_req_t discard = {rx,4096,MADV_DONTNEED};
        require(!vex_syscall1(VEX_SYS_MADVISE,(uintptr_t)&discard),"paired decommit");
        require(rw[0]==0 && rx[0]==0,"both aliases rematerialize zero backing");
        patch_thread(rw);
        /* A fork retains legitimate references: parent destruction must not
           invalidate the child's executable alias. */
        int channel[2]; require(!pipe(channel),"fork handshake pipe");
        pid_t child=fork(); require(child>=0,"fork JIT backing");
        if (!child) {
            close(channel[1]); unsigned char token;
            if (read(channel[0],&token,1)!=1 || ((int(*)(void))rx)()!=73) _exit(3);
            _exit(0);
        }
        close(channel[0]);
        vex_jit_arena_destroy_req_t destroy = {rx,arena.arena_id,0};
        require(!vex_syscall1(VEX_SYS_JIT_ARENA_DESTROY,(uintptr_t)&destroy),"destroy both parent aliases");
        require(write(channel[1],"x",1)==1,"release fork child"); close(channel[1]);
        int status; require(waitpid(child,&status,0)==child && status==0,"child RX survives parent destroy");
        require(vex_syscall1(VEX_SYS_JIT_ARENA_DESTROY,(uintptr_t)&destroy)!=0,"stale arena rejected");
    }
    /* Physical quota is shared by the aliases. A second alias PTE must not
       charge another page; the next backing page must actually be refused. */
    vex_jit_arena_create_req_t limited={0};limited.size=3*4096;limited.quota_bytes=4096;
    require(!vex_syscall1(VEX_SYS_JIT_ARENA_CREATE,(uintptr_t)&limited),"quota arena");
    unsigned char *rw=limited.rw_base,*rx=limited.rx_base;
    rw[0]=42;require(rx[0]==42,"one physical page charged across both aliases");
    expected_address=(uintptr_t)(rw+4096);resume_ip=0;
    sig_atomic_t previous_faults=faults;
    __asm__ volatile("leaq 1f(%%rip),%%rax; movq %%rax,%1; movb $7,(%0); 1:"
        : : "r"(rw+4096),"m"(resume_ip) : "rax","memory");
    require(faults==previous_faults+1,"new physical page exceeds quota");
    resume_ip=0;
    vex_jit_arena_destroy_req_t destroy={rx,limited.arena_id,0};
    require(!vex_syscall1(VEX_SYS_JIT_ARENA_DESTROY,(uintptr_t)&destroy),"quota arena cleanup");
    puts("JIT alias regression: 16 cycles lazy/thread/patch/W^X/decommit/fork/cleanup + physical quota passed");
    return 0;
}
