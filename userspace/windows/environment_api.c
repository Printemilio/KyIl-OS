#include "process.h"
static wr_mutex allocations_gate;
typedef struct allocation {struct allocation *next;uint16_t *data;} allocation;
static allocation *allocations;
void wr_environment_quiesce(int begin) {
    if(begin){wr_environment_lock(1);wr_lock(&allocations_gate);}
    else {wr_unlock(&allocations_gate);wr_environment_lock(0);}
}
static unsigned length(const uint16_t *s){unsigned n=0;while(s[n])n++;return n;}
static unsigned lower(unsigned n){return n>='A' && n<='Z'?n+32:n;}
static int key(const uint16_t *a,const uint16_t *b,unsigned n) {
    for(unsigned i=0;i<n;i++)if(!a[i] || a[i]=='=' || lower(a[i])!=lower(b[i]))return 0;return a[n]=='=';
}
static int valid(const uint16_t *name,unsigned *n) {
    if(!name || !*name)return 87;*n=0;
    while(name[*n]){if(*n>=32766 || name[*n]=='=')return 87;if(name[*n]>127)return 50;(*n)++;}return 0;
}
static unsigned WR_WIN GetEnvironmentVariableW(const uint16_t *name,uint16_t *out,unsigned cap) {
    unsigned n,why=valid(name,&n);if(why){wr_current_teb()->last_error=why;return 0;}
    wr_environment_lock(1);const uint16_t *block=wr_environment_block(),*value=0;
    for(const uint16_t *s=block;*s;s+=length(s)+1)if(key(s,name,n)){value=s+n+1;break;}
    unsigned result=0;
    if(value){unsigned bytes=length(value);if(bytes>=cap)result=bytes+1;else if(out){memcpy(out,value,(bytes+1)*2);result=bytes;}else why=87;}
    else {why=203;if(out && cap)*out=0;}
    wr_environment_lock(0);if(why)wr_current_teb()->last_error=why;return result;
}
static uint16_t *WR_WIN GetEnvironmentStringsW(void) {
    allocation *a=wr_native_alloc(sizeof(*a));uint16_t *data=wr_native_alloc(65536);
    if(!a || !data){if(a)wr_native_free(a);if(data)wr_native_free(data);wr_current_teb()->last_error=8;return 0;}
    if(wr_environment_snapshot(data,32768)<0){wr_native_free(data);wr_native_free(a);wr_current_teb()->last_error=8;return 0;}
    a->data=data;wr_lock(&allocations_gate);a->next=allocations;allocations=a;wr_unlock(&allocations_gate);return data;
}
static int WR_WIN FreeEnvironmentStringsW(uint16_t *data) {
    wr_lock(&allocations_gate);allocation **p=&allocations;while(*p && (*p)->data!=data)p=&(*p)->next;
    allocation *a=*p;if(a)*p=a->next;wr_unlock(&allocations_gate);
    if(!a){wr_current_teb()->last_error=87;return 0;}wr_native_free(a->data);wr_native_free(a);return 1;
}
static int WR_WIN SetEnvironmentVariableW(const uint16_t *name,const uint16_t *value) {
    unsigned n,why=valid(name,&n);if(why){wr_current_teb()->last_error=why;return 0;}
    unsigned v=value?length(value):0;if(v>32764 || n>32764-v){wr_current_teb()->last_error=8;return 0;}
    uint16_t *next=wr_native_alloc(65536);if(!next){wr_current_teb()->last_error=8;return 0;}
    wr_environment_lock(1);const uint16_t *old=wr_environment_block();unsigned at=0;
    for(const uint16_t *s=old;*s;s+=length(s)+1) {
        unsigned size=length(s)+1;if(key(s,name,n))continue;
        if(size>32766-at){why=8;break;}memcpy(next+at,s,size*2);at+=size;
    }
    if(!why && value){if(n+v+2>32767-at)why=8;else{memcpy(next+at,name,n*2);at+=n;next[at++]='=';memcpy(next+at,value,(v+1)*2);at+=v+1;}}
    if(!at)next[at++]=0;next[at++]=0;
    if(!why && wr_environment_replace(next,at))why=8;
    wr_environment_lock(0);wr_native_free(next);if(why)wr_current_teb()->last_error=why;return !why;
}
static unsigned WR_WIN ExpandEnvironmentStringsW(const uint16_t *source,uint16_t *out,unsigned cap) {
    if(!source || (!out && cap)){wr_current_teb()->last_error=87;return 0;}
    wr_environment_lock(1);const uint16_t *block=wr_environment_block();size_t total=0;
    for(unsigned i=0;source[i];) {
        const uint16_t *data=source+i;unsigned count=1,advance=1;
        if(source[i]=='%') {
            unsigned end=i+1;while(source[end] && source[end]!='%')end++;
            if(source[end]=='%')for(const uint16_t *s=block;*s;s+=length(s)+1)
                if(key(s,source+i+1,end-i-1)){data=s+(end-i);count=length(data);advance=end-i+1;break;}
        }
        if(total>UINT32_MAX-1-count){wr_environment_lock(0);wr_current_teb()->last_error=8;return 0;}
        if(out && total+count<cap)memcpy(out+total,data,count*2);total+=count;i+=advance;
    }
    if(out && total<cap)out[total]=0;wr_environment_lock(0);return (unsigned)total+1;
}
void *wr_environment_exports(const char *name) {
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(GetEnvironmentVariableW);E(GetEnvironmentStringsW);E(FreeEnvironmentStringsW);E(SetEnvironmentVariableW);E(ExpandEnvironmentStringsW);
#undef E
    return 0;
}
