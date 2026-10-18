#include "windows.h"
#include "process.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
typedef struct {uint16_t year,month,weekday,day,hour,minute,second,millisecond;} system_time;
static int clock_native(unsigned id,struct timespec *out) {
    vex_clock_gettime_req_t r={id,out};return (int)vex_syscall1(VEX_SYS_CLOCK_GETTIME,(uintptr_t)&r);
}
static int WR_WIN QueryPerformanceCounter(int64_t *out) {
    if(!out){wr_current_teb()->last_error=87;return 0;}struct timespec t;
    if(clock_native(1,&t)){wr_current_teb()->last_error=1117;return 0;}*out=(int64_t)((uint64_t)t.tv_sec*1000000000ull+(uint64_t)t.tv_nsec);return 1;
}
static int WR_WIN QueryPerformanceFrequency(int64_t *out) {
    if(!out){wr_current_teb()->last_error=87;return 0;}*out=1000000000ll;return 1; /* actual nanosecond counter units, not a guessed TSC frequency */
}
static void WR_WIN GetSystemTimeAsFileTime(uint64_t *out) {
    struct timespec t;if(!out || clock_native(0,&t)){wr_current_teb()->last_error=1117;return;}
    *out=((uint64_t)t.tv_sec+11644473600ull)*10000000ull+(uint64_t)t.tv_nsec/100;
}
static void WR_WIN GetSystemTimePreciseAsFileTime(uint64_t *out){GetSystemTimeAsFileTime(out);}
static int WR_WIN FileTimeToSystemTime(const uint64_t *file,system_time *out) {
    if(!file || !out || (*file>>63)){wr_current_teb()->last_error=87;return 0;}
    uint64_t ticks=*file;int64_t seconds=(int64_t)(ticks/10000000ull)-11644473600ll;
    int64_t days=seconds/86400,rem=seconds%86400;if(rem<0){rem+=86400;days--;}
    int64_t z=days+719468,era=(z>=0?z:z-146096)/146097;unsigned doe=(unsigned)(z-era*146097);
    unsigned yoe=(doe-doe/1460+doe/36524-doe/146096)/365;int64_t year=(int64_t)yoe+era*400;
    unsigned doy=doe-(365*yoe+yoe/4-yoe/100),mp=(5*doy+2)/153;
    unsigned day=doy-(153*mp+2)/5+1,month=mp+(mp<10?3:-9);year+=month<=2;
    if(year<1601 || year>30827){wr_current_teb()->last_error=87;return 0;}
    int64_t weekday=(days+4)%7;if(weekday<0)weekday+=7;
    *out=(system_time){(uint16_t)year,(uint16_t)month,(uint16_t)weekday,(uint16_t)day,(uint16_t)(rem/3600),(uint16_t)(rem/60%60),(uint16_t)(rem%60),(uint16_t)(ticks/10000%1000)};return 1;
}
static void WR_WIN GetSystemTime(system_time *out){uint64_t time;GetSystemTimeAsFileTime(&time);FileTimeToSystemTime(&time,out);}
static int WR_WIN IsWow64Process(void *handle,int *out) {
    if(!out){wr_current_teb()->last_error=87;return 0;}
    void *raw=wr_process_exports("GetProcessId");unsigned (WR_WIN *id)(void *)=raw;
    if(!id(handle))return 0;*out=0;return 1; /* all supported guests are native AMD64, no WOW64 engine */
}
static int WR_WIN IsDebuggerPresent(void){return wr_process_peb.reserved[2]!=0;}
static int WR_WIN CheckRemoteDebuggerPresent(void *handle,int *out) {
    if(!out){wr_current_teb()->last_error=87;return 0;}
    void *raw=wr_process_exports("GetProcessId");unsigned (WR_WIN *id)(void *)=raw;
    if(id(handle)!=(unsigned)wr_pid()){wr_current_teb()->last_error=50;return 0;}*out=IsDebuggerPresent();return 1;
}
static void WR_WIN DebugBreak(void){__asm__ volatile("int3");}
static void WR_WIN OutputDebugStringW(const uint16_t *text) {
    char bytes[1024];while(text && *text){uint16_t part[192];unsigned n=0;while(text[n] && n<190){part[n]=text[n];n++;}
        if(n && part[n-1]>=0xd800 && part[n-1]<=0xdbff)n--;part[n]=0;
        if(!n || wr_utf8(part,bytes,sizeof(bytes))){wr_current_teb()->last_error=1113;return;}wr_log(bytes);text+=n;}
}
static uint64_t pointer_secret;static wr_mutex secret_gate;
static void *WR_WIN EncodePointer(void *p){wr_lock(&secret_gate);if(!pointer_secret && wr_random(&pointer_secret,sizeof(pointer_secret))) {wr_unlock(&secret_gate);wr_current_teb()->last_error=8;return 0;}uint64_t key=pointer_secret;wr_unlock(&secret_gate);return (void *)((uintptr_t)p^key);}
static void *WR_WIN DecodePointer(void *p){return EncodePointer(p);}
typedef struct {uint16_t architecture,reserved;uint32_t page;void *minimum,*maximum;uint64_t mask;
    uint32_t processors,type,granularity;uint16_t level,revision;} system_info;
