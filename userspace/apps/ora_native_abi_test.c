#include <stdint.h>
#include <string.h>
#include <unos/vex_legacy_compat.h>
#include "vex_runtime.h"
#include "vex_syscall.h"

int main(void) {
    const uint64_t a=0x1000200030004000ull,b=a+4096;
    const char x[]="original",y[]="replacement";
    char bytes[32]={0};uint32_t v1=0,v2=0,resolved=0;size_t size=0;
    vex_mmap_req_t map={0};vex_memory_req_t memory={0};
    uint64_t address=0;int result=1;
    if(vex_mrac_put(a,x,sizeof(x),&v1) || v1!=1 ||
       vex_mrac_put(b,x,sizeof(x),NULL) ||
       vex_mrac_put(a,y,sizeof(y),&v2) || v2!=2 ||
       vex_mrac_get(a,1,bytes,sizeof(bytes),&size,&resolved) ||
       size!=sizeof(x) || resolved!=1 || memcmp(bytes,x,size) ||
       vex_mrac_get(b,0,bytes,sizeof(bytes),&size,&resolved) ||
       size!=sizeof(x) || memcmp(bytes,x,size) || vex_mrac_version_count(a)!=2)goto done;
    uint32_t info_version=0;size_t info_size=0;uint8_t current=0;
    if(vex_mrac_version_info(a,0,&info_version,&info_size,&current) ||
       info_version!=2 || info_size!=sizeof(y) || !current)goto done;
    vex_mrac_put_req_t put={0};put.key_hash=a;put.size=8;
    put.data=(const void *)(uintptr_t)0x1234;
    if(vex_syscall1(VEX_SYS_MRAC_PUT,(uintptr_t)&put)!=(uint64_t)-1)goto done;
    put.data=(const void *)(uintptr_t)0xffffffff80000000ull;
    if(vex_syscall1(VEX_SYS_MRAC_PUT,(uintptr_t)&put)!=(uint64_t)-1 ||
       vex_syscall1(VEX_SYS_MRAC_GET,0xffffffff80000000ull)!=(uint64_t)-1)goto done;
    vex_mrac_get_req_t get={0};get.key_hash=a;get.buffer_size=sizeof(bytes);
    get.buffer=(void *)(uintptr_t)0xffffffff80000000ull;
    if(vex_syscall1(VEX_SYS_MRAC_GET,(uintptr_t)&get)!=(uint64_t)-1)goto done;
    get.buffer=bytes;get.buffer_size=1;
    if(vex_syscall1(VEX_SYS_MRAC_GET,(uintptr_t)&get)!=(uint64_t)-1 ||
       get.size_out!=sizeof(y) || get.resolved_version!=2)goto done;
    vex_mrac_version_info_req_t info={0};info.key_hash=a;
    info.info=(vex_mrac_version_info_t *)(uintptr_t)0xffffffff80000000ull;
    if(vex_syscall1(VEX_SYS_MRAC_VERSION_INFO,(uintptr_t)&info)!=(uint64_t)-1)goto done;
    map.length=4096;map.prot=PROT_READ|PROT_WRITE;map.flags=MAP_PRIVATE|MAP_ANONYMOUS;
    address=vex_syscall1(VEX_SYS_MMAP,(uintptr_t)&map);
    if(!address || address==(uint64_t)-1){address=0;goto done;}
    *(volatile uint8_t *)(uintptr_t)address=0x31;
    memory.addr=(void *)(uintptr_t)address;memory.length=4096;memory.prot_or_advice=PROT_READ;
    if(vex_syscall1(VEX_SYS_MPROTECT,(uintptr_t)&memory))goto done;
    get.buffer=(void *)(uintptr_t)address;get.buffer_size=4096;
    if(vex_syscall1(VEX_SYS_MRAC_GET,(uintptr_t)&get)!=(uint64_t)-1 ||
       *(volatile uint8_t *)(uintptr_t)address!=0x31)goto done;
    if(vex_mrac_remove(a,0,VEX_MRAC_REMOVE_CURRENT) || vex_mrac_current_version(a)!=1 ||
       vex_mrac_remove(a,0,VEX_MRAC_REMOVE_ALL) || vex_mrac_version_count(a)!=0 ||
       vex_mrac_get(b,0,bytes,sizeof(bytes),&size,NULL) || memcmp(bytes,x,sizeof(x)))goto done;
    result=0;
done:
    if(address) {
        memory.addr=(void *)(uintptr_t)address;memory.length=4096;memory.prot_or_advice=0;
        if(vex_syscall1(VEX_SYS_MUNMAP,(uintptr_t)&memory))result=1;
    }
    (void)vex_mrac_remove(a,0,VEX_MRAC_REMOVE_ALL);
    (void)vex_mrac_remove(b,0,VEX_MRAC_REMOVE_ALL);
    vex_syscall1(VEX_SYS_DEBUG_LOG,(uintptr_t)(result ?
        "ORA native ABI: FAILED\n" :
        "ORA native ABI: versions/rebind/keys and invalid/kernel/readonly buffers PASS\n"));
    return result;
}
