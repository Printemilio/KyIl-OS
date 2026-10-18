/* Native parent: observes whole-stack physical RAM, launches real applications.
 * No drawing or Paint policy is implemented here. */
#include "vex_runtime.h"
#include "vex_syscall.h"
#include "vex_bench.h"
#include <unos/vex_web_compat.h>
#include <stdio.h>
#include <string.h>
static uint64_t ns(void){struct timespec ts;clock_gettime(1,&ts);return (uint64_t)ts.tv_sec*1000000000ull+ts.tv_nsec;}
static uint64_t ram(void){vex_bulk_result_t out={0};vex_bulk_req_t r={VEX_BULK_MEMORY,&out};vex_syscall1(VEX_SYS_BULK,(uintptr_t)&r);return out.used_pages;}
static void logline(const char *s){vex_syscall1(VEX_SYS_DEBUG_LOG,(uintptr_t)s);}
static struct {uint64_t ns,pages;} samples[2048];
int main(int argc,char **argv) {
    if(argc<2)return 1;
    const int suchu=strcmp(argv[1],"suchu")==0;
    const char *path=suchu?"/usr/bin/kyil-paint":"/usr/native-tests/paint_lvgl_bench";
    int children[2]={0},count=argc>2?2:1,remaining=count,status=0;
    uint64_t base=ram(),start=ns(),peak=base,last=0;unsigned sample_count=0;char line[240];
    vex_bench_format(line,sizeof(line),"PAINTBENCH RUNNER %s instances=%d ns=%llu baseline_pages=%llu\n",argv[1],count,(unsigned long long)start,(unsigned long long)base);logline(line);
    for(int i=0;i<count;++i){
        int pid=fork();if(pid<0)return 2;
        if(pid==0){char *args[]={(char*)path,"--bench",NULL};char *env[]={"HOME=/home/root","TMPDIR=/tmp",NULL};execve(path,args,env);return 3;}
        children[i]=pid;
        /* Two instances are both real windows/processes. Offset startup to
         * measure the stable first instance before creating the second. */
        if(count==2 && i==0)usleep(3000000);
    }
    while(remaining){
        uint64_t current=ram(),now=ns();if(current>peak)peak=current;
        if(now-last>=250000000ull){
            if(sample_count<2048){samples[sample_count].ns=now;samples[sample_count++].pages=current;}last=now;
        }
        for(int i=0;i<count;++i)if(children[i] && waitpid(children[i],&status,WNOHANG)>0){children[i]=0;--remaining;}
        usleep(20000);
    }
    for(unsigned i=0;i<sample_count;++i){
        vex_bench_format(line,sizeof(line),"PAINTBENCH RAM %s ns=%llu pages=%llu\n",argv[1],
            (unsigned long long)samples[i].ns,(unsigned long long)samples[i].pages);logline(line);
    }
    vex_bench_format(line,sizeof(line),"PAINTBENCH EXIT %s peak_pages=%llu after_pages=%llu status=%d\n",argv[1],(unsigned long long)peak,(unsigned long long)ram(),status);logline(line);
    return status!=0;
}
