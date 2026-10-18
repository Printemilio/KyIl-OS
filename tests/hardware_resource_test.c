#include <unos/hardware_resources.h>
#include <unos/driver_vm.h>
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
static unsigned active,acquired;
static void *thread(void *p) {
    uintptr_t owner=(uintptr_t)p;
    for(unsigned i=0;i<5000;i++) {
        int e=hardware_claim(HW_PIO,0x600,8,owner);
        if(!e){assert(__atomic_fetch_add(&active,1,__ATOMIC_SEQ_CST)==0);
            __atomic_fetch_add(&acquired,1,__ATOMIC_SEQ_CST);
            for(unsigned j=0;j<8;j++)__asm__ volatile("pause");
            assert(__atomic_fetch_sub(&active,1,__ATOMIC_SEQ_CST)==1);
            hardware_release_owner(owner);
        } else assert(e==DRV_CONFLICT);
    }
    return NULL;
}
int main(void) {
    assert(hardware_claim(HW_PIO,0x100,8,1)==0);
    assert(hardware_claim(HW_PIO,0x100,8,1)==0);
    assert(hardware_claim(HW_PIO,0x107,8,2)==DRV_CONFLICT);
    assert(hardware_claim(HW_PIO,0x108,8,2)==0);
    assert(hardware_claim(HW_MMIO,0x100,8,3)==0);
    assert(hardware_claim(HW_PIO,65535,2,4)==DRV_BOUNDS);
    assert(hardware_claim(HW_MMIO,UINT64_MAX,1,4)==DRV_BOUNDS);
    assert(hardware_claim(HW_IRQ,3,1,0)==DRV_BOUNDS);
    assert(!hardware_claim_shared_irq(9,200));
    assert(!hardware_claim_shared_irq(9,201));
    assert(hardware_claim(HW_IRQ,9,1,202)==DRV_CONFLICT);
    hardware_release_owner(200);
    assert(hardware_claim(HW_IRQ,9,1,202)==DRV_CONFLICT);
    hardware_release_owner(201);
    assert(!hardware_claim(HW_IRQ,9,1,202));
    assert(hardware_claim_shared_irq(9,201)==DRV_CONFLICT);
    hardware_release_owner(202);
    assert(!hardware_claim(HW_IRQ,8,4,202));
    assert(hardware_claim_shared_irq(9,201)==DRV_CONFLICT);
    hardware_release_owner(202);
    assert(hardware_claim_shared_irq(8,201)==DRV_BOUNDS);
    hardware_release_owner(1);assert(!hardware_claim(HW_PIO,0x100,8,5));
    hardware_release_owner(2);hardware_release_owner(3);hardware_release_owner(5);
    assert(hardware_native_pci(0,2,0)==0);
    assert(hardware_claim(HW_PCI,16,1,5)==DRV_CONFLICT);
    pthread_t t[8];for(uintptr_t i=0;i<8;i++)assert(!pthread_create(&t[i],NULL,thread,(void *)(i+100)));
    for(unsigned i=0;i<8;i++)assert(!pthread_join(t[i],NULL));
    assert(!active&&acquired);printf("PASS: actual broker exclusivity, ranges, native ownership, 8 threads / 40000 attempts (%u acquired)\n",acquired);
}
