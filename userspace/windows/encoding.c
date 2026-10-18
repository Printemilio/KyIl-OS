#include "windows.h"
static int supported(unsigned page){return page==0 || page==1 || page==3 || page==65001;}
static int error(unsigned code){wr_current_teb()->last_error=code;return 0;}
static int wide(const uint16_t *s,int n,unsigned flags,char *out,int capacity) {
    int at=0,bytes=0;
    while(at<n) {
        uint32_t c=s[at++];
        if(c>=0xd800 && c<=0xdbff) {
            if(at<n && s[at]>=0xdc00 && s[at]<=0xdfff)c=0x10000+((c-0xd800)<<10)+(s[at++]-0xdc00);
            else {if(flags)return error(1113);c=0xfffd;}
        }else if(c>=0xdc00 && c<=0xdfff){if(flags)return error(1113);c=0xfffd;}
        unsigned count=c<128?1:c<2048?2:c<65536?3:4;
        if(bytes>INT32_MAX-(int)count)return error(8);
        if(out){if(bytes+(int)count>capacity)return error(122);
            if(count==1)out[bytes]=(char)c;
            else {out[bytes]=(char)((count==2?0xc0:count==3?0xe0:0xf0)|(c>>(6*(count-1))));for(unsigned j=1;j<count;j++)out[bytes+j]=(char)(0x80|((c>>(6*(count-1-j)))&63));}}
        bytes+=(int)count;
    }return bytes;
}
static int WR_WIN WideCharToMultiByte(unsigned page,unsigned flags,const uint16_t *source,int count,char *out,int capacity,const char *default_char,int *used_default) {
    if(!supported(page))return error(50);
    if(flags&~0x80u)return error(1004);
    if(!source || count==0 || count< -1 || capacity<0 || (!out && capacity) || default_char || used_default || (void *)source==(void *)out)return error(87);
    if(count==-1){count=0;while(source[count]){if(count==INT32_MAX-1)return error(8);count++;}count++;}
    int required=wide(source,count,flags,0,0);if(!required || !capacity)return required;
    if(required>capacity)return error(122);return wide(source,count,flags,out,capacity);
}
/* Windows UTF-8 replacement consumes a continuation that violates a lead's
   scalar bound, but never consumes a following non-continuation byte. */
static int narrow(const uint8_t *s,int n,unsigned flags,uint16_t *out,int capacity) {
    int at=0,units=0;
    while(at<n) {
        unsigned start=at,c=s[at++],extra=0;uint32_t cp=c;int valid=1;
        if(c<128)extra=0;
        else if(c>=0xc2 && c<=0xdf){cp=c&31;extra=1;}
        else if(c>=0xe0 && c<=0xef){cp=c&15;extra=2;}
        else if(c>=0xf0 && c<=0xf4){cp=c&7;extra=3;}
        else valid=0;
        for(unsigned j=0;valid && j<extra;j++) {
            if(at>=n){valid=0;break;}unsigned b=s[at];
            if((b&0xc0)!=0x80){valid=0;break;}
            if(j==0 && ((c==0xe0 && b<0xa0) || (c==0xed && b>=0xa0) || (c==0xf0 && b<0x90) || (c==0xf4 && b>=0x90))){at++;valid=0;break;}
            at++;cp=(cp<<6)|(b&63);
        }
        if(!valid){if(flags)return error(1113);cp=0xfffd;if(at==(int)start)at++;}
        int count=cp>=65536?2:1;if(units>INT32_MAX-count)return error(8);
        if(out){if(units+count>capacity)return error(122);if(count==1)out[units]=(uint16_t)cp;else{cp-=65536;out[units]=(uint16_t)(0xd800+(cp>>10));out[units+1]=(uint16_t)(0xdc00+(cp&1023));}}
        units+=count;
    }return units;
}
static int WR_WIN MultiByteToWideChar(unsigned page,unsigned flags,const char *source,int count,uint16_t *out,int capacity) {
    if(!supported(page))return error(50);if(flags&~8u)return error(1004);
    if(!source || count==0 || count< -1 || capacity<0 || (!out && capacity) || (void *)source==(void *)out)return error(87);
    if(count==-1){size_t n=wr_len(source);if(n>=INT32_MAX)return error(8);count=(int)n+1;}
    int required=narrow((const uint8_t *)source,count,flags,0,0);if(!required || !capacity)return required;
    if(required>capacity)return error(122);return narrow((const uint8_t *)source,count,flags,out,capacity);
}
static unsigned WR_WIN GetOEMCP(void){return 65001;}
static int WR_WIN IsValidCodePage(unsigned page){return page==65001;}
void *wr_encoding_exports(const char *name) {
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(WideCharToMultiByte);E(MultiByteToWideChar);E(GetOEMCP);E(IsValidCodePage);
#undef E
    return 0;
}
