#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../userspace/windows/pipe_policy.h"
static void put16(unsigned char *p,unsigned n){p[0]=n;p[1]=n>>8;}
static void put32(unsigned char *p,unsigned n){for(unsigned i=0;i<4;i++)p[i]=n>>(8*i);}
static unsigned ace(unsigned char *p,unsigned type,unsigned mask,unsigned uid){p[0]=type;p[1]=0;put16(p+2,24);put32(p+4,mask);p[8]=1;p[9]=2;p[15]=22;put32(p+16,1);put32(p+20,uid);return 24;}
int main(void){unsigned char sd[160]={1,0,4,128};put32(sd+16,20);sd[20]=2;put16(sd+22,56);put16(sd+24,2);
    ace(sd+28,1,2,17);ace(sd+52,0,0x10000000,17);
    assert(!wp_policy_valid(sd,76));assert(!wp_authorize(sd,76,0,17,0x80000000));assert(wp_authorize(sd,76,0,17,0x40000000)==5);assert(wp_authorize(sd,76,0,18,0x80000000)==5);
    sd[29]=8;assert(!wp_authorize(sd,76,0,17,0xc0000000));sd[29]=0;
    for(unsigned n=0;n<76;n++)assert(wp_policy_valid(sd,n));
    sd[36]=2;assert(wp_policy_valid(sd,76)==1337);sd[36]=1;
    put32(sd+16,0);assert(!wp_authorize(sd,76,0,99,0xc0000000));put32(sd+16,20);put16(sd+22,8);put16(sd+24,0);assert(wp_authorize(sd,76,0,17,1)==5);
    assert(!wp_authorize(0,0,17,17,0xc0000000) && wp_authorize(0,0,17,18,1)==5);
    put16(sd+22,32);put16(sd+24,1);ace(sd+28,0,0x10000000,17);sd[43]=5;assert(wp_policy_valid(sd,52)==50);
    unsigned seed=0x12345678;for(unsigned cycle=0;cycle<10000;cycle++){unsigned char input[160];for(unsigned n=0;n<160;n++){seed=seed*1664525+1013904223;input[n]=seed>>24;}wp_authorize(input,cycle%161,0,17,0xc0000000);}
    puts("windows-pipe-policy: ordered deny/allow, requested rights, real UID, null/empty/default, inherited-only, every truncation, unrepresented principal, 10000 malformed records PASS");return 0;
}

