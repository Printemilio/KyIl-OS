#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kernel/include/unos/security_write_window.h"
static unsigned checks;
static void check(int v){checks++;if(!v){fprintf(stderr,"write-window FAIL at %u\n",checks);exit(1);}}
int main(void){
    security_write_window_t w={0};
    for(unsigned i=0;i<10000;i++)check(!security_write_window_record(&w,1,1+i%2));
    check(w.distinct==2); /* transactional A/B snapshots are two objects */
    for(unsigned i=0;i<8;i++)check(!security_write_window_record(&w,2,1+i));
    check(security_write_window_record(&w,2,9)); /* same original mass-write threshold */
    w=(security_write_window_t){0};
    for(unsigned i=0;i<13;i++)check(!security_write_window_record(&w,1,1+i%9));
    check(security_write_window_record(&w,1,9));
    check(!security_write_window_record(&w,82,10) && w.distinct==1 && w.writes==1);
    w=(security_write_window_t){0};
    for(unsigned i=0;i<32;i++)check(!security_write_window_record(&w,1,0x100000001ull));
    for(unsigned i=0;i<7;i++)check(!security_write_window_record(&w,1,0x200000001ull+i));
    check(security_write_window_record(&w,1,0x300000001ull)); /* no truncated/hash identity equality */
    printf("security-write-window: %u exact identity / journal / mass rewrite / reset checks PASS\n",checks);
}
