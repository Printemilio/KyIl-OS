/* Deterministic protocol tests execute the native TCP sender/receiver itself. */
#define VEX_NETWORK_HOST_TEST 1
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../../kernel/src/tcp.c"
static uint64_t ticks;
static unsigned allocated,freed,wakes,packets;
static uint8_t packet[1500];static size_t packet_size;
static uint32_t sent_sequence[64];static size_t sent_payload[64];
void *kmalloc(size_t size){allocated++;return malloc(size);}
void kfree(void *p){if(p)freed++;free(p);}
uint64_t irq_timer_ticks(void){return ticks;}
void process_sleep_ticks(uint64_t n){ticks+=n;}
void ethernet_wake(void){wakes++;}
int ethernet_poll(void){return 0;}
void debug_log_puts(const char *s){(void)s;}
uint32_t ipv4_get_local_ip(void){return 0x0a00020f;}
uint32_t dhcp_netmask(void){return 0xffffff00;}
uint32_t dhcp_gateway(void){return 0x0a000202;}
int arp_lookup(uint32_t ip,uint8_t mac[6]){(void)ip;memset(mac,1,6);return 0;}
int arp_request(uint32_t ip){(void)ip;return 0;}
void ipv4_register_handler(uint8_t protocol,ipv4_handler_t handler){(void)protocol;(void)handler;}
int ipv4_send(uint32_t ip,uint8_t protocol,const void *data,size_t size){
    assert(ip==0x0a000202 && protocol==6 && size<=sizeof(packet));
    memcpy(packet,data,size);packet_size=size;
    if(size>=sizeof(tcp_header_t)&&packets<64u){
        const tcp_header_t *header=data;
        sent_sequence[packets]=bswap32(header->seq);
        sent_payload[packets]=size-sizeof(tcp_header_t);
    }
    packets++;return 0;
}
static void receive(int handle,uint32_t sequence,uint32_t ack,unsigned flags,const char *text){
    uint8_t bytes[100]={0};tcp_header_t *header=(void *)bytes;
    tcp_socket_t *s=&sockets[handle];size_t size=text?strlen(text):0;
    header->src_port=bswap16(s->remote_port);header->dest_port=bswap16(s->local_port);
    header->seq=bswap32(sequence);header->ack=bswap32(ack);header->flags=flags;
    header->data_offset_reserved=5<<4;header->window=bswap16(TCP_WINDOW_DEFAULT);
    if(size)memcpy(bytes+sizeof(*header),text,size);
    header->checksum=bswap16(tcp_checksum(s->remote_ip,ipv4_get_local_ip(),bytes,sizeof(*header)+size));
    tcp_handle_ipv4(s->remote_ip,bytes,sizeof(*header)+size);
}
int main(void){
    int fd=tcp_connect_start(0x0a000202,8080);assert(fd>=0 && wakes && tcp_connection_status(fd)==0);
    assert(tcp_service() && packets==1 && ((tcp_header_t *)packet)->flags==TCP_SYN);
    uint32_t next=sockets[fd].send_seq;
    receive(fd,100,next,TCP_SYN|TCP_ACK,NULL);assert(tcp_connection_status(fd)==1);
    char out[10];assert(tcp_try_read(fd,out,sizeof(out))==-2);
    char data[20000];memset(data,'X',sizeof(data));
    assert(tcp_try_write(fd,data,sizeof(data))==sizeof(data));
    const uint32_t data_start=sockets[fd].send_seq;
    unsigned before=packets;assert(tcp_service() && packets==before+10);
    assert(packet_size==sizeof(tcp_header_t)+1460 && sockets[fd].tx_size==sizeof(data));
    assert(sockets[fd].tx_inflight==10u*TCP_MAX_SEGMENT_SIZE);
    for(unsigned i=0;i<10u;++i){
        assert(sent_sequence[before+i]==data_start+i*TCP_MAX_SEGMENT_SIZE);
        assert(sent_payload[before+i]==TCP_MAX_SEGMENT_SIZE);
    }
    /* A future/invalid ACK cannot release bytes not transmitted. */
    receive(fd,101,sockets[fd].send_seq+1,TCP_ACK,NULL);assert(sockets[fd].tx_size==sizeof(data));
    receive(fd,101,data_start+10u*TCP_MAX_SEGMENT_SIZE,TCP_ACK,NULL);
    assert(sockets[fd].tx_size==sizeof(data)-10u*TCP_MAX_SEGMENT_SIZE);
    before=packets;tcp_service();
    assert(packets==before+4u && sockets[fd].tx_inflight==sizeof(data)-10u*TCP_MAX_SEGMENT_SIZE);
    assert(sent_payload[before]==1460u && sent_payload[before+3u]==1020u);
    receive(fd,101,sockets[fd].send_seq,TCP_ACK,NULL);assert(sockets[fd].tx_size==0u);
    receive(fd,101,sockets[fd].send_seq,TCP_ACK|TCP_FIN,"hello");
    assert(sockets[fd].recv_seq==107 && sockets[fd].tx_size==0);
    assert(tcp_try_read(fd,out,sizeof(out))==5 && !memcmp(out,"hello",5));
    assert(tcp_try_read(fd,out,sizeof(out))==0 && tcp_try_read(fd,out,sizeof(out))==0);
    tcp_close(fd);assert(!sockets[fd].used && allocated==freed);
    /* Loss recovery keeps retransmitting the oldest outstanding segment,
       without exceeding the peer's cumulative ACK or leaking the socket. */
    fd=tcp_connect_start(0x0a000202,8080);tcp_service();receive(fd,500,sockets[fd].send_seq,TCP_SYN|TCP_ACK,NULL);
    uint32_t loss_start=sockets[fd].send_seq;
    char loss_data[5000];memset(loss_data,'L',sizeof(loss_data));
    assert(tcp_try_write(fd,loss_data,sizeof(loss_data))==sizeof(loss_data));
    before=packets;tcp_service();assert(packets==before+4u && sockets[fd].tx_inflight==sizeof(loss_data));
    receive(fd,501,loss_start,TCP_ACK,NULL);receive(fd,501,loss_start,TCP_ACK,NULL);
    receive(fd,501,loss_start,TCP_ACK,NULL);assert(sockets[fd].fast_retransmit_pending);
    before=packets;tcp_service();assert(packets==before+1u);
    assert(bswap32(((tcp_header_t *)packet)->seq)==loss_start && packet_size==sizeof(tcp_header_t)+1460u);
    ticks+=TCP_INITIAL_RTO_TICKS;before=packets;tcp_service();assert(packets==before+1u);
    assert(bswap32(((tcp_header_t *)packet)->seq)==loss_start && sockets[fd].retries==1u);
    tcp_close(fd);assert(sockets[fd].used);
    receive(fd,501,loss_start+sizeof(loss_data),TCP_ACK,NULL);
    tcp_service();assert(sockets[fd].state==TCP_FIN_WAIT);
    receive(fd,501,sockets[fd].send_seq,TCP_ACK,NULL);tcp_service();
    assert(!sockets[fd].used && allocated==freed);
    puts("Native TCP: sliding window, cumulative ACK refill, invalid ACK, fast retransmit/RTO, FIN/EOF and close lifetime PASS");
}
