#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unos/browser_edit.h>
#define CHECK(x) do{if(!(x))abort();}while(0)
int main(void){char b[32]="https://old.example/";uint64_t n=strlen(b),c=n;uint8_t selected=1;
    const char *uri="file:///usr/browser/a.html";
    for(const char *p=uri;*p;p++)CHECK(!browser_edit_insert(b,sizeof(b),&n,&c,&selected,*p));
    CHECK(!strcmp(b,uri) && n==strlen(b) && c==n);
    c=5;CHECK(!browser_edit_insert(b,sizeof(b),&n,&c,&selected,'X'));CHECK(b[5]=='X');
    browser_edit_backspace(b,&n,&c,&selected);CHECK(!strcmp(b,uri));
    selected=1;browser_edit_backspace(b,&n,&c,&selected);CHECK(!n && !c && !b[0]);
    for(unsigned i=0;i<31;i++)CHECK(!browser_edit_insert(b,sizeof(b),&n,&c,&selected,'a'));
    CHECK(browser_edit_insert(b,sizeof(b),&n,&c,&selected,'b')==-1 && strlen(b)==31);
    puts("Native address editor: select/replace terminator, insert/caret, backspace, bounds PASS");
}
