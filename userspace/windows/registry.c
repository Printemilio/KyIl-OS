#include "windows.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
/* Per-user Windows hive; all names/access policy live in userspace. Logical
 * key IDs survive value updates. Deleted IDs are tombstoned, never rebound.
 * Two independently checksummed snapshots keep the last committed generation
 * while writing the inactive slot. No native namespace/index is repurposed. */
#define OK 0u
#define INVALID 0xc000000du
#define BAD_HANDLE 0xc0000008u
#define DENIED 0xc0000022u
#define MISSING 0xc0000034u
#define DELETED 0xc000017cu
#define CORRUPT 0xc000014cu
#define NOT_SUPPORTED 0xc00000bbu
#define NO_MEMORY 0xc0000017u
#define IO_ERROR 0xc0000185u
typedef struct {uint64_t magic;uint32_t version,header;uint64_t sequence,next_id,bytes,records,checksum,reserved;} hive_header;
_Static_assert(sizeof(hive_header)==64,"hive v1 header");
typedef struct {uint32_t bytes,type,flags,name_bytes;uint64_t id,parent;} record;
_Static_assert(sizeof(record)==32,"hive v1 record");
typedef struct {uint32_t size,pad;void *root;wr_unicode *name;uint32_t flags,pad2;void *security,*qos;} object_attributes;
_Static_assert(sizeof(object_attributes)==48,"OBJECT_ATTRIBUTES");
typedef struct {uint8_t *bytes;size_t length;int slot;} hive;
static struct {uint64_t key;uint32_t generation,rights;} handles[256];
static wr_mutex gate;static int root=-1,lock_fd=-1;static uint16_t user_sid[64];
unsigned wr_registry_handle_count(void){wr_lock(&gate);unsigned count=0;for(unsigned i=0;i<256;i++)if(handles[i].key)count++;wr_unlock(&gate);return count;}
static const uint64_t magic=0x314745524c49594bull;
static uint32_t native_error(int error){switch(-error){case 2:return MISSING;case 13:case 1:return DENIED;case 9:return BAD_HANDLE;case 12:case 24:return NO_MEMORY;case 14:return 0xc0000005u;default:return IO_ERROR;}}
static void diagnostic(const char *operation,int result){char text[]=" native=0x00000000\n";for(unsigned i=0;i<8;i++)text[10+i]="0123456789abcdef"[((unsigned)result>>(28-i*4))&15];wr_log("windows-registry: ");wr_log(operation);wr_log(text);}
static int copy_large(void *out,const void *source,size_t bytes){if((uintptr_t)out>UINTPTR_MAX-bytes || (uintptr_t)source>UINTPTR_MAX-bytes)return -1;size_t at=0;
    while(at<bytes){size_t n=bytes-at;if(n>VEX_MEMORY_COPY_MAX)n=VEX_MEMORY_COPY_MAX;if(wr_memory_copy((uint8_t *)out+at,(const uint8_t *)source+at,n))return -1;at+=n;}return 0;}
