#include "windows.h"
#include "process.h"
static unsigned fail(unsigned e){wr_current_teb()->last_error=e;return 0;}
static int WR_WIN GetConsoleMode(void *handle,unsigned *out) {
    unsigned access;if(!out)return fail(87);if(!wr_file_console_kind(handle,&access))return 0;
    /* Native console input is raw, without echo/line editing. tty_putchar
       processes output controls. Do not advertise modes it does not provide. */
    unsigned mode=(access&2)?1:0;return wr_memory_copy(out,&mode,4)?fail(998):1;
}
static unsigned WR_WIN GetConsoleOutputCP(void){return 65001;}
static int WR_WIN WriteConsoleW(void *handle,const uint16_t *input,unsigned count,unsigned *written,void *reserved) {
    if(written)*written=0;if(reserved || (!input && count))return fail(87);
    unsigned access;if(!wr_file_console_kind(handle,&access))return 0;if(!(access&2))return fail(5);
    if(count>INT32_MAX/4)return fail(8);
    uint16_t *text=wr_native_alloc(((size_t)count+1)*2);char *bytes=wr_native_alloc((size_t)count*4+1);
    if(!text || !bytes){if(text)wr_native_free(text);if(bytes)wr_native_free(bytes);return fail(8);}
    unsigned error=0,n=0,length=0;
    if(wr_memory_copy(text,input,(size_t)count*2))error=998;
    else if(count) {
        int (WR_WIN *convert)(unsigned,unsigned,const uint16_t *,int,char *,int,const char *,int *)=wr_encoding_exports("WideCharToMultiByte");
        int result=convert(65001,0,text,(int)count,bytes,(int)count*4,0,0);
        if(!result)error=wr_current_teb()->last_error;else length=(unsigned)result;
    }
    if(!error){int (WR_WIN *write)(void *,const void *,unsigned,unsigned *,void *)=wr_file_exports("WriteFile");
        unsigned at=0;
        while(at<length){if(!write(handle,bytes+at,length-at,&n,0)){error=wr_current_teb()->last_error;break;}if(!n){error=29;break;}at+=n;}
        if(!error && written)*written=count;
    }
    wr_native_free(text);wr_native_free(bytes);return error?fail(error):1;
}
static int WR_WIN ReadConsoleW(void *handle,uint16_t *out,unsigned capacity,unsigned *read,void *control) {
    if(read)*read=0;if(control || (!out && capacity))return fail(control?50:87);
    unsigned access;if(!wr_file_console_kind(handle,&access))return 0;if(!(access&1))return fail(5);
    if(!capacity)return 1;
    /* A raw console read completes after one input scalar. */
    if(capacity<2)return fail(122);
    uint8_t first;if(!wr_file_console_byte(handle,&first))return 0;
    unsigned value=first,extra=0,min=0;
    if(first>=0xc2 && first<=0xdf){value&=31;extra=1;min=0x80;}
    else if(first>=0xe0 && first<=0xef){value&=15;extra=2;min=0x800;}
    else if(first>=0xf0 && first<=0xf4){value&=7;extra=3;min=0x10000;}
    else if(first>=0x80)return fail(1113);
    for(unsigned i=0;i<extra;i++){uint8_t next;if(!wr_file_console_byte(handle,&next))return 0;if((next&0xc0)!=0x80)return fail(1113);value=(value<<6)|(next&63);}
    if(value<min || value>0x10ffff || (value>=0xd800 && value<=0xdfff))return fail(1113);
    uint16_t units[2];unsigned n=1;
    if(value<0x10000)units[0]=(uint16_t)value;
    else {value-=0x10000;units[0]=(uint16_t)(0xd800+(value>>10));units[1]=(uint16_t)(0xdc00+(value&1023));n=2;}
    if(wr_memory_copy(out,units,n*2))return fail(998);if(read)*read=n;return 1;
}
void *wr_console_exports(const char *name) {
    extern void *wr_console_control_exports(const char *);void *control=wr_console_control_exports(name);if(control)return control;
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(GetConsoleMode);E(GetConsoleOutputCP);E(WriteConsoleW);E(ReadConsoleW);
#undef E
    return 0;
}
