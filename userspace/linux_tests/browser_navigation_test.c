#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unos/browser_navigation.h>
#include <unos/browser_state.h>
int main(void) {
    struct {char guard0;char output[VEX_BROWSER_URI_SIZE];char guard1;} b={.guard0=23,.guard1=42};
    assert(!browser_navigation_normalize("  file:///usr/browser/a.html  ",b.output,sizeof(b.output)));
    assert(!strcmp(b.output,"file:///usr/browser/a.html"));
    assert(!browser_navigation_normalize("HTTPS://example.com/a?b=1&c=2",b.output,sizeof(b.output)));
    assert(!strcmp(b.output,"HTTPS://example.com/a?b=1&c=2"));
    assert(!browser_navigation_normalize("pomme & fruit+#",b.output,sizeof(b.output)));
    assert(!strcmp(b.output,"https://duckduckgo.com/?q=pomme+%26+fruit%2B%23"));
    assert(!browser_navigation_normalize("localhost:8080/a",b.output,sizeof(b.output)));
    assert(!strcmp(b.output,"http://localhost:8080/a"));
    assert(!browser_navigation_normalize("example.com",b.output,sizeof(b.output)));
    assert(!strcmp(b.output,"https://example.com"));
    char long_uri[VEX_BROWSER_URI_SIZE];memset(long_uri,'a',sizeof(long_uri));memcpy(long_uri,"https://example.com/",20);long_uri[sizeof(long_uri)-1]=0;
    assert(!browser_navigation_normalize(long_uri,b.output,sizeof(b.output)) && !strcmp(long_uri,b.output));
    assert(!browser_navigation_normalize(long_uri+8,b.output,sizeof(b.output)));
    assert(browser_navigation_normalize(long_uri+7,b.output,sizeof(b.output))==-2);
    char expanded[400];memset(expanded,'&',sizeof(expanded));expanded[3]=' ';expanded[399]=0;
    assert(browser_navigation_normalize(expanded,b.output,sizeof(b.output))==-2 && !b.output[0]);
    assert(browser_navigation_normalize("x\ny",b.output,sizeof(b.output))==-1);
    assert(b.guard0==23 && b.guard1==42);
    puts("Native URI: long address, exact bounds, encoded search, no silent truncation/neighbor overwrite PASS");
}
