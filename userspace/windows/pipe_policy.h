#ifndef KYIL_PIPE_POLICY_H
#define KYIL_PIPE_POLICY_H
#include <stdint.h>
#include <stddef.h>
static unsigned wp_u16(const void *p){const uint8_t *b=p;return b[0]|(unsigned)b[1]<<8;}
static uint32_t wp_u32(const void *p){const uint8_t *b=p;return b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
static int wp_span(size_t p,size_t n,size_t size){return p<=size && n<=size-p;}
static unsigned wp_map(unsigned mask){unsigned m=mask&0x0fffffffu;if(mask&0x80000000u)m|=0x120089;if(mask&0x40000000u)m|=0x120116;if(mask&0x20000000u)m|=0x1200a0;if(mask&0x10000000u)m|=0x1f01ff;return m;}
/* Returns match/nonmatch, or -1 for a principal requiring an unrepresented
 * token model. Normal native UID subjects are not NT SYSTEM, Administrators,
 * application containers, restricted tokens or impersonated identities. */
static int wp_sid(const uint8_t *p,size_t n,uint32_t uid){
    if(n<8 || p[0]!=1 || p[1]>15 || !wp_span(8,4u*p[1],n))return -2;
    uint64_t a=0;for(unsigned i=2;i<8;i++)a=(a<<8)|p[i];
    if(a==22 && p[1]==2 && wp_u32(p+8)==1)return wp_u32(p+12)==uid;
    if(a==1 && p[1]==1 && !wp_u32(p+8))return 1; /* Everyone */
    if(a==5 && p[1]==1 && wp_u32(p+8)==11)return 1; /* authenticated native subject */
    if(a==5 && p[1]==1 && wp_u32(p+8)==18)return 0; /* no NT SYSTEM token */
    if(a==15 && p[1]==2 && wp_u32(p+8)==2 && wp_u32(p+12)==1)return 0;
    return -1;
}
static unsigned wp_acl_valid(const uint8_t *p,size_t n,int sacl){
    if(n<8 || (p[0]!=2 && p[0]!=4) || wp_u16(p+2)>n || wp_u16(p+2)<8)return 1336;
    n=wp_u16(p+2);size_t at=8;for(unsigned i=0;i<wp_u16(p+4);i++){
        if(!wp_span(at,8,n))return 1336;unsigned size=wp_u16(p+at+2),kind=p[at];
        if((size&3) || !wp_span(at,size,n) || size<16)return 1336;
        const uint8_t *sid=p+at+8;if(sid[0]!=1 || sid[1]>15 || 8+4u*sid[1]!=size-8)return 1337;
        if(sacl){if(kind!=17 || sid[1]!=1 || sid[7]!=16 || wp_u32(sid+8) || wp_u32(p+at+4))return 50;
            for(unsigned k=2;k<7;k++)if(sid[k])return 50;}
        else {if(kind>1 || (p[at+1]&~31u))return 50;int match=wp_sid(sid,size-8,0);if(match<0)return match==-2?1337:50;}
        at+=size;
    }return 0;
}
static unsigned wp_policy_valid(const uint8_t *sd,size_t n){
    if(!sd)return n?1338:0;if(n<20 || sd[0]!=1 || !(wp_u16(sd+2)&0x8000))return 1338;
    for(unsigned i=0;i<2;i++){unsigned p=wp_u32(sd+4+4*i);if(p && (!wp_span(p,8,n) || sd[p]!=1 || sd[p+1]>15 || !wp_span(p,8+4u*sd[p+1],n)))return 1338;}
    for(unsigned i=0;i<2;i++){unsigned p=wp_u32(sd+12+4*i),present=wp_u16(sd+2)&(i?4:16);
        if(!present && p)return 1338;if(present && p){if((p&3)||!wp_span(p,8,n))return 1338;unsigned e=wp_acl_valid(sd+p,n-p,!i);if(e)return e;}}
    return 0;
}
static unsigned wp_authorize(const uint8_t *sd,size_t n,uint32_t owner,uint32_t uid,unsigned desired){
    unsigned error=wp_policy_valid(sd,n);if(error)return error;
    if(!sd)return uid==owner?0:5; /* Native user's default DACL. */
    unsigned p=wp_u32(sd+16);if(!(wp_u16(sd+2)&4) || !p)return 0;
    unsigned remaining=wp_map(desired),at=8;const uint8_t *acl=sd+p;
    for(unsigned i=0;i<wp_u16(acl+4);i++){const uint8_t *ace=acl+at;unsigned size=wp_u16(ace+2);at+=size;
        if(ace[1]&8)continue;int match=wp_sid(ace+8,size-8,uid);if(match<0)return 50;if(!match)continue;
        unsigned mask=wp_map(wp_u32(ace+4));if(ace[0]==1 && (mask&remaining))return 5;
        if(!ace[0])remaining&=~mask;
    }return remaining?5:0;
}
#endif
