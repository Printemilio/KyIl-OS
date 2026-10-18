#ifndef KYIL_DRIVER_VM_H
#define KYIL_DRIVER_VM_H
#include <stddef.h>
#include <stdint.h>

/* Archive/VM v1 wire records are little endian; never an ELF/native call ABI. */
#define DRV_ARCHIVE_MAX 65536u
#define DRV_PAYLOAD_MAX 65536u
#define DRV_RES_MAX 4u
#define DRV_REGS 32u
#define DRV_CODE_MAX 2048u
#define DRV_SCRATCH_MAX 4096u
#define DRV_ENTRY_COUNT 8u
#define DRV_ABSENT UINT32_MAX
enum {DRV_OK=0,DRV_FORMAT=-1,DRV_VERSION=-2,DRV_INTEGRITY=-3,DRV_DENIED=-4,
      DRV_CONFLICT=-5,DRV_BOUNDS=-6,DRV_TIMEOUT=-7,DRV_NOMEM=-8,
      DRV_UNSUPPORTED=-9,DRV_NOT_BOUND=-10,DRV_BUSY=-11,DRV_IO=-12,DRV_AGAIN=-13};
enum {DRV_BUS_PLATFORM=1,DRV_BUS_PCI=2};
enum {DRV_CLASS_CLOCK=1,DRV_CLASS_CHARACTER=2,DRV_CLASS_NETWORK=3};
enum {DRV_PIO=1,DRV_MMIO=2,DRV_INDEXED_PIO=3,DRV_DMA=4};
enum {DRV_READ=1,DRV_WRITE=2};
enum {DRV_F_BASE=1,DRV_F_PIO=2,DRV_F_MMIO=4,DRV_F_INDEXED=8,DRV_F_BUFFER=16,DRV_F_IRQ=32,DRV_F_DMA=64};
/* v1.0 keeps its exact feature set. v1.1 DMA is admitted only by a host
 * resource policy; parsing an image does not grant access to a PCI device. */
#define DRV_SUPPORTED (DRV_F_BASE|DRV_F_PIO|DRV_F_INDEXED|DRV_F_BUFFER|DRV_F_IRQ)
#define DRV_SUPPORTED_V2 (DRV_SUPPORTED|DRV_F_DMA|DRV_F_MMIO)
enum {DRV_PROBE=0,DRV_INIT=1,DRV_STOP=2,DRV_READ_ENTRY=3,DRV_WRITE_ENTRY=4,DRV_IRQ_ENTRY=5,DRV_CONTROL=6,DRV_DIAGNOSTIC=7};
enum {DRV_RET=0,DRV_CONST,DRV_MOV,DRV_ADD,DRV_SUB,DRV_MUL,DRV_DIV,DRV_MOD,
      DRV_AND,DRV_OR,DRV_XOR,DRV_SHL,DRV_SHR,DRV_JMP,DRV_JZ,DRV_JNZ,
      DRV_EQ,DRV_LT,DRV_IO_READ,DRV_IO_WRITE,DRV_LOAD,DRV_STORE,DRV_EMIT,DRV_DMA_OP};
typedef struct __attribute__((packed)) drv_archive_header {
    uint8_t magic[8]; uint16_t major,minor; uint32_t header_size;
    uint32_t codec,compressed_size,payload_size,flags;
    uint8_t payload_sha256[32],reserved[16];
} drv_archive_header_t;
typedef struct __attribute__((packed)) drv_member {
    char name[16]; uint32_t offset,size,type,flags;
} drv_member_t;
typedef struct __attribute__((packed)) drv_requirement {
    uint32_t kind,selector; uint64_t offset; uint32_t length,rights,widths,reserved;
} drv_requirement_t;
typedef struct __attribute__((packed)) drv_manifest {
    char name[32],platform[32];
    uint32_t bus,device_class,vendor,device,pci_class,features,engine_min,priority;
    uint32_t entries[DRV_ENTRY_COUNT];
    uint32_t code_count,resource_count,scratch_size,irq;
    drv_requirement_t resources[DRV_RES_MAX];
} drv_manifest_t;
typedef struct __attribute__((packed)) drv_instruction {
    uint8_t op,dst,a,b; uint32_t aux; uint64_t immediate;
} drv_instruction_t;
_Static_assert(sizeof(drv_archive_header_t)==80,"driver archive ABI");
_Static_assert(sizeof(drv_manifest_t)==272,"driver manifest ABI");
_Static_assert(sizeof(drv_instruction_t)==16,"driver instruction ABI");
typedef struct drv_image {
    drv_manifest_t manifest;
    const drv_instruction_t *code;
    void *payload;
} drv_image_t;
typedef struct drv_io {
    void *context;
    /* Offsets are relative to validated/leased resources. Never CPU pointers. */
    int (*read)(void *,unsigned,unsigned,uint64_t,uint64_t *);
    int (*write)(void *,unsigned,unsigned,uint64_t,uint64_t);
    int (*emit)(void *,uint64_t);
    uint64_t (*clock_ns)(void *);
    /* Eight register values, never pointers. immediate selects first input
     * register. Backend resolves local references to private ORA objects. */
    int (*dma)(void *,unsigned,const uint64_t[8],uint64_t *,uint8_t *,size_t);
} drv_io_t;
typedef struct drv_allocator {
    void *(*allocate)(size_t); void (*release)(void *);
    int (*sha256)(const void *,size_t,uint8_t[32]);
} drv_allocator_t;
int drv_image_load(const void *,size_t,const drv_allocator_t *,drv_image_t *);
void drv_image_release(drv_image_t *,const drv_allocator_t *);
int drv_execute(const drv_image_t *,unsigned,const uint64_t[8],uint64_t[8],
                uint8_t *,const drv_io_t *,unsigned,uint64_t);
const char *drv_error_name(int);
#endif
