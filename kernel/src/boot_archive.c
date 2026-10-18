#include <unos/boot_archive.h>
#include <mbedtls/sha256.h>
#include <string.h>
static int valid_path(const char *p) {
    if(p[0]!='/' || p[1]=='\0')return 0;
    size_t start=1u;
    for(size_t i=1;i<BOOT_ARCHIVE_PATH;++i) {
        if(p[i]=='/' || p[i]=='\0') {
            size_t n=i-start;
            if(!n || (n==1 && p[start]=='.') || (n==2 && p[start]=='.' && p[start+1]=='.'))return 0;
            if(!p[i])return 1;
            start=i+1;
        } else if((unsigned char)p[i]<32u || p[i]=='\\')return 0;
    }
    return 0;
}
int boot_archive_visit(const void *data,size_t size,int (*visit)(const boot_archive_file_t *,void *),void *context) {
    if(!data || size<sizeof(boot_archive_header_t))return -1;
    const boot_archive_header_t *h=data;
    if(memcmp(h->magic,"KYILBOOT",8)||h->version!=1||h->size!=size||h->reserved||
       !h->count||h->count>65536u||h->count>(size-sizeof(*h))/sizeof(boot_archive_file_t))return -1;
    uint8_t digest[32];
    if(mbedtls_sha256((const uint8_t *)data+sizeof(*h),size-sizeof(*h),digest,0)||memcmp(digest,h->sha256,32))return -1;
    const boot_archive_file_t *files=(const void *)((const uint8_t *)data+sizeof(*h));
    uint64_t directory_end=sizeof(*h)+(uint64_t)h->count*sizeof(*files);
    for(uint32_t i=0;i<h->count;++i) {
        const boot_archive_file_t *f=&files[i];
        if(!valid_path(f->path)||(i && strcmp(files[i-1].path,f->path)>=0)||
           (f->offset&4095u)||f->offset<directory_end||f->offset>size||f->size>size-f->offset)return -1;
    }
    for(uint32_t i=0;visit && i<h->count;++i)if(visit(&files[i],context))return -1;
    return 0;
}
