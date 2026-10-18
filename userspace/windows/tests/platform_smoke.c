#include <stdint.h>
typedef struct {uint16_t year,month,weekday,day,hour,minute,second,ms;} time_fields;
__declspec(dllimport) void *CreateSemaphoreW(void *,int,int,const uint16_t *),*CreateEventA(void *,int,int,const char *);
__declspec(dllimport) int ReleaseSemaphore(void *,int,int *),CloseHandle(void *),QueryPerformanceCounter(int64_t *),QueryPerformanceFrequency(int64_t *);
__declspec(dllimport) unsigned WaitForSingleObject(void *,unsigned),GetLastError(void);
__declspec(dllimport) void GetSystemTimeAsFileTime(uint64_t *),Sleep(unsigned),OutputDebugStringA(const char *),ExitProcess(unsigned);
__declspec(dllimport) int FileTimeToSystemTime(const uint64_t *,time_fields *),IsWow64Process(void *,int *);
__declspec(dllimport) void *EncodePointer(void *),*DecodePointer(void *),*GetCurrentProcess(void);
__declspec(dllimport) void *GetProcessHeap(void);
__declspec(dllimport) unsigned GetProcessHeaps(unsigned,void **);
typedef struct {uint16_t arch,pad;unsigned page;void *min,*max;uint64_t mask;unsigned cpus,type,granularity;uint16_t level,revision;} system_info;
__declspec(dllimport) void GetSystemInfo(system_info *),GetNativeSystemInfo(system_info *);
__declspec(dllimport) int IsProcessorFeaturePresent(unsigned);
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
void mainCRTStartup(void) {
    void *semaphore=CreateSemaphoreW(0,2,3,0);check(semaphore!=0,"platform: semaphore create FAIL\n");
    check(!WaitForSingleObject(semaphore,0) && !WaitForSingleObject(semaphore,0) && WaitForSingleObject(semaphore,0)==258,"platform: actual permit consumption FAIL\n");
    int previous=-1;check(ReleaseSemaphore(semaphore,2,&previous) && !previous,"platform: actual permit release FAIL\n");
    check(!ReleaseSemaphore(semaphore,2,0) && GetLastError()==298,"platform: semaphore overflow refused FAIL\n");
    check(!WaitForSingleObject(semaphore,0) && CloseHandle(semaphore),"platform: semaphore close FAIL\n");
    void *event=CreateEventA(0,1,1,0);check(event && !WaitForSingleObject(event,0) && CloseHandle(event),"platform: ANSI adapter actual event FAIL\n");
    int64_t frequency,start,end;check(QueryPerformanceFrequency(&frequency) && frequency>0 && QueryPerformanceCounter(&start),"platform: actual clock query FAIL\n");
    Sleep(20);check(QueryPerformanceCounter(&end) && end>start && end-start>=frequency/1000,"platform: advancing monotonic clock FAIL\n");
    uint64_t epoch=0;time_fields utc;check(FileTimeToSystemTime(&epoch,&utc) && utc.year==1601 && utc.month==1 && utc.day==1,"platform: FILETIME epoch FAIL\n");
    GetSystemTimeAsFileTime(&epoch);check(FileTimeToSystemTime(&epoch,&utc) && utc.year>=2020 && utc.month>=1 && utc.month<=12,"platform: actual RTC time FAIL\n");
    void *pointer=(void *)0x123456781234;check(DecodePointer(EncodePointer(pointer))==pointer,"platform: actual pointer secret roundtrip FAIL\n");
    int wow=1;check(IsWow64Process(GetCurrentProcess(),&wow) && !wow,"platform: actual guest architecture FAIL\n");
    void *heap=0;check(GetProcessHeaps(0,0)==1 && GetProcessHeaps(1,&heap)==1 && heap==GetProcessHeap(),"platform: actual heap inventory FAIL\n");
    system_info actual,native;GetSystemInfo(&actual);GetNativeSystemInfo(&native);
    check(actual.arch==9 && actual.page==4096 && actual.cpus && actual.cpus<=64 && actual.granularity==65536 && actual.cpus==native.cpus &&
        actual.mask==(actual.cpus==64?UINT64_MAX:((1ull<<actual.cpus)-1)) && actual.level,"platform: real native machine facts FAIL\n");
    check(IsProcessorFeaturePresent(10) && IsProcessorFeaturePresent(12) && !IsProcessorFeaturePresent(0xffffffffu),"platform: CPU and enabled NX facts FAIL\n");
    OutputDebugStringA("windows-platform: counted semaphore / ANSI event / monotonic counter / actual RTC / FILETIME / pointer secret / guest architecture PASS\n");ExitProcess(0);
}
