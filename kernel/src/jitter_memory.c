#include "jitterentropy.h"
#include <unos/heap.h>
#include <mbedtls/platform_util.h>
#include <string.h>
/* Existing kernel allocator/lifetime. Kernel pages are resident, inaccessible
 * to user mappings and never deduplicated as immutable ORA content. */
void jent_memset_secure(void *pointer,size_t length) {mbedtls_platform_zeroize(pointer,length);}
void *jent_zalloc(size_t length,unsigned int flags) {
    (void)flags;void *pointer=kmalloc(length);
    if(pointer)memset(pointer,0,length);return pointer;
}
void *jent_zalloc_unlocked(size_t length) {return jent_zalloc(length,0);}
void jent_zfree(void *pointer,size_t length) {
    if(pointer){jent_memset_secure(pointer,length);kfree(pointer);}
}
void *jent_zalloc_secure(size_t length) {return jent_zalloc(length,0);}
