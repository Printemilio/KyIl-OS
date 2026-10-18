#include <stdint.h>
#include <stddef.h>

static long call(long n, long a, long b, long c, long d, long e, long f) {
    register long r10 __asm__("r10")=d, r8 __asm__("r8")=e, r9 __asm__("r9")=f;
    __asm__ volatile("syscall" : "+a"(n) : "D"(a), "S"(b), "d"(c),
                     "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return n;
}
static long open_path(const char *p) { return call(2,(long)p,0,0,0,0,0); }
static long check_path(const char *p) {
    uint64_t st[32];
    return call(4,(long)p,(long)st,0,0,0,0);
}
static int opens(const char *p) {
    long fd=open_path(p); if(fd<0)return 0;
    return call(3,fd,0,0,0,0,0)==0;
}
static void copy(char *p,const char *s) { do {*p++=*s;}while(*s++); }
static void fill(char *p,size_t n,char c) { while(n--)*p++=c; }

int test_main(void) {
    const char path[]="/lib/libc.so.6";
    long base=call(9,0,8192,3,0x22,-1,0);
    int failure=0;
    if(base<0) {failure=1;goto done;}
    char *p=(char *)base;
    /* Crossing a page boundary, including lazy materialization. */
    copy(p+4090,path);
    if(!opens(p+4090)) {failure=2;goto cleanup;}
    if(call(10,base,8192,1,0,0,0)) {failure=3;goto cleanup;}
    if(!opens(p+4090)) {failure=4;goto cleanup;}
    if(call(10,base,8192,3,0,0,0)) {failure=5;goto cleanup;}
    /* NUL in the very last byte must not validate the next unmapped page. */
    copy(p+4096-sizeof(path),path);
    if(call(11,base+4096,4096,0,0,0,0)) {failure=6;goto cleanup;}
    if(!opens(p+4096-sizeof(path))) {failure=7;goto cleanup;}
    fill(p+4080,16,'x');
    if(check_path(p+4080)!=-14) {failure=8;goto cleanup;}
    fill(p,4096,'x');
    if(check_path(p)!=-14) {failure=9;goto cleanup;}
    if(check_path((char *)0)!=-14 || check_path((char *)1)!=-14 ||
       check_path((char *)(uintptr_t)0xffff800000000000ull)!=-14 ||
       check_path((char *)(uintptr_t)0x00007fffffffffffull)!=-14) {
        failure=10;goto cleanup;
    }
    if(call(10,base,4096,0,0,0,0)) {failure=11;goto cleanup;}
    if(check_path(p)!=-14) {failure=12;goto cleanup;}
cleanup:
    call(11,base,4096,0,0,0,0);
    /* Use native DEBUG_LOG to validate an empty lazy string without depending
     * on filesystem empty-path policy. The next page is deliberately unreadable. */
    base=call(9,0,8192,3,0x22,-1,0);
    if(base<0 || call(10,base+4096,4096,0,0,0,0) ||
       call(0x5645584e0000005aull,base,0,0,0,0,0)!=0)failure=13;
    if(base>=0)call(11,base,8192,0,0,0,0);
done:
    if(failure) {
        char msg[]="user-validation: FAIL case 00\n";
        msg[27]='0'+failure/10;msg[28]='0'+failure%10;
        call(1,2,(long)msg,sizeof(msg)-1,0,0,0);
    } else {
        const char msg[]="user-validation: PASS (13 cases)\n";
        call(1,2,(long)msg,sizeof(msg)-1,0,0,0);
    }
    return failure;
}
__asm__(".global _start\n_start:\n andq $-16,%rsp\n call test_main\n mov %eax,%edi\n mov $231,%eax\n syscall\n ud2\n");
