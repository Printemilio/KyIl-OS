#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <assert.h>
#include "../userspace/windows/command_parse.h"
static void compare(const uint16_t *input){int count=0;wchar_t **reference=CommandLineToArgvW((const wchar_t *)input,&count);assert(reference);
    size_t n,units;wr_command_parse(input,NULL,NULL,&n,&units);uint16_t **v=malloc((n+1)*sizeof(*v)+units*2);assert(v);
    uint16_t *bytes=(void *)(v+n+1);size_t a,b;wr_command_parse(input,v,bytes,&a,&b);assert(a==n && b==units && !v[n]);
    if(n!=(unsigned)count){fwprintf(stderr,L"argc mismatch [%ls] native=%d ours=%zu\n",input,count,n);abort();}
    for(size_t i=0;i<n;i++)if(wcscmp((wchar_t *)v[i],reference[i])){fwprintf(stderr,L"arg %zu mismatch [%ls] native=[%ls] ours=[%ls]\n",i,input,reference[i],v[i]);abort();}
    free(v);LocalFree(reference);
}
int main(void){const wchar_t *cases[]={L" ",L"\tp a",L"p a\"\"b",L"p \"a\"\"b\"",L"\"p q\"r s",L"p \"\"\"",L"p \"\"\"\"",L"p a\\\\\"b c",L"p \"a\\\\\"b c",L"p \"a\"\"\"b",L"p a\"\"\"b",L"C:\\a.exe x",L"p \u00e9\u6f22\U0001f30d"};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++)compare((const uint16_t *)cases[i]);
    const wchar_t alphabet[]=L"ab \t\\\"";unsigned seed=0x12345678;
    for(unsigned t=0;t<60000;t++){uint16_t line[96];unsigned n=1+t%94;for(unsigned i=0;i<n;i++){seed=seed*1664525+1013904223;line[i]=(uint16_t)alphabet[(seed>>16)%6];}line[n]=0;compare(line);}
    puts("windows-command-reference: 60000 real Shell32 comparisons / argv0 / quotes / backslashes / whitespace / UTF16 PASS");return 0;
}
