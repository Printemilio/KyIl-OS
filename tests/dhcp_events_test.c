#include <unos/dhcp.h>
#include <unos/udp.h>
#include <unos/ora.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
static udp_handler_t receiver;static uint64_t now;static uint32_t local;static int link;static unsigned sends,writes;static uint8_t sent[600];
static const uint8_t mac[6]={8,0,0x27,0xaa,0xbb,0xcc};
uint64_t boot_time_ms(void){return now;}uint64_t network_state_lock(void){return 0;}void network_state_unlock(uint64_t f){(void)f;}
const uint8_t *netdev_mac(void){return mac;}int netdev_link_up(void){return link;}void ethernet_wake(void){}
void ipv4_set_local_ip(uint32_t ip){local=ip;}
void udp_register_handler(uint16_t port,udp_handler_t h){assert(port==68);receiver=h;}
int udp_send(uint32_t dst,uint16_t src,uint16_t port,const void *bytes,size_t n){assert(dst==UINT32_MAX&&src==68&&port==67&&n<=sizeof(sent));memcpy(sent,bytes,n);sends++;return 0;}
int fs_read_view(const char *p,void **b,size_t *n,ora_view_t *v){(void)p;(void)b;(void)n;(void)v;return -1;}
int fs_write(const char *p,const void *b,size_t n){(void)p;(void)b;assert(n);writes++;return 0;}
void ora_view_release(ora_view_t *v){(void)v;}void tty_puts(const char *s){(void)s;}void tty_putchar(char c){(void)c;}void debug_log_puts(const char *s){(void)s;}
static void reply(unsigned type,uint32_t address,uint32_t mask) {
    uint8_t p[300]={2,1,6};memcpy(p+4,sent+4,4);memcpy(p+28,mac,6);
    p[16]=address>>24;p[17]=address>>16;p[18]=address>>8;p[19]=address;
    p[236]=0x63;p[237]=0x82;p[238]=0x53;p[239]=0x63;
    uint8_t options[]={53,1,type,1,4,mask>>24,mask>>16,mask>>8,mask,3,4,10,0,2,2,6,4,10,0,2,3,255};
    memcpy(p+240,options,sizeof(options));receiver(0x0a000202,67,p,sizeof(p));
}
int main(void){
    dhcp_init();dhcp_start();assert(local==0);
    for(unsigned i=0;i<20;i++){now+=100;assert(!dhcp_service());}assert(sends==0);
    link=1;assert(dhcp_service()>0&&sends==1&&sent[242]==1); /* Real link event, then DISCOVER. */
    now+=100;assert(dhcp_service()>0&&sends==1); /* No polling/retransmit before deadline. */
    reply(2,0x0a00020f,0xffffff00);assert(dhcp_service()>0&&sends==2&&sent[242]==3);
    reply(5,0x0a00020e,0xffffff00);assert(local==0&&writes==0); /* Unrequested lease refused. */
    reply(5,0x0a00020f,0);assert(local==0&&writes==0);
    reply(5,0x0a00020f,0xffffff00);assert(local==0x0a00020f&&writes==1&&dhcp_dns_server()==0x0a000203&&!dhcp_service());
    dhcp_init();dhcp_start();now+=100;for(unsigned i=0;i<10;i++){dhcp_service();now+=1000;}
    assert(local==0); /* Timeout must never fabricate a VirtualBox address. */
    puts("PASS: actual DHCP state machine, link wait, deadlines, OFFER/REQUEST/ACK, invalid ACK and no fallback IP");
}
