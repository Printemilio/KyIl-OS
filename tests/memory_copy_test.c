#include <unos/memory_copy.h>
#include <assert.h>
#include <stdio.h>
int main(void) {
    unsigned char source[5000],dest[5000];
    for(unsigned i=0;i<5000;i++)source[i]=(unsigned char)(i*37);
    for(unsigned a=0;a<32;a++)for(unsigned b=0;b<32;b++)for(unsigned n=0;n<512;n++) {
        for(unsigned i=0;i<5000;i++)dest[i]=0xa5;
        assert(kyil_memory_copy(dest+b,source+a,n)==dest+b);
        for(unsigned i=0;i<5000;i++)assert(dest[i]==(i>=b && i<b+n?source[a+i-b]:0xa5));
    }
    assert(kyil_memory_copy(dest,source,4097)==dest);
    for(unsigned i=0;i<4097;i++)assert(dest[i]==source[i]);
    puts("memory-copy: PASS 32x32 alignments, lengths 0..511, exact tails and 4097-byte boundary");
}
