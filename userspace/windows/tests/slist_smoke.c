#include <stdint.h>
typedef struct {uint64_t words[2];} __attribute__((aligned(16))) head;
typedef struct entry {struct entry *next;unsigned id,pad;} __attribute__((aligned(16))) entry;
__declspec(dllimport) void InitializeSListHead(head *),*InterlockedFlushSList(head *),*InterlockedPushEntrySList(head *,entry *),*InterlockedPopEntrySList(head *);
__declspec(dllimport) unsigned short QueryDepthSList(head *);
__declspec(dllimport) void *CreateThread(void *,uint64_t,unsigned (*)(void *),void *,unsigned,unsigned *);
__declspec(dllimport) unsigned WaitForSingleObject(void *,unsigned),GetLastError(void);
__declspec(dllimport) int CloseHandle(void *),IsProcessorFeaturePresent(unsigned),GetComputerNameExW(unsigned,uint16_t *,unsigned *);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static head shared;
static entry nodes[4][2000];static unsigned seen[8000],popped;
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
static unsigned producer(void *arg){unsigned id=(unsigned)(uintptr_t)arg;for(unsigned i=0;i<2000;i++){nodes[id][i].id=id*2000+i;InterlockedPushEntrySList(&shared,&nodes[id][i]);}return 0;}
static unsigned consumer(void *arg){(void)arg;for(;;){entry *e=InterlockedPopEntrySList(&shared);if(!e)break;
    check(e->id<8000 && __atomic_fetch_add(&seen[e->id],1,__ATOMIC_RELAXED)==0,"slist: duplicate/corruption FAIL\n");__atomic_fetch_add(&popped,1,__ATOMIC_RELAXED);}return 0;}
static void parallel(unsigned (*fn)(void *)){void *threads[4];for(unsigned i=0;i<4;i++){threads[i]=CreateThread(0,0,fn,(void *)(uintptr_t)i,0,0);check(threads[i]!=0,"slist: thread create FAIL\n");}
    for(unsigned i=0;i<4;i++)check(!WaitForSingleObject(threads[i],0xffffffffu) && CloseHandle(threads[i]),"slist: thread completion FAIL\n");}
void mainCRTStartup(void) {
    check(IsProcessorFeaturePresent(14),"slist: CPU lacks required CMPXCHG16B (explicit unsupported backend) FAIL\n");InitializeSListHead(&shared);
    check(!InterlockedFlushSList(&shared) && !QueryDepthSList(&shared),"slist: empty FAIL\n");
    parallel(producer);check(QueryDepthSList(&shared)==8000,"slist: depth after concurrent pushes FAIL\n");parallel(consumer);
    check(popped==8000 && !QueryDepthSList(&shared) && !InterlockedPopEntrySList(&shared),"slist: ordered atomic ownership FAIL\n");
    for(unsigned i=0;i<8000;i++)check(seen[i]==1,"slist: missing node FAIL\n");
    for(unsigned i=0;i<20;i++)InterlockedPushEntrySList(&shared,&nodes[0][i]);entry *list=InterlockedFlushSList(&shared);
    for(unsigned i=20;i;i--){check(list==&nodes[0][i-1],"slist: flush order FAIL\n");list=list->next;}check(!list && !QueryDepthSList(&shared),"slist: flush depth FAIL\n");
    unsigned capacity=0;check(!GetComputerNameExW(1,0,&capacity) && capacity>1 && GetLastError()==234,"slist: hostname required length FAIL\n");
    uint16_t name[65];capacity=65;check(GetComputerNameExW(1,name,&capacity) && capacity && name[capacity]==0,"slist: actual native hostname FAIL\n");
    capacity=65;check(GetComputerNameExW(2,name,&capacity) && !capacity && !name[0],"slist: no invented DNS domain FAIL\n");
    OutputDebugStringA("windows-slist: 8000 concurrent pushes-pops / exact-once nodes / flush order / native hostname PASS\n");ExitProcess(0);
}
