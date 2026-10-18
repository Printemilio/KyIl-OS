/* Real unaligned PT_LOAD, with page-based mprotect at both byte boundaries.
 * No libc substitute: this freestanding regression exercises the loader,
 * inventory, MRAC split, PTE permissions, fork snapshots and cleanup. */
#include <stdint.h>
#include <stddef.h>
unsigned char payload[0x3040] __attribute__((section(".data.payload"),aligned(1),used)) = {[0 ... 0x303f]=0x5a};
static long call(long n,long a,long b,long c,long d){long r;
    register long fourth __asm__("r10")=d;
    __asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a),"S"(b),"d"(c),"r"(fourth):"rcx","r11","memory");return r;}
static int checksum(void){for(size_t i=0;i<sizeof(payload);i++)if(payload[i]!=0x5a)return 0;return 1;}
static void finish(int status){call(60,status,0,0,0);for(;;){}}
int test_main(void){
    const uintptr_t first=(uintptr_t)payload&~(uintptr_t)4095;
    const uintptr_t last=((uintptr_t)payload+sizeof(payload)-1)&~(uintptr_t)4095;
    int status=0;long child;
    if(!checksum() || call(10,first,4096,1,0) || call(10,last,4096,1,0))goto fail;
    child=call(57,0,0,0,0);if(child<0)goto fail;
    if(!child){if(!checksum())finish(78);payload[4096]=0x33;finish(0);}
    if(call(61,child,(long)&status,0,0)!=child || status || !checksum())goto fail;
    child=call(57,0,0,0,0);if(child<0)goto fail;
    if(!child){payload[0]=0x33;finish(77);}
    if(call(61,child,(long)&status,0,0)!=child || (status&127)!=11)goto fail;
    if(call(10,first,4096,3,0) || call(10,last,4096,3,0) || !checksum())goto fail;
    payload[0]=0x12;payload[sizeof(payload)-1]=0x34;
    if(payload[0]!=0x12 || payload[sizeof(payload)-1]!=0x34)goto fail;
    {static const char s[]="ELF protection edges: leading/tail split, checksum, fork isolation, readonly fault, restore PASS\n";call(1,2,(long)s,sizeof(s)-1,0);}
    return 0;
fail:{static const char s[]="ELF protection edges: FAIL\n";call(1,2,(long)s,sizeof(s)-1,0);}return 1;
}
__asm__(".text\n.global _start\n_start:\ncall test_main\nmov %eax,%edi\nmov $60,%eax\nsyscall\nud2\n");