_Static_assert(sizeof(system_info)==48,"AMD64 SYSTEM_INFO");
static int machine(vex_memory_machine_info_t *out) {
    vex_memory_view_req_t r={.size=sizeof(r),.operation=VEX_MEMORY_VIEW_MACHINE,.out=out};
    return (int)vex_syscall1(VEX_SYS_MEMORY_VIEW,(uintptr_t)&r);
}
static void WR_WIN GetSystemInfo(system_info *out) {
    vex_memory_machine_info_t native;if(machine(&native))wr_exit(0xc00000bbu);
    /* Processor groups above 64 aren't exposed by this personality yet. Never
     * fabricate a 64-bit affinity mask for a larger native scheduling domain. */
    if(!native.online_cpus || native.online_cpus>64)wr_exit(0xc00000bbu);
    uint32_t a=1,b,c,d;__asm__ volatile("cpuid":"+a"(a),"=b"(b),"=c"(c),"=d"(d));
    unsigned family=(a>>8)&15,model=(a>>4)&15;
    if(family==6 || family==15)model|=((a>>16)&15)<<4;
    if(family==15)family+=(a>>20)&255;
    system_info value={9,0,(uint32_t)native.page_size,(void *)65536,(void *)(uintptr_t)(native.user_limit-1),
        native.online_cpus==64?UINT64_MAX:((1ull<<native.online_cpus)-1),native.online_cpus,8664,65536,(uint16_t)family,(uint16_t)((model<<8)|(a&15))};
    if(wr_memory_copy(out,&value,sizeof(value)))wr_exit(0xc0000005u);
}
static void WR_WIN GetNativeSystemInfo(system_info *out){GetSystemInfo(out);}
typedef struct {uint64_t mask;uint32_t relationship,pad;uint8_t data[16];} logical_info;
static int WR_WIN GetLogicalProcessorInformation(logical_info *out,unsigned *bytes) {
    unsigned cap;if(!bytes || wr_memory_copy(&cap,bytes,4)){wr_current_teb()->last_error=87;return 0;}
    vex_memory_machine_info_t machine_info;if(machine(&machine_info) || machine_info.online_cpus>64 || !machine_info.online_cpus){wr_current_teb()->last_error=50;return 0;}
    unsigned count=machine_info.online_cpus;size_t max=(size_t)count*11+1;
    logical_info *records=wr_native_alloc(max*sizeof(*records));vex_cpu_topology_info_t *cpus=wr_native_alloc((size_t)count*sizeof(*cpus));
    if(!records || !cpus){if(records)wr_native_free(records);if(cpus)wr_native_free(cpus);wr_current_teb()->last_error=8;return 0;}
    unsigned n=0,error=0;
    for(unsigned i=0;i<count;i++) {
        vex_memory_view_req_t r={.size=sizeof(r),.operation=VEX_MEMORY_VIEW_CPU,.address=i,.out=&cpus[i]};
        if(vex_syscall1(VEX_SYS_MEMORY_VIEW,(uintptr_t)&r) || !cpus[i].online || !cpus[i].valid){error=50;break;}
    }
    for(unsigned i=0;!error && i<count;i++) {
        for(unsigned kind=0;kind<2;kind++) {
            unsigned first=1;for(unsigned j=0;j<i;j++)if(cpus[j].package==cpus[i].package && (kind || cpus[j].core==cpus[i].core))first=0;
            if(!first)continue;logical_info *record=&records[n++];memset(record,0,sizeof(*record));record->relationship=kind?3:0;
            unsigned members=0;for(unsigned j=0;j<count;j++)if(cpus[j].package==cpus[i].package && (kind || cpus[j].core==cpus[i].core)){record->mask|=1ull<<j;members++;}
            if(!kind && members>1)record->data[0]=1;
        }
        for(unsigned k=0;k<cpus[i].cache_count;k++) {
            const vex_cpu_cache_info_t *cache=&cpus[i].caches[k];unsigned first=1;
            for(unsigned j=0;j<i;j++)for(unsigned q=0;q<cpus[j].cache_count;q++) {
                const vex_cpu_cache_info_t *prior=&cpus[j].caches[q];
                if(prior->level==cache->level && prior->type==cache->type && prior->sharing_domain==cache->sharing_domain)first=0;
            }
            if(!first)continue;if(cache->bytes>UINT32_MAX || cache->line_bytes>65535){error=50;break;}
            logical_info *record=&records[n++];memset(record,0,sizeof(*record));record->relationship=2;
            record->data[0]=(uint8_t)cache->level;record->data[1]=cache->ways>254?255:(uint8_t)cache->ways;
            *(uint16_t *)(record->data+2)=(uint16_t)cache->line_bytes;*(uint32_t *)(record->data+4)=(uint32_t)cache->bytes;
            *(uint32_t *)(record->data+8)=cache->type==1?2:cache->type==2?1:0;
            for(unsigned j=0;j<count;j++)for(unsigned q=0;q<cpus[j].cache_count;q++) {
                const vex_cpu_cache_info_t *other=&cpus[j].caches[q];
                if(other->level==cache->level && other->type==cache->type && other->sharing_domain==cache->sharing_domain)record->mask|=1ull<<j;
            }
        }
    }
    unsigned required=n*sizeof(*records);
    if(!error && wr_memory_copy(bytes,&required,4))error=998;
    if(!error && (cap<required || !out))error=122;
    if(!error && wr_memory_copy(out,records,required))error=998;
    wr_native_free(records);wr_native_free(cpus);if(error)wr_current_teb()->last_error=error;return !error;
}
static int WR_WIN IsProcessorFeaturePresent(unsigned feature) {
    vex_memory_machine_info_t native;if(machine(&native))return 0;
    uint32_t a=1,b,c,d;__asm__ volatile("cpuid":"+a"(a),"=b"(b),"=c"(c),"=d"(d));
    switch(feature) {
    case 2:return (d&(1u<<8))!=0; /* CMPXCHG8B */
    case 3:return (native.flags&VEX_MEMORY_MACHINE_FXSAVE) && (d&(1u<<23));
    case 6:return (native.flags&VEX_MEMORY_MACHINE_FXSAVE) && (d&(1u<<25));
    case 9:return 1; /* Native long-mode paging is PAE. */
    case 10:return (native.flags&VEX_MEMORY_MACHINE_FXSAVE) && (d&(1u<<26));
    case 12:return (native.flags&VEX_MEMORY_MACHINE_NX)!=0;
    case 13:return (native.flags&VEX_MEMORY_MACHINE_FXSAVE) && (c&1);
    case 14:return (c&(1u<<13))!=0; /* CMPXCHG16B, optional on older x64. */
    default:return 0;
    }
}
static int WR_WIN GetComputerNameExW(unsigned format,uint16_t *out,unsigned *capacity) {
    unsigned cap;if(format>7 || !capacity || wr_memory_copy(&cap,capacity,4)){wr_current_teb()->last_error=87;return 0;}
    if(!out && cap){wr_current_teb()->last_error=87;return 0;}
    struct utsname facts;if(vex_syscall1(VEX_SYS_UNAME,(uintptr_t)&facts)){wr_current_teb()->last_error=1117;return 0;}
    unsigned type=format&3;char name[65];size_t full=wr_len(facts.nodename),first=0;
    while(first<full && facts.nodename[first]!='.')first++;
    size_t start=type==2?(first<full?first+1:full):0,length=type==2?full-start:(type==3?full:first);
    if(type==0 && length>15)length=15;
    for(size_t i=0;i<length;i++){unsigned c=(uint8_t)facts.nodename[start+i];if(type==0 && c>='a' && c<='z')c-=32;name[i]=(char)c;}name[length]=0;
    uint16_t text[65];int units=wr_utf16(name,text,65);if(units<0){wr_current_teb()->last_error=1113;return 0;}
    unsigned required=(unsigned)units+1;
    if(cap<required){if(wr_memory_copy(capacity,&required,4)){wr_current_teb()->last_error=998;return 0;}wr_current_teb()->last_error=234;return 0;}
    unsigned copied=(unsigned)units;
    if(wr_memory_copy(out,text,required*2) || wr_memory_copy(capacity,&copied,4)){wr_current_teb()->last_error=998;return 0;}return 1;
}
void *wr_system_exports(const char *name) {
    void *profile=wr_profile_exports(name);if(profile)return profile;
    void *message=wr_message_exports(name);if(message)return message;
    void *console=wr_console_exports(name);if(console)return console;
    void *wait=wr_registered_wait_exports(name);if(wait)return wait;
    void *calendar=wr_calendar_exports(name);if(calendar)return calendar;
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(QueryPerformanceCounter);E(QueryPerformanceFrequency);E(GetSystemTimeAsFileTime);E(GetSystemTimePreciseAsFileTime);
    E(FileTimeToSystemTime);E(GetSystemTime);E(IsWow64Process);E(IsDebuggerPresent);E(CheckRemoteDebuggerPresent);E(DebugBreak);E(OutputDebugStringW);E(EncodePointer);E(DecodePointer);
    E(GetLogicalProcessorInformation);E(GetSystemInfo);E(GetNativeSystemInfo);E(IsProcessorFeaturePresent);
    E(GetComputerNameExW);
#undef E
    return 0;
}