static int writable(void *out,size_t length){vex_memory_view_req_t r={.size=sizeof(r),.operation=VEX_MEMORY_VIEW_ACCESS,.address=(uintptr_t)out,.length=length,.permissions=PE_W};return (int)(int64_t)vex_syscall1(VEX_SYS_MEMORY_VIEW,(uintptr_t)&r)==0;}
static int file(unsigned op,const char *path,unsigned flags,void *out){vex_fs_root_req_t r={.size=sizeof(r),.operation=op,.fd=root,.path=path,.flags=flags,.mode=op==VEX_FS_ROOT_OPEN && (flags&O_CREAT)?0600:0,.out=out};return (int)(int64_t)vex_syscall1(VEX_SYS_FS_ROOT,(uintptr_t)&r);}
static int close_fd(int fd){return (int)(int64_t)vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)fd);}
static int lease(unsigned mode){vex_file_range_req_t r={sizeof(r),mode,lock_fd,mode?VEX_FILE_RANGE_WAIT:0,0,0};return (int)(int64_t)vex_syscall1(VEX_SYS_FILE_RANGE,(uintptr_t)&r);}
static uint32_t initialize(void) {
    if(root>=0)return OK;
    int result=wr_namespace_call(VEX_FS_ROOT_MKDIR,(const uint16_t *)u"C:\\.kyil-registry",0,0700,0,1);
    if(result && result!=-17){diagnostic("mkdir",result);return native_error(result);}
    /* Derive write/create authority from C: once, atomically. The anchored
     * directory cannot be rebound while this capability remains referenced. */
    vex_fs_root_req_t derive={.size=sizeof(derive),.operation=VEX_FS_ROOT_DERIVE,.fd=wr_prefix_root(),
        .rights=VEX_FS_READ|VEX_FS_WRITE|VEX_FS_CREATE|VEX_FS_ENUM,.path=".kyil-registry"};
    root=(int)(int64_t)vex_syscall1(VEX_SYS_FS_ROOT,(uintptr_t)&derive);if(root<0){diagnostic("derive",root);return native_error(root);}
    lock_fd=file(VEX_FS_ROOT_OPEN,"transaction.lock",O_RDWR|O_CREAT|O_CLOEXEC|VEX_FS_OPEN_REGULAR,0);
    if(lock_fd<0){diagnostic("lock-file open",lock_fd);uint32_t error=native_error(lock_fd);close_fd(root);root=-1;return error;}
    vex_user_identity_t identity={.size=sizeof(identity)};
    if(vex_syscall1(VEX_SYS_USER_IDENTITY,(uintptr_t)&identity)){close_fd(lock_fd);close_fd(root);root=lock_fd=-1;return IO_ERROR;}
    const char *prefix="S-1-22-1-";unsigned n=0;while(prefix[n]){user_sid[n]=(uint8_t)prefix[n];n++;}
    uint16_t digits[16];unsigned count=0,id=identity.uid;do{digits[count++]=(uint16_t)('0'+id%10);id/=10;}while(id);
    while(count)user_sid[n++]=digits[--count];user_sid[n]=0;return OK;
}
static uint64_t checksum(const uint8_t *bytes,size_t count){uint64_t h=14695981039346656037ull;for(size_t i=0;i<count;i++){uint8_t b=i>=48 && i<56?0:bytes[i];h^=b;h*=1099511628211ull;}return h;}
static record *at(hive *h,size_t offset){return (record *)(h->bytes+offset);}
static record *key(hive *h,uint64_t id){for(size_t p=64;p<h->length;){record *r=at(h,p);if(r->type==1 && r->id==id)return r;p+=r->bytes;}return 0;}
static int same(const record *r,const uint16_t *name,unsigned bytes){if(r->name_bytes!=bytes)return 0;const uint16_t *actual=(const void *)(r+1);for(unsigned i=0;i<bytes/2;i++)if(wr_unicode_case(actual[i],1)!=wr_unicode_case(name[i],1))return 0;return 1;}
static record *child(hive *h,uint64_t parent,const uint16_t *name,unsigned bytes){for(size_t p=64;p<h->length;){record *r=at(h,p);if(r->type==1 && !(r->flags&1) && r->parent==parent && same(r,name,bytes))return r;p+=r->bytes;}return 0;}
static record *value(hive *h,uint64_t id,const uint16_t *name,unsigned bytes){for(size_t p=64;p<h->length;){record *r=at(h,p);if(r->type==2 && r->id==id && same(r,name,bytes))return r;p+=r->bytes;}return 0;}
static int validate(hive *h) {
    if(h->length<64)return 0;hive_header *head=(void *)h->bytes;
    if(head->magic!=magic || head->version!=1 || head->header!=64 || !head->sequence || head->next_id<4 || head->bytes!=h->length || head->reserved || head->checksum!=checksum(h->bytes,h->length))return 0;
    size_t count=0;for(size_t p=64;p<h->length;){record *r=at(h,p);
        if(h->length-p<32 || r->bytes<32 || (r->bytes&7) || r->bytes>h->length-p || (r->name_bytes&1) || r->name_bytes>r->bytes-32 || !r->id)return 0;
        if(r->type==1){if(r->flags&~1u || r->id>=head->next_id || r->bytes!=((32ull+r->name_bytes+7)&~7ull))return 0;}
        else if(r->type==2){if(r->parent>r->bytes-32-r->name_bytes || r->bytes!=((32ull+r->name_bytes+r->parent+7)&~7ull))return 0;}
        else return 0;
        const uint16_t *name=(const void *)(r+1);for(unsigned i=0;i<r->name_bytes/2;i++)if(!name[i] || (name[i]>=0xd800 && name[i]<=0xdfff))return 0;
        count++;p+=r->bytes;
    }
    if(count!=head->records)return 0;
    for(size_t p=64;p<h->length;){record *r=at(h,p);
        if(r->type==1){if(r->parent){record *parent=key(h,r->parent);if(!parent || parent->id>=r->id || (!(r->flags&1) && (parent->flags&1)))return 0;}
            for(size_t q=p+r->bytes;q<h->length;){record *other=at(h,q);if(other->type==1 && (other->id==r->id || (!(other->flags&1) && !(r->flags&1) && other->parent==r->parent && same(other,(const void *)(r+1),r->name_bytes))))return 0;q+=other->bytes;}}
        else {if(!key(h,r->id))return 0;for(size_t q=p+r->bytes;q<h->length;){record *other=at(h,q);if(other->type==2 && other->id==r->id && same(other,(const void *)(r+1),r->name_bytes))return 0;q+=other->bytes;}}
        p+=r->bytes;
    }
    record *machine=key(h,1),*users=key(h,2),*self=key(h,3);
    unsigned sid_bytes=0;while(user_sid[sid_bytes/2])sid_bytes+=2;
    return machine && users && self && !machine->parent && !users->parent && self->parent==2 && !(machine->flags|users->flags|self->flags) &&
        same(machine,(const uint16_t *)u"MACHINE",14) && same(users,(const uint16_t *)u"USER",8) && same(self,user_sid,sid_bytes);
}
static void dispose(hive *h){if(h->bytes)wr_native_free(h->bytes);*h=(hive){0};}
static int read_slot(int slot,hive *out){const char *path=slot?"hive.1":"hive.0";int fd=file(VEX_FS_ROOT_OPEN,path,O_RDONLY|O_CLOEXEC|VEX_FS_OPEN_REGULAR,0);if(fd<0)return fd;
    uint8_t *view;size_t bytes;int result=wr_read_fd(fd,&view,&bytes);close_fd(fd);if(result)return -5;
    out->bytes=wr_native_alloc(bytes);if(!out->bytes){wr_unmap(view,bytes);return -12;}out->length=bytes;out->slot=slot;memcpy(out->bytes,view,bytes);wr_unmap(view,bytes);
    if(!validate(out)){dispose(out);return -74;}return 0;
}
static int append(hive *h,unsigned type,unsigned flags,uint64_t id,uint64_t parent,const uint16_t *name,unsigned name_bytes,const void *data,size_t data_bytes) {
    if(data_bytes>SIZE_MAX-32-name_bytes-7 || h->length>SIZE_MAX-((32+name_bytes+data_bytes+7)&~7ull))return 0;
    size_t length=(32+name_bytes+data_bytes+7)&~7ull;if(length>UINT32_MAX)return 0;
    uint8_t *replacement=wr_native_alloc(h->length+length);if(!replacement)return 0;memcpy(replacement,h->bytes,h->length);memset(replacement+h->length,0,length);
    record *r=(void *)(replacement+h->length);*r=(record){(unsigned)length,type,flags,name_bytes,id,parent};memcpy(r+1,name,name_bytes);if(data_bytes)memcpy((uint8_t *)(r+1)+name_bytes,data,data_bytes);
    wr_native_free(h->bytes);h->bytes=replacement;h->length+=length;hive_header *head=(void *)h->bytes;head->bytes=h->length;head->records++;return 1;
}
static uint32_t fresh(hive *h){h->bytes=wr_native_alloc(64);if(!h->bytes)return NO_MEMORY;h->length=64;h->slot=1;
    *(hive_header *)h->bytes=(hive_header){magic,1,64,1,4,64,0,0,0};
    uint16_t machine[]={'M','A','C','H','I','N','E'},users[]={'U','S','E','R'};unsigned units=0;while(user_sid[units])units++;
    if(!append(h,1,0,1,0,machine,sizeof(machine),0,0) || !append(h,1,0,2,0,users,sizeof(users),0,0) || !append(h,1,0,3,2,user_sid,units*2,0,0)){dispose(h);return NO_MEMORY;}return OK;
}
static uint32_t load(hive *h){hive a={0},b={0};int ar=read_slot(0,&a),br=read_slot(1,&b);
    if(ar==-12 || br==-12){dispose(&a);dispose(&b);return NO_MEMORY;}
    if(!ar || !br){if(br || (!ar && ((hive_header *)a.bytes)->sequence>=((hive_header *)b.bytes)->sequence)){*h=a;dispose(&b);}else{*h=b;dispose(&a);}return OK;}
    if(ar==-2 && br==-2)return fresh(h);
    return ar==-74 || br==-74?CORRUPT:IO_ERROR;
}
static uint32_t commit(hive *h) {
    hive_header *head=(void *)h->bytes;if(head->sequence==UINT64_MAX)return NO_MEMORY;
    /* A deletion shrinks the snapshot without append(). Publish the actual
       length centrally before hashing, rather than writing a newer invalid
       slot which load() would correctly reject in favour of stale contents. */
    head->bytes=h->length;head->sequence++;head->checksum=checksum(h->bytes,h->length);
    int fd=file(VEX_FS_ROOT_OPEN,h->slot?"hive.0":"hive.1",O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|VEX_FS_OPEN_REGULAR,0);if(fd<0)return native_error(fd);
    size_t at=0;uint32_t result=OK;
    while(at<h->length){vex_fd_io_req_t request={fd,h->bytes+at,h->length-at};int64_t bytes=(int64_t)vex_syscall1(VEX_SYS_WRITEFD,(uintptr_t)&request);
        if(bytes<=0){result=IO_ERROR;break;}at+=(size_t)bytes;}
    if(!result && vex_syscall1(VEX_SYS_FSYNC,fd))result=IO_ERROR;
    if(close_fd(fd) && !result)result=IO_ERROR;return result;
}
static uint32_t begin(hive *h){wr_lock(&gate);uint32_t status=initialize();if(!status){int result=lease(VEX_FILE_RANGE_WRITE);if(result)status=native_error(result);else {status=load(h);if(status)lease(VEX_FILE_RANGE_RELEASE);}}
    if(status)wr_unlock(&gate);return status;
}
static uint32_t end(hive *h,uint32_t result){dispose(h);int unlock=lease(VEX_FILE_RANGE_RELEASE);wr_unlock(&gate);return result?result:(unlock?native_error(unlock):OK);}
static int handle_index(void *handle){uintptr_t v=(uintptr_t)handle;unsigned i=wr_handle_slot(v);return wr_handle_tag(v)==0x5c && handles[i].key && wr_handle_generation(v)==handles[i].generation?(int)i:-1;}
static uint32_t resolve_handle(hive *h,void *handle,unsigned rights,uint64_t *id){int i=handle_index(handle);if(i<0)return BAD_HANDLE;if(rights&~handles[i].rights)return DENIED;record *r=key(h,handles[i].key);if(!r || (r->flags&1))return DELETED;*id=r->id;return OK;}
int wr_registry_close(void *handle){if(wr_handle_tag((uintptr_t)handle)!=0x5c)return -1;wr_lock(&gate);int i=handle_index(handle);if(i>=0)handles[i].key=0;wr_unlock(&gate);if(i<0)wr_current_teb()->last_error=6;return i>=0;}
void wr_registry_after_stop(void) {
    /* Only after native peer CPU quiescence. A peer may have died while
     * waiting for an external lease with this userspace gate held. Its local
     * uncommitted snapshot is disposable; the two durable slots stay intact.
     * DLL detach on the sole survivor must not inherit a dead owner's mutex. */
    if(lock_fd>=0)close_fd(lock_fd);if(root>=0)close_fd(root);root=lock_fd=-1;
    memset(user_sid,0,sizeof(user_sid));__atomic_store_n(&gate.held,0,__ATOMIC_RELEASE);
}
static uint32_t unicode(wr_unicode *source,uint16_t **out,unsigned *bytes){wr_unicode string;if(!source)return INVALID;if(wr_memory_copy(&string,source,sizeof(string)))return 0xc0000005u;
    if((string.length&1) || string.length>string.maximum || (string.length && !string.buffer))return INVALID;
    uint16_t *name=wr_native_alloc((size_t)string.length+2);if(!name)return NO_MEMORY;if(string.length && wr_memory_copy(name,string.buffer,string.length)){wr_native_free(name);return 0xc0000005u;}
    for(unsigned i=0;i<string.length/2;i++)if(!name[i] || (name[i]>=0xd800 && name[i]<=0xdfff)){wr_native_free(name);return NOT_SUPPORTED;}name[string.length/2]=0;*out=name;*bytes=string.length;return OK;}
