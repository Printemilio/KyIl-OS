#include <stdint.h>
#include <unos/vex_abi.h>
#include "vex_syscall.h"
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
__declspec(dllimport) void *CreateFileMappingW(void *,void *,unsigned,unsigned,unsigned,const uint16_t *),*MapViewOfFile(void *,unsigned,unsigned,unsigned,uint64_t);
__declspec(dllimport) int UnmapViewOfFile(void *),CloseHandle(void *);
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
static vex_memory_usage_t usage(void){vex_memory_usage_t out;vex_memory_view_req_t r={.size=sizeof(r),.operation=VEX_MEMORY_VIEW_USAGE,.out=&out};
    check(!vex_syscall1(VEX_SYS_MEMORY_VIEW,(uintptr_t)&r),"memory-usage: native facts query FAIL\n");return out;}
void mainCRTStartup(void){
    usage();vex_memory_usage_t before=usage();
    check(before.physical_bytes && before.available_bytes<=before.physical_bytes && before.peak_used_bytes>=before.physical_bytes-before.available_bytes &&
        before.resident_bytes && before.peak_resident_bytes>=before.resident_bytes && before.private_bytes<=before.resident_bytes &&
        before.peak_private_bytes>=before.private_bytes && before.allocator_bytes>=before.owned_allocator_bytes &&
        before.peak_owned_allocator_bytes>=before.owned_allocator_bytes && before.processes && before.threads>=before.processes && before.resource_handles,"memory-usage: genuine counter invariants FAIL\n");
    vex_mmap_req_t map={0,128ull<<30,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0};
    uint64_t address=vex_syscall1(VEX_SYS_MMAP,(uintptr_t)&map);check(address && address!=UINT64_MAX,"memory-usage: huge VA reservation FAIL\n");
    vex_memory_usage_t reserved=usage();check(reserved.resident_bytes-before.resident_bytes<65536 && reserved.private_bytes-before.private_bytes<65536,"memory-usage: sparse reservation must not commit RAM FAIL\n");
    check(reserved.private_charge_bytes==before.private_charge_bytes+(128ull<<30) && reserved.committed_bytes>=reserved.private_charge_bytes && reserved.peak_committed_bytes>=reserved.committed_bytes,"memory-usage: honest logical overcommit vs physical backing FAIL\n");
    for(unsigned i=0;i<32;i++)*(volatile uint8_t *)(uintptr_t)(address+4096ull*i)=(uint8_t)i;
    vex_memory_usage_t touched=usage();check(touched.resident_bytes>=reserved.resident_bytes+32*4096 && touched.private_bytes>=reserved.private_bytes+32*4096 && touched.materializations>=reserved.materializations+32,"memory-usage: actual backing materialization FAIL\n");
    vex_memory_req_t protect={(void *)(uintptr_t)address,32*4096,0};check(!vex_syscall1(VEX_SYS_MPROTECT,(uintptr_t)&protect),"memory-usage: real protection FAIL\n");
    vex_memory_usage_t none=usage();check(none.private_bytes==touched.private_bytes && none.resident_bytes==touched.resident_bytes,"memory-usage: PROT_NONE retains backing charge FAIL\n");
    vex_memory_req_t unmap={(void *)(uintptr_t)address,128ull<<30,0};check(!vex_syscall1(VEX_SYS_MUNMAP,(uintptr_t)&unmap),"memory-usage: actual unmap FAIL\n");
    vex_memory_usage_t freed=usage();check(freed.private_bytes+32*4096<=none.private_bytes && freed.peak_private_bytes>=touched.private_bytes && freed.peak_resident_bytes>=touched.resident_bytes,"memory-usage: unmap / historical peaks FAIL\n");
    check(freed.private_charge_bytes==before.private_charge_bytes && freed.peak_private_charge_bytes>=reserved.private_charge_bytes && freed.peak_committed_bytes>=reserved.committed_bytes,"memory-usage: charge released / retained logical peak FAIL\n");
    void *section=CreateFileMappingW((void *)-1,0,4,0,4096,0);check(section!=0,"memory-usage: real shared backing FAIL\n");
    volatile uint8_t *a=MapViewOfFile(section,6,0,0,4096),*b=MapViewOfFile(section,4,0,0,4096);check(a && b,"memory-usage: real shared aliases FAIL\n");
    vex_memory_usage_t shared_before=usage();a[0]=71;check(b[0]==71,"memory-usage: actual shared bytes FAIL\n");
    vex_memory_usage_t shared=usage();check(shared.private_bytes==shared_before.private_bytes && shared.resident_bytes>=shared_before.resident_bytes,"memory-usage: shared aliases not private backing FAIL\n");
    check(UnmapViewOfFile((void *)a) && UnmapViewOfFile((void *)b) && CloseHandle(section),"memory-usage: shared lifetime FAIL\n");
    vex_memory_view_req_t bad={.size=sizeof(bad),.operation=VEX_MEMORY_VIEW_USAGE,.out=(void *)0xffff800000001000ull};
    check((int64_t)vex_syscall1(VEX_SYS_MEMORY_VIEW,(uintptr_t)&bad)==-14,"memory-usage: kernel output boundary FAIL\n");
    OutputDebugStringA("windows-memory-usage: actual native RAM / sparse 128GiB / physical private vs shared / protection / unmap / historical peaks / output boundary PASS\n");ExitProcess(0);
}
