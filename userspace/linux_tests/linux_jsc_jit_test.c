#define _GNU_SOURCE
#define VEX_HOSTED_BUILD 1
#include <unos/vex_abi.h>
#include <vex_syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void log_line(char *line) {
    vex_syscall1(VEX_SYS_DEBUG_LOG,(uintptr_t)line);
    vex_syscall1(VEX_SYS_DEBUG_LOG,(uintptr_t)"\n");
}
int main(void) {
    for (int tier=0;tier<4;++tier) {
        int channel[2],status;
        if (pipe(channel)) return 1;
        pid_t child=fork(); if(child<0) return 2;
        if (!child) {
            close(channel[0]);
            if (dup2(channel[1],1)<0 || dup2(channel[1],2)<0) _exit(3);
            close(channel[1]);
            char *args[]={"jsc","--reportCompileTimes=true","--reportBaselineCompileTimes=true",
                tier==3 ? "--useConcurrentJIT=true" : "--useConcurrentJIT=false",
                tier ? "--useDFGJIT=true" : "--useDFGJIT=false",
                tier>=2 ? "--useFTLJIT=true" : "--useFTLJIT=false",
                "/usr/linux-tests/jsc-jit-tiers.js","--",
                tier==3 ? "concurrent" : "synchronous",NULL};
            char *env[]={"LD_LIBRARY_PATH=/lib:/usr/lib:/usr/local/lib",NULL};
            execve("/usr/linux-tests/jsc",args,env); _exit(4);
        }
        close(channel[1]);
        char line[2048],buffer[512]; size_t at=0;
        int baseline=0,dfg=0,ftl=0,checksum=0,arena=0,final_tier=0;
        ssize_t count;
        while ((count=read(channel[0],buffer,sizeof(buffer)))>0) {
            for (ssize_t i=0;i<count;++i) {
                if (buffer[i]!='\n' && at+1<sizeof(line)) { line[at++]=buffer[i]; continue; }
                line[at]=0;
                baseline |= strstr(line,"with Baseline")!=NULL;
                dfg |= strstr(line,"with DFG")!=NULL;
                ftl |= strstr(line,"with FTL")!=NULL;
                checksum |= strstr(line,"JSC JIT tiers: checksum=")!=NULL;
                arena |= strstr(line,"VexJIT: dual-alias arena")!=NULL;
                final_tier |= strstr(line,"final_tier_executed=true")!=NULL;
                log_line(line); at=0;
            }
        }
        close(channel[0]);
        if (waitpid(child,&status,0)!=child || status || count<0 || !arena || !checksum ||
            !baseline || (tier>=1 && (!dfg || !final_tier)) || (tier>=2 && !ftl)) {
            snprintf(line,sizeof(line),"JSC tier regression failed: mode=%d status=%d arena=%d baseline=%d dfg=%d ftl=%d checksum=%d final=%d",
                tier,status,arena,baseline,dfg,ftl,checksum,final_tier);
            log_line(line); return 5;
        }
        snprintf(line,sizeof(line),"JSC tier regression passed: mode=%d baseline=%d dfg=%d ftl=%d checksum=%d final=%d",
            tier,baseline,dfg,ftl,checksum,final_tier); log_line(line);
    }
    return 0;
}
