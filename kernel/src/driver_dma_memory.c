#include <unos/driver_dma.h>
#include <unos/boot_requests.h>
#include <unos/pmm.h>
#include <unos/vmm.h>
#include <unos/heap.h>
#include <string.h>

static int allocate_pages(void *context,size_t bytes,void **cpu,uint64_t *bus,void **cookie) {
    (void)context;
    if(!bytes || bytes%4096 || bytes>DRV_DMA_MAX_BYTES || !hhdm_request.response)return DRV_BOUNDS;
    uint64_t physical=(uint64_t)pmm_alloc_contiguous_pages(bytes/4096);
    if(!physical)return DRV_NOMEM;
    uint64_t offset=hhdm_request.response->offset;
    if(physical>UINT64_MAX-bytes || physical>UINT64_MAX-offset || physical+offset>UINT64_MAX-(bytes-1))goto invalid;
    uint64_t cr3;__asm__ volatile("mov %%cr3,%0":"=r"(cr3));
    void *root=vmm_cr3_to_address_space(cr3);
    /* Reuse the kernel HHDM, never manufacture a user mapping or borrow an
     * unrelated ORA content page. Check each PMM page before touching it. */
    for(size_t i=0;i<bytes;i+=4096) {
        uint64_t va=offset+physical+i;
        uint64_t flags=vmm_get_page_flags(root,va);
        if(vmm_get_physical(root,va)!=physical+i || !(flags&VMM_PAGE_PRESENT) ||
           !(flags&VMM_PAGE_WRITABLE) || (flags&VMM_PAGE_USER))goto invalid;
    }
    memset((void *)(offset+physical),0,bytes);
    *cpu=(void *)(offset+physical);*bus=physical;*cookie=(void *)physical;return 0;
invalid:pmm_free_contiguous_pages((void *)physical,bytes/4096);return DRV_DENIED;
}
static void release_pages(void *context,void *cookie,size_t bytes) {
    (void)context;pmm_free_contiguous_pages(cookie,bytes/4096);
}
drv_dma_provider_t driver_dma_memory_provider(void) {
    return (drv_dma_provider_t){.allocate=allocate_pages,.release=release_pages,
        .metadata_alloc=kmalloc,.metadata_free=kfree};
}
int driver_dma_self_test(void) {
    drv_dma_policy_t policy={.grant_count=2,.max_bytes=16384,.address_limit=UINT64_MAX};
    policy.grants[0]=(drv_dma_grant_t){.kind=DRV_DMA_BUFFER,.rights=3,.widths=15,.max_bytes=4096};
    policy.grants[1]=(drv_dma_grant_t){.kind=DRV_DMA_RING,.rights=3,.widths=15,.max_bytes=4096,.stride=16,.field_count=3};
    policy.grants[1].fields[0]=(drv_dma_field_t){.offset=0,.width=8,.kind=DRV_DMA_ADDRESS,.length_field=1};
    policy.grants[1].fields[1]=(drv_dma_field_t){.offset=8,.width=2,.kind=DRV_DMA_LENGTH,.length_field=DRV_ABSENT};
    policy.grants[1].fields[2]=(drv_dma_field_t){.offset=10,.width=1,.kind=DRV_DMA_SCALAR,.length_field=DRV_ABSENT,.write_mask=3};
    drv_dma_provider_t provider=driver_dma_memory_provider();
    drv_dma_context_t c={0};
    int e=drv_dma_init(&c,&policy,&provider);if(e)return e;
    uint64_t a[8]={0,4096},buffer=0,ring=0,out=0;
    e=drv_dma_operation(&c,DRV_DMA_ALLOC,a,&buffer,NULL,0);
    if(!e){a[0]=1;a[1]=32;e=drv_dma_operation(&c,DRV_DMA_ALLOC,a,&ring,NULL,0);}
    if(!e){uint64_t args[8]={ring,buffer,0,0,32,128};e=drv_dma_operation(&c,DRV_DMA_BIND,args,&out,NULL,0);}
    if(!e){uint64_t args[8]={ring,0,8};if(drv_dma_operation(&c,DRV_DMA_READ,args,&out,NULL,0)!=DRV_DENIED)e=DRV_IO;}
    if(!e){uint64_t args[8]={ring,0,8,UINT64_MAX};if(drv_dma_operation(&c,DRV_DMA_WRITE,args,&out,NULL,0)!=DRV_DENIED)e=DRV_IO;}
    if(!e){uint64_t args[8]={buffer};e=drv_dma_operation(&c,DRV_DMA_RELEASE,args,&out,NULL,0);if(!e && c.bytes!=8192)e=DRV_IO;}
    if(!e){uint64_t args[8]={buffer};if(drv_dma_operation(&c,DRV_DMA_READ,args,&out,NULL,0)!=DRV_NOT_BOUND)e=DRV_IO;}
    if(!e){uint64_t args[8]={ring};e=drv_dma_operation(&c,DRV_DMA_RELEASE,args,&out,NULL,0);if(!e && c.bytes)e=DRV_IO;}
    int closed=drv_dma_destroy(&c);if(!e)e=closed;
    return e;
}
