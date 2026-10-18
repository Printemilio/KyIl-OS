#include <stdint.h>
__declspec(dllimport) int WideCharToMultiByte(unsigned,unsigned,const uint16_t *,int,char *,int,const char *,int *);
__declspec(dllimport) int MultiByteToWideChar(unsigned,unsigned,const char *,int,uint16_t *,int);
__declspec(dllimport) unsigned GetLastError(void);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
void mainCRTStartup(void) {
    uint16_t wide[]={0x41,0xe9,0x20ac,0xd83d,0xde00,0};char expected[]={0x41,(char)0xc3,(char)0xa9,(char)0xe2,(char)0x82,(char)0xac,(char)0xf0,(char)0x9f,(char)0x98,(char)0x80,0};
    char bytes[16];uint16_t copy[16];
    check(WideCharToMultiByte(0,0x80,wide,-1,0,0,0,0)==11,"encoding: actual UTF8 required length FAIL\n");
    check(WideCharToMultiByte(65001,0x80,wide,-1,bytes,16,0,0)==11,"encoding: wide conversion FAIL\n");
    for(unsigned i=0;i<11;i++)check(bytes[i]==expected[i],"encoding: actual UTF8 bytes FAIL\n");
    check(MultiByteToWideChar(65001,8,bytes,-1,copy,16)==6,"encoding: UTF16 conversion FAIL\n");
    for(unsigned i=0;i<6;i++)check(copy[i]==wide[i],"encoding: surrogate pair roundtrip FAIL\n");
    check(!WideCharToMultiByte(65001,0,wide,-1,bytes,3,0,0) && GetLastError()==122,"encoding: short output buffer FAIL\n");
    const char invalid[]={(char)0xe0,(char)0x85,(char)0x93};
    check(!MultiByteToWideChar(65001,8,invalid,3,copy,16) && GetLastError()==1113,"encoding: invalid UTF8 strict FAIL\n");
    check(MultiByteToWideChar(65001,0,invalid,3,copy,16)==2 && copy[0]==0xfffd && copy[1]==0xfffd,"encoding: Windows replacement semantics FAIL\n");
    check(!MultiByteToWideChar(1252,0,bytes,3,copy,16) && GetLastError()==50,"encoding: unsupported page refusal FAIL\n");
    OutputDebugStringA("windows-encoding: exact UTF8-UTF16 / supplementary scalar / sizes / bounds / invalid sequences / Windows replacements PASS\n");ExitProcess(0);
}
