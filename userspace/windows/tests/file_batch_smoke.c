#include <stdint.h>
typedef uint16_t W;
__declspec(dllimport) unsigned GetTempPathW(unsigned,W *),GetLastError(void),GetFullPathNameW(const W *,unsigned,W *,W **),GetLongPathNameW(const W *,W *,unsigned),GetDriveTypeW(const W *),GetFileType(void *);
__declspec(dllimport) void *CreateFileW(const W *,unsigned,unsigned,void *,unsigned,unsigned,void *),*FindFirstFileExW(const W *,unsigned,void *,unsigned,void *,unsigned),*GetStdHandle(unsigned);
__declspec(dllimport) int CreateDirectoryW(const W *,void *),DeleteFileW(const W *),RemoveDirectoryW(const W *),CloseHandle(void *),FindNextFileW(void *,void *),FindClose(void *),SetEndOfFile(void *),SetFilePointerEx(void *,int64_t,int64_t *,unsigned),GetFileSizeEx(void *,int64_t *),GetFileInformationByHandle(void *,void *),WriteFile(void *,const void *,unsigned,unsigned *,void *),ReadFile(void *,void *,unsigned,unsigned *,void *),SetStdHandle(unsigned,void *);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
typedef struct {unsigned attr,times[6],hi,lo,reserved[2];W name[260],alternate[14];} find_data;
typedef struct {unsigned attr,times[6],volume,hi,lo,links,ihi,ilo;} info;
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
static unsigned append(W *out,unsigned at,const W *part){while(*part)out[at++]=*part++;out[at]=0;return at;}
static int equal(const W *a,const W *b){while(*a==*b){if(!*a)return 1;a++;b++;}return 0;}
void mainCRTStartup(void) {
    static W directory[512],path[512],pattern[512],normalized[512];W *last=0;unsigned base=GetTempPathW(512,directory);
    base=append(directory,base,(const W *)L"batch-directory");CreateDirectoryW(directory,0);
    for(unsigned i=0;i<=base;i++)path[i]=directory[i];unsigned start=append(path,base,(const W *)L"\\");
    append(path,start,(const W *)L"alpha.txt");void *h=CreateFileW(path,0xc0000000u,7,0,1,0x80,0);
    if(h==(void *)-1 && GetLastError()==80)h=CreateFileW(path,0xc0000000u,7,0,5,0x80,0);
    check(h!=(void *)-1,"file-batch: create FAIL\n");unsigned written;info before,after;
    check(GetFileType(h)==1 && GetFileInformationByHandle(h,&before) && before.links && (before.ilo || before.ihi),"file-batch: actual identity FAIL\n");
    check(WriteFile(h,"abcdef",6,&written,0) && written==6 && SetFilePointerEx(h,3,0,0) && SetEndOfFile(h),"file-batch: truncate FAIL\n");
    int64_t size;check(GetFileSizeEx(h,&size) && size==3 && GetFileInformationByHandle(h,&after) && after.lo==3 && after.ilo==before.ilo && after.ihi==before.ihi,"file-batch: stable metadata FAIL\n");
    check(SetFilePointerEx(h,9,0,0) && SetEndOfFile(h) && SetFilePointerEx(h,3,0,0),"file-batch: grow FAIL\n");
    uint8_t bytes[6]={1,1,1,1,1,1};check(ReadFile(h,bytes,6,&written,0) && written==6,"file-batch: zero tail read FAIL\n");for(unsigned i=0;i<6;i++)check(!bytes[i],"file-batch: zero tail FAIL\n");
    check(CloseHandle(h) && !GetFileType(h) && GetLastError()==6,"file-batch: type stale FAIL\n");
    unsigned length=GetFullPathNameW(path,512,normalized,&last);check(length && last && equal(last,(const W *)L"alpha.txt") && equal(path,normalized),"file-batch: normalization/filepart FAIL\n");
    check(GetFullPathNameW(path,0,0,0)==length+1 && GetFullPathNameW(path,2,normalized,0)==length+1,"file-batch: sizing FAIL\n");
    check(GetLongPathNameW(path,normalized,512)==length && equal(path,normalized) && GetDriveTypeW((const W *)L"C:\\")==3 && GetDriveTypeW((const W *)L"Z:\\")==1,"file-batch: mounted paths FAIL\n");
    for(unsigned i=0;i<=base;i++)pattern[i]=directory[i];append(pattern,base,(const W *)L"\\ALPHA.*");find_data data;
    void *search=FindFirstFileExW(pattern,1,&data,0,0,2);check(search!=(void *)-1 && equal(data.name,(const W *)L"alpha.txt") && data.lo==9 && !data.alternate[0],"file-batch: real wildcard search FAIL\n");
    check(!FindNextFileW(search,&data) && GetLastError()==18 && FindClose(search) && !FindClose(search) && GetLastError()==6,"file-batch: EOF/stale search FAIL\n");
    search=FindFirstFileExW(pattern,1,&data,0,0,1);check(search==(void *)-1 && GetLastError()==2,"file-batch: sensitive search FAIL\n");
    check(FindFirstFileExW(pattern,1,(void *)0xffff800000000000ull,0,0,0)==(void *)-1 && GetLastError()==998,"file-batch: kernel output denied FAIL\n");
    void *original=GetStdHandle((unsigned)-11);check(SetStdHandle((unsigned)-11,0) && !GetStdHandle((unsigned)-11) && SetStdHandle((unsigned)-11,original),"file-batch: standard handles FAIL\n");
    check(DeleteFileW(path) && RemoveDirectoryW(directory),"file-batch: cleanup FAIL\n");
    OutputDebugStringA("windows-file-batch: enumeration / anchored authority / identity / truncate-grow / paths / stale / output rights PASS\n");ExitProcess(0);
}
