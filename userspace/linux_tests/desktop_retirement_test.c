#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

int main(void) {
    const size_t huge=(size_t)128*1024*1024*1024;
    volatile uint64_t *shared=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
    assert(shared!=MAP_FAILED);shared[0]=0;
    for(unsigned iteration=1;iteration<=6;++iteration) {
        pid_t pid=fork();assert(pid>=0);
        if(!pid) {
            volatile unsigned char *sparse=mmap(NULL,huge,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
            volatile unsigned char *dense=mmap(NULL,12*1024*1024,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
            if(sparse==MAP_FAILED || dense==MAP_FAILED)_exit(1);
            sparse[0]=17;sparse[huge-4096]=23;
            for(size_t i=0;i<12*1024*1024;i+=4096)dense[i]=(unsigned char)(i/4096);
            if(mprotect((void *)dense,4096,PROT_NONE))_exit(2);
            shared[0]=iteration;
            /* Leave all leaves, including PROT_NONE and shared mappings, to
             * final-owner retirement. No munmap hides a teardown defect. */
            _exit(0);
        }
        int status=-1;assert(waitpid(pid,&status,0)==pid);
        assert(WIFEXITED(status) && WEXITSTATUS(status)==0);
        assert(shared[0]==iteration); /* Creator's exit cannot free shared data. */
        shared[1]=iteration*3;
    }
    assert(!munmap((void *)shared,4096));
    puts("desktop retirement: six fork/exits, sparse 128 GiB, dense leaves, PROT_NONE, shared lifetime PASS");
    return 0;
}
