#include "windows.h"
#include "process.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
/* PE resource bytes remain immutable. VERSION.dll is a userspace projection
 * over confined file capabilities; no registry or NT code enters the kernel. */
static int file_span(const pe_image *image,uint32_t rva,uint32_t length,size_t total,uint64_t *offset) {
    if(rva<image->headers && pe_span(rva,length,image->headers) && pe_span(rva,length,total)){*offset=rva;return 1;}
    for(unsigned i=0;i<image->count;i++) {
        const pe_section *s=&image->sections[i];
        if(rva>=s->rva && pe_span(rva-s->rva,length,s->raw_size) && pe_span((uint64_t)s->raw+rva-s->rva,length,total)) {
            *offset=(uint64_t)s->raw+rva-s->rva;return 1;
        }
    }return 0;
}
static int resource_entry(const uint8_t *tree,unsigned size,unsigned at,unsigned wanted,int first,uint32_t *target) {
    if(!pe_span(at,16,size))return 0;
    unsigned named=pe_u16(tree+at+12),ids=pe_u16(tree+at+14);uint64_t count=(uint64_t)named+ids;
    if(!pe_span((uint64_t)at+16,count*8,size))return 0;
    for(unsigned i=named;i<count;i++) {
        const uint8_t *entry=tree+at+16+8ull*i;unsigned name=pe_u32(entry);
        if(!(name>>31) && (first || name==wanted)){*target=pe_u32(entry+4);return 1;}
    }return 0;
}
static int version_bytes(const uint16_t *path,uint8_t **file,size_t *file_size,const uint8_t **value,unsigned *length) {
    char native[1024];*file=0;*file_size=0;
    if(!path || wr_path_native(path,native,sizeof(native),0)){wr_current_teb()->last_error=5;return 0;}
    int fd=wr_namespace_call(VEX_FS_ROOT_OPEN,path,O_RDONLY|O_CLOEXEC|VEX_FS_OPEN_REGULAR,0,0,0);
    if(fd<0){wr_current_teb()->last_error=wr_file_error(-fd);return 0;}
    int read=wr_read_fd(fd,file,file_size);vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)fd);
    if(read){wr_current_teb()->last_error=193;return 0;}
    pe_image image;if(pe_parse(*file,*file_size,&image)){wr_current_teb()->last_error=193;return 0;}
    unsigned size=image.dirs[2][1];uint64_t at;uint32_t entry;
    if(!size || !file_span(&image,image.dirs[2][0],size,*file_size,&at)){wr_current_teb()->last_error=1812;return 0;}
    const uint8_t *tree=*file+at;
    if(!resource_entry(tree,size,0,16,0,&entry) || !(entry>>31) ||
       !resource_entry(tree,size,entry&0x7fffffffu,1,0,&entry) || !(entry>>31) ||
       !resource_entry(tree,size,entry&0x7fffffffu,0,1,&entry) || (entry>>31) || !pe_span(entry,16,size)) {
        wr_current_teb()->last_error=1813;return 0;
    }
    uint32_t rva=pe_u32(tree+entry),bytes=pe_u32(tree+entry+4);
    if(bytes<6 || !file_span(&image,rva,bytes,*file_size,&at) || pe_u16(*file+at)>bytes || pe_u16(*file+at)<6) {
        wr_current_teb()->last_error=1812;return 0;
    }
    *value=*file+at;*length=bytes;return 1;
}
static unsigned WR_WIN GetFileVersionInfoSizeW(const uint16_t *path,unsigned *ignored) {
    if(ignored)*ignored=0;uint8_t *file=0;size_t size=0;const uint8_t *value;unsigned length=0;
    int ok=version_bytes(path,&file,&size,&value,&length);unsigned error=wr_current_teb()->last_error;
    if(file)wr_unmap(file,size);wr_current_teb()->last_error=error;return ok?length:0;
}
static int WR_WIN GetFileVersionInfoW(const uint16_t *path,unsigned ignored,unsigned capacity,void *out) {
    (void)ignored;if(!out){wr_current_teb()->last_error=87;return 0;}
    uint8_t *file=0;size_t size=0;const uint8_t *value;unsigned length;
    int ok=version_bytes(path,&file,&size,&value,&length);
    if(ok && capacity<length){wr_current_teb()->last_error=122;ok=0;}
    if(ok)memcpy(out,value,length);
    unsigned error=wr_current_teb()->last_error;if(file)wr_unmap(file,size);wr_current_teb()->last_error=error;return ok;
}
typedef struct {unsigned end,key,value,value_bytes,value_length,children,type;} version_node;
static int node(const uint8_t *bytes,unsigned total,unsigned at,version_node *out) {
    if(!pe_span(at,6,total))return 0;unsigned length=pe_u16(bytes+at),units=pe_u16(bytes+at+2),type=pe_u16(bytes+at+4);
    if(length<6 || !pe_span(at,length,total) || type>1)return 0;
    unsigned end=at+length,key=at+6,cursor=key;
    while(pe_span(cursor,2,end) && pe_u16(bytes+cursor))cursor+=2;
    if(!pe_span(cursor,2,end))return 0;
    unsigned value=(cursor+2+3)&~3u,value_bytes=units*(type?2u:1u);
    /* Containers may end immediately after their NUL key, without padding. */
    if(value>end && !value_bytes)value=end;
    if(!pe_span(value,value_bytes,end))return 0;
    *out=(version_node){end,key,value,value_bytes,units,(value+value_bytes+3)&~3u,type};return 1;
}
static int key_equal(const uint8_t *bytes,unsigned key,const uint16_t *name,unsigned length) {
    for(unsigned i=0;i<length;i++) {
        unsigned a=pe_u16(bytes+key+2*i),b=name[i];
        if(a>='A' && a<='Z')a+=32;if(b>='A' && b<='Z')b+=32;if(a!=b)return 0;
    }return !pe_u16(bytes+key+2*length);
}
static int WR_WIN VerQueryValueW(const void *block,const uint16_t *subblock,void **out,unsigned *length) {
    if(!block || !subblock || !out || !length){wr_current_teb()->last_error=87;return 0;}
    *out=0;*length=0;const uint8_t *bytes=block;unsigned total=pe_u16(bytes),at=0;version_node current;
    if(!node(bytes,total,0,&current)){wr_current_teb()->last_error=1812;return 0;}
    const uint16_t *part=subblock;
    while(*part) {
        while(*part=='\\')part++;if(!*part)break;
        unsigned n=0;while(part[n] && part[n]!='\\')n++;
        unsigned child=current.children;int found=0;
        while(child<current.end) {
            version_node candidate;if(!node(bytes,current.end,child,&candidate))break;
            unsigned key_units=0;while(candidate.key+2*key_units+2<=candidate.value && pe_u16(bytes+candidate.key+2*key_units))key_units++;
            if(key_units==n && key_equal(bytes,candidate.key,part,n)){at=child;current=candidate;found=1;break;}
            unsigned next=(candidate.end+3)&~3u;if(next<=child)break;child=next;
        }
        if(!found){wr_current_teb()->last_error=1813;return 0;}part+=n;
    }
    (void)at;
    if(!current.value_bytes){wr_current_teb()->last_error=1813;return 0;}
    *out=(void *)(bytes+current.value);*length=current.value_length;return 1;
}
void *wr_version_exports(const char *name) {
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(GetFileVersionInfoSizeW);E(GetFileVersionInfoW);E(VerQueryValueW);
#undef E
    return 0;
}
