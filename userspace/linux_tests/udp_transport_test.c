#define VEX_NETWORK_HOST_TEST 1
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../../kernel/src/udp.c"
#undef assert
#define assert(x) do{if(!(x)){fprintf(stderr,"UDP FAIL line %d\n",__LINE__);abort();}}while(0)
static unsigned allocations,frees,packets;static uint64_t ticks;static int fail;
void *kmalloc(size_t n){if(fail)return NULL;allocations++;return malloc(n);}
void kfree(void *p){if(p)frees++;free(p);}
uint64_t irq_timer_ticks(void){return ticks;}
void ethernet_wake(void){}
uint32_t ipv4_get_local_ip(void){return 0x0a00020f;}
void ipv4_register_handler(uint8_t p,ipv4_handler_t h){(void)p;(void)h;}
int ipv4_send(uint32_t ip,uint8_t protocol,const void *data,size_t n){(void)ip;(void)data;assert(protocol==17 && n<=1480);packets++;return 0;}
static void packet(int fd,uint32_t ip,uint16_t port,const char *text){uint8_t bytes[100]={0};udp_header_t *h=(void *)bytes;size_t n=strlen(text);
    h->src_port=bswap16(port);h->dest_port=bswap16(udp_endpoint_port(fd));h->length=bswap16(8+n);memcpy(bytes+8,text,n);
    h->checksum=bswap16(udp_checksum(ip,ipv4_get_local_ip(),bytes,8+n));udp_handle_ipv4(ip,bytes,8+n);}
int main(void){udp_init();fail=1;assert(udp_endpoint_open(60000)==-12);fail=0;
    int h=udp_endpoint_open(60000);assert(h>=0 && udp_endpoint_open(60000)==-98);
    char b[16];uint32_t ip;uint16_t port;assert(udp_endpoint_receive(h,b,16,&ip,&port,0,0)==-11);
    packet(h,0x0a000202,53,"first");packet(h,0x0a000203,54,"second");
    assert(udp_endpoint_receive(h,b,2,&ip,&port,1,1)==5 && ip==0x0a000202 && port==53);
    assert(udp_endpoint_receive(h,b,2,NULL,NULL,0,0)==2 && !memcmp(b,"fi",2));
    assert(udp_endpoint_receive(h,b,16,&ip,&port,0,0)==6 && !memcmp(b,"second",6));
    udp_endpoint_peer(h,0x0a000202,53);packet(h,0x0a000203,53,"wrong");assert(!udp_endpoint_readable(h));
    assert(udp_endpoint_send(h,0x0a000202,53,"DNS",3)==3 && !packets);
    assert(udp_endpoint_send(h,0x0a000202,53,"DNS",3)==-11);udp_service();assert(packets==1 && udp_endpoint_writable(h));
    uint8_t quoted[31]={0x45,0,0,31,0,0,0,0,64,17};
    uint32_t source=bswap32(ipv4_get_local_ip()),destination=bswap32(0x0a000202);
    memcpy(quoted+12,&source,4);memcpy(quoted+16,&destination,4);
    uint16_t sum=bswap16(checksum16(quoted,20));memcpy(quoted+10,&sum,2);
    udp_header_t qh={.src_port=bswap16(60000),.dest_port=bswap16(53),.length=bswap16(11)};
    memcpy(quoted+20,&qh,8);memcpy(quoted+28,"DNS",3);
    udp_transport_fault_t fault;
    udp_transport_fault(0x0a000203,3,3,0,quoted,sizeof(quoted));
    assert(udp_endpoint_fault_status(h,&fault,1) && fault.code==3);
    assert(udp_endpoint_fault_receive(h,&fault)==-11 && !udp_endpoint_fault_pending(h));
    assert(udp_endpoint_fault_reporting(h,1)==0);
    udp_transport_fault(0x0a000203,3,3,0,quoted,27);assert(!udp_endpoint_fault_pending(h));
    quoted[10]^=1;udp_transport_fault(0x0a000203,3,3,0,quoted,31);assert(!udp_endpoint_fault_pending(h));quoted[10]^=1;
    quoted[23]^=1;udp_transport_fault(0x0a000203,3,3,0,quoted,31);assert(!udp_endpoint_fault_pending(h));quoted[23]^=1;
    for(unsigned i=0;i<6;i++)udp_transport_fault(0x0a000203,3,i,1500+i,quoted,31);
    for(unsigned i=0;i<4;i++){assert(!udp_endpoint_fault_receive(h,&fault));assert(fault.code==i && fault.detail==1500+i && fault.destination_ip==0x0a000202 && fault.destination_port==53 && fault.payload_size==3 && !memcmp(fault.payload,"DNS",3));}
    assert(udp_endpoint_fault_receive(h,&fault)==-11);
    udp_transport_fault(0x0a000203,11,0,0,quoted,31);assert(udp_endpoint_fault_pending(h));
    assert(!udp_endpoint_fault_reporting(h,0) && !udp_endpoint_fault_pending(h));
    udp_endpoint_close(h);assert(udp_endpoint_receive(h,b,16,NULL,NULL,0,0)==-9 && allocations==frees);
    for(unsigned i=0;i<1000;i++){h=udp_endpoint_open(0);assert(h>=0);udp_endpoint_close(h);}assert(allocations==frees);
    puts("Native UDP: OOM, port ownership, datagrams, peek/truncate, ICMP faults/validation/overflow, queued sends, cleanup PASS");
}
