#ifndef KYIL_DRIVER_DMA_H
#define KYIL_DRIVER_DMA_H
#include <unos/driver_vm.h>
#include <unos/ora.h>

/* Engine-private contracts. Archives never receive these pointers or bus
 * addresses. The owner serializes ALL calls with its per-device gate; no call
 * sleeps. Policies must come from hardware authority, not the archive. */
#define DRV_DMA_OBJECTS 64u
#define DRV_DMA_FIELDS 8u
#define DRV_DMA_REGS 64u
#define DRV_DMA_MAX_BYTES (16u*1024u*1024u)
#define DRV_DMA_MAX_DESCRIPTORS 4096u
#define DRV_DMA_MAX_METADATA (4u*1024u*1024u)
enum {DRV_DMA_BUFFER=1,DRV_DMA_RING=2};
enum {DRV_DMA_ADDRESS=1,DRV_DMA_LENGTH=2,DRV_DMA_SCALAR=3,DRV_DMA_STATUS=4};
enum {DRV_REG_SCALAR=1,DRV_REG_ADDRESS_LOW,DRV_REG_ADDRESS_HIGH,
      DRV_REG_LENGTH,DRV_REG_DOORBELL};
enum {DRV_DMA_ALLOC=1,DRV_DMA_RELEASE,DRV_DMA_READ,DRV_DMA_WRITE,
      DRV_DMA_BIND,DRV_DMA_PUBLISH,DRV_DMA_RECLAIM,DRV_DMA_COPY_IN,DRV_DMA_COPY_OUT,
      DRV_DMA_SLOT_ACQUIRE,DRV_DMA_SLOT_POST,DRV_DMA_SLOT_STATUS};
typedef struct drv_dma_field {
    uint32_t offset,width,kind,length_field;
    uint64_t write_mask;
} drv_dma_field_t;
typedef struct drv_dma_grant {
    uint32_t kind,rights,widths,max_bytes,stride,field_count;
    drv_dma_field_t fields[DRV_DMA_FIELDS];
} drv_dma_grant_t;
typedef struct drv_dma_register {
    uint32_t offset,width,kind,rights;
    uint64_t write_mask;
    uint32_t count,stride; /* zero count = one scalar register */
    uint32_t guard_mask,require_endpoint,invalidate_mask;
    uint32_t block_endpoint; /* index+1; zero = no published-ring exclusion */
} drv_dma_register_t;
typedef struct drv_dma_endpoint {
    uint32_t grant,low,high,length,doorbell;
    uint64_t doorbell_value;
    uint32_t streaming,initial_owned,post_next,length_output;
    uint32_t completion_field,completion_mask,completion_value,min_span;
    uint32_t min_slots,max_slots;
} drv_dma_endpoint_t;
typedef struct drv_dma_policy {
    uint32_t grant_count,register_count,endpoint_count;
    uint64_t max_bytes,address_limit;
    drv_dma_grant_t grants[DRV_RES_MAX];
    drv_dma_register_t registers[DRV_DMA_REGS];
    drv_dma_endpoint_t endpoints[DRV_RES_MAX];
} drv_dma_policy_t;
typedef struct drv_dma_provider {
    void *context;
    /* Allocator owns CPU/DMA translation and zeroes the FULL allocation. */
    int (*allocate)(void *,size_t,void **cpu,uint64_t *bus,void **cookie);
    void (*release)(void *,void *cookie,size_t);
    void *(*metadata_alloc)(size_t);
    void (*metadata_free)(void *);
    int (*reg_read)(void *,uint32_t,unsigned,uint64_t *);
    int (*reg_write)(void *,uint32_t,unsigned,uint64_t);
    /* Hardware-owner proof, NOT a flag supplied by package bytecode. Includes
     * device quiescence and posted write/completion visibility. */
    int (*quiesced)(void *,unsigned endpoint);
} drv_dma_provider_t;
typedef struct drv_dma_slot {uint64_t token;ora_ref_t object;} drv_dma_slot_t;
typedef struct drv_dma_context {
    drv_dma_policy_t policy;
    drv_dma_provider_t provider;
    drv_dma_slot_t slots[DRV_DMA_OBJECTS];
    void *storages; /* Live ORA storage registry, including closed local tokens. */
    uint64_t bytes,metadata_bytes;
    uint32_t epoch,next;
    drv_requirement_t requests[DRV_RES_MAX];
    unsigned request_count,allowed_grants,attached;
    uint64_t hardware_generation;
} drv_dma_context_t;
int drv_dma_init(drv_dma_context_t *,const drv_dma_policy_t *,const drv_dma_provider_t *);
/* BUSY leaves everything intact: never free DMA memory on an unproven stop. */
int drv_dma_destroy(drv_dma_context_t *);
int drv_dma_quiesce_all(drv_dma_context_t *);
int drv_dma_operation(drv_dma_context_t *,unsigned,const uint64_t[8],uint64_t *,uint8_t *,size_t);
int drv_dma_register_read(drv_dma_context_t *,unsigned,unsigned,uint64_t,uint64_t *);
int drv_dma_register_write(drv_dma_context_t *,unsigned,unsigned,uint64_t,uint64_t);
/* Intersects the immutable manifest with host authority BEFORE exposing any
 * callback. One archive/context; no allocations may predate this binding. */
int drv_dma_attach_io(drv_dma_context_t *,const drv_image_t *,uint64_t (*clock)(void *),drv_io_t *);
/* PMM contiguous provider wrapped in ORA_STORAGE; HHDM is kernel-only. */
drv_dma_provider_t driver_dma_memory_provider(void);
int driver_dma_self_test(void);
#endif
