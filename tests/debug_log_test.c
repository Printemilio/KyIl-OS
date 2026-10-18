#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unos/debug_log.h>

static char transmitted[24000];
static size_t tx_count, polls, credit;
static int fifo, dlab;
void debug_log_test_outb(uint16_t port, uint8_t byte) {
    if (port==0x3fb) {dlab=byte&0x80;return;}
    if (port!=0x3f8) return;
    /* During init DLAB selects the divisor, not the transmitter. */
    if (dlab) return;
    assert(credit>0);
    --credit;
    assert(tx_count<sizeof(transmitted));
    transmitted[tx_count++]=(char)byte;
}
uint8_t debug_log_test_inb(uint16_t port) {
    if (port==0x3fa) return fifo?0xc0:0;
    assert(port==0x3fd);
    ++polls;
    credit=fifo?16:1;
    return 0x20;
}
static void check(int enabled) {
    fifo=enabled; credit=0; debug_log_init(); tx_count=polls=0;
    char bytes[10000], expected[11000]; size_t used=0;
    for (size_t i=0;i<sizeof(bytes);++i) {
        bytes[i]=i%19==15?'\n':(char)('a'+i%26);
        if (bytes[i]=='\n') expected[used++]='\r';
        expected[used++]=bytes[i];
    }
    bytes[10]=0; expected[10]=0; /* counted stderr must preserve binary bytes */
    debug_log_write(bytes,sizeof(bytes));
    assert(tx_count==used && !memcmp(transmitted,expected,used));
    assert(polls==(used+(enabled?15:0))/(enabled?16:1));
    char ring[8192]; uint32_t sequence=0;
    assert(debug_log_copy_recent(ring,sizeof(ring),&sequence)==sizeof(ring));
    assert(!memcmp(ring,bytes+sizeof(bytes)-sizeof(ring),sizeof(ring)));
    tx_count=polls=0; debug_log_puts("fatal diagnostic\n");
    assert(tx_count==18 && !memcmp(transmitted,"fatal diagnostic\r\n",18));
    printf("UART %s: exact bytes, CRLF, FIFO bursts, ring wrap, fatal log PASS\n",enabled?"FIFO":"single-byte");
}
int main(void) {check(1);check(0);return 0;}
