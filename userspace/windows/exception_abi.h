#ifndef KYIL_WIN_EXCEPTION_ABI_H
#define KYIL_WIN_EXCEPTION_ABI_H
#include "windows.h"
typedef struct {
    uint64_t home[6];uint32_t flags,mxcsr;
    uint16_t cs,ds,es,fs,gs,ss;uint32_t eflags;
    uint64_t debug[6];
    uint64_t rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15,rip;
    uint8_t fp[512];uint8_t tail[0x4d0-0x300];
} __attribute__((aligned(16))) win_context;
typedef struct {uint32_t code,flags;void *nested,*address;uint32_t count,pad;uint64_t arguments[15];} exception_record;
typedef struct {exception_record *record;win_context *context;} exception_pointers;
typedef struct {
    uint64_t frame,rbx,rsp,rbp,rsi,rdi,r12,r13,r14,r15,rip;
    uint32_t mxcsr;uint16_t fpcsr,spare;
    uint8_t xmm[10][16];
} wr_jump_buffer;
_Static_assert(sizeof(wr_jump_buffer)==256 && offsetof(wr_jump_buffer,xmm)==96,"AMD64 _JUMP_BUFFER");
_Static_assert(sizeof(win_context)==0x4d0,"Windows AMD64 CONTEXT");
_Static_assert(offsetof(win_context,rax)==0x78 && offsetof(win_context,rip)==0xf8 && offsetof(win_context,fp)==0x100,"CONTEXT registers");
typedef struct {uint32_t begin,end,unwind;} runtime_function;
typedef struct {void *floating[16],*integer[16];} context_pointers;
void WR_WIN wr_capture_context(win_context *);
unsigned short WR_WIN wr_capture_backtrace(unsigned,unsigned,void **,unsigned *);
int wr_deliver_exception(exception_record *,win_context *);
int wr_seh_dispatch(exception_record *,win_context *);
unsigned short wr_walk_backtrace(win_context *,unsigned,unsigned,void **,unsigned *);
void *wr_seh_exports(const char *);
_Noreturn void wr_restore_context(win_context *);
#endif
