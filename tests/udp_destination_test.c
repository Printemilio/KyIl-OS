#include <unos/ipv4.h>
#include <unos/udp.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static ipv4_handler_t receive;
void ipv4_register_handler(uint8_t protocol,ipv4_handler_t h){assert(protocol==17);receive=h;}
uint32_t ipv4_get_local_ip(void){return 0x0a00020f;}
int ipv4_send(uint32_t dst,uint8_t proto,const void *p,size_t n){(void)dst;(void)proto;(void)p;(void)n;abort();}
void *kmalloc(size_t n){return malloc(n);}void kfree(void *p){free(p);}
void ethernet_wake(void){}uint64_t irq_timer_ticks(void){return 0;}
static unsigned delivered;
static void handler(uint32_t src,uint16_t port,const void *bytes,size_t n){assert(src==0x0a000202&&port==67&&n==4&&!memcmp(bytes,"test",4));delivered++;}
static uint16_t checksum(uint8_t *p,size_t n){uint32_t sum=0;for(size_t i=0;i<n;i+=2)sum+=(uint16_t)p[i]<<8|(i+1<n?p[i+1]:0);while(sum>>16)sum=(sum&65535)+(sum>>16);return ~sum;}
int main(void){
    udp_init();udp_register_handler(68,handler);
    uint8_t pseudo[]={10,0,2,2,255,255,255,255,0,17,0,12,0,67,0,68,0,12,0,0,'t','e','s','t'};
    uint16_t sum=checksum(pseudo,sizeof(pseudo));pseudo[18]=sum>>8;pseudo[19]=sum;
    receive(0x0a000202,UINT32_MAX,pseudo+12,12);assert(delivered==1);
    /* Valid broadcast packet must not be checked against configured unicast IP. */
    receive(0x0a000202,0x0a00020f,pseudo+12,12);assert(delivered==1);
    pseudo[23]^=1;receive(0x0a000202,UINT32_MAX,pseudo+12,12);assert(delivered==1);
    puts("PASS: actual UDP receiver validates destination pseudoheader and rejects bad checksum");
}
