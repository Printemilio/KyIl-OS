#include "windows.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
/* Windows security DATA contracts. Serialization/inspection never grants
 * native authority, changes an ACL on a kernel object, or invents a token.
 * Native object access remains capability-controlled at its owning boundary. */
typedef struct {const uint16_t *p;size_t n;} text;
typedef struct {uint8_t *p;size_t size,capacity;} bytes;
typedef struct {uint8_t revision,pad;uint16_t control;void *owner,*group,*sacl,*dacl;} absolute_sd;
typedef struct {uint8_t revision,pad;uint16_t control;uint32_t owner,group,sacl,dacl;} relative_sd;
_Static_assert(sizeof(absolute_sd)==40 && sizeof(relative_sd)==20,"AMD64 security descriptors");
static int failure(unsigned error){wr_current_teb()->last_error=error;return 0;}
static void *local_alloc(size_t size){void *(WR_WIN *allocate)(unsigned,size_t)=wr_heap_exports("LocalAlloc");return allocate(0,size);}
static void local_free(void *p){void *(WR_WIN *release)(void *)=wr_heap_exports("LocalFree");if(p)release(p);}
static int reserve(bytes *b,size_t size){
    if(size<=b->capacity)return 1;size_t capacity=b->capacity?b->capacity:128;
    while(capacity<size){if(capacity>SIZE_MAX/2){capacity=size;break;}capacity*=2;}
    uint8_t *p=wr_native_alloc(capacity);if(!p)return failure(8);if(b->size)memcpy(p,b->p,b->size);
    if(b->p)wr_native_free(b->p);b->p=p;b->capacity=capacity;return 1;
}
static void dispose(bytes *b){if(b->p)wr_native_free(b->p);*b=(bytes){0};}
static int capture(const uint16_t *source,bytes *b,text *out){
    if(!source)return failure(87);size_t count=0;
    for(;;){uint16_t chunk[128];size_t units=(4096-(((uintptr_t)source+count*2)&4095))/2;
        if(!units)units=1;if(units>128)units=128;
        if(count>(SIZE_MAX-sizeof(chunk))/2)return failure(8);
        if(wr_memory_copy(chunk,source+count,units*2))return failure(998);
        size_t n=0;while(n<units && chunk[n])n++;
        if(!reserve(b,(count+n+1)*2))return 0;memcpy(b->p+count*2,chunk,n*2);count+=n;b->size=count*2;
        if(n<units){((uint16_t *)b->p)[count]=0;*out=(text){(void *)b->p,count};return 1;}
    }
}
static int space(uint16_t c){return c==' ' || c=='\t' || c=='\r' || c=='\n';}
static text trim(text s){while(s.n && space(*s.p)){s.p++;s.n--;}while(s.n && space(s.p[s.n-1]))s.n--;return s;}
static int equal(text s,const char *a){size_t n=wr_len(a);if(s.n!=n)return 0;for(size_t i=0;i<n;i++)if(s.p[i]!=(uint8_t)a[i])return 0;return 1;}
static int digit(uint16_t c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static int number(text s,uint64_t maximum,uint64_t *out){
    unsigned base=10;size_t i=0;if(s.n>2 && s.p[0]=='0' && (s.p[1]=='x'||s.p[1]=='X')){base=16;i=2;}if(i==s.n)return 0;
    uint64_t value=0;for(;i<s.n;i++){int d=digit(s.p[i]);if(d<0 || (unsigned)d>=base || value>(maximum-(unsigned)d)/base)return 0;value=value*base+(unsigned)d;}*out=value;return 1;
}
static const struct {const char *name,*sid;} aliases[]={
    {"SY","S-1-5-18"},{"BA","S-1-5-32-544"},{"BU","S-1-5-32-545"},{"BG","S-1-5-32-546"},
    {"AU","S-1-5-11"},{"AN","S-1-5-7"},{"WD","S-1-1-0"},{"CO","S-1-3-0"},{"CG","S-1-3-1"},{"OW","S-1-3-4"},
    {"LS","S-1-5-19"},{"NS","S-1-5-20"},{"NU","S-1-5-2"},{"SU","S-1-5-6"},{"IU","S-1-5-4"},
    {"RC","S-1-5-12"},{"ED","S-1-5-9"},{"PS","S-1-5-10"},{"BO","S-1-5-32-551"},{"PO","S-1-5-32-550"},
    {"SO","S-1-5-32-549"},{"AO","S-1-5-32-548"},{"RD","S-1-5-32-555"},{"RU","S-1-5-32-554"},
    {"AC","S-1-15-2-1"},{"LW","S-1-16-4096"},{"ME","S-1-16-8192"},{"MP","S-1-16-8448"},{"HI","S-1-16-12288"},{"SI","S-1-16-16384"}
};
static int parse_sid(text s,uint8_t out[68],unsigned *size){
    uint16_t expanded[96];s=trim(s);
    if(s.n==2){const char *a=0;for(unsigned i=0;i<sizeof(aliases)/sizeof(*aliases);i++)if(equal(s,aliases[i].name)){a=aliases[i].sid;break;}
        if(!a)return failure(1337);size_t n=0;while(a[n]){expanded[n]=(uint8_t)a[n];n++;}s=(text){expanded,n};}
    if(s.n<5 || s.p[0]!='S' || s.p[1]!='-' || s.p[2]!='1' || s.p[3]!='-')return failure(1337);
    size_t at=4,first=at;while(at<s.n && s.p[at]!='-')at++;uint64_t authority;
    if(!number((text){s.p+first,at-first},0xffffffffffffull,&authority))return failure(1337);
    memset(out,0,68);out[0]=1;for(unsigned i=0;i<6;i++)out[2+i]=(uint8_t)(authority>>(40-8*i));unsigned count=0;
    while(at<s.n){first=++at;while(at<s.n && s.p[at]!='-')at++;uint64_t value;
        if(count==15 || !number((text){s.p+first,at-first},UINT32_MAX,&value))return failure(1337);
        uint32_t sub=(uint32_t)value;memcpy(out+8+4*count,&sub,4);count++;}
    out[1]=(uint8_t)count;*size=8+count*4;return 1;
}
static int sid_bytes(const void *source,uint8_t out[68],unsigned *size){
    uint8_t header[8];if(!source || wr_memory_copy(header,source,8) || header[0]!=1 || header[1]>15)return failure(1337);
    unsigned n=8+4*header[1];if(wr_memory_copy(out,source,n) || out[0]!=header[0] || out[1]!=header[1])return failure(1337);*size=n;return 1;
}
static int WR_WIN IsValidSid(const void *sid){uint8_t copy[68];unsigned n;return sid_bytes(sid,copy,&n);}
static unsigned WR_WIN GetLengthSid(const void *sid){uint8_t copy[68];unsigned n;return sid_bytes(sid,copy,&n)?n:0;}
static int WR_WIN EqualSid(const void *a,const void *b){uint8_t first[68],second[68];unsigned n,m;if(!sid_bytes(a,first,&n)||!sid_bytes(b,second,&m))return 0;
    if(n!=m)return 0;for(unsigned i=0;i<n;i++)if(first[i]!=second[i])return 0;return 1;}
static int WR_WIN CopySid(unsigned capacity,void *out,const void *sid){uint8_t copy[68];unsigned n;if(!sid_bytes(sid,copy,&n))return 0;
    if(capacity<n)return failure(122);return wr_memory_copy(out,copy,n)?failure(998):1;}
static int WR_WIN ConvertStringSidToSidW(const uint16_t *string,void **out){
    if(!out)return failure(87);bytes captured={0};text input;uint8_t sid[68];unsigned size;int result=0;
    if(capture(string,&captured,&input) && parse_sid(input,sid,&size)){void *p=local_alloc(size);if(p){memcpy(p,sid,size);
            if(wr_memory_copy(out,&p,sizeof(p))){local_free(p);failure(998);}else result=1;}}
    dispose(&captured);return result;
}
static int acl_validate(const uint8_t *p,unsigned n,unsigned *used){
    if(n<8 || (p[0]!=2 && p[0]!=4) || pe_u16(p+2)!=n)return 0;
    unsigned at=8,count=pe_u16(p+4);for(unsigned i=0;i<count;i++){
        if(!pe_span(at,4,n))return 0;unsigned type=p[at],size=pe_u16(p+at+2),sid=8;
        if(size<4 || (size&3) || !pe_span(at,size,n))return 0;
        if(type>=5 && type<=8){if(p[0]!=4 || size<12)return 0;unsigned flags=pe_u32(p+at+8);if(flags&~3u)return 0;sid=12+((flags&1)?16:0)+((flags&2)?16:0);}
        else if(type>3 && type!=17)return 0;
        if(!pe_span(sid,8,size) || p[at+sid]!=1 || p[at+sid+1]>15 || !pe_span(sid,8+4u*p[at+sid+1],size))return 0;
        at+=size;
    }if(used)*used=at;return 1;
}
static int acl_copy(const void *source,bytes *b,unsigned *used){
    uint8_t header[8];if(!source || wr_memory_copy(header,source,8))return failure(1336);unsigned size=pe_u16(header+2);
    if(size<8 || !reserve(b,size))return size<8?failure(1336):0;
    if(wr_memory_copy(b->p,source,size) || !acl_validate(b->p,size,used))return failure(1336);b->size=size;return 1;
}
static int WR_WIN IsValidAcl(const void *acl){bytes copy={0};int result=acl_copy(acl,&copy,0);dispose(&copy);return result;}
static int WR_WIN InitializeAcl(void *acl,unsigned length,unsigned revision){
    if(revision!=2 && revision!=4)return failure(1305);if(length<8 || length>65535)return failure(1336);
    uint8_t header[8]={0};header[0]=(uint8_t)revision;uint16_t size=(uint16_t)length;memcpy(header+2,&size,2);
    return wr_memory_copy(acl,header,8)?failure(998):1;
}
static int WR_WIN GetAce(void *acl,unsigned index,void **out){bytes copy={0};int result=0;
    if(!out)return failure(87);if(acl_copy(acl,&copy,0)){if(index>=pe_u16(copy.p+4))failure(87);else{
            unsigned at=8;for(unsigned i=0;i<index;i++)at+=pe_u16(copy.p+at+2);void *p=(uint8_t *)acl+at;result=wr_memory_copy(out,&p,sizeof(p))?failure(998):1;}}
    dispose(&copy);return result;
}
static int add_ace(void *acl,unsigned revision,unsigned mask,const void *sid,unsigned type,unsigned flags){
    if(revision!=2 && revision!=4)return failure(1305);bytes copy={0};uint8_t value[68];unsigned length,used;int result=0;
    if(sid_bytes(sid,value,&length) && acl_copy(acl,&copy,&used)){
        if(used+8+length>copy.size)failure(1344);
        else {uint8_t *p=copy.p+used;p[0]=(uint8_t)type;p[1]=(uint8_t)flags;uint16_t size=(uint16_t)(length+8);memcpy(p+2,&size,2);memcpy(p+4,&mask,4);memcpy(p+8,value,length);
            uint16_t count=pe_u16(copy.p+4)+1;memcpy(copy.p+4,&count,2);if(revision>copy.p[0])copy.p[0]=(uint8_t)revision;
            result=wr_memory_copy(acl,copy.p,used+size)?failure(998):1;}}
    dispose(&copy);return result;
}
static int WR_WIN AddAccessAllowedAce(void *acl,unsigned revision,unsigned mask,const void *sid){return add_ace(acl,revision,mask,sid,0,0);}
static int WR_WIN AddAccessDeniedAce(void *acl,unsigned revision,unsigned mask,const void *sid){return add_ace(acl,revision,mask,sid,1,0);}
static int WR_WIN AddAuditAccessAce(void *acl,unsigned revision,unsigned mask,const void *sid,int success,int failed){return add_ace(acl,revision,mask,sid,2,(success?0x40:0)|(failed?0x80:0));}
static int descriptor(const void *source,absolute_sd *out){
    uint8_t header[4];if(!source || wr_memory_copy(header,source,4) || header[0]!=1)return failure(1338);
    memset(out,0,sizeof(*out));out->revision=1;out->pad=header[1];out->control=pe_u16(header+2);
    if(out->control&0x8000){relative_sd relative;if(wr_memory_copy(&relative,source,sizeof(relative)))return failure(1338);
        uint32_t offsets[4]={relative.owner,relative.group,relative.sacl,relative.dacl};void **pointers[4]={&out->owner,&out->group,&out->sacl,&out->dacl};
        for(unsigned i=0;i<4;i++)if(offsets[i]){if(offsets[i]<20 || (offsets[i]&3) || (uintptr_t)source>UINTPTR_MAX-offsets[i])return failure(1338);*pointers[i]=(uint8_t *)source+offsets[i];}}
    else if(wr_memory_copy(out,source,sizeof(*out)))return failure(1338);
    uint8_t sid[68];unsigned length;if((out->owner && !sid_bytes(out->owner,sid,&length)) || (out->group && !sid_bytes(out->group,sid,&length)))return 0;
    if((out->control&4) && out->dacl && !IsValidAcl(out->dacl))return 0;if((out->control&16) && out->sacl && !IsValidAcl(out->sacl))return 0;return 1;
}
static int WR_WIN IsValidSecurityDescriptor(const void *sd){absolute_sd state;return descriptor(sd,&state);}
static int WR_WIN InitializeSecurityDescriptor(void *out,unsigned revision){if(revision!=1)return failure(1305);absolute_sd sd={.revision=1};return wr_memory_copy(out,&sd,sizeof(sd))?failure(998):1;}
static int WR_WIN GetSecurityDescriptorControl(const void *sd,uint16_t *control,unsigned *revision){absolute_sd state;if(!control||!revision)return failure(87);
    if(!descriptor(sd,&state))return 0;unsigned r=state.revision;return wr_memory_copy(control,&state.control,2)||wr_memory_copy(revision,&r,4)?failure(998):1;}
static int get_acl(const void *sd,int *present,void **acl,int *defaulted,int sacl){absolute_sd state;if(!present||!acl||!defaulted)return failure(87);if(!descriptor(sd,&state))return 0;
    int has=(state.control&(sacl?16:4))!=0,def=(state.control&(sacl?32:8))!=0;void *value=sacl?state.sacl:state.dacl;
    return wr_memory_copy(present,&has,4)||wr_memory_copy(acl,&value,sizeof(value))||wr_memory_copy(defaulted,&def,4)?failure(998):1;
}
static int WR_WIN GetSecurityDescriptorDacl(const void *sd,int *present,void **acl,int *defaulted){return get_acl(sd,present,acl,defaulted,0);}
static int WR_WIN GetSecurityDescriptorSacl(const void *sd,int *present,void **acl,int *defaulted){return get_acl(sd,present,acl,defaulted,1);}
static int get_sid(const void *sd,void **sid,int *defaulted,int group){absolute_sd state;if(!sid||!defaulted)return failure(87);if(!descriptor(sd,&state))return 0;
    void *value=group?state.group:state.owner;int def=(state.control&(group?2:1))!=0;return wr_memory_copy(sid,&value,sizeof(value))||wr_memory_copy(defaulted,&def,4)?failure(998):1;}
static int WR_WIN GetSecurityDescriptorOwner(const void *sd,void **owner,int *defaulted){return get_sid(sd,owner,defaulted,0);}
static int WR_WIN GetSecurityDescriptorGroup(const void *sd,void **group,int *defaulted){return get_sid(sd,group,defaulted,1);}
static int set_acl(void *sd,int present,void *acl,int defaulted,int sacl){absolute_sd state;
    if(!descriptor(sd,&state))return 0;if(state.control&0x8000)return failure(1338);if(present && acl && !IsValidAcl(acl))return 0;
    unsigned has=sacl?16:4,def=sacl?32:8;state.control=(uint16_t)((state.control&~(has|def))|(present?has:0)|(defaulted?def:0));
    if(sacl)state.sacl=present?acl:0;else state.dacl=present?acl:0;return wr_memory_copy(sd,&state,sizeof(state))?failure(998):1;
}
static int WR_WIN SetSecurityDescriptorDacl(void *sd,int present,void *acl,int defaulted){return set_acl(sd,present,acl,defaulted,0);}
static int WR_WIN SetSecurityDescriptorSacl(void *sd,int present,void *acl,int defaulted){return set_acl(sd,present,acl,defaulted,1);}
static int set_sid(void *sd,void *sid,int defaulted,int group){absolute_sd state;uint8_t bytes[68];unsigned n;
    if(!descriptor(sd,&state))return 0;if(state.control&0x8000)return failure(1338);if(sid && !sid_bytes(sid,bytes,&n))return 0;
    unsigned bit=group?2:1;state.control=(uint16_t)((state.control&~bit)|(defaulted?bit:0));if(group)state.group=sid;else state.owner=sid;
    return wr_memory_copy(sd,&state,sizeof(state))?failure(998):1;
}
static int WR_WIN SetSecurityDescriptorOwner(void *sd,void *sid,int defaulted){return set_sid(sd,sid,defaulted,0);}
static int WR_WIN SetSecurityDescriptorGroup(void *sd,void *sid,int defaulted){return set_sid(sd,sid,defaulted,1);}
static int guid(text s,uint8_t out[16]){
    if(s.n!=36 || s.p[8]!='-' || s.p[13]!='-' || s.p[18]!='-' || s.p[23]!='-')return 0;
    uint8_t raw[16];unsigned n=0;for(unsigned i=0;i<36;){if(s.p[i]=='-'){i++;continue;}int a=digit(s.p[i++]),b=digit(s.p[i++]);if(a<0||b<0||n==16)return 0;raw[n++]=(uint8_t)((a<<4)|b);}
    if(n!=16)return 0;for(unsigned i=0;i<4;i++)out[i]=raw[3-i];out[4]=raw[5];out[5]=raw[4];out[6]=raw[7];out[7]=raw[6];memcpy(out+8,raw+8,8);return 1;
}
static const struct {const char *name;unsigned value;} rights[]={
    {"GA",0x10000000},{"GR",0x80000000},{"GW",0x40000000},{"GX",0x20000000},{"RC",0x20000},{"SD",0x10000},{"WD",0x40000},{"WO",0x80000},
    {"RP",0x10},{"WP",0x20},{"CC",1},{"DC",2},{"LC",4},{"SW",8},{"DT",0x40},{"LO",0x80},{"CR",0x100},
    {"FA",0x1f01ff},{"FR",0x120089},{"FW",0x120116},{"FX",0x1200a0},{"KA",0xf003f},{"KR",0x20019},{"KW",0x20006},{"KX",0x20019},
    {"NW",1},{"NR",2},{"NX",4}
};
static int mask(text s,unsigned *value){s=trim(s);*value=0;if(!s.n)return 1;if(s.p[0]>='0'&&s.p[0]<='9'){uint64_t n;if(!number(s,UINT32_MAX,&n))return 0;*value=(unsigned)n;return 1;}
    if(s.n&1)return 0;for(size_t at=0;at<s.n;at+=2){int found=0;for(unsigned i=0;i<sizeof(rights)/sizeof(*rights);i++)if(equal((text){s.p+at,2},rights[i].name)){*value|=rights[i].value;found=1;break;}if(!found)return 0;}return 1;}
static int parse_ace(text s,bytes *acl){
    text fields[6];size_t at=0;for(unsigned i=0;i<6;i++){size_t first=at;while(at<s.n && s.p[at]!=';')at++;fields[i]=trim((text){s.p+first,at-first});if(i<5){if(at==s.n)return failure(87);at++;}else if(at!=s.n)return failure(50);}
    unsigned type;if(equal(fields[0],"A"))type=0;else if(equal(fields[0],"D"))type=1;else if(equal(fields[0],"AU"))type=2;else if(equal(fields[0],"AL"))type=3;
    else if(equal(fields[0],"OA"))type=5;else if(equal(fields[0],"OD"))type=6;else if(equal(fields[0],"OU"))type=7;else if(equal(fields[0],"OL"))type=8;else if(equal(fields[0],"ML"))type=17;
    else return failure(50); /* Conditional/resource-attribute ACEs are not fabricated. */
    static const struct {const char *name;unsigned value;} flags[]={{"OI",1},{"CI",2},{"NP",4},{"IO",8},{"ID",16},{"SA",64},{"FA",128}};
    unsigned attributes=0;if(fields[1].n&1)return failure(87);
    for(size_t i=0;i<fields[1].n;i+=2){int found=0;for(unsigned j=0;j<sizeof(flags)/sizeof(*flags);j++)if(equal((text){fields[1].p+i,2},flags[j].name)){attributes|=flags[j].value;found=1;break;}if(!found)return failure(87);}
    unsigned access;if(!mask(fields[2],&access))return failure(87);uint8_t sid[68],guids[32];unsigned sid_size;
    if(!parse_sid(fields[5],sid,&sid_size))return 0;
    unsigned object=0,extra=0;if(type>=5 && type<=8){for(unsigned i=0;i<2;i++)if(fields[3+i].n){if(!guid(fields[3+i],guids+extra))return failure(87);extra+=16;object|=1u<<i;}
        if(!object)type-=5;else extra+=4;}
    else if(fields[3].n||fields[4].n)return failure(87);
    unsigned size=8+extra+sid_size;if(acl->size>65535-size || pe_u16(acl->p+4)==65535)return failure(1336);
    if(!reserve(acl,acl->size+size))return 0;uint8_t *p=acl->p+acl->size;p[0]=(uint8_t)type;p[1]=(uint8_t)attributes;uint16_t small=(uint16_t)size;memcpy(p+2,&small,2);memcpy(p+4,&access,4);
    if(object){memcpy(p+8,&object,4);memcpy(p+12,guids,extra-4);acl->p[0]=4;}
    memcpy(p+8+extra,sid,sid_size);acl->size+=size;uint16_t count=pe_u16(acl->p+4)+1;memcpy(acl->p+4,&count,2);small=(uint16_t)acl->size;memcpy(acl->p+2,&small,2);return 1;
}
static int parse_acl(text s,bytes *acl,uint16_t *control,int sacl){
    s=trim(s);*control|=sacl?16:4;size_t at=0;while(at<s.n && s.p[at]!='('){
        if(space(s.p[at])){at++;continue;}
        if(s.p[at]=='P'){*control|=sacl?0x2000:0x1000;at++;}
        else if(at+1<s.n && s.p[at]=='A' && s.p[at+1]=='R'){*control|=sacl?0x200:0x100;at+=2;}
        else if(at+1<s.n && s.p[at]=='A' && s.p[at+1]=='I'){*control|=sacl?0x800:0x400;at+=2;}
        else if(equal(trim((text){s.p+at,s.n-at}),"NO_ACCESS_CONTROL"))return 1;
        else return failure(87);
    }
    if(!reserve(acl,8))return 0;memset(acl->p,0,8);acl->p[0]=2;acl->p[2]=8;acl->size=8;
    while(at<s.n){while(at<s.n && space(s.p[at]))at++;if(at==s.n)break;if(s.p[at++]!='(')return failure(87);
        size_t first=at;while(at<s.n && s.p[at]!=')'){if(s.p[at]=='(')return failure(50);at++;}if(at==s.n)return failure(87);
        if(!parse_ace((text){s.p+first,at-first},acl))return 0;at++;}
    return 1;
}
static int WR_WIN ConvertStringSecurityDescriptorToSecurityDescriptorW(const uint16_t *string,unsigned revision,void **out,unsigned *length){
    if(revision!=1)return failure(1305);if(!out)return failure(87);bytes captured={0},acls[2]={{0}};text input;uint8_t sids[2][68];unsigned sizes[2]={0};uint16_t control=0x8000;unsigned seen=0;int result=0;
    if(!capture(string,&captured,&input))goto done;input=trim(input);size_t at=0;
    while(at<input.n){while(at<input.n && space(input.p[at]))at++;if(at==input.n)break;
        uint16_t kind=input.p[at++];if(at==input.n || input.p[at++]!=':'){failure(87);goto done;}
        unsigned part=kind=='O'?0:kind=='G'?1:kind=='S'?2:kind=='D'?3:4;if(part==4 || (seen&(1u<<part))){failure(87);goto done;}seen|=1u<<part;
        size_t first=at;unsigned depth=0;while(at<input.n){uint16_t c=input.p[at];if(c=='(')depth++;else if(c==')'){if(!depth){failure(87);goto done;}depth--;}
            if(!depth && at+1<input.n && input.p[at+1]==':' && (c=='O'||c=='G'||c=='D'||c=='S'))break;at++;}
        text component=trim((text){input.p+first,at-first});
        if(part<2){if(!parse_sid(component,sids[part],&sizes[part]))goto done;}
        else if(!parse_acl(component,&acls[part-2],&control,part==2))goto done;
    }
    if(!seen){failure(87);goto done;}
    size_t total=20+sizes[0]+sizes[1]+acls[0].size+acls[1].size;uint8_t *p=local_alloc(total);if(!p)goto done;
    relative_sd header={.revision=1,.control=control};unsigned cursor=20;
    if(sizes[0]){header.owner=cursor;memcpy(p+cursor,sids[0],sizes[0]);cursor+=sizes[0];}
    if(sizes[1]){header.group=cursor;memcpy(p+cursor,sids[1],sizes[1]);cursor+=sizes[1];}
    if(acls[0].size){header.sacl=cursor;memcpy(p+cursor,acls[0].p,acls[0].size);cursor+=(unsigned)acls[0].size;}
    if(acls[1].size){header.dacl=cursor;memcpy(p+cursor,acls[1].p,acls[1].size);}
    memcpy(p,&header,20);unsigned bytes=(unsigned)total;
    if((length && wr_memory_copy(length,&bytes,4)) || wr_memory_copy(out,&p,sizeof(p))){local_free(p);failure(998);}else result=1;
done:dispose(&captured);dispose(&acls[0]);dispose(&acls[1]);return result;
}
typedef struct {void *multiple;uint32_t operation,form,type,pad;void *name;} trustee;
typedef struct {uint32_t access,mode,inherit,pad;trustee trustee;} explicit_access;
_Static_assert(sizeof(trustee)==32 && offsetof(trustee,name)==24 && sizeof(explicit_access)==48,"AMD64 trustees");
static void WR_WIN BuildTrusteeWithSidW(trustee *out,void *sid){trustee value={.name=sid};if(wr_memory_copy(out,&value,sizeof(value)))failure(998);}
static void WR_WIN BuildExplicitAccessWithNameW(explicit_access *out,uint16_t *name,unsigned access,unsigned mode,unsigned inherit){explicit_access value={.access=access,.mode=mode,.inherit=inherit,.trustee={.form=1,.name=name}};
    if(wr_memory_copy(out,&value,sizeof(value)))failure(998);}
static int trustee_sid(const trustee *source,uint8_t sid[68],unsigned *length){trustee t;
    if(!source || wr_memory_copy(&t,source,sizeof(t)))return failure(998);
    if(t.multiple || t.operation)return failure(50);
    if(t.form==1){bytes name={0};text value;int current=0;
        if(!capture(t.name,&name,&value)){dispose(&name);return 0;}
        const char *special="CURRENT_USER";current=value.n==12;
        for(size_t i=0;current && i<value.n;i++)if(value.p[i]!=(uint8_t)special[i])current=0;
        dispose(&name);if(!current)return failure(1332);
        /* Same stable native-UID SID namespace as the per-user registry hive.
         * Resolving data is not token creation or an authority grant. */
        vex_user_identity_t identity={.size=sizeof(identity)};
        if(vex_syscall1(VEX_SYS_USER_IDENTITY,(uintptr_t)&identity))return failure(1359);
        memset(sid,0,68);sid[0]=1;sid[1]=2;sid[7]=22;unsigned kind=1;
        memcpy(sid+8,&kind,4);memcpy(sid+12,&identity.uid,4);*length=16;return 1;
    }
    if(t.form!=0)return failure(50); /* Domain/account databases are not fabricated. */
    return sid_bytes(t.name,sid,length);
}
static unsigned ace_sid_offset(const uint8_t *ace){unsigned type=ace[0];if(type>=5 && type<=8){unsigned flags=pe_u32(ace+8);return 12+((flags&1)?16:0)+((flags&2)?16:0);}return 8;}
static int same_sid_bytes(const uint8_t *a,const uint8_t *b,unsigned size){if(8+4u*a[1]!=size)return 0;for(unsigned i=0;i<size;i++)if(a[i]!=b[i])return 0;return 1;}
static int merge_entries(bytes *acl,unsigned count,const explicit_access *entries){
    if(count && !entries)return failure(87);
    if((uintptr_t)entries>UINTPTR_MAX-(uint64_t)count*sizeof(*entries))return failure(998);
    unsigned used;if(!acl_validate(acl->p,(unsigned)acl->size,&used))return failure(1336);acl->size=used;
    for(unsigned i=0;i<count;i++){explicit_access e;uint8_t sid[68];unsigned sid_size;
        if(wr_memory_copy(&e,entries+i,sizeof(e)))return failure(998);
        if(e.mode<1 || e.mode>6 || (e.inherit&~31u))return failure(87);
        if(!trustee_sid(&e.trustee,sid,&sid_size))return 0;
        unsigned type=e.mode==3?1:e.mode>=5?2:0,flags=e.inherit|(e.mode==5?0x40:e.mode==6?0x80:0);
        unsigned merged=0,position=UINT32_MAX;
        for(unsigned at=8;at<acl->size;){uint8_t *ace=acl->p+at;unsigned size=pe_u16(ace+2),kind=ace[0],offset=ace_sid_offset(ace);
            int same=same_sid_bytes(ace+offset,sid,sid_size);
            if(same && (e.mode==2 || e.mode==4)){
                if(kind>=5 || (kind==2 && e.mode==2))return failure(50); /* Object-scoped replacement needs an object trustee. */
                /* REVOKE removes grants/audits, not explicit denials. */
                if((e.mode==4 && (kind==0 || kind==2)) || (e.mode==2 && kind<=1)){for(size_t p=at;p<acl->size-size;p++)acl->p[p]=acl->p[p+size];acl->size-=size;uint16_t n=pe_u16(acl->p+4)-1;memcpy(acl->p+4,&n,2);continue;}
            }
            if(!merged && same && kind==type && ace[1]==flags && e.mode!=2 && e.mode!=4){unsigned mask=pe_u32(ace+4)|e.access;memcpy(ace+4,&mask,4);merged=1;}
            if(position==UINT32_MAX && kind==0)position=at;
            at+=size;
        }
        if(e.mode!=4 && !merged){unsigned size=8+sid_size;if(acl->size>65535-size)return failure(1336);
            unsigned at=type==1?8:(type==0 && position!=UINT32_MAX?position:(unsigned)acl->size);
            if(!reserve(acl,acl->size+size))return 0;for(size_t p=acl->size;p>at;p--)acl->p[p+size-1]=acl->p[p-1];
            uint8_t *ace=acl->p+at;ace[0]=(uint8_t)type;ace[1]=(uint8_t)flags;uint16_t small=(uint16_t)size;memcpy(ace+2,&small,2);memcpy(ace+4,&e.access,4);memcpy(ace+8,sid,sid_size);acl->size+=size;
            small=pe_u16(acl->p+4)+1;memcpy(acl->p+4,&small,2);
        }
        uint16_t length=(uint16_t)acl->size;memcpy(acl->p+2,&length,2);
    }return 1;
}
static unsigned WR_WIN SetEntriesInAclW(unsigned count,const explicit_access *entries,void *old,void **out){
    unsigned saved=wr_current_teb()->last_error,result=0;bytes acl={0};void *value=0;
    if(!out)result=87;
    else if(old){if(!acl_copy(old,&acl,0))result=wr_current_teb()->last_error;}
    else if(count){if(reserve(&acl,8)){memset(acl.p,0,8);acl.p[0]=2;acl.p[2]=8;acl.size=8;}else result=wr_current_teb()->last_error;}
    if(!result && count && !merge_entries(&acl,count,entries))result=wr_current_teb()->last_error;
    if(!result && acl.size){value=local_alloc(acl.size);if(!value)result=wr_current_teb()->last_error;else memcpy(value,acl.p,acl.size);}
    if(!result && wr_memory_copy(out,&value,sizeof(value))){local_free(value);result=998;}
    dispose(&acl);wr_current_teb()->last_error=saved;return result;
}
static int pack_descriptor(const absolute_sd *sd,void **out,unsigned *length){
    uint8_t sids[2][68];unsigned sizes[2]={0};bytes acls[2]={{0}};int result=0;
    if((sd->owner && !sid_bytes(sd->owner,sids[0],&sizes[0])) || (sd->group && !sid_bytes(sd->group,sids[1],&sizes[1])))goto done;
    if(((sd->control&16) && sd->sacl && !acl_copy(sd->sacl,&acls[0],0)) || ((sd->control&4) && sd->dacl && !acl_copy(sd->dacl,&acls[1],0)))goto done;
    unsigned total=20+sizes[0]+sizes[1]+(unsigned)acls[0].size+(unsigned)acls[1].size;uint8_t *value=local_alloc(total);if(!value)goto done;
    relative_sd header={.revision=1,.pad=sd->pad,.control=(uint16_t)(sd->control|0x8000)};unsigned cursor=20;
    if(sizes[0]){header.owner=cursor;memcpy(value+cursor,sids[0],sizes[0]);cursor+=sizes[0];}
    if(sizes[1]){header.group=cursor;memcpy(value+cursor,sids[1],sizes[1]);cursor+=sizes[1];}
    if(acls[0].size){header.sacl=cursor;memcpy(value+cursor,acls[0].p,acls[0].size);cursor+=(unsigned)acls[0].size;}
    if(acls[1].size){header.dacl=cursor;memcpy(value+cursor,acls[1].p,acls[1].size);}
    memcpy(value,&header,20);
    if((length && wr_memory_copy(length,&total,4)) || wr_memory_copy(out,&value,sizeof(value))){local_free(value);failure(998);}else result=1;
done:dispose(&acls[0]);dispose(&acls[1]);return result;
}
static unsigned WR_WIN BuildSecurityDescriptorW(const trustee *owner,const trustee *group,unsigned access_count,const explicit_access *access,unsigned audit_count,const explicit_access *audit,const void *old,unsigned *length,void **out){
    unsigned saved=wr_current_teb()->last_error,result=0;absolute_sd state={.revision=1};uint8_t sids[2][68];unsigned sizes[2];void *new_acls[2]={0};
    if(!out || !length || (access_count && !access) || (audit_count && !audit))result=87;
    if(!result && old && !descriptor(old,&state))result=wr_current_teb()->last_error;
    if(!result && owner){if(trustee_sid(owner,sids[0],&sizes[0])){state.owner=sids[0];state.control&=~1u;}else result=wr_current_teb()->last_error;}
    if(!result && group){if(trustee_sid(group,sids[1],&sizes[1])){state.group=sids[1];state.control&=~2u;}else result=wr_current_teb()->last_error;}
    if(!result && access){result=SetEntriesInAclW(access_count,access,(state.control&4)?state.dacl:0,&new_acls[1]);if(!result){state.dacl=new_acls[1];state.control=(uint16_t)((state.control|4)&~8u);}}
    if(!result && audit){result=SetEntriesInAclW(audit_count,audit,(state.control&16)?state.sacl:0,&new_acls[0]);if(!result){state.sacl=new_acls[0];state.control=(uint16_t)((state.control|16)&~32u);}}
    if(!result && !pack_descriptor(&state,out,length))result=wr_current_teb()->last_error;
    local_free(new_acls[0]);local_free(new_acls[1]);wr_current_teb()->last_error=saved;return result;
}
int wr_security_snapshot(const void *source,void **out,unsigned *length){
    absolute_sd state;void *local=0;*out=0;*length=0;
    if(!descriptor(source,&state) || !pack_descriptor(&state,&local,length))return 0;
    void *copy=wr_native_alloc(*length);if(!copy){local_free(local);return failure(8);}
    memcpy(copy,local,*length);local_free(local);*out=copy;return 1;
}
void *wr_security_data_exports(const char *name){
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(ConvertStringSidToSidW);E(ConvertStringSecurityDescriptorToSecurityDescriptorW);E(IsValidSid);E(GetLengthSid);E(EqualSid);E(CopySid);
    E(InitializeAcl);E(IsValidAcl);E(GetAce);E(AddAccessAllowedAce);E(AddAccessDeniedAce);E(AddAuditAccessAce);
    E(InitializeSecurityDescriptor);E(IsValidSecurityDescriptor);E(GetSecurityDescriptorControl);E(GetSecurityDescriptorOwner);E(GetSecurityDescriptorGroup);
    E(GetSecurityDescriptorDacl);E(GetSecurityDescriptorSacl);E(SetSecurityDescriptorDacl);E(SetSecurityDescriptorSacl);E(SetSecurityDescriptorOwner);E(SetSecurityDescriptorGroup);
    E(BuildTrusteeWithSidW);E(BuildExplicitAccessWithNameW);
    E(SetEntriesInAclW);E(BuildSecurityDescriptorW);
#undef E
    return 0;
}
