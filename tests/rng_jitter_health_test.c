#include "jitterentropy.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Deterministic NEGATIVE test of upstream's actual startup health checks.
 * This callback is linked only by the host test, never into the kernel. */
void jent_get_nstime(uint64_t *out) {static uint64_t counter;*out=++counter;}
void jent_memset_secure(void *p,size_t n) {volatile unsigned char *b=p;while(n--)*b++=0;}
static int fail_allocate,live;
void *jent_zalloc(size_t n,unsigned flags) {
    (void)flags;if(fail_allocate)return NULL;
    void *p=calloc(1,n);if(p)++live;return p;
}
void *jent_zalloc_unlocked(size_t n) {return jent_zalloc(n,0);}
void jent_zfree(void *p,size_t n) {if(p){jent_memset_secure(p,n);free(p);--live;}}
int main(int argc,char **argv) {
    (void)argv;fail_allocate=argc>1;
    int result=jent_entropy_init();
    assert(result!=0 && live==0);
    printf("rng-health: PASS %s rejected, no retained allocations (%d)\n",fail_allocate?"OOM":"predictable constant-delta clock",result);
}
