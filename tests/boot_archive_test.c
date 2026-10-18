#include <unos/boot_archive.h>
#include <mbedtls/sha256.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void mbedtls_free(void *pointer){free(pointer);}
static unsigned visited;
static int visit(const boot_archive_file_t *f,void *context) {
    const char *bytes=context;assert(!memcmp(bytes+f->offset,"hello",5));++visited;return 0;
}
static void seal(unsigned char *bytes,size_t size) {
    boot_archive_header_t *h=(void *)bytes;
    assert(!mbedtls_sha256(bytes+64,size-64,h->sha256,0));
}
int main(void) {
    size_t size=8192;unsigned char *bytes=calloc(1,size);assert(bytes);
    boot_archive_header_t *h=(void *)bytes;
    memcpy(h->magic,"KYILBOOT",8);h->version=1;h->count=2;h->size=size;
    boot_archive_file_t *f=(void *)(bytes+64);
    strcpy(f[0].path,"/a");strcpy(f[1].path,"/b");
    f[0].offset=f[1].offset=4096;f[0].size=f[1].size=5;memcpy(bytes+4096,"hello",5);seal(bytes,size);
    assert(!boot_archive_visit(bytes,size,visit,bytes) && visited==2);
    visited=0;bytes[4096]^=1;assert(boot_archive_visit(bytes,size,visit,bytes)<0 && !visited);bytes[4096]^=1;
    f[1].size=UINT64_MAX;seal(bytes,size);assert(boot_archive_visit(bytes,size,visit,bytes)<0 && !visited);f[1].size=5;
    strcpy(f[1].path,"/a");seal(bytes,size);assert(boot_archive_visit(bytes,size,visit,bytes)<0 && !visited);
    strcpy(f[1].path,"/b/../c");seal(bytes,size);assert(boot_archive_visit(bytes,size,visit,bytes)<0 && !visited);
    strcpy(f[1].path,"/b");f[1].offset=128;seal(bytes,size);assert(boot_archive_visit(bytes,size,visit,bytes)<0 && !visited);
    f[1].offset=4096;seal(bytes,size);
    for(size_t n=0;n<size;++n)assert(boot_archive_visit(bytes,n,visit,bytes)<0);
    assert(!boot_archive_visit(bytes,size,visit,bytes) && visited==2);
    free(bytes);puts("boot-archive: PASS aliases/views/SHA256/paths/bounds/duplicates/all truncations");
}
