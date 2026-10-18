#include <stdint.h>
typedef uint16_t W;
__declspec(dllimport) void *CreateFileW(const W *,unsigned,unsigned,void *,unsigned,unsigned,void *);
__declspec(dllimport) int WriteFile(void *,const void *,unsigned,unsigned *,void *),ReadFile(void *,void *,unsigned,unsigned *,void *),CloseHandle(void *),DeleteFileW(const W *),MoveFileW(const W *,const W *),ReplaceFileW(const W *,const W *,const W *,unsigned,void *,void *),SetFileAttributesW(const W *,unsigned);
__declspec(dllimport) unsigned GetTempPathW(unsigned,W *),GetFileAttributesW(const W *),GetLastError(void);
__declspec(dllimport) void *GetStdHandle(unsigned);
__declspec(dllimport) int GetFileInformationByHandle(void *,void *);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static W paths[4][512];
static void check(int ok,const char *why){if(!ok){unsigned error=GetLastError(),length=0,done;char hex[]="error=0x00000000\n";for(unsigned i=0;i<8;i++)hex[8+i]="0123456789abcdef"[(error>>(28-i*4))&15];while(why[length])length++;OutputDebugStringA(why);WriteFile(GetStdHandle((unsigned)-12),why,length,&done,0);WriteFile(GetStdHandle((unsigned)-12),hex,sizeof(hex)-1,&done,0);OutputDebugStringA(hex);ExitProcess(1);}}
static void put(unsigned i,char byte){void *f=CreateFileW(paths[i],0x40000000u,7,0,1,0x80,0);unsigned done;
    check(f!=(void *)-1 && WriteFile(f,&byte,1,&done,0) && done==1 && CloseHandle(f),"namespace-mutation: actual creation FAIL\n");}
static void get(unsigned i,char byte){void *f=CreateFileW(paths[i],0x80000000u,7,0,3,0x80,0);unsigned done;char b=0;
    check(f!=(void *)-1 && ReadFile(f,&b,1,&done,0) && done==1 && b==byte && CloseHandle(f),"namespace-mutation: exact content FAIL\n");}
static uint64_t identity(unsigned i){unsigned info[13];void *f=CreateFileW(paths[i],0x80000000u,7,0,3,0x80,0);
    check(f!=(void *)-1 && GetFileInformationByHandle(f,info) && CloseHandle(f),"namespace-mutation: actual file identity FAIL\n");return ((uint64_t)info[11]<<32)|info[12];}
void mainCRTStartup(void){
    for(unsigned i=0;i<4;i++){unsigned n=GetTempPathW(470,paths[i]);check(n && n<470,"namespace-mutation: actual temp FAIL\n");
        const W *name=L"kyil-namespace-0.txt";while(*name)paths[i][n++]=*name++;paths[i][n-5]=(W)('0'+i);paths[i][n]=0;DeleteFileW(paths[i]);}
    for(unsigned cycle=0;cycle<12;cycle++){
        put(0,'A');put(1,'B');put(2,'C');
        uint64_t a=identity(0),b=identity(1);check(a && b && a!=b,"namespace-mutation: independent identities FAIL\n");
        check(!MoveFileW(paths[0],paths[1]) && GetLastError()==183,"namespace-mutation: no overwrite FAIL\n");get(0,'A');get(1,'B');
        check(MoveFileW(paths[0],paths[3]) && GetFileAttributesW(paths[0])==UINT32_MAX,"namespace-mutation: real rename FAIL\n");get(3,'A');
        check(identity(3)==a,"namespace-mutation: rename preserves identity FAIL\n");
        check(ReplaceFileW(paths[1],paths[3],paths[2],0,0,0),"namespace-mutation: replacement and backup FAIL\n");get(1,'A');get(2,'B');
        check(identity(1)==a && identity(2)==b,"namespace-mutation: replacement and backup identities FAIL\n");
        check(GetFileAttributesW(paths[3])==UINT32_MAX,"namespace-mutation: replacement consumed FAIL\n");
        check(SetFileAttributesW(paths[1],1) && (GetFileAttributesW(paths[1])&1),"namespace-mutation: readonly state FAIL\n");
        check(SetFileAttributesW(paths[1],0x80) && !(GetFileAttributesW(paths[1])&1),"namespace-mutation: writable state FAIL\n");
        check(!ReplaceFileW(paths[1],paths[3],0,0,0,0),"namespace-mutation: absent replacement refused FAIL\n");get(1,'A');
        put(3,'D');check(ReplaceFileW(paths[1],paths[3],0,0,0,0),"namespace-mutation: replacement without backup FAIL\n");get(1,'D');
        check(DeleteFileW(paths[1]) && DeleteFileW(paths[2]),"namespace-mutation: exact cleanup FAIL\n");
    }
#ifdef KYIL_NATIVE_TEST
    put(0,'L');void *file=CreateFileW(paths[0],0x80000000u,7,0,3,0x80,0);
    check(file!=(void *)-1 && !MoveFileW(paths[0],paths[1]) && GetLastError()==32 && CloseHandle(file),"namespace-mutation: live description refusal FAIL\n");
    check(!MoveFileW(paths[0],L"C:\\..\\escape.txt") && GetLastError()==5 && !MoveFileW(paths[0],L"A:\\escape.txt") && GetLastError()==5,"namespace-mutation: root confinement FAIL\n");
    check(!SetFileAttributesW(paths[0],2) && GetLastError()==50 && DeleteFileW(paths[0]),"namespace-mutation: unsupported metadata honest FAIL\n");
#endif
    OutputDebugStringA("windows-namespace-mutation: rename / no overwrite / replacement / backup / contents / readonly / repeated cleanup PASS\n");ExitProcess(0);
}
