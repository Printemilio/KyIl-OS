#ifndef UNOS_BROWSER_NAVIGATION_H
#define UNOS_BROWSER_NAVIGATION_H
#include <stddef.h>
/* Native address interpretation. No network fetch or synthetic result page.
 * Every accepted byte must fit; an expanded query is never silently truncated. */
static inline int browser_navigation_prefix(const char *s,size_t n,const char *prefix) {
    for(size_t i=0;prefix[i];i++) {
        if(i>=n)return 0;
        unsigned char c=(unsigned char)s[i];if(c>='A'&&c<='Z')c+='a'-'A';
        if(c!=(unsigned char)prefix[i])return 0;
    }
    return 1;
}
static inline int browser_navigation_normalize(const char *input,char *output,size_t capacity) {
    if(!input || !output || !capacity)return -1;
    output[0]=0;size_t begin=0,end=0;
    while(input[end])end++;
    while(begin<end && (input[begin]==' '||input[begin]=='\t'))begin++;
    while(end>begin && (input[end-1]==' '||input[end-1]=='\t'))end--;
    if(begin==end)return -1;
    const char *s=input+begin;size_t n=end-begin;int space=0,host=0;
    for(size_t i=0;i<n;i++){unsigned char c=s[i];if(c<32 || c==127)return -1;if(c==' ')space=1;if(c=='.'||c==':'||c=='/')host=1;}
    int explicit_uri=browser_navigation_prefix(s,n,"http://") || browser_navigation_prefix(s,n,"https://") ||
        browser_navigation_prefix(s,n,"file://") || browser_navigation_prefix(s,n,"about:") || browser_navigation_prefix(s,n,"vex://");
    int local=browser_navigation_prefix(s,n,"localhost") && (n==9||s[9]==':'||s[9]=='/');
    local|=browser_navigation_prefix(s,n,"127.") || browser_navigation_prefix(s,n,"[::1]");
    int search=!explicit_uri && (space || (!host && !local));
    const char *prefix=explicit_uri?"":(search?"https://duckduckgo.com/?q=":(local?"http://":"https://"));
    size_t used=0;while(prefix[used]){if(used+1>=capacity)goto too_long;output[used]=prefix[used];used++;}
    for(size_t i=0;i<n;i++) {
        unsigned char c=s[i];
        int plain=!search || (c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='-'||c=='_'||c=='.'||c=='~';
        size_t needed=plain||c==' '?1:3;
        if(needed>=capacity-used)goto too_long;
        if(plain)output[used++]=c;
        else if(c==' ')output[used++]='+';
        else {static const char hex[]="0123456789ABCDEF";output[used++]='%';output[used++]=hex[c>>4];output[used++]=hex[c&15];}
    }
    output[used]=0;return 0;
too_long:
    output[0]=0;return -2;
}
#endif
