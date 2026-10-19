#include "windows.h"
/* Win32 names/return codes over the existing per-user, transactional NT hive.
 * No host registry, process-global native namespace or kernel NT dispatch. */
typedef struct {uint32_t size,pad;void *root;wr_unicode *name;uint32_t flags,pad2;void *security,*qos;} attributes;
static unsigned status_error(uint32_t status){unsigned (WR_WIN *convert)(uint32_t)=wr_nt_core_exports("RtlNtStatusToDosErrorNoTeb");return convert(status);}
static unsigned finish(unsigned result,unsigned saved){wr_current_teb()->last_error=saved;return result;}
static int predefined(void *key){uintptr_t value=(uintptr_t)key;return (value>>32==0 || value>>32==UINT32_MAX) && (unsigned)value>=0x80000000u && (unsigned)value<=0x80000006u;}
static unsigned counted(const uint16_t *text,wr_unicode *out){
    unsigned n=0;if(text)while(n<32767){uint16_t units[128];size_t count=(4096-(((uintptr_t)text+2ull*n)&4095))/2;
        if(!count)count=1;if(count>128)count=128;if(count>32767-n)count=32767-n;
        if(wr_memory_copy(units,text+n,count*2))return 998;
        unsigned i=0;while(i<count && units[i])i++;n+=i;if(i<count)break;}
    if(n>=32767)return 206;*out=(wr_unicode){(uint16_t)(2*n),(uint16_t)(2*n),0,(uint16_t *)text};return 0;
}
static unsigned nt_open(void *root,const uint16_t *path,unsigned rights,int create,void **out,unsigned *disposition){
    wr_unicode name;unsigned result=counted(path,&name);if(result)return result;
    attributes a={sizeof(a),0,root,&name,0x40,0,0,0};uint32_t status;
    if(create){uint32_t (WR_WIN *call)(void **,unsigned,attributes *,unsigned,wr_unicode *,unsigned,unsigned *)=wr_registry_exports("NtCreateKey");status=call(out,rights,&a,0,0,0,disposition);}
    else {uint32_t (WR_WIN *call)(void **,unsigned,attributes *,unsigned)=wr_registry_exports("NtOpenKeyEx");status=call(out,rights,&a,0);}
    return status_error(status);
}
static void close_key(void *key){(void)wr_registry_close(key);}
static unsigned root_key(void *source,unsigned rights,void **out,int *owned){
    *out=source;*owned=0;if(!predefined(source))return 0;
    const uint16_t *path=0;wr_unicode current={0};unsigned result=0;
    switch((unsigned)(uintptr_t)source){
    case 0x80000001u:{uint32_t (WR_WIN *format)(wr_unicode *)=wr_registry_exports("RtlFormatCurrentUserKeyPath");result=status_error(format(&current));path=current.buffer;break;}
    case 0x80000002u:path=(const uint16_t *)u"\\Registry\\Machine";break;
    case 0x80000003u:path=(const uint16_t *)u"\\Registry\\User";break;
    default:return 50; /* HKCR merge, performance data, current-config absent. */
    }
    if(!result)result=nt_open(0,path,rights,0,out,0);
    if(current.buffer){void (WR_WIN *release)(wr_unicode *)=wr_nt_core_exports("RtlFreeUnicodeString");release(&current);}
    if(!result)*owned=1;return result;
}
static unsigned WR_WIN RegCloseKey(void *key){unsigned saved=wr_current_teb()->last_error;
    if(predefined(key))return finish(0,saved);int result=wr_registry_close(key);return finish(result==1?0:6,saved);
}
static unsigned WR_WIN RegOpenKeyExW(void *key,const uint16_t *path,unsigned options,unsigned rights,void **out){
    unsigned saved=wr_current_teb()->last_error,result=0;void *root;int owned;
    if(options || !out)return finish(87,saved);
    result=root_key(key,rights,&root,&owned);
    if(!result)result=nt_open(root,path,rights,0,out,0);
    if(!result || owned){if(owned)close_key(root);}return finish(result,saved);
}
static unsigned WR_WIN RegOpenCurrentUser(unsigned rights,void **out){return RegOpenKeyExW((void *)(uintptr_t)0xffffffff80000001ull,0,0,rights,out);}
static unsigned WR_WIN RegCreateKeyExW(void *key,const uint16_t *path,unsigned reserved,uint16_t *class_name,unsigned options,unsigned rights,void *security,void **out,unsigned *disposition){
    unsigned saved=wr_current_teb()->last_error;if(reserved || !out)return finish(87,saved);
    if(class_name || options || security)return finish(50,saved);
    wr_unicode name;unsigned result=counted(path,&name);if(result)return finish(result,saved);
    uint16_t *text=wr_native_alloc((size_t)name.length+2);if(!text)return finish(8,saved);
    if(name.length && wr_memory_copy(text,path,name.length)){wr_native_free(text);return finish(998,saved);}text[name.length/2]=0;
    void *current=0;int owned=0;result=root_key(key,rights|4,&current,&owned);unsigned action=2;
    /* Win32 creates intermediate keys; NT CreateKey creates only the final
       component. Keep real handles/rights and report the final disposition. */
    unsigned at=0;
    while(!result){unsigned first=at;while(text[at] && text[at]!='\\')at++;uint16_t separator=text[at];text[at]=0;
        void *next=0;result=nt_open(current,text+first,rights|(separator?4:0),1,&next,&action);
        if(owned)close_key(current);owned=0;
        if(result)break;current=next;owned=1;if(!separator)break;text[at++]='\\';
    }
    if(!result){if(wr_memory_copy(out,&current,sizeof(current)) || (disposition && wr_memory_copy(disposition,&action,4)))result=998;else owned=0;}
    if(owned)close_key(current);wr_native_free(text);return finish(result,saved);
}
static unsigned WR_WIN RegSetValueExW(void *key,const uint16_t *name,unsigned reserved,unsigned type,const void *bytes,unsigned length){
    unsigned saved=wr_current_teb()->last_error;if(reserved)return finish(87,saved);wr_unicode value;unsigned result=counted(name,&value);
    void *root=0;int owned=0;if(!result)result=root_key(key,2,&root,&owned);
    if(!result){uint32_t (WR_WIN *set)(void *,wr_unicode *,unsigned,unsigned,const void *,unsigned)=wr_registry_exports("NtSetValueKey");result=status_error(set(root,&value,0,type,bytes,length));}
    if(owned)close_key(root);return finish(result,saved);
}
static unsigned WR_WIN RegQueryValueExW(void *key,const uint16_t *name,unsigned *reserved,unsigned *type,void *out,unsigned *length){
    unsigned saved=wr_current_teb()->last_error,capacity=0,result;wr_unicode value;
    if(reserved || (out && !length))return finish(87,saved);
    if(length && wr_memory_copy(&capacity,length,4))return finish(998,saved);
    result=counted(name,&value);void *root=0;int owned=0;if(!result)result=root_key(key,1,&root,&owned);
    uint32_t (WR_WIN *query)(void *,wr_unicode *,unsigned,void *,unsigned,unsigned *)=wr_registry_exports("NtQueryValueKey");
    unsigned header[3]={0},required=0;
    if(!result){uint32_t status=query(root,&value,2,header,sizeof(header),&required);if(status && status!=0x80000005u)result=status_error(status);}
    if(!result && type && wr_memory_copy(type,header+1,4))result=998;
    if(!result && length && wr_memory_copy(length,header+2,4))result=998;
    if(!result && out){
        if(capacity<header[2])result=234;
        else if(header[2]>UINT32_MAX-12)result=8;
        else {uint8_t *buffer=wr_native_alloc((size_t)header[2]+12);if(!buffer)result=8;
            else {uint32_t status=query(root,&value,2,buffer,header[2]+12,&required);result=status_error(status);
                if(!result || status==0x80000005u){unsigned *fields=(void *)buffer;
                    if((type && wr_memory_copy(type,fields+1,4)) || (length && wr_memory_copy(length,fields+2,4)))result=998;
                    if(!result){size_t at=0;while(at<fields[2]){size_t part=fields[2]-at;if(part>1048576)part=1048576;
                        if(wr_memory_copy((uint8_t *)out+at,buffer+12+at,part)){result=998;break;}at+=part;}}}
                wr_native_free(buffer);}}
    }
    if(owned)close_key(root);return finish(result,saved);
}
static unsigned WR_WIN RegDeleteValueW(void *key,const uint16_t *name){unsigned saved=wr_current_teb()->last_error;wr_unicode value;unsigned result=counted(name,&value);
    void *root=0;int owned=0;if(!result)result=root_key(key,2,&root,&owned);
    if(!result){uint32_t (WR_WIN *remove)(void *,wr_unicode *)=wr_registry_exports("NtDeleteValueKey");result=status_error(remove(root,&value));}
    if(owned)close_key(root);return finish(result,saved);
}
static unsigned WR_WIN RegDeleteKeyW(void *key,const uint16_t *name){unsigned saved=wr_current_teb()->last_error;
    if(!name)return finish(87,saved);void *handle=0;unsigned result=RegOpenKeyExW(key,name,0,0x10000,&handle);
    if(!result){uint32_t (WR_WIN *remove)(void *)=wr_registry_exports("NtDeleteKey");result=status_error(remove(handle));close_key(handle);}
    return finish(result,saved);
}
void *wr_advapi_exports(const char *name){
    void *data=wr_security_data_exports(name);if(data)return data;
    data=wr_token_exports(name);if(data)return data;
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(RegOpenKeyExW);E(RegOpenCurrentUser);E(RegCloseKey);E(RegCreateKeyExW);E(RegSetValueExW);E(RegQueryValueExW);E(RegDeleteValueW);E(RegDeleteKeyW);
#undef E
    return 0;
}
