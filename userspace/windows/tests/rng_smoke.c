#include <stdint.h>
__declspec(dllimport) void *LoadLibraryExW(const uint16_t *,void *,unsigned),*GetProcAddress(void *,const char *),*CreateThread(void *,uint64_t,unsigned (*)(void *),void *,unsigned,unsigned *);
__declspec(dllimport) int FreeLibrary(void *),CloseHandle(void *);
__declspec(dllimport) unsigned WaitForSingleObject(void *,unsigned);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static int (__attribute__((ms_abi)) *random_bytes)(void *,uint64_t);
static uint8_t bytes[2][131072];static unsigned failed;
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
static unsigned worker(void *arg){uint8_t local[256];(void)arg;for(unsigned i=0;i<64;i++)if(!random_bytes(local,sizeof(local)))__atomic_store_n(&failed,1,__ATOMIC_RELAXED);return 0;}
void mainCRTStartup(void){
    void *library=LoadLibraryExW(L"bcryptprimitives",0,0x800);check(library!=0,"rng: genuine protected provider load FAIL\n");
    random_bytes=GetProcAddress(library,"ProcessPrng");check(random_bytes!=0,"rng: actual named export FAIL\n");
    for(unsigned i=0;i<2;i++)check(random_bytes(bytes[i],sizeof(bytes[i])),"rng: actual random fill FAIL\n");
    unsigned different=0,nonzero=0;for(unsigned i=0;i<sizeof(bytes[0]);i++){different+=bytes[0][i]!=bytes[1][i];nonzero+=bytes[0][i]!=0;}
    check(different>100000 && nonzero>100000,"rng: nonconstant independent fills FAIL\n");
    void *threads[4];for(unsigned i=0;i<4;i++){threads[i]=CreateThread(0,0,worker,0,0,0);check(threads[i]!=0,"rng: real concurrent caller FAIL\n");}
    for(unsigned i=0;i<4;i++)check(!WaitForSingleObject(threads[i],10000) && CloseHandle(threads[i]),"rng: completion FAIL\n");
    check(!failed,"rng: concurrent native RNG failure FAIL\n");
#ifdef KYIL_NATIVE_TEST
    check(!random_bytes((void *)0xffff800000001000ull,32),"rng: kernel output pointer refused FAIL\n");
#endif
    check(FreeLibrary(library),"rng: provider reference release FAIL\n");
    OutputDebugStringA("windows-rng: genuine provider / 2x128KiB actual native random bytes / four concurrent callers / isolation PASS\n");ExitProcess(0);
}
