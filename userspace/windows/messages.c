#include "windows.h"
#include "process.h"
/* Message formatting is a userspace contract. Loaded PE message tables stay
 * owned by the loader; take a private copy before releasing its lock. */
typedef struct {unsigned id;const char *text;} system_message;
static const system_message errors[]={
 {0,"The operation completed successfully.\r\n"},{2,"The system cannot find the file specified.\r\n"},
 {3,"The system cannot find the path specified.\r\n"},{5,"Access is denied.\r\n"},
 {6,"The handle is invalid.\r\n"},{8,"Not enough memory resources are available to process this command.\r\n"},
 {13,"The data is invalid.\r\n"},{18,"There are no more files.\r\n"},{32,"The process cannot access the file because it is being used by another process.\r\n"},
 {33,"The process cannot access the file because another process has locked a portion of the file.\r\n"},
 {50,"The request is not supported.\r\n"},{80,"The file exists.\r\n"},{87,"The parameter is incorrect.\r\n"},
 {109,"The pipe has been ended.\r\n"},{112,"There is not enough space on the disk.\r\n"},
 {122,"The data area passed to a system call is too small.\r\n"},{126,"The specified module could not be found.\r\n"},
 {127,"The specified procedure could not be found.\r\n"},{183,"Cannot create a file when that file already exists.\r\n"},
 {193,"%1 is not a valid Win32 application.\r\n"},{206,"The filename or extension is too long.\r\n"},
 {234,"More data is available.\r\n"},{258,"The wait operation timed out.\r\n"},
 {317,"The system cannot find message text for message number 0x%1 in the message file for %2.\r\n"},
 {995,"The I/O operation has been aborted because of either a thread exit or an application request.\r\n"},
 {997,"Overlapped I/O operation is in progress.\r\n"},{998,"Invalid access to memory location.\r\n"},
 {1113,"No mapping for the Unicode character exists in the target multi-byte code page.\r\n"},
 {1150,"The specified program requires a newer version of Windows.\r\n"}
};
static unsigned fail(unsigned e){wr_current_teb()->last_error=e;return 0;}
static int directory(const uint8_t *tree,unsigned size,unsigned offset,unsigned id,int first,uint32_t *target) {
    if(!pe_span(offset,16,size))return 0;
    unsigned named=pe_u16(tree+offset+12),count=named+pe_u16(tree+offset+14);
    if(!pe_span((uint64_t)offset+16,(uint64_t)count*8,size))return 0;
    for(unsigned i=named;i<count;i++){const uint8_t *p=tree+offset+16+8*i;
        if(!(pe_u32(p)>>31) && (first || pe_u32(p)==id)){*target=pe_u32(p+4);return 1;}}
    return 0;
}
static int module_text(void *handle,unsigned id,unsigned language,uint16_t *out,unsigned cap) {
    int result=0;wr_loader_lock();module *m=0;
    for(unsigned i=0;i<wr_modules_count();i++){module *q=wr_module_at(i);if(q->base==handle || (!handle && !i)){m=q;break;}}
    if(!m){wr_loader_unlock();return -6;}
    uint32_t size=m->image.dirs[2][1],rva=m->image.dirs[2][0],entry;
    if(!size || !pe_span(rva,size,m->image.size))goto done;
    const uint8_t *tree=m->base+rva;
    if(!directory(tree,size,0,11,0,&entry) || !(entry>>31))goto done;
    unsigned table=entry&0x7fffffffu;
    if(!pe_span(table,16,size))goto done;
    unsigned count=pe_u16(tree+table+12)+pe_u16(tree+table+14);
    if(!pe_span((uint64_t)table+16,(uint64_t)count*8,size))goto done;
    for(unsigned n=0;n<count;n++) {
        entry=pe_u32(tree+table+20+8*n);if(!(entry>>31))continue;
        unsigned langdir=entry&0x7fffffffu;
        if(!directory(tree,size,langdir,language?language:0x409,0,&entry) &&
           (language || !directory(tree,size,langdir,0,1,&entry)))continue;
        if((entry>>31) || !pe_span(entry,16,size))continue;
        unsigned data=pe_u32(tree+entry),length=pe_u32(tree+entry+4);
        if(!pe_span(data,length,m->image.size) || length<4)continue;
        const uint8_t *bytes=m->base+data;unsigned blocks=pe_u32(bytes);
        if(!pe_span(4,(uint64_t)blocks*12,length))continue;
        for(unsigned b=0;b<blocks;b++) {
            unsigned low=pe_u32(bytes+4+12*b),high=pe_u32(bytes+8+12*b),at=pe_u32(bytes+12+12*b);
            if(id<low || id>high)continue;
            for(unsigned number=low;;number++) {
                if(!pe_span(at,4,length))break;unsigned len=pe_u16(bytes+at),flags=pe_u16(bytes+at+2);
                if(len<4 || !pe_span(at,len,length) || flags>1)break;
                if(number==id){unsigned units=flags?(len-4)/2:len-4;
                    if(units>=cap){result=-234;goto done;}
                    for(unsigned c=0;c<units;c++)out[c]=flags?pe_u16(bytes+at+4+2*c):bytes[at+4+c];
                    out[units]=0;result=1;goto done;}
                if(number==UINT32_MAX)break;at+=len;
            }
        }
    }
done:wr_loader_unlock();return result;
}
static unsigned format(unsigned flags,const void *source,unsigned id,unsigned language,void *output,unsigned capacity,void *arguments,int wide) {
    const unsigned allowed=0xff|0x100|0x200|0x400|0x800|0x1000|0x2000;
    if(flags&~allowed || !output || ((flags&0x400) && (flags&0x1800)) || !(flags&0x1c00))return fail(87);
    if(language && language!=0x409 && language!=0x400)return fail(1815);
    /* Wrapping at arbitrary caller widths is not silently approximated. */
    if((flags&255)!=0 && (flags&255)!=255)return fail(50);
    uint16_t *text=wr_native_alloc(65536),*result=wr_native_alloc(131072);
    if(!text || !result){if(text)wr_native_free(text);if(result)wr_native_free(result);return fail(8);}
    unsigned error=0,n=0;
    if(flags&0x400) {
        if(!source)error=87;
        else if(wide){unsigned i;for(i=0;i<32767 && ((const uint16_t *)source)[i];i++)text[i]=((const uint16_t *)source)[i];text[i]=0;if(i==32767)error=234;}
        else if(wr_utf16(source,text,32768)<0)error=1113;
    } else {
        int found=(flags&0x800)?module_text((void *)source,id,language,text,32768):0;
        if(found<0)error=(unsigned)-found;
        if(!found && !error && (flags&0x1000))for(unsigned i=0;i<sizeof(errors)/sizeof(errors[0]);i++)if(errors[i].id==id){wr_utf16(errors[i].text,text,32768);found=1;break;}
        if(!found && !error)error=317;
    }
    for(unsigned i=0;!error && text[i];) {
        unsigned c=text[i++];
        if(c!='%'){if(n>=65534){error=234;break;}result[n++]=(uint16_t)c;continue;}
        unsigned start=i-1;c=text[i++];if(!c){error=87;break;}
        if(c=='0')break;
        if(c>='1' && c<='9') {
            unsigned index=c-'0';if(text[i]>='0' && text[i]<='9')index=index*10+text[i++]-'0';
            char spec[32]={'s',0};unsigned k=0;
            if(text[i]=='!'){i++;while(text[i] && text[i]!='!' && k<31){if(text[i]>127){error=87;break;}spec[k++]=(char)text[i++];}spec[k]=0;
                if(text[i]!='!' || !k){error=87;break;}i++;}
            if(flags&0x200){while(start<i && n<65534)result[n++]=text[start++];if(start<i)error=234;continue;}
            if(!arguments){error=87;break;}
            uintptr_t *args=(flags&0x2000)?arguments:*(uintptr_t **)arguments;
            if(!args){error=87;break;}uintptr_t value=args[index-1];
            unsigned width=0,j=0,zero=0;if(spec[j]=='0'){zero=1;j++;}while(spec[j]>='0' && spec[j]<='9'){width=width*10+spec[j++]-'0';if(width>65534){error=234;break;}}
            int is_wide=wide;if(spec[j]=='l' || spec[j]=='w'){is_wide=1;j++;}else if(spec[j]=='h'){is_wide=0;j++;}
            char kind=spec[j++];if(spec[j]){error=50;break;}
            uint16_t number[40];uint16_t *part=number;unsigned len=0;
            if(kind=='s' || kind=='S') {
                if(kind=='S')is_wide=!is_wide;
                if(!value){error=87;break;}
                if(is_wide){part=(void *)value;while(part[len] && len<65535)len++;}
                else {uint16_t *converted=wr_native_alloc(65536);if(!converted){error=8;break;}
                    int units=wr_utf16((const char *)value,converted,32768);if(units<0){wr_native_free(converted);error=1113;break;}
                    if(n+(width>(unsigned)units?width:(unsigned)units)>=65535){wr_native_free(converted);error=234;break;}
                    while(n+width>n+(unsigned)units && width>(unsigned)units){result[n++]=' ';width--;}
                    memcpy(result+n,converted,(size_t)units*2);n+=(unsigned)units;wr_native_free(converted);continue;}
            } else if(kind=='d' || kind=='i' || kind=='u' || kind=='x' || kind=='X' || kind=='p') {
                uint64_t v=kind=='p'?value:(uint32_t)value;int minus=0;
                if((kind=='d' || kind=='i') && (int32_t)value<0){minus=1;v=(uint64_t)-(int64_t)(int32_t)value;}
                unsigned base=(kind=='x' || kind=='X' || kind=='p')?16:10;uint16_t reversed[24];unsigned digits=0;
                do{unsigned digit=(unsigned)(v%base);reversed[digits++]=(uint16_t)(digit<10?'0'+digit:(kind=='X'?'A':'a')+digit-10);v/=base;}while(v);
                if(minus)number[len++]='-';while(digits)number[len++]=reversed[--digits];
            } else if(kind=='c' || kind=='C'){number[0]=(uint16_t)value;len=1;}
            else {error=50;break;}
            if(n+(width>len?width:len)>=65535){error=234;break;}
            while(width>len){result[n++]=zero?'0':' ';width--;}
            memcpy(result+n,part,(size_t)len*2);n+=len;
        } else {
            if(c=='n'){if(n>=65533){error=234;break;}result[n++]='\r';result[n++]='\n';}
            else if(n>=65534)error=234;
            else result[n++]=c=='r'?'\r':c=='t'?'\t':c=='b'?' ':(uint16_t)c;
        }
    }
    unsigned count=0;void *allocated=0;
    if(!error) {
        result[n]=0;void *data=result;size_t bytes=(n+1)*2;char *narrow=0;
        if(!wide){narrow=wr_native_alloc((size_t)n*4+1);if(!narrow)error=8;
            else if(wr_utf8(result,narrow,(size_t)n*4+1))error=1113;
            else {data=narrow;bytes=wr_len(narrow)+1;}}
        count=wide?n:(unsigned)(bytes-1);
        if(!error && (flags&0x100)) {
            void *(WR_WIN *allocate)(unsigned,size_t)=wr_heap_exports("LocalAlloc");
            size_t min=(size_t)capacity*(wide?2:1);allocated=allocate(0,min>bytes?min:bytes);
            if(!allocated)error=8;else {memcpy(allocated,data,bytes);if(wr_memory_copy(output,&allocated,sizeof(allocated)))error=998;}
        } else if(!error && capacity<count+1)error=122;
        else if(!error && wr_memory_copy(output,data,bytes))error=998;
        if(narrow)wr_native_free(narrow);
    }
    if(error && allocated){void *(WR_WIN *free_local)(void *)=wr_heap_exports("LocalFree");free_local(allocated);}
    wr_native_free(text);wr_native_free(result);return error?fail(error):count;
}
static unsigned WR_WIN FormatMessageW(unsigned flags,const void *source,unsigned id,unsigned language,void *out,unsigned size,void *args){return format(flags,source,id,language,out,size,args,1);}
static unsigned WR_WIN FormatMessageA(unsigned flags,const void *source,unsigned id,unsigned language,void *out,unsigned size,void *args){return format(flags,source,id,language,out,size,args,0);}
void *wr_message_exports(const char *name) {
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(FormatMessageA);E(FormatMessageW);
#undef E
    return 0;
}
