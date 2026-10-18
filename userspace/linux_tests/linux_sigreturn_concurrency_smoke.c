#define _GNU_SOURCE
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ucontext.h>
#include <sched.h>
#include <fcntl.h>
#include <stdatomic.h>
static atomic_int start,failed;
static _Thread_local unsigned delivered;
static void handler(int sig,siginfo_t *info,void *opaque) {
    ucontext_t *uc=opaque;
    const unsigned char *ip=(void *)(uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
    if(sig!=SIGSEGV || info->si_addr || ip[0]!=0x8b || ip[1]!=0x00)_exit(20);
    uc->uc_mcontext.gregs[REG_RIP]+=2;
    uc->uc_mcontext.gregs[REG_RAX]=0x1357;
    ++delivered;
}
static void *faulting(void *unused) {
    (void)unused;
    while(!atomic_load_explicit(&start,memory_order_acquire))sched_yield();
    for(unsigned i=0;i<600;i++) {
        unsigned result;
        __asm__ volatile("xor %%eax,%%eax; .byte 0x8b,0x00":"=a"(result)::"memory","cc");
        if(result!=0x1357 || delivered!=i+1)atomic_store(&failed,1);
        sched_yield();
    }
    return NULL;
}
static void *ordinary(void *unused) {
    (void)unused;int fd=open("/dev/null",O_RDWR);
    if(fd<0){atomic_store(&failed,1);return NULL;}
    while(!atomic_load_explicit(&start,memory_order_acquire))sched_yield();
    for(unsigned i=0;i<1800;i++) {
        /* Independent syscall frames suspended/resumed alongside sigreturn. */
        if(getpid()<=0 || fcntl(fd,F_GETFD)!=0 || write(fd,"x",1)!=1)atomic_store(&failed,1);
        sched_yield();
    }
    close(fd);return NULL;
}
int main(void) {
    struct sigaction action={0};action.sa_sigaction=handler;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);
    if(sigaction(SIGSEGV,&action,NULL))return 1;
    pthread_t threads[4];
    for(unsigned i=0;i<4;i++)if(pthread_create(&threads[i],NULL,i<2?faulting:ordinary,NULL))return 2;
    atomic_store_explicit(&start,1,memory_order_release);
    for(unsigned i=0;i<4;i++)if(pthread_join(threads[i],NULL))return 3;
    fprintf(stderr,"sigreturn concurrency: 1200 precise resumptions + 10800 ordinary syscalls %s\n",atomic_load(&failed)?"FAIL":"PASS");
    return atomic_load(&failed)?4:0;
}
