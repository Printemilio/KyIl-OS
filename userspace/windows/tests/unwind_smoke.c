#include <stdint.h>
typedef struct {uint64_t home[6];unsigned flags,mxcsr;uint16_t segments[6];unsigned eflags;uint64_t debug[6];
    uint64_t rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15,rip;
    unsigned char fp[512],tail[0x1d0];} __declspec(align(16)) context;
typedef struct {unsigned begin,end,unwind;} function;
typedef struct {void *floating[16],*integer[16];} pointers;
__declspec(dllimport) void RtlCaptureContext(context *);
__declspec(dllimport) unsigned short RtlCaptureStackBackTrace(unsigned,unsigned,void **,unsigned *);
__declspec(dllimport) function *RtlLookupFunctionEntry(uint64_t,uint64_t *,void *);
__declspec(dllimport) void *RtlVirtualUnwind(unsigned,uint64_t,uint64_t,function *,context *,void **,uint64_t *,pointers *);
__declspec(dllimport) void *RtlPcToFileHeader(void *,void **),*GetModuleHandleW(const uint16_t *),*VirtualAlloc(void *,uint64_t,unsigned,unsigned);
__declspec(dllimport) int VirtualFree(void *,uint64_t,unsigned);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
extern void unwind_fixture(void (*)(context *),context *);
extern uint64_t unwind_saved_rbx;
extern char unwind_after_push[];
static uint64_t stack[32];static unsigned calls;
void *memcpy(void *out,const void *in,uint64_t length){unsigned char *a=out;const unsigned char *b=in;while(length--)*a++=*b++;return out;}
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
static void captured(context *original) {
    calls++;uint64_t base=0;function *entry=RtlLookupFunctionEntry(original->rip,&base,0);void *header=0;
    check(entry && base==(uint64_t)GetModuleHandleW(0) && RtlPcToFileHeader((void *)original->rip,&header)==(void *)base && header==(void *)base,
        "unwind: real PE function table/header FAIL\n");
    context c=*original;uint64_t frame=0,old=c.rsp;void *data=(void *)1;pointers locations;
    check(!RtlVirtualUnwind(0,base,c.rip,entry,&c,&data,&frame,&locations) && !data && c.rsp==old+80 && c.rbx==unwind_saved_rbx &&
        locations.integer[3]==(void *)(old+64) && frame==old,"unwind: actual live frame and saved register FAIL\n");
    c=*original;stack[0]=0x123456;stack[1]=0x3917539;c.rsp=(uint64_t)stack;c.rip=(uint64_t)unwind_after_push;
    check(!RtlVirtualUnwind(0,base,c.rip,entry,&c,0,&frame,0) && c.rbx==stack[0] && c.rip==stack[1] && c.rsp==(uint64_t)(stack+2),
        "unwind: partial prologue executed codes only FAIL\n");
    void *trace[16];unsigned hash=0;unsigned short count=RtlCaptureStackBackTrace(0,16,trace,&hash);
    check(count>=2 && trace[0] && trace[1] && hash,"unwind: actual metadata backtrace FAIL\n");
}
static void zero(void *value,unsigned size){unsigned char *p=value;while(size--)*p++=0;}
void mainCRTStartup(void) {
    context c;unwind_fixture(captured,&c);check(calls==1,"unwind: real frame callback FAIL\n");
    unsigned char *image=VirtualAlloc(0,4096,0x3000,4);check(image!=0,"unwind: dynamic metadata allocation FAIL\n");
    function f={0x100,0x180,0x200};unsigned char *u=image+0x200;
    /* General V2: explicit epilog at end-2, and a real fixed stack allocation. */
    u[0]=2;u[1]=4;u[2]=3;u[3]=0;u[4]=1;u[5]=6;u[6]=2;u[7]=6;u[8]=4;u[9]=0x32;
    image[0x17e]=0xc3;image[0x17f]=0xcc;stack[4]=0x3917539;
    zero(&c,sizeof(c));c.rsp=(uint64_t)stack;c.rip=(uint64_t)image+0x110;uint64_t frame;void *data;
    check(!RtlVirtualUnwind(0,(uint64_t)image,c.rip,&f,&c,&data,&frame,0) && c.rip==stack[4] && c.rsp==(uint64_t)(stack+5),"unwind: V2 body codes FAIL\n");
    c.rsp=(uint64_t)(stack+4);c.rip=(uint64_t)image+0x17e;
    check(!RtlVirtualUnwind(0,(uint64_t)image,c.rip,&f,&c,&data,&frame,0) && c.rip==stack[4] && c.rsp==(uint64_t)(stack+5),"unwind: V2 epilog avoids duplicate deallocation FAIL\n");
    /* Chained shrink-wrap followed by a parent saved-register unwind. */
    zero(u,64);u[0]=0x21;u[1]=4;u[2]=1;u[4]=4;u[5]=0x32;
    function parent={0x100,0x180,0x220};unsigned char *p=(void *)&parent;for(unsigned i=0;i<12;i++)u[8+i]=p[i];
    image[0x220]=1;image[0x221]=1;image[0x222]=1;image[0x224]=1;image[0x225]=0x30;
    stack[4]=0x991;stack[5]=0x3917539;c.rsp=(uint64_t)stack;c.rip=(uint64_t)image+0x110;
    check(!RtlVirtualUnwind(0,(uint64_t)image,c.rip,&f,&c,0,&frame,0) && c.rbx==0x991 && c.rip==stack[5] && c.rsp==(uint64_t)(stack+6),
        "unwind: real chain composition FAIL\n");
    /* Saved nonvolatile XMM6 plus fixed stack: values really recovered. */
    zero(u,64);u[0]=1;u[1]=4;u[2]=3;u[4]=4;u[5]=0x68;u[8]=1;u[9]=0x32;
    unsigned char *saved=(void *)stack;for(unsigned i=0;i<16;i++)saved[i]=(unsigned char)(i+39);stack[4]=0x3917539;
    c.rsp=(uint64_t)stack;c.rip=(uint64_t)image+0x110;
    check(!RtlVirtualUnwind(0,(uint64_t)image,c.rip,&f,&c,0,&frame,0) && c.fp[256]==39 && c.fp[271]==54 && c.rip==stack[4],"unwind: actual XMM recovery FAIL\n");
    check(VirtualFree(image,0,0x8000),"unwind: metadata cleanup FAIL\n");
    OutputDebugStringA("windows-unwind: real PE pdata/context/backtrace / partial prologue / V2 epilog / chain / saved XMM / native validated reads PASS\n");ExitProcess(0);
}
