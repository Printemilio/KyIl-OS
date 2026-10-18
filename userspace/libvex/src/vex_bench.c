#include "vex_bench.h"
#include "vex_syscall.h"
#include <unos/vex_abi.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
int vex_bench_format(char *out,size_t cap,const char *fmt,...) {
    va_list ap;va_start(ap,fmt);size_t n=0;
    for(size_t i=0;fmt[i];++i){
        char temp[32];const char *s=temp;size_t len=0;
        if(fmt[i]!='%'){temp[len++]=fmt[i];}
        else {
            ++i;int wide=0;if(fmt[i]=='l' && fmt[i+1]=='l'){wide=1;i+=2;}
            if(fmt[i]=='s'){s=va_arg(ap,const char*);len=strlen(s);}
            else {
                uint64_t v=wide?va_arg(ap,unsigned long long):(uint64_t)va_arg(ap,unsigned int);
                char rev[24];unsigned r=0;do{rev[r++]=(char)('0'+v%10);v/=10;}while(v);
                while(r)temp[len++]=rev[--r];
            }
        }
        for(size_t j=0;j<len;++j){if(n+1<cap)out[n]=s[j];++n;}
    }
    if(cap)out[n<cap?n:cap-1]=0;va_end(ap);return (int)n;
}
typedef struct {uint64_t begin,end,cpu,present,pages,cpu_end;unsigned phase;} row_t;
static row_t rows[1024];
static unsigned enabled,phase,used,idle_reset;
static uint64_t begin_ns,begin_cpu,present_begin,present_ns,first_ns,idle_cpu,idle_pages;
static char label[32];
static uint64_t now(int id) {
    struct timespec ts={0};vex_clock_gettime_req_t r={id,&ts};
    if((int64_t)vex_syscall1(VEX_SYS_CLOCK_GETTIME,(uintptr_t)&r)<0)return 0;
    return (uint64_t)ts.tv_sec*1000000000ull+(uint64_t)ts.tv_nsec;
}
static uint64_t pages(void) {
    vex_bulk_result_t out={0};vex_bulk_req_t r={VEX_BULK_MEMORY,&out};
    vex_syscall1(VEX_SYS_BULK,(uintptr_t)&r);return out.used_pages;
}
static void log_line(const char *s){vex_syscall1(VEX_SYS_DEBUG_LOG,(uintptr_t)s);}
void vex_bench_start(const char *name) {
    enabled=1;phase=0;used=0;first_ns=0;
    unsigned n=0;while(n<31 && name[n]){label[n]=name[n];++n;}label[n]=0;
    char s[200];vex_bench_format(s,sizeof(s),"PAINTBENCH BEGIN %s ns=%llu pages=%llu\n",label,
        (unsigned long long)now(1),(unsigned long long)pages());log_line(s);
}
void vex_bench_begin(void) {
    if(!enabled)return;begin_ns=now(1);begin_cpu=now(2);present_ns=0;
}
void vex_bench_present_notify(int after) {
    if(!enabled)return;
    if(!after)present_begin=now(1);else present_ns+=now(1)-present_begin;
}
void vex_bench_end(void) {
    if(!enabled || !begin_ns)return;
    uint64_t end=now(1),cpu=now(2),ram=pages();
    if(used<1024)rows[used++]=(row_t){begin_ns,end,cpu-begin_cpu,present_ns,ram,cpu,phase};
    begin_ns=0;
    if(idle_reset || phase==1){first_ns=end;idle_cpu=cpu;idle_pages=ram;idle_reset=0;}
    if(!first_ns){
        first_ns=end;idle_cpu=cpu;idle_pages=ram;
        char s[200];vex_bench_format(s,sizeof(s),"PAINTBENCH READY %s ns=%llu cpu=%llu pages=%llu\n",label,
            (unsigned long long)end,(unsigned long long)cpu,(unsigned long long)ram);log_line(s);
    }
}
void vex_bench_tag(unsigned n){phase=n;}
void vex_bench_idle(void){idle_reset=1;phase=1;}
void vex_bench_run(void) {
    char s[220];vex_bench_format(s,sizeof(s),"PAINTBENCH IDLE %s wall_ns=%llu cpu_ns=%llu pages=%llu\n",label,
        (unsigned long long)(now(1)-first_ns),(unsigned long long)(now(2)-idle_cpu),
        (unsigned long long)idle_pages);log_line(s);
}
void vex_bench_report(unsigned count,uint64_t checksum) {
    if(!enabled)return;enabled=0;
    char s[260];
    vex_bench_format(s,sizeof(s),"PAINTBENCH SUMMARY %s count=%u checksum=%llu rows=%u\n",label,count,(unsigned long long)checksum,used);log_line(s);
    for(unsigned i=0;i<used;++i){row_t *r=&rows[i];
        vex_bench_format(s,sizeof(s),"PAINTBENCH FRAME %s %u %llu %llu %llu %llu %llu %llu\n",label,r->phase,
            (unsigned long long)r->begin,(unsigned long long)r->end,(unsigned long long)r->cpu,
            (unsigned long long)r->present,(unsigned long long)r->pages,(unsigned long long)r->cpu_end);log_line(s);
    }
    vex_bench_format(s,sizeof(s),"PAINTBENCH DONE %s\n",label);log_line(s);
}
