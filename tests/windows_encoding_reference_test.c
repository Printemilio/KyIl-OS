#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "../userspace/windows/windows.h"
static wr_teb teb;
int wr_equal(const char *a,const char *b,int fold){(void)fold;return strcmp(a,b)==0;}
size_t wr_len(const char *s){return strlen(s);}
void *wr_encoding_exports(const char *);
static void *lookup(const char *n){return wr_encoding_exports(n);}
int main(void) {
    /* The target uses GS:0x30; attach a private TEB for these pure conversion
       functions and restore the actual host's pointer before any Win32 call. */
    void *host;__asm__ volatile("movq %%gs:0x30,%0":"=r"(host));
    typedef int (__attribute__((ms_abi)) *mb)(unsigned,unsigned,const char *,int,uint16_t *,int);
    typedef int (__attribute__((ms_abi)) *wc)(unsigned,unsigned,const uint16_t *,int,char *,int,const char *,int *);
    mb convert_mb=lookup("MultiByteToWideChar");wc convert_wc=lookup("WideCharToMultiByte");
    unsigned checked=0,state=0x12481632;
    for(unsigned iteration=0;iteration<40000;iteration++) {
        unsigned char input[16];uint16_t ours[40]={0},native[40]={0};unsigned n=1+iteration%16;
        for(unsigned i=0;i<n;i++){state=state*1664525u+1013904223u;input[i]=(unsigned char)(state>>24);}
        unsigned flags=iteration&1?8:0;
        SetLastError(0);int want=MultiByteToWideChar(CP_UTF8,flags,(const char *)input,n,(wchar_t *)native,40);unsigned error=GetLastError();
        teb.self=(uintptr_t)&teb;teb.last_error=0;__asm__ volatile("movq %0,%%gs:0x30"::"r"(&teb):"memory");
        int got=convert_mb(65001,flags,(const char *)input,n,ours,40);unsigned actual=teb.last_error;
        __asm__ volatile("movq %0,%%gs:0x30"::"r"(host):"memory");
        if(got!=want || (got && memcmp(ours,native,got*2)) || (!got && actual!=error)){printf("UTF8 decode divergence iteration %u want=%d got=%d errors=%u/%u\n",iteration,want,got,error,actual);for(unsigned i=0;i<n;i++)printf("%02x ",input[i]);printf("\nhost: ");for(int i=0;i<want;i++)printf("%04x ",native[i]);printf("\nours: ");for(int i=0;i<got;i++)printf("%04x ",ours[i]);printf("\n");return 1;}checked++;
        uint16_t wide[16];char bytes_ours[80]={0},bytes_native[80]={0};
        for(unsigned i=0;i<n;i++){state=state*1664525u+1013904223u;wide[i]=(uint16_t)(state>>16);}
        flags=iteration&1?0x80:0;SetLastError(0);
        want=WideCharToMultiByte(CP_UTF8,flags,(wchar_t *)wide,n,bytes_native,80,0,0);error=GetLastError();
        teb.last_error=0;__asm__ volatile("movq %0,%%gs:0x30"::"r"(&teb):"memory");
        got=convert_wc(65001,flags,wide,n,bytes_ours,80,0,0);actual=teb.last_error;
        __asm__ volatile("movq %0,%%gs:0x30"::"r"(host):"memory");
        if(got!=want || (got && memcmp(bytes_ours,bytes_native,got)) || (!got && actual!=error)){printf("UTF16 encode divergence iteration %u want=%d got=%d errors=%u/%u\n",iteration,want,got,error,actual);return 1;}checked++;
    }
    printf("windows-encoding-reference: %u actual Windows UTF8/UTF16 comparisons PASS\n",checked);return 0;
}