static uint32_t open_key(void **out,unsigned rights,object_attributes *source,int create,unsigned title,wr_unicode *class_name,unsigned options,unsigned *disposition) {
    object_attributes attributes;if(!out || !source)return INVALID;if(wr_memory_copy(&attributes,source,sizeof(attributes)))return 0xc0000005u;
    if(attributes.size!=48 || (attributes.flags&~0x40u))return INVALID;
    if(attributes.security || attributes.qos || title || options || class_name)return NOT_SUPPORTED;
    /* This AMD64 personality has one 64-bit hive view. Explicit 64-bit view
       selects that same view; the distinct WOW64 32-bit view is unsupported. */
    if(!rights || (rights&~0x000f033fu) || (rights&0x200u))return NOT_SUPPORTED;
    if(!writable(out,sizeof(void *)) || (disposition && !writable(disposition,4)))return 0xc0000005u;
    unsigned granted=rights&~0x100u;uint16_t *name;unsigned bytes;uint32_t result=unicode(attributes.name,&name,&bytes);if(result)return result;
    hive h={0};result=begin(&h);if(result){wr_native_free(name);return result;}
    unsigned slot=0;while(slot<256 && (handles[slot].key || handles[slot].generation>=WR_HANDLE_GENERATION_MAX))slot++;if(slot==256)result=NO_MEMORY;
    uint64_t current=0;unsigned p=0,created=0;
    if(!result && attributes.root)result=resolve_handle(&h,attributes.root,create?4:0,&current);
    if(!result && !attributes.root){const uint16_t prefix[]={'\\','R','E','G','I','S','T','R','Y','\\'};
        if(bytes<sizeof(prefix))result=MISSING;else {for(unsigned i=0;i<sizeof(prefix)/2;i++)if(wr_unicode_case(name[i],1)!=prefix[i])result=MISSING;p=sizeof(prefix)/2;}}
    else if(!result && name[0]=='\\')result=INVALID;
    while(!result && p<bytes/2){unsigned start=p;while(p<bytes/2 && name[p]!='\\')p++;if(p==start){result=INVALID;break;}
        record *r=child(&h,current,name+start,(p-start)*2);
        if(!r){if(!create){result=MISSING;break;}if(p<bytes/2){result=0xc000003au;break;}if(!current){result=DENIED;break;}hive_header *head=(void *)h.bytes;
            if(head->next_id==UINT64_MAX || !append(&h,1,0,head->next_id,current,name+start,(p-start)*2,0,0)){result=NO_MEMORY;break;}
            head=(void *)h.bytes;r=key(&h,head->next_id++);created=1;}
        current=r->id;if(p<bytes/2){p++;if(p==bytes/2)result=INVALID;}
    }
    if(!result && !current)result=INVALID;
    if(!result && created)result=commit(&h);
    if(!result){if(!++handles[slot].generation)++handles[slot].generation;handles[slot].key=current;handles[slot].rights=granted;
        void *handle=(void *)(wr_handle_pack(0x5c,handles[slot].generation,slot));unsigned action=created?1:2;
        if(wr_memory_copy(out,&handle,sizeof(handle)) || (disposition && wr_memory_copy(disposition,&action,4))){handles[slot].key=0;result=0xc0000005u;}}
    wr_native_free(name);return end(&h,result);
}
static uint32_t WR_WIN NtCreateKey(void **out,unsigned access,object_attributes *attributes,unsigned title,wr_unicode *class_name,unsigned options,unsigned *disposition){return open_key(out,access,attributes,1,title,class_name,options,disposition);}
static uint32_t WR_WIN NtOpenKeyEx(void **out,unsigned access,object_attributes *attributes,unsigned options){return open_key(out,access,attributes,0,0,0,options,0);}
static uint32_t WR_WIN NtOpenKey(void **out,unsigned access,object_attributes *attributes){return NtOpenKeyEx(out,access,attributes,0);}
static uint32_t WR_WIN NtDeleteKey(void *handle){hive h={0};uint32_t result=begin(&h);if(result)return result;uint64_t id;result=resolve_handle(&h,handle,0x10000,&id);
    if(!result && id<=3)result=DENIED;
    if(!result)for(size_t p=64;p<h.length;){record *r=at(&h,p);if(r->type==1 && !(r->flags&1) && r->parent==id){result=0xc0000121u;break;}p+=r->bytes;}
    if(!result){key(&h,id)->flags|=1;result=commit(&h);}return end(&h,result);}
