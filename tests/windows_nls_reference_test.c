/* Independent differential test against this host's real Windows NLS. Runs
 * only on Windows; it is not a guest compatibility shim or a VM measurement. */
#include "../userspace/windows/windows.h"
extern int printf(const char *,...);
__declspec(dllimport) void *LoadLibraryA(const char *);
__declspec(dllimport) void *GetProcAddress(void *,const char *);
int wr_equal(const char *a,const char *b,int fold) {
    (void)fold;while(*a==*b) {if(!*a)return 1;a++;b++;}return 0;
}
static uint32_t seed=0x90129471;
static unsigned next(void) {seed=seed*1664525u+1013904223u;return seed;}
int main(void) {
    typedef int (WR_WIN *compare)(unsigned,unsigned,const uint16_t *,int,const uint16_t *,int);
    compare native=(compare)GetProcAddress(LoadLibraryA("kernel32.dll"),"CompareStringW");
    compare vex=(compare)wr_nls_exports("CompareStringW");
    const char alphabet[]="0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    unsigned cases=0;
    for(unsigned i=0;i<20000;i++) {
        uint16_t a[17],b[17];int na=(int)(next()%17),nb=(int)(next()%17);
        for(int k=0;k<na;k++)a[k]=(uint16_t)alphabet[next()%62];a[na]=0;
        for(int k=0;k<nb;k++)b[k]=(uint16_t)alphabet[next()%62];b[nb]=0;
        for(unsigned locale=0;locale<2;locale++)for(unsigned flags=0;flags<2;flags++) {
            unsigned l=locale?0x409:0x7f;
            int x=native(l,flags,a,na,b,nb),y=vex(l,flags,a,na,b,nb);cases++;
            if(x!=y) {printf("windows-nls-reference: mismatch case=%u locale=%x flags=%u expected=%d actual=%d\n",i,l,flags,x,y);return 1;}
        }
    }
    printf("windows-nls-reference: %u actual Windows comparisons PASS (basic-Latin alphanumeric only)\n",cases);return 0;
}
