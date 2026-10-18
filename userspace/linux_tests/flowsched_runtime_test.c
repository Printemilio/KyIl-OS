#define _GNU_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <signal.h>
#include <unos/vex_abi.h>
#include "vex_syscall.h"
typedef struct shared {
    uint32_t start,stop,ready[2];
    uint64_t iterations[31];
} shared_t;
static shared_t *state;
static uint32_t local_start;
static pthread_mutex_t pi_a,pi_b;
static int pi_ready[2],pi_release[2];
static int pi_errors;
static uint32_t *pi_word;
static int pi_wait_result,pi_wait_errno,pi_owner_died;
static void *pi_raw_wait(void *unused) {
    (void)unused;pi_wait_result=(int)syscall(SYS_futex,pi_word,134,0,NULL,NULL,0);pi_wait_errno=errno;
    if(!pi_wait_result) {
        pi_owner_died=(*pi_word&0x40000000u)!=0;
        if(syscall(SYS_futex,pi_word,135,0,NULL,NULL,0))__atomic_add_fetch(&pi_errors,1,__ATOMIC_RELAXED);
    }
    return NULL;
}
static void *pi_raw_owner(void *unused) {
    (void)unused;char c='r';
    if(syscall(SYS_futex,pi_word,134,0,NULL,NULL,0))__atomic_add_fetch(&pi_errors,1,__ATOMIC_RELAXED);
    if(write(pi_ready[1],&c,1)!=1 || read(pi_release[0],&c,1)!=1)__atomic_add_fetch(&pi_errors,1,__ATOMIC_RELAXED);
    /* Deliberately exit while owning the PI resource. */
    return NULL;
}
static int pi_lifetime_test(void) {
    pthread_t owner,waiter;char token;struct timespec delay={0,30000000};
    pi_word=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(pi_word==MAP_FAILED || syscall(SYS_futex,pi_word,134,0,NULL,NULL,0) ||
       pthread_create(&waiter,NULL,pi_raw_wait,NULL))return 1;
    nanosleep(&delay,NULL);
    if(!(*pi_word&0x80000000u) || munmap(pi_word,4096))return 1;
    pthread_join(waiter,NULL);
    if(pi_wait_result!=-1 || pi_wait_errno!=EFAULT)return 1;
    if(pipe(pi_ready)||pipe(pi_release))return 1;
    pi_word=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(pi_word==MAP_FAILED || pthread_create(&owner,NULL,pi_raw_owner,NULL) ||
       read(pi_ready[0],&token,1)!=1 || pthread_create(&waiter,NULL,pi_raw_wait,NULL))return 1;
    nanosleep(&delay,NULL);
    if(!(*pi_word&0x80000000u) || write(pi_release[1],"r",1)!=1)return 1;
    pthread_join(owner,NULL);pthread_join(waiter,NULL);
    if(pi_wait_result || !pi_owner_died || *pi_word || munmap(pi_word,4096))return 1;
    for(unsigned n=0;n<2;n++){close(pi_ready[n]);close(pi_release[n]);}
    /* Kill whole processes with live owner/waiter stacks repeatedly. */
    for(unsigned cycle=0;cycle<8;cycle++) {
        int ready[2];if(pipe(ready))return 1;
        pid_t child=fork();if(child<0)return 1;
        if(!child) {
            close(ready[0]);uint32_t word=0;pi_word=&word;
            if(syscall(SYS_futex,pi_word,134,0,NULL,NULL,0) || pthread_create(&waiter,NULL,pi_raw_wait,NULL))_exit(1);
            nanosleep(&delay,NULL);
            if(!(*pi_word&0x80000000u) || write(ready[1],"r",1)!=1)_exit(2);
            struct timespec hold={30,0};nanosleep(&hold,NULL);_exit(3);
        }
        close(ready[1]);if(read(ready[0],&token,1)!=1 || kill(child,SIGKILL))return 1;
        int status=0;if(waitpid(child,&status,0)!=child || !WIFSIGNALED(status) || WTERMSIG(status)!=SIGKILL)return 1;
        close(ready[0]);
    }
    vex_syscall1(VEX_SYS_DEBUG_LOG,(uintptr_t)(pi_errors?"FlowSched PI unmap/owner-exit/kill cycles: FAILED\n":"FlowSched PI unmap/owner-exit/kill cycles: PASS\n"));
    return pi_errors!=0;
}
static void *pi_last(void *unused) {
    (void)unused;char c='c';
    if(pthread_mutex_lock(&pi_b))__atomic_add_fetch(&pi_errors,1,__ATOMIC_RELAXED);
    if(write(pi_ready[1],&c,1)!=1 || read(pi_release[0],&c,1)!=1)__atomic_add_fetch(&pi_errors,1,__ATOMIC_RELAXED);
    if(pthread_mutex_unlock(&pi_b))__atomic_add_fetch(&pi_errors,1,__ATOMIC_RELAXED);
    return NULL;
}
static void *pi_middle(void *unused) {
    (void)unused;char c='b';
    if(pthread_mutex_lock(&pi_a))__atomic_add_fetch(&pi_errors,1,__ATOMIC_RELAXED);
    if(write(pi_ready[1],&c,1)!=1)__atomic_add_fetch(&pi_errors,1,__ATOMIC_RELAXED);
    if(pthread_mutex_lock(&pi_b))__atomic_add_fetch(&pi_errors,1,__ATOMIC_RELAXED);
    if(pthread_mutex_unlock(&pi_b)||pthread_mutex_unlock(&pi_a))__atomic_add_fetch(&pi_errors,1,__ATOMIC_RELAXED);
    return NULL;
}
static void *pi_first(void *unused) {
    (void)unused;
    if(pthread_mutex_lock(&pi_a))__atomic_add_fetch(&pi_errors,1,__ATOMIC_RELAXED);
    if(pthread_mutex_unlock(&pi_a))__atomic_add_fetch(&pi_errors,1,__ATOMIC_RELAXED);
    return NULL;
}
static int pi_chain_test(void) {
    pthread_mutexattr_t attr;pthread_t a,b,c;char token;
    if(pipe(pi_ready)||pipe(pi_release)||pthread_mutexattr_init(&attr)||
       pthread_mutexattr_setprotocol(&attr,PTHREAD_PRIO_INHERIT)||
       pthread_mutex_init(&pi_a,&attr)||pthread_mutex_init(&pi_b,&attr))return 1;
    pthread_mutexattr_destroy(&attr);
    if(pthread_create(&c,NULL,pi_last,NULL)||read(pi_ready[0],&token,1)!=1 ||
       pthread_create(&b,NULL,pi_middle,NULL)||read(pi_ready[0],&token,1)!=1 ||
       pthread_create(&a,NULL,pi_first,NULL))return 1;
    /* A timer event makes the blocked chain run before releasing C. */
    struct timespec delay={0,30000000};nanosleep(&delay,NULL);
    if(write(pi_release[1],"r",1)!=1)return 1;
    pthread_join(a,NULL);pthread_join(b,NULL);pthread_join(c,NULL);
    pthread_mutex_destroy(&pi_a);pthread_mutex_destroy(&pi_b);
    for(unsigned n=0;n<2;n++){close(pi_ready[n]);close(pi_release[n]);}
    uint32_t word=0;
    if(syscall(SYS_futex,&word,134,0,NULL,NULL,0) ||
       syscall(SYS_futex,&word,136,0,NULL,NULL,0)!=-1 || errno!=EDEADLK ||
       syscall(SYS_futex,&word,135,0,NULL,NULL,0) || word)return 1;
    vex_syscall1(VEX_SYS_DEBUG_LOG,(uintptr_t)(pi_errors?"FlowSched PI mutex chain: FAILED\n":"FlowSched PI mutex chain: PASS\n"));
    return pi_errors!=0;
}
static uint64_t now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000000000ull+t.tv_nsec;}
static void *compute(void *opaque){
    unsigned index=(uintptr_t)opaque;uint64_t value=index+1,count=0;
    while(!__atomic_load_n(&local_start,__ATOMIC_ACQUIRE))
        syscall(SYS_futex,&local_start,128,0,NULL,NULL,0);
    while(!__atomic_load_n(&state->stop,__ATOMIC_ACQUIRE)) {
        for(unsigned n=0;n<4096;n++)value=value*6364136223846793005ull+1442695040888963407ull;
        ++count;__asm__ volatile(""::"r"(value):"memory");
    }
    state->iterations[index]=count;return NULL;
}
int main(int argc,char **argv){
    /* Inherit an explicitly shared open object through fork/exec. Independent
       opens are not an IPC contract and must not be used as a hidden SHM API. */
    int fd=argc>2?atoi(argv[2]):open("/tmp/flowsched-bench",O_RDWR|O_CREAT,0600);
    if(fd<0)return 1;
    if(argc==1 && ftruncate(fd,4096))return 1;
    state=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(state==MAP_FAILED){perror("flowsched mmap");return 1;}
    if(argc>1){
        int ready_fd=atoi(argv[3]),gate_fd=atoi(argv[4]);char ready='r',gate;
        if(argv[1][0]=='s'){
            if(write(ready_fd,&ready,1)!=1 || read(gate_fd,&gate,1)!=1)return 6;
            local_start=1;compute((void *)0);return 0;
        }
        pthread_t threads[30];
        for(unsigned n=0;n<30;n++)if(pthread_create(&threads[n],NULL,compute,(void *)(uintptr_t)(n+1)))return 4;
        if(write(ready_fd,&ready,1)!=1 || read(gate_fd,&gate,1)!=1)return 6;
        __atomic_store_n(&local_start,1,__ATOMIC_RELEASE);
        syscall(SYS_futex,&local_start,129,2147483647,NULL,NULL,0);
        for(unsigned n=0;n<30;n++)pthread_join(threads[n],NULL);return 0;
    }
    *state=(shared_t){0};
    /* Regression: req/rem aliasing must not turn a real sleep into yield. */
    struct timespec alias={0,10000000};uint64_t alias_start=now();
    if(nanosleep(&alias,&alias) || now()-alias_start<10000000ull)return 9;
    char descriptor[16];snprintf(descriptor,sizeof(descriptor),"%d",fd);
    int ready_pipe[2],gates[2][2];
    if(pipe(ready_pipe)||pipe(gates[0])||pipe(gates[1]))return 7;
    char ready_fd[16],gate_fd[2][16];
    snprintf(ready_fd,sizeof(ready_fd),"%d",ready_pipe[1]);
    for(unsigned n=0;n<2;n++)snprintf(gate_fd[n],sizeof(gate_fd[n]),"%d",gates[n][0]);
    pid_t single=fork();if(single<0)return 2;
    if(!single){execl("/usr/linux-tests/flowsched_runtime_test","flowsched_runtime_test","single",descriptor,ready_fd,gate_fd[0],NULL);_exit(5);}
    pid_t many=fork();if(many<0)return 3;
    if(!many){execl("/usr/linux-tests/flowsched_runtime_test","flowsched_runtime_test","many",descriptor,ready_fd,gate_fd[1],NULL);_exit(5);}
    char ready[2],gate='g';
    if(read(ready_pipe[0],ready,1)!=1 || read(ready_pipe[0],ready+1,1)!=1 ||
       write(gates[0][1],&gate,1)!=1 || write(gates[1][1],&gate,1)!=1)return 8;
    __atomic_store_n(&state->start,1,__ATOMIC_RELEASE);
    uint64_t latencies[200],start=now();
    for(unsigned n=0;n<200;n++) {
        uint64_t before=now();struct timespec interval={0,10000000};
        while(nanosleep(&interval,NULL) && errno==EINTR){}
        uint64_t elapsed=now()-before;latencies[n]=elapsed>10000000?elapsed-10000000:0;
    }
    uint64_t duration=now()-start;__atomic_store_n(&state->stop,1,__ATOMIC_RELEASE);
    int a=0,b=0;waitpid(single,&a,0);waitpid(many,&b,0);
    for(unsigned i=0;i<200;i++)for(unsigned j=i+1;j<200;j++)if(latencies[j]<latencies[i]){uint64_t v=latencies[i];latencies[i]=latencies[j];latencies[j]=v;}
    uint64_t total=0;for(unsigned n=1;n<=30;n++)total+=state->iterations[n];
    char report[384];
    snprintf(report,sizeof(report),"FlowSched runtime: duration_ns=%llu one=%llu thirty=%llu wake_late_ns p50=%llu p95=%llu p99=%llu max=%llu child_status=%d,%d\n",
        (unsigned long long)duration,(unsigned long long)state->iterations[0],(unsigned long long)total,
        (unsigned long long)latencies[99],(unsigned long long)latencies[189],(unsigned long long)latencies[197],(unsigned long long)latencies[199],a,b);
    vex_syscall1(VEX_SYS_DEBUG_LOG,(uintptr_t)report);
    /* Instruction-throughput ratio is diagnostic, not an invented CPU counter.
       Kernel flowstats measures actual received vs entitled CPU separately. */
    int failed=a || b || duration<2000000000ull || !total || !state->iterations[0];
    munmap(state,4096);close(fd);unlink("/tmp/flowsched-bench");
    return failed || pi_chain_test() || pi_lifetime_test();
}