static uint32_t WR_WIN NtSetValueKey(void *handle,wr_unicode *source,unsigned title,unsigned type,const void *data,unsigned length) {
    if(title || (length && !data))return INVALID;uint16_t *name;unsigned bytes;uint32_t result=unicode(source,&name,&bytes);if(result)return result;
    void *copy=length?wr_native_alloc(length):0;if(length && !copy){wr_native_free(name);return NO_MEMORY;}
    if(length && copy_large(copy,data,length)){wr_native_free(copy);wr_native_free(name);return 0xc0000005u;}
    hive h={0};result=begin(&h);if(result){if(copy)wr_native_free(copy);wr_native_free(name);return result;}uint64_t id;result=resolve_handle(&h,handle,2,&id);
    if(!result){record *old=value(&h,id,name,bytes);if(old){size_t offset=(uint8_t *)old-h.bytes,length_old=old->bytes;for(size_t i=offset;i<h.length-length_old;i++)h.bytes[i]=h.bytes[i+length_old];h.length-=length_old;((hive_header *)h.bytes)->records--;}
        if(!append(&h,2,type,id,length,name,bytes,copy,length))result=NO_MEMORY;else result=commit(&h);}
    if(copy)wr_native_free(copy);wr_native_free(name);return end(&h,result);
}
static uint32_t WR_WIN NtQueryValueKey(void *handle,wr_unicode *source,unsigned kind,void *out,unsigned capacity,unsigned *required) {
    if(kind>4)return kind==5?NOT_SUPPORTED:0xc0000003u;
    if(!required || (capacity && !out))return INVALID;if((kind==3 || kind==4) && ((uintptr_t)out&7))return 0x80000002u;
    uint16_t *name;unsigned bytes;uint32_t result=unicode(source,&name,&bytes);if(result)return result;hive h={0};result=begin(&h);if(result){wr_native_free(name);return result;}
    uint64_t id;result=resolve_handle(&h,handle,1,&id);record *r=!result?value(&h,id,name,bytes):0;if(!result && !r)result=MISSING;
    if(!result){unsigned base=kind==0?12:(kind==1 || kind==3?20:(kind==4?8:12)),offset=base;
        if(kind==1 || kind==3)offset=(base+r->name_bytes+(kind==3?7:3))&~(kind==3?7u:3u);
        uint64_t total=kind==0?(uint64_t)base+r->name_bytes:(uint64_t)offset+r->parent;
        if(total>UINT32_MAX)result=NO_MEMORY;
        else {unsigned length=(unsigned)total;if(wr_memory_copy(required,&length,4))result=0xc0000005u;
            else if(capacity<base)result=0xc0000023u;
            else {uint8_t *buffer=wr_native_alloc(length);if(!buffer)result=NO_MEMORY;
                else {memset(buffer,0,length);unsigned fields[5]={0,r->flags,r->name_bytes,0,0};
                    if(kind==1 || kind==3){fields[2]=offset;fields[3]=(unsigned)r->parent;fields[4]=r->name_bytes;}
                    else if(kind==2)fields[2]=(unsigned)r->parent;else if(kind==4){fields[0]=r->flags;fields[1]=(unsigned)r->parent;}
                    memcpy(buffer,fields,base);if(kind==0 || kind==1 || kind==3)memcpy(buffer+base,r+1,r->name_bytes);
                    if(kind!=0 && r->parent)memcpy(buffer+offset,(const uint8_t *)(r+1)+r->name_bytes,(size_t)r->parent);
                    unsigned copy=capacity<length?capacity:length;if(copy_large(out,buffer,copy))result=0xc0000005u;else if(copy<length)result=0x80000005u;
                    wr_native_free(buffer);}}}}
    wr_native_free(name);return end(&h,result);
}
static uint32_t WR_WIN NtDeleteValueKey(void *handle,wr_unicode *source){
    uint16_t *name;unsigned bytes;uint32_t status=unicode(source,&name,&bytes);if(status)return status;
    hive h={0};status=begin(&h);if(status){wr_native_free(name);return status;}uint64_t id;
    status=resolve_handle(&h,handle,2,&id);record *r=!status?value(&h,id,name,bytes):0;
    if(!status && !r)status=MISSING;
    if(!status){size_t offset=(uint8_t *)r-h.bytes,removed=r->bytes;
        for(size_t i=offset;i<h.length-removed;i++)h.bytes[i]=h.bytes[i+removed];h.length-=removed;((hive_header *)h.bytes)->records--;status=commit(&h);}
    wr_native_free(name);return end(&h,status);
}
static uint32_t WR_WIN RtlFormatCurrentUserKeyPath(wr_unicode *out) {
    if(!out)return INVALID;wr_lock(&gate);uint32_t result=initialize();uint16_t text[128];unsigned n=0;
    if(!result){const char *prefix="\\Registry\\User\\";while(prefix[n]){text[n]=(uint8_t)prefix[n];n++;}unsigned p=0;while(user_sid[p])text[n++]=user_sid[p++];text[n]=0;}
    wr_unlock(&gate);if(result)return result;
    void *(WR_WIN *allocate)(void *,unsigned,size_t)=wr_heap_exports("HeapAlloc");uint16_t *buffer=allocate(wr_process_heap(),0,(n+1)*2);if(!buffer)return NO_MEMORY;
    memcpy(buffer,text,(n+1)*2);wr_unicode value={(uint16_t)(n*2),(uint16_t)((n+1)*2),0,buffer};
    if(wr_memory_copy(out,&value,sizeof(value))){int (WR_WIN *free_heap)(void *,unsigned,void *)=wr_heap_exports("HeapFree");free_heap(wr_process_heap(),0,buffer);return 0xc0000005u;}return OK;
}
void *wr_registry_exports(const char *name) {
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(NtCreateKey);E(NtOpenKey);E(NtOpenKeyEx);E(NtDeleteKey);E(NtSetValueKey);E(NtQueryValueKey);E(NtDeleteValueKey);E(RtlFormatCurrentUserKeyPath);
#undef E
    return 0;
}
