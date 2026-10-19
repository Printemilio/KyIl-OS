/* Read-only primary identity projection, never native authority. The SID and
   enabled groups are exactly those accepted by the existing pipe DACL broker.
   No NT administrator, impersonation, MIC or privilege-adjustment model is
   fabricated. Native capabilities remain authoritative for every operation. */
#include "process.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
#define TOKEN_TAG 0x62u
typedef struct {uint32_t uid,refs;} subject;
static struct {subject *object;uint32_t generation,access,flags;} handles[256];
static wr_mutex gate;
static int fail(unsigned e){wr_current_teb()->last_error=e;return 0;}
static int index(void *h){uintptr_t v=(uintptr_t)h;unsigned i=wr_handle_slot(v);
    return wr_handle_tag(v)==TOKEN_TAG && handles[i].object && handles[i].generation==wr_handle_generation(v)?(int)i:-1;}
static void *install(subject *s,unsigned access){unsigned i=0;while(i<256 && (handles[i].object || handles[i].generation>=WR_HANDLE_GENERATION_MAX))i++;
    if(i==256)return NULL;handles[i].generation++;handles[i].object=s;handles[i].access=access;handles[i].flags=0;s->refs++;
    return (void *)wr_handle_pack(TOKEN_TAG,handles[i].generation,i);}
void wr_token_quiesce(int begin){if(begin)wr_lock(&gate);else wr_unlock(&gate);}
unsigned wr_token_handle_count(void){wr_lock(&gate);unsigned count=0;for(unsigned i=0;i<256;i++)if(handles[i].object)count++;wr_unlock(&gate);return count;}
int wr_token_close(void *h){if(wr_handle_tag((uintptr_t)h)!=TOKEN_TAG)return -1;
    wr_lock(&gate);int i=index(h);unsigned why=i<0?6:(handles[i].flags&2)?5:0;
    if(!why){subject *s=handles[i].object;handles[i].object=NULL;if(!--s->refs)wr_native_free(s);}wr_unlock(&gate);return why?fail(why):1;}
int wr_token_flags(void *h,unsigned mask,unsigned flags,unsigned *out){if(wr_handle_tag((uintptr_t)h)!=TOKEN_TAG)return -1;
    wr_lock(&gate);int i=index(h);unsigned why=i<0?6:(mask&1 && flags&1)?50:0;
    if(!why){handles[i].flags=(handles[i].flags&~mask)|(flags&mask);unsigned value=handles[i].flags;if(out && wr_memory_copy(out,&value,4))why=998;}
    wr_unlock(&gate);return why?fail(why):1;}
void *wr_token_duplicate(void *h,int inherit){if(inherit){fail(50);return NULL;}wr_lock(&gate);int i=index(h);void *copy=i>=0?install(handles[i].object,handles[i].access):NULL;
    wr_unlock(&gate);if(!copy)fail(i<0?6:8);return copy;}
static int WR_WIN OpenProcessToken(void *process,unsigned access,void **out){
    static unsigned traced;if(__atomic_fetch_add(&traced,1,__ATOMIC_RELAXED)<16)wr_trace_contract("OpenProcessToken: process access out site",(uintptr_t)process,access,(uintptr_t)out,(uintptr_t)__builtin_return_address(0));
    if(!out)return fail(87);void *zero=NULL;if(wr_memory_copy(out,&zero,sizeof(zero)))return fail(998);
    /* TOKEN_QUERY and TOKEN_DUPLICATE are real. WRITE_DAC, assignment,
       impersonation, adjustment and privilege grants are not accepted. */
    if(!access)return fail(5);if(access&~0xau)return fail(50);if(!wr_process_self_access(process,0x1000))return 0;
    vex_user_identity_t id={.size=sizeof(id)};if(vex_syscall1(VEX_SYS_USER_IDENTITY,(uintptr_t)&id))return fail(1359);
    subject *s=wr_native_alloc(sizeof(*s));if(!s)return fail(8);*s=(subject){.uid=id.uid};wr_lock(&gate);void *h=install(s,access);wr_unlock(&gate);
    if(!h){wr_native_free(s);return fail(8);}if(wr_memory_copy(out,&h,sizeof(h))){wr_token_close(h);return fail(998);}return 1;
}
static int WR_WIN OpenThreadToken(void *thread,unsigned access,int open_as_self,void **out){
    (void)access;(void)open_as_self;if(!out)return fail(87);void *zero=NULL;if(wr_memory_copy(out,&zero,sizeof(zero)))return fail(998);
    unsigned (WR_WIN *get_id)(void *)=wr_thread_exports("GetThreadId");if(!get_id(thread))return 0;
    /* There is no successful impersonation API in this personality. */
    return fail(1008);
}
static void user_sid(uint8_t *p,unsigned uid){p[0]=1;p[1]=2;p[7]=22;unsigned kind=1;memcpy(p+8,&kind,4);memcpy(p+12,&uid,4);}
static void group_sid(uint8_t *p,unsigned authority,unsigned rid){p[0]=1;p[1]=1;p[7]=(uint8_t)authority;memcpy(p+8,&rid,4);}
static int WR_WIN GetTokenInformation(void *handle,unsigned kind,void *out,unsigned capacity,unsigned *used){unsigned uid=0,why=0;
    wr_lock(&gate);int i=index(handle);if(i<0)why=6;else if(!(handles[i].access&8))why=5;else uid=handles[i].object->uid;wr_unlock(&gate);
    if(why)return fail(why);if(!used)return fail(87);
    uint8_t data[128]={0};unsigned size=0;uintptr_t pointer;unsigned value;
    switch(kind){
    case 1:size=32;pointer=(uintptr_t)out+16;memcpy(data,&pointer,8);user_sid(data+16,uid);break;
    case 2:size=64;value=2;memcpy(data,&value,4);pointer=(uintptr_t)out+40;memcpy(data+8,&pointer,8);pointer=(uintptr_t)out+52;memcpy(data+24,&pointer,8);
        value=7;memcpy(data+16,&value,4);memcpy(data+32,&value,4);group_sid(data+40,1,0);group_sid(data+52,5,11);break;
    case 3:size=4;break; /* No Windows privileges are granted by a native UID. */
    case 4:size=24;pointer=(uintptr_t)out+8;memcpy(data,&pointer,8);user_sid(data+8,uid);break;
    case 8:size=4;value=1;memcpy(data,&value,4);break;
    case 11:size=8;break; /* No Windows restricted-SID filtering is installed. */
    case 18:size=4;value=1;memcpy(data,&value,4);break; /* Default; no linked UAC token. */
    case 20:case 23:case 24:case 26:case 29:size=4;break;
    default:wr_trace_contract("GetTokenInformation unsupported: kind size handle site",kind,capacity,(uintptr_t)handle,(uintptr_t)__builtin_return_address(0));return fail(kind && kind<64?50:87);
    }
    if(wr_memory_copy(used,&size,4))return fail(998);if(capacity<size)return fail(122);
    if(!out || (uintptr_t)out>UINTPTR_MAX-size || wr_memory_copy(out,data,size))return fail(998);return 1;
}
void *wr_token_exports(const char *name){
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(OpenProcessToken);E(OpenThreadToken);E(GetTokenInformation);
#undef E
    return NULL;
}
