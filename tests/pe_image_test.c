#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../userspace/windows/pe_image.h"
static unsigned char f[2048], b[16384];
static void u16(size_t p,unsigned v) {f[p]=v;f[p+1]=v>>8;}
static void u32(size_t p,unsigned v) {u16(p,v);u16(p+2,v>>16);}
static void fixture(void) {
    memset(f,0,sizeof(f));u16(0,0x5a4d);u32(60,128);u32(128,0x4550);
    u16(132,0x8664);u16(134,2);u16(148,240);u16(150,2);
    size_t o=152;u16(o,0x20b);u32(o+16,4096);pe_put64(f+o+24,0x140000000ull);
    u32(o+32,4096);u32(o+36,512);u32(o+56,16384);u32(o+60,512);u16(o+68,3);u32(o+108,16);
    u32(392+8,512);u32(392+12,4096);u32(392+16,512);u32(392+20,512);u32(392+36,0x60000000);
    u32(432+8,512);u32(432+12,8192);u32(432+16,512);u32(432+20,1024);u32(432+36,0xc0000000);
}
int main(void) {
    pe_image im;unsigned checks=0;
#define CHECK(x) do{assert(x);checks++;}while(0)
    fixture();CHECK(!pe_parse(f,sizeof(f),&im));CHECK(pe_page_access(&im,4096)==(PE_R|PE_X));
    CHECK(pe_page_access(&im,8192)==(PE_R|PE_W));CHECK(pe_page_access(&im,12288)==0);
    CHECK(pe_parse(f,20,&im)!=0);CHECK(!pe_span(UINT64_MAX,2,UINT64_MAX));
    fixture();u32(60,0xfffffff0);CHECK(pe_parse(f,sizeof(f),&im)!=0);
    fixture();u16(152,0x10b);CHECK(pe_parse(f,sizeof(f),&im)!=0);
    fixture();u32(392+36,0xe0000000);CHECK(pe_parse(f,sizeof(f),&im)!=0);
    fixture();u32(432+12,4096);CHECK(pe_parse(f,sizeof(f),&im)!=0);
    fixture();u32(392+16,0xfffffe00);CHECK(pe_parse(f,sizeof(f),&im)!=0);
    fixture();u32(152+16,8192);CHECK(pe_parse(f,sizeof(f),&im)!=0);
    fixture();u32(152+16,4096+600);CHECK(pe_parse(f,sizeof(f),&im)!=0);
    fixture();u32(152+108,17);CHECK(pe_parse(f,sizeof(f),&im)!=0);
    fixture();CHECK(!pe_parse(f,sizeof(f),&im));memset(b,0,sizeof(b));
    CHECK(!im.relocations_stripped && !pe_relocate(b,&im,im.preferred+65536));
    u16(150,3);CHECK(!pe_parse(f,sizeof(f),&im) && im.relocations_stripped);
    CHECK(pe_relocate(b,&im,im.preferred+65536)!=0);
    CHECK(!pe_relocate(b,&im,im.preferred));
    u16(150,2);CHECK(!pe_parse(f,sizeof(f),&im));
    im.dirs[5][0]=8192;im.dirs[5][1]=12;
    b[8192+1]=0x10;b[8196]=12;b[8200]=16;b[8201]=0xa0;pe_put64(b+4112,im.preferred+123);
    CHECK(!pe_relocate(b,&im,im.preferred+65536));CHECK(pe_u64(b+4112)==im.preferred+65536+123);
    b[8201]=0x30;CHECK(pe_relocate(b,&im,im.preferred)!=0);
    b[8196]=0;CHECK(pe_relocate(b,&im,im.preferred)!=0);
    b[8196]=12;b[8192+1]=0xf0;b[8201]=0xa0;CHECK(pe_relocate(b,&im,im.preferred)!=0);
    printf("pe-image: %u checks PASS\n",checks);
}
