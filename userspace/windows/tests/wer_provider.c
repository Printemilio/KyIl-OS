#include "../wer.h"
__declspec(dllimport) unsigned GetCurrentProcessId(void),GetProcessId(void *),GetThreadId(void *),GetTempPathW(unsigned,uint16_t *);
__declspec(dllimport) int GetThreadContext(void *,void *),WriteFile(void *,const void *,unsigned,unsigned *,void *),FlushFileBuffers(void *),CloseHandle(void *);
__declspec(dllimport) void *CreateFileW(const uint16_t *,unsigned,unsigned,void *,unsigned,unsigned,void *);
static unsigned phase;
static void text(uint16_t *out,const uint16_t *s){while((*out++=*s++));}
int DllMain(void *module,unsigned reason,void *reserved){(void)module;(void)reserved;if(reason==1)phase=0;return 1;}
__declspec(dllexport) uint32_t OutOfProcessExceptionEventCallback(void *opaque,const wer_information *info,int *claim,uint16_t *event,unsigned *capacity,unsigned *count) {
    if(opaque!=(void *)0x12345678 || info->size!=1424 || info->record.code!=0xe0424242u ||
       !info->fatal || !info->context.rip || !info->context.rsp || GetProcessId(info->process)==GetCurrentProcessId() ||
       !GetThreadId(info->thread) || !info->report_id || *capacity<13)return 0x8007000du;
    static win_context c;c.flags=0x10000b;
    if(!GetThreadContext(info->thread,&c) || c.rip!=info->context.rip || c.rsp!=info->context.rsp)return 0x8007000du;
    text(event,(const uint16_t *)u"KyIlWERTest");*capacity=12;*count=1;*claim=1;phase=1;return 0;
}
__declspec(dllexport) uint32_t OutOfProcessExceptionEventSignatureCallback(void *opaque,const wer_information *info,unsigned index,uint16_t *name,unsigned *name_size,uint16_t *value,unsigned *value_size) {
    if(opaque!=(void *)0x12345678 || phase!=1 || index || info->record.code!=0xe0424242u || *name_size<5 || *value_size<9)return 0x8007000du;
    text(name,(const uint16_t *)u"Code");text(value,(const uint16_t *)u"e0424242");*name_size=5;*value_size=9;phase=2;return 0;
}
__declspec(dllexport) uint32_t OutOfProcessExceptionEventDebuggerLaunchCallback(void *opaque,const wer_information *info,int *custom,uint16_t *command,unsigned *capacity,int *automatic) {
    (void)command;(void)capacity;
    if(opaque!=(void *)0x12345678 || phase!=2 || info->report_id[36] || info->report_id[8]!='-' || info->report_id[14]!='4')return 0x8007000du;
    static uint16_t path[512];unsigned n=GetTempPathW(512,path);if(!n || n>450)return 0x80070005u;
    text(path+n,(const uint16_t *)u"wer-provider-executed.bin");
    void *file=CreateFileW(path,0x40000000u,7,0,1,0x80,0);if(file==(void *)-1)return 0x80070005u;
    struct {uint32_t source,helper,tid,code;uint64_t rip;uint32_t phase,pad;} record={GetProcessId(info->process),GetCurrentProcessId(),GetThreadId(info->thread),info->record.code,info->context.rip,3,0};
    unsigned written;int ok=WriteFile(file,&record,sizeof(record),&written,0) && written==sizeof(record) && FlushFileBuffers(file);if(!CloseHandle(file))ok=0;
    if(!ok)return 0x8007045du;*custom=*automatic=0;phase=3;return 0;
}
