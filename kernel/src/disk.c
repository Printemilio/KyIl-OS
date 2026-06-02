#include <unos/disk.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/boot_requests.h>
#include <unos/heap.h>
#include <unos/pmm.h>
#include <unos/vmm.h>

#define PCI_CONFIG_ADDRESS 0xcf8u
#define PCI_CONFIG_DATA 0xcfcu
#define PCI_VENDOR_INVALID 0xffffu
#define PCI_CLASS_STORAGE 0x01u
#define PCI_SUBCLASS_AHCI 0x06u
#define PCI_SUBCLASS_NVME 0x08u

#define PAGE_SIZE 4096ull
#define PAGE_MASK 0x000ffffffffff000ull
#define DISK_MMIO_BASE 0xffffffffa0000000ull
#define DISK_SECTOR_SIZE 512u
#define DISK_BOUNCE_SECTORS 8u
#define DISK_BOUNCE_SIZE (DISK_SECTOR_SIZE * DISK_BOUNCE_SECTORS)

#define AHCI_PORT_COUNT 32u
#define AHCI_CMD_READ_DMA_EXT 0x25u
#define AHCI_CMD_WRITE_DMA_EXT 0x35u
#define AHCI_CMD_IDENTIFY_DEVICE 0xecu
#define AHCI_FIS_TYPE_REG_H2D 0x27u
#define AHCI_DEVICE_LBA 0x40u
#define AHCI_PORT_CMD_ST 0x0001u
#define AHCI_PORT_CMD_FRE 0x0010u
#define AHCI_PORT_CMD_FR 0x4000u
#define AHCI_PORT_CMD_CR 0x8000u
#define AHCI_SATA_SIG_ATA 0x00000101u

#define NVME_ADMIN_CREATE_IO_SQ 0x01u
#define NVME_ADMIN_CREATE_IO_CQ 0x05u
#define NVME_ADMIN_IDENTIFY 0x06u
#define NVME_CMD_WRITE 0x01u
#define NVME_CMD_READ 0x02u
#define NVME_QUEUE_DEPTH 16u

typedef enum {
    DISK_NONE,
    DISK_AHCI,
    DISK_NVME
} disk_backend_t;

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
} pci_device_t;

typedef volatile struct {
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;
    uint32_t ccc_ctl;
    uint32_t ccc_pts;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;
    uint8_t reserved[0xa0 - 0x2c];
} ahci_hba_t;

typedef volatile struct {
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t reserved0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint8_t reserved1[0x80 - 0x48];
    uint8_t vendor[0x80];
} ahci_port_t;

typedef struct {
    uint8_t cfl;
    uint8_t a;
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t reserved[4];
} ahci_cmd_header_t;

typedef struct {
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved;
    uint32_t dbc;
} ahci_prdt_entry_t;

typedef struct {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved[48];
    ahci_prdt_entry_t prdt[1];
} ahci_cmd_table_t;

typedef struct {
    ahci_hba_t *hba;
    ahci_port_t *port;
    ahci_cmd_header_t *cmd_list;
    ahci_cmd_table_t *cmd_table;
    void *bounce_phys;
    uint8_t *bounce_virt;
    uint64_t sectors;
} ahci_state_t;

typedef volatile struct {
    uint64_t cap;
    uint32_t vs;
    uint32_t intms;
    uint32_t intmc;
    uint32_t cc;
    uint32_t reserved0;
    uint32_t csts;
    uint32_t nssr;
    uint32_t aqa;
    uint64_t asq;
    uint64_t acq;
} nvme_regs_t;

typedef struct {
    uint8_t opcode;
    uint8_t flags;
    uint16_t cid;
    uint32_t nsid;
    uint64_t reserved0;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} nvme_command_t;

typedef struct {
    uint32_t result;
    uint32_t reserved;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status;
} nvme_completion_t;

typedef struct {
    nvme_regs_t *regs;
    uint32_t doorbell_stride;
    nvme_command_t *admin_sq;
    nvme_completion_t *admin_cq;
    nvme_command_t *io_sq;
    nvme_completion_t *io_cq;
    void *admin_sq_phys;
    void *admin_cq_phys;
    void *io_sq_phys;
    void *io_cq_phys;
    void *bounce_phys;
    uint8_t *bounce_virt;
    uint16_t admin_tail;
    uint16_t admin_head;
    uint16_t io_tail;
    uint16_t io_head;
    uint16_t cid;
    uint8_t admin_phase;
    uint8_t io_phase;
    uint64_t sectors;
} nvme_state_t;

static disk_backend_t disk_backend;
static int disk_initialized;
static ahci_state_t ahci;
static nvme_state_t nvme;
static uint64_t next_mmio_virt = DISK_MMIO_BASE;

/* ATAPI / CD-ROM state — declared here so disk_init() can reference them
 * even though the full ATAPI implementation follows later in the file. */
static int atapi_ahci_ready = 0;
static int atapi_pio_ready  = 0;
static void ahci_probe_atapi(ahci_hba_t *hba);
static void ata_pio_probe_atapi(void);

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" :: "a"(value), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile ("outb %0, %1" :: "a"(v), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t v;
    __asm__ volatile ("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outw(uint16_t port, uint16_t v) {
    __asm__ volatile ("outw %0, %1" :: "a"(v), "Nd"(port));
}

static uint64_t align_down(uint64_t value, uint64_t alignment) {
    return value & ~(alignment - 1ull);
}

static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1ull) & ~(alignment - 1ull);
}

static uint64_t read_cr3(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & PAGE_MASK;
}

static void *current_pml4(void) {
    return (void *)(hhdm_request.response->offset + read_cr3());
}

static void *phys_to_virt(uint64_t phys) {
    return (void *)(hhdm_request.response->offset + phys);
}

static void zero_page(void *page) {
    uint64_t *words = page;
    for (uint64_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); ++i) {
        words[i] = 0;
    }
}

static void copy_bytes(void *dst, const void *src, uint64_t size) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (uint64_t i = 0; i < size; ++i) {
        d[i] = s[i];
    }
}

static void *map_mmio(uint64_t phys, uint64_t length) {
    const uint64_t phys_base = align_down(phys, PAGE_SIZE);
    const uint64_t phys_offset = phys - phys_base;
    const uint64_t mapped_length = align_up(length + phys_offset, PAGE_SIZE);
    const uint64_t virt_base = next_mmio_virt;

    for (uint64_t offset = 0; offset < mapped_length; offset += PAGE_SIZE) {
        vmm_map_page(current_pml4(), virt_base + offset, phys_base + offset, VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE);
    }

    next_mmio_virt += mapped_length;
    return (void *)(virt_base + phys_offset);
}

static uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    const uint32_t address = 0x80000000u
        | ((uint32_t)bus << 16u)
        | ((uint32_t)device << 11u)
        | ((uint32_t)function << 8u)
        | (offset & 0xfcu);

    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
    const uint32_t address = 0x80000000u
        | ((uint32_t)bus << 16u)
        | ((uint32_t)device << 11u)
        | ((uint32_t)function << 8u)
        | (offset & 0xfcu);

    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

static uint64_t pci_bar(pci_device_t device, uint8_t bar) {
    const uint8_t offset = (uint8_t)(0x10u + bar * 4u);
    const uint32_t low = pci_read32(device.bus, device.device, device.function, offset);

    if ((low & 0x1u) != 0) {
        return low & ~0x3ull;
    }

    if ((low & 0x6u) == 0x4u) {
        const uint32_t high = pci_read32(device.bus, device.device, device.function, offset + 4u);
        return (((uint64_t)high << 32u) | low) & ~0xfull;
    }

    return low & ~0xfull;
}

static void pci_enable_bus_mastering(pci_device_t device) {
    uint32_t command = pci_read32(device.bus, device.device, device.function, 0x04u);
    command |= 0x0006u;
    pci_write32(device.bus, device.device, device.function, 0x04u, command);
}

static int pci_find_storage(uint8_t subclass, pci_device_t *out) {
    for (uint16_t bus = 0; bus < 256u; ++bus) {
        for (uint8_t device = 0; device < 32u; ++device) {
            const uint16_t vendor = (uint16_t)pci_read32((uint8_t)bus, device, 0, 0x00u);
            if (vendor == PCI_VENDOR_INVALID) {
                continue;
            }

            const uint8_t header = (uint8_t)(pci_read32((uint8_t)bus, device, 0, 0x0cu) >> 16u);
            const uint8_t function_count = (header & 0x80u) != 0 ? 8u : 1u;

            for (uint8_t function = 0; function < function_count; ++function) {
                const uint16_t fn_vendor = (uint16_t)pci_read32((uint8_t)bus, device, function, 0x00u);
                if (fn_vendor == PCI_VENDOR_INVALID) {
                    continue;
                }

                const uint32_t class_reg = pci_read32((uint8_t)bus, device, function, 0x08u);
                const uint8_t class_code = (uint8_t)(class_reg >> 24u);
                const uint8_t sub = (uint8_t)(class_reg >> 16u);

                if (class_code == PCI_CLASS_STORAGE && sub == subclass) {
                    out->bus = (uint8_t)bus;
                    out->device = device;
                    out->function = function;
                    out->class_code = class_code;
                    out->subclass = sub;
                    out->prog_if = (uint8_t)(class_reg >> 8u);
                    return 0;
                }
            }
        }
    }

    return -1;
}

static int ahci_port_present(ahci_port_t *port) {
    const uint32_t ssts = port->ssts;
    const uint32_t det = ssts & 0x0fu;
    const uint32_t ipm = (ssts >> 8u) & 0x0fu;

    return det == 3u && ipm == 1u && port->sig == AHCI_SATA_SIG_ATA;
}

static int ahci_stop_port(ahci_port_t *port) {
    port->cmd &= ~(AHCI_PORT_CMD_ST | AHCI_PORT_CMD_FRE);

    for (uint32_t i = 0; i < 1000000u; ++i) {
        if ((port->cmd & (AHCI_PORT_CMD_FR | AHCI_PORT_CMD_CR)) == 0) {
            return 0;
        }
    }

    return -1;
}

static void ahci_start_port(ahci_port_t *port) {
    port->cmd |= AHCI_PORT_CMD_FRE;
    port->cmd |= AHCI_PORT_CMD_ST;
}

static int ahci_find_cmd_slot(ahci_port_t *port) {
    return ((port->sact | port->ci) & 1u) == 0 ? 0 : -1;
}

static int ahci_command(uint64_t lba, uint32_t count, void *phys_buffer, int write) {
    if (count == 0 || count > DISK_BOUNCE_SECTORS) {
        return -1;
    }

    const int slot = ahci_find_cmd_slot(ahci.port);
    if (slot < 0) {
        return -1;
    }

    ahci_cmd_header_t *header = &ahci.cmd_list[slot];
    ahci_cmd_table_t *table = &ahci.cmd_table[slot];
    uint8_t *fis = table->cfis;

    for (uint64_t i = 0; i < sizeof(ahci_cmd_table_t); ++i) {
        ((uint8_t *)table)[i] = 0;
    }

    header->cfl = 5u;
    header->a = write ? 0x40u : 0u;
    header->prdtl = 1u;

    table->prdt[0].dba = (uint32_t)(uint64_t)phys_buffer;
    table->prdt[0].dbau = (uint32_t)((uint64_t)phys_buffer >> 32u);
    table->prdt[0].dbc = (count * DISK_SECTOR_SIZE - 1u);

    fis[0] = AHCI_FIS_TYPE_REG_H2D;
    fis[1] = 1u << 7u;
    fis[2] = write ? AHCI_CMD_WRITE_DMA_EXT : AHCI_CMD_READ_DMA_EXT;
    fis[4] = (uint8_t)lba;
    fis[5] = (uint8_t)(lba >> 8u);
    fis[6] = (uint8_t)(lba >> 16u);
    fis[7] = AHCI_DEVICE_LBA;
    fis[8] = (uint8_t)(lba >> 24u);
    fis[9] = (uint8_t)(lba >> 32u);
    fis[10] = (uint8_t)(lba >> 40u);
    fis[12] = (uint8_t)count;
    fis[13] = (uint8_t)(count >> 8u);

    ahci.port->is = 0xffffffffu;
    ahci.port->ci = 1u << (uint32_t)slot;

    for (uint32_t i = 0; i < 10000000u; ++i) {
        if ((ahci.port->ci & (1u << (uint32_t)slot)) == 0) {
            return (ahci.port->is & 0x40000000u) != 0 ? -1 : 0;
        }
    }

    return -1;
}

static int ahci_identify(void) {
    const int slot = ahci_find_cmd_slot(ahci.port);
    if (slot < 0) {
        return -1;
    }

    ahci_cmd_header_t *header = &ahci.cmd_list[slot];
    ahci_cmd_table_t *table = &ahci.cmd_table[slot];
    uint8_t *fis = table->cfis;

    for (uint64_t i = 0; i < sizeof(ahci_cmd_table_t); ++i) {
        ((uint8_t *)table)[i] = 0;
    }

    header->cfl = 5u;
    header->a = 0u;
    header->prdtl = 1u;

    table->prdt[0].dba = (uint32_t)(uint64_t)ahci.bounce_phys;
    table->prdt[0].dbau = (uint32_t)((uint64_t)ahci.bounce_phys >> 32u);
    table->prdt[0].dbc = DISK_SECTOR_SIZE - 1u;

    fis[0] = AHCI_FIS_TYPE_REG_H2D;
    fis[1] = 1u << 7u;
    fis[2] = AHCI_CMD_IDENTIFY_DEVICE;

    ahci.port->is = 0xffffffffu;
    ahci.port->ci = 1u << (uint32_t)slot;

    for (uint32_t i = 0; i < 10000000u; ++i) {
        if ((ahci.port->ci & (1u << (uint32_t)slot)) == 0) {
            if ((ahci.port->is & 0x40000000u) != 0) {
                return -1;
            }

            const uint16_t *identify = (const uint16_t *)ahci.bounce_virt;
            ahci.sectors = ((uint64_t)identify[103] << 48u)
                | ((uint64_t)identify[102] << 32u)
                | ((uint64_t)identify[101] << 16u)
                | (uint64_t)identify[100];
            return 0;
        }
    }

    return -1;
}

static int ahci_init(pci_device_t device) {
    pci_enable_bus_mastering(device);

    ahci.hba = map_mmio(pci_bar(device, 5u), 0x1100u);
    if (ahci.hba == NULL) {
        return -1;
    }

    const uint32_t implemented = ahci.hba->pi;
    ahci_port_t *ports = (ahci_port_t *)((uint8_t *)ahci.hba + 0x100u);

    for (uint32_t i = 0; i < AHCI_PORT_COUNT; ++i) {
        if ((implemented & (1u << i)) == 0) {
            continue;
        }

        if (!ahci_port_present(&ports[i])) {
            continue;
        }

        ahci.port = &ports[i];
        if (ahci_stop_port(ahci.port) != 0) {
            return -1;
        }

        void *cmd_list_phys = pmm_alloc_page();
        void *fis_phys = pmm_alloc_page();
        void *table_phys = pmm_alloc_page();
        ahci.bounce_phys = pmm_alloc_page();
        if (cmd_list_phys == NULL || fis_phys == NULL || table_phys == NULL || ahci.bounce_phys == NULL) {
            return -1;
        }

        ahci.cmd_list = phys_to_virt((uint64_t)cmd_list_phys);
        ahci.cmd_table = phys_to_virt((uint64_t)table_phys);
        ahci.bounce_virt = phys_to_virt((uint64_t)ahci.bounce_phys);
        zero_page(ahci.cmd_list);
        zero_page(phys_to_virt((uint64_t)fis_phys));
        zero_page(ahci.cmd_table);
        zero_page(ahci.bounce_virt);

        ahci.port->clb = (uint32_t)(uint64_t)cmd_list_phys;
        ahci.port->clbu = (uint32_t)((uint64_t)cmd_list_phys >> 32u);
        ahci.port->fb = (uint32_t)(uint64_t)fis_phys;
        ahci.port->fbu = (uint32_t)((uint64_t)fis_phys >> 32u);

        ahci.cmd_list[0].ctba = (uint32_t)(uint64_t)table_phys;
        ahci.cmd_list[0].ctbau = (uint32_t)((uint64_t)table_phys >> 32u);

        ahci_start_port(ahci.port);
        return ahci_identify();
    }

    return -1;
}

static volatile uint32_t *nvme_doorbell(uint16_t qid, int cq) {
    return (volatile uint32_t *)((uint8_t *)nvme.regs + 0x1000u + (2u * qid + (cq ? 1u : 0u)) * nvme.doorbell_stride);
}

static int nvme_wait_ready(uint32_t ready) {
    for (uint32_t i = 0; i < 10000000u; ++i) {
        if ((nvme.regs->csts & 1u) == ready) {
            return 0;
        }
    }

    return -1;
}

static int nvme_submit_admin(nvme_command_t *cmd) {
    const uint16_t cid = nvme.cid++;
    cmd->cid = cid;

    nvme.admin_sq[nvme.admin_tail] = *cmd;
    nvme.admin_tail = (uint16_t)((nvme.admin_tail + 1u) % NVME_QUEUE_DEPTH);
    *nvme_doorbell(0, 0) = nvme.admin_tail;

    for (uint32_t timeout = 0; timeout < 10000000u; ++timeout) {
        nvme_completion_t *completion = &nvme.admin_cq[nvme.admin_head];
        if (((completion->status & 1u) == nvme.admin_phase) && completion->cid == cid) {
            const uint16_t status = completion->status >> 1u;
            nvme.admin_head = (uint16_t)((nvme.admin_head + 1u) % NVME_QUEUE_DEPTH);
            if (nvme.admin_head == 0) {
                nvme.admin_phase ^= 1u;
            }
            *nvme_doorbell(0, 1) = nvme.admin_head;
            return status == 0 ? 0 : -1;
        }
    }

    return -1;
}

static int nvme_submit_io(nvme_command_t *cmd) {
    const uint16_t cid = nvme.cid++;
    cmd->cid = cid;

    nvme.io_sq[nvme.io_tail] = *cmd;
    nvme.io_tail = (uint16_t)((nvme.io_tail + 1u) % NVME_QUEUE_DEPTH);
    *nvme_doorbell(1, 0) = nvme.io_tail;

    for (uint32_t timeout = 0; timeout < 10000000u; ++timeout) {
        nvme_completion_t *completion = &nvme.io_cq[nvme.io_head];
        if (((completion->status & 1u) == nvme.io_phase) && completion->cid == cid) {
            const uint16_t status = completion->status >> 1u;
            nvme.io_head = (uint16_t)((nvme.io_head + 1u) % NVME_QUEUE_DEPTH);
            if (nvme.io_head == 0) {
                nvme.io_phase ^= 1u;
            }
            *nvme_doorbell(1, 1) = nvme.io_head;
            return status == 0 ? 0 : -1;
        }
    }

    return -1;
}

static int nvme_identify_namespace(void) {
    nvme_command_t cmd = {0};
    cmd.opcode = NVME_ADMIN_IDENTIFY;
    cmd.nsid = 1u;
    cmd.prp1 = (uint64_t)nvme.bounce_phys;
    cmd.cdw10 = 0u;

    if (nvme_submit_admin(&cmd) != 0) {
        return -1;
    }

    nvme.sectors = *(uint64_t *)nvme.bounce_virt;
    return 0;
}

static int nvme_init(pci_device_t device) {
    pci_enable_bus_mastering(device);

    const uint64_t bar0 = pci_bar(device, 0u);
    nvme.regs = map_mmio(bar0, 0x4000u);
    if (nvme.regs == NULL) {
        return -1;
    }

    nvme.doorbell_stride = 4u << ((nvme.regs->cap >> 32u) & 0x0fu);
    nvme.regs->cc = 0;
    if (nvme_wait_ready(0) != 0) {
        return -1;
    }

    nvme.admin_sq_phys = pmm_alloc_page();
    nvme.admin_cq_phys = pmm_alloc_page();
    nvme.io_sq_phys = pmm_alloc_page();
    nvme.io_cq_phys = pmm_alloc_page();
    nvme.bounce_phys = pmm_alloc_page();
    if (nvme.admin_sq_phys == NULL || nvme.admin_cq_phys == NULL ||
        nvme.io_sq_phys == NULL || nvme.io_cq_phys == NULL || nvme.bounce_phys == NULL) {
        return -1;
    }

    nvme.admin_sq = phys_to_virt((uint64_t)nvme.admin_sq_phys);
    nvme.admin_cq = phys_to_virt((uint64_t)nvme.admin_cq_phys);
    nvme.io_sq = phys_to_virt((uint64_t)nvme.io_sq_phys);
    nvme.io_cq = phys_to_virt((uint64_t)nvme.io_cq_phys);
    nvme.bounce_virt = phys_to_virt((uint64_t)nvme.bounce_phys);
    zero_page(nvme.admin_sq);
    zero_page(nvme.admin_cq);
    zero_page(nvme.io_sq);
    zero_page(nvme.io_cq);
    zero_page(nvme.bounce_virt);

    nvme.admin_tail = 0;
    nvme.admin_head = 0;
    nvme.io_tail = 0;
    nvme.io_head = 0;
    nvme.admin_phase = 1;
    nvme.io_phase = 1;
    nvme.cid = 1;

    nvme.regs->aqa = ((NVME_QUEUE_DEPTH - 1u) << 16u) | (NVME_QUEUE_DEPTH - 1u);
    nvme.regs->asq = (uint64_t)nvme.admin_sq_phys;
    nvme.regs->acq = (uint64_t)nvme.admin_cq_phys;
    nvme.regs->cc = (6u << 20u) | (4u << 16u) | 1u;
    if (nvme_wait_ready(1) != 0) {
        return -1;
    }

    nvme_command_t create_cq = {0};
    create_cq.opcode = NVME_ADMIN_CREATE_IO_CQ;
    create_cq.prp1 = (uint64_t)nvme.io_cq_phys;
    create_cq.cdw10 = 1u | ((NVME_QUEUE_DEPTH - 1u) << 16u);
    create_cq.cdw11 = 1u;
    if (nvme_submit_admin(&create_cq) != 0) {
        return -1;
    }

    nvme_command_t create_sq = {0};
    create_sq.opcode = NVME_ADMIN_CREATE_IO_SQ;
    create_sq.prp1 = (uint64_t)nvme.io_sq_phys;
    create_sq.cdw10 = 1u | ((NVME_QUEUE_DEPTH - 1u) << 16u);
    create_sq.cdw11 = (1u << 16u) | 1u;
    if (nvme_submit_admin(&create_sq) != 0) {
        return -1;
    }

    return nvme_identify_namespace();
}

static int nvme_rw(uint64_t lba, uint32_t count, int write) {
    if (count == 0 || count > DISK_BOUNCE_SECTORS) {
        return -1;
    }

    nvme_command_t cmd = {0};
    cmd.opcode = write ? NVME_CMD_WRITE : NVME_CMD_READ;
    cmd.nsid = 1u;
    cmd.prp1 = (uint64_t)nvme.bounce_phys;
    cmd.cdw10 = (uint32_t)lba;
    cmd.cdw11 = (uint32_t)(lba >> 32u);
    cmd.cdw12 = count - 1u;

    return nvme_submit_io(&cmd);
}

static int disk_rw(uint64_t lba, uint32_t count, void *buffer, int write) {
    if (buffer == NULL || count == 0 || disk_backend == DISK_NONE) {
        return -1;
    }

    uint8_t *bytes = buffer;
    uint32_t remaining = count;
    uint64_t current_lba = lba;

    while (remaining > 0) {
        const uint32_t chunk = remaining > DISK_BOUNCE_SECTORS ? DISK_BOUNCE_SECTORS : remaining;
        const uint32_t byte_count = chunk * DISK_SECTOR_SIZE;
        int result;

        if (write) {
            if (disk_backend == DISK_AHCI) {
                copy_bytes(ahci.bounce_virt, bytes, byte_count);
            } else {
                copy_bytes(nvme.bounce_virt, bytes, byte_count);
            }
        }

        if (disk_backend == DISK_NVME) {
            result = nvme_rw(current_lba, chunk, write);
        } else {
            result = ahci_command(current_lba, chunk, disk_backend == DISK_AHCI ? ahci.bounce_phys : nvme.bounce_phys, write);
        }

        if (result != 0) {
            return -1;
        }

        if (!write) {
            if (disk_backend == DISK_AHCI) {
                copy_bytes(bytes, ahci.bounce_virt, byte_count);
            } else {
                copy_bytes(bytes, nvme.bounce_virt, byte_count);
            }
        }

        remaining -= chunk;
        current_lba += chunk;
        bytes += byte_count;
    }

    return 0;
}

void disk_init(void) {
    pci_device_t device;

    if (disk_initialized) {
        return;
    }

    disk_initialized = 1;
    disk_backend = DISK_NONE;

    if (pci_find_storage(PCI_SUBCLASS_NVME, &device) == 0 && nvme_init(device) == 0) {
        disk_backend = DISK_NVME;
    }

    if (pci_find_storage(PCI_SUBCLASS_AHCI, &device) == 0) {
        /* ahci_init maps the HBA and stores it in ahci.hba; it returns 0 only
           if it found a SATA ATA port.  We still want to probe ATAPI ports
           even when no SATA disk is present, so we call it regardless. */
        if (ahci_init(device) == 0) {
            disk_backend = DISK_AHCI;
        }
        /* ahci.hba is valid (mapped) after ahci_init, even on SATA-port failure.
           Probe all ports of the same HBA for ATAPI. */
        if (ahci.hba != NULL && !atapi_ahci_ready) {
            ahci_probe_atapi(ahci.hba);
        }
    }

    /* Fallback: ATA PIO ATAPI on classic IDE (VirtualBox default for CD-ROM) */
    if (!atapi_ahci_ready) {
        ata_pio_probe_atapi();
    }
}

const char *disk_type(void) {
    if (disk_backend == DISK_NVME) {
        return "NVMe";
    }

    if (disk_backend == DISK_AHCI) {
        return "AHCI";
    }

    return "aucun";
}

int disk_read(uint64_t lba, uint32_t count, void *buffer) {
    return disk_rw(lba, count, buffer, 0);
}

int disk_write(uint64_t lba, uint32_t count, void *buffer) {
    return disk_rw(lba, count, buffer, 1);
}

uint64_t disk_size(void) {
    if (disk_backend == DISK_NVME) {
        return nvme.sectors;
    }

    if (disk_backend == DISK_AHCI) {
        return ahci.sectors;
    }

    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * ATAPI / CD-ROM support
 * Two paths attempted (in order):
 *   1. AHCI ATAPI  – port with signature 0xEB140101 on the same HBA.
 *   2. ATA PIO     – classic IDE ports (0x1F0 / 0x170) for VirtualBox default.
 * ══════════════════════════════════════════════════════════════════════════════ */

#define AHCI_SATA_SIG_ATAPI     0xEB140101u
#define AHCI_CMD_PACKET         0xA0u
#define ATAPI_ISO_SECTOR_SIZE   2048u

/* ── AHCI ATAPI state ───────────────────────────────────────────────────── */

#define ATAPI_AHCI_BOUNCE_PAGES 8u   /* 8 × 4096 = 32 768 bytes = 16 ISO sectors */
#define ATAPI_AHCI_CHUNK        16u  /* sectors per DMA (32 KB) */

typedef struct {
    ahci_port_t       *port;
    ahci_cmd_header_t *cmd_list;
    ahci_cmd_table_t  *cmd_table;
    void              *bounce_phys;
    uint8_t           *bounce_virt;
} atapi_ahci_t;

static atapi_ahci_t atapi_ahci;

static int ahci_atapi_port_present(ahci_port_t *port) {
    const uint32_t ssts = port->ssts;
    return (ssts & 0x0fu) == 3u && ((ssts >> 8u) & 0x0fu) == 1u
           && port->sig == AHCI_SATA_SIG_ATAPI;
}

static int ahci_atapi_init_port(ahci_port_t *port) {
    if (ahci_stop_port(port) != 0) return -1;

    void *cl_phys  = pmm_alloc_page();
    void *fis_phys = pmm_alloc_page();
    void *ct_phys  = pmm_alloc_page();
    void *bn_phys  = pmm_alloc_contiguous_pages(ATAPI_AHCI_BOUNCE_PAGES);
    if (!cl_phys || !fis_phys || !ct_phys || !bn_phys) return -1;

    atapi_ahci.port       = port;
    atapi_ahci.cmd_list   = phys_to_virt((uint64_t)cl_phys);
    atapi_ahci.cmd_table  = phys_to_virt((uint64_t)ct_phys);
    atapi_ahci.bounce_phys = bn_phys;
    atapi_ahci.bounce_virt = phys_to_virt((uint64_t)bn_phys);

    zero_page(atapi_ahci.cmd_list);
    zero_page(phys_to_virt((uint64_t)fis_phys));
    zero_page(atapi_ahci.cmd_table);

    port->clb  = (uint32_t)(uint64_t)cl_phys;
    port->clbu = (uint32_t)((uint64_t)cl_phys  >> 32u);
    port->fb   = (uint32_t)(uint64_t)fis_phys;
    port->fbu  = (uint32_t)((uint64_t)fis_phys >> 32u);
    atapi_ahci.cmd_list[0].ctba  = (uint32_t)(uint64_t)ct_phys;
    atapi_ahci.cmd_list[0].ctbau = (uint32_t)((uint64_t)ct_phys >> 32u);

    ahci_start_port(port);
    return 0;
}

/* Read 'count' 2048-byte sectors via AHCI ATAPI DMA, count ≤ ATAPI_AHCI_CHUNK */
static int ahci_atapi_dma_read(uint32_t lba, uint32_t count, void *dst) {
    uint32_t byte_count = count * ATAPI_ISO_SECTOR_SIZE;

    ahci_cmd_header_t *hdr   = &atapi_ahci.cmd_list[0];
    ahci_cmd_table_t  *table = atapi_ahci.cmd_table;

    for (uint64_t i = 0; i < sizeof(ahci_cmd_table_t); ++i)
        ((uint8_t *)table)[i] = 0;

    /* CFL=5 | ATAPI bit (bit 5) */
    hdr->cfl   = 5u | (1u << 5u);
    hdr->a     = 0u;
    hdr->prdtl = 1u;

    table->prdt[0].dba  = (uint32_t)(uint64_t)atapi_ahci.bounce_phys;
    table->prdt[0].dbau = (uint32_t)((uint64_t)atapi_ahci.bounce_phys >> 32u);
    table->prdt[0].dbc  = byte_count - 1u;

    uint8_t *fis = table->cfis;
    fis[0] = AHCI_FIS_TYPE_REG_H2D;
    fis[1] = 1u << 7u;
    fis[2] = AHCI_CMD_PACKET;
    fis[3] = 0x00u;                                  /* PIO, no DMA */
    fis[4] = 0x00u;
    fis[5] = (uint8_t)(byte_count & 0xFFu);          /* byte count lo */
    fis[6] = (uint8_t)(byte_count >> 8u);            /* byte count hi */
    fis[7] = 0xA0u;

    uint8_t *acmd = table->acmd;
    acmd[0]  = 0xA8u;  /* READ(12) */
    acmd[2]  = (uint8_t)(lba >> 24u);
    acmd[3]  = (uint8_t)(lba >> 16u);
    acmd[4]  = (uint8_t)(lba >>  8u);
    acmd[5]  = (uint8_t)(lba);
    acmd[9]  = (uint8_t)count;

    atapi_ahci.port->is = 0xFFFFFFFFu;
    atapi_ahci.port->ci = 1u;

    for (uint32_t i = 0; i < 10000000u; ++i) {
        if ((atapi_ahci.port->ci & 1u) == 0) {
            if ((atapi_ahci.port->is & 0x40000000u) != 0) return -1;
            copy_bytes(dst, atapi_ahci.bounce_virt, byte_count);
            return 0;
        }
    }
    return -1;
}

/* ── ATA PIO ATAPI fallback (IDE, VirtualBox default) ───────────────────── */

#define ATA_PIO_CHUNK        8u   /* sectors per PIO burst */
#define ATA_PIO_CHUNK_BYTES  (ATA_PIO_CHUNK * ATAPI_ISO_SECTOR_SIZE)

static uint16_t atapi_pio_base  = 0;
static uint8_t  atapi_pio_drive = 0;

/* 16KB static bounce — lives in BSS, no alloc needed */
static uint8_t atapi_pio_buf[ATA_PIO_CHUNK_BYTES];

static int ata_pio_wait_not_busy(uint16_t stat) {
    for (uint32_t i = 0; i < 5000000u; ++i)
        if ((inb(stat) & 0x80u) == 0) return 0;
    return -1;
}
static int ata_pio_wait_drq(uint16_t stat) {
    for (uint32_t i = 0; i < 5000000u; ++i) {
        uint8_t s = inb(stat);
        if (s & 0x80u) continue;   /* BSY */
        if (s & 0x08u) return 0;   /* DRQ */
        if (s & 0x01u) return -1;  /* ERR */
    }
    return -1;
}

/* Detect ATAPI: LBA mid=0x14, LBA hi=0xEB after soft-reset-like select */
static int ata_pio_detect(uint16_t base, uint8_t drive) {
    outb((uint16_t)(base + 6u), drive);
    for (int i = 0; i < 4; ++i) inb((uint16_t)(base + 7u)); /* 400ns delay */
    if (ata_pio_wait_not_busy((uint16_t)(base + 7u)) != 0) return -1;
    return (inb((uint16_t)(base + 4u)) == 0x14u &&
            inb((uint16_t)(base + 5u)) == 0xEBu) ? 0 : -1;
}

/* Read 'count' ISO sectors via ATA PIO ATAPI (count ≤ ATA_PIO_CHUNK) */
static int ata_pio_atapi_read_chunk(uint32_t lba, uint32_t count, void *dst) {
    uint16_t base  = atapi_pio_base;
    uint8_t  drive = atapi_pio_drive;
    uint16_t stat  = (uint16_t)(base + 7u);
    uint32_t bytes = count * ATAPI_ISO_SECTOR_SIZE;

    outb((uint16_t)(base + 6u), drive);
    for (int i = 0; i < 4; ++i) inb(stat);
    if (ata_pio_wait_not_busy(stat) != 0) return -1;

    outb((uint16_t)(base + 1u), 0x00u);                         /* features = 0 */
    outb((uint16_t)(base + 4u), (uint8_t)(bytes & 0xFFu));      /* byte count lo */
    outb((uint16_t)(base + 5u), (uint8_t)(bytes >> 8u));        /* byte count hi */
    outb(stat, 0xA0u);                                           /* PACKET command */

    if (ata_pio_wait_drq(stat) != 0) return -1;

    /* Send 12-byte ATAPI READ(12) packet as 6 × 16-bit words */
    uint8_t pkt[12] = {
        0xA8u, 0x00u,
        (uint8_t)(lba >> 24u), (uint8_t)(lba >> 16u),
        (uint8_t)(lba >>  8u), (uint8_t)(lba),
        0x00u, 0x00u, 0x00u, (uint8_t)count,
        0x00u, 0x00u
    };
    for (int w = 0; w < 6; ++w)
        outw(base, (uint16_t)((uint16_t)pkt[w * 2] | ((uint16_t)pkt[w * 2 + 1] << 8u)));

    /* Read data: count * 1024 words */
    uint16_t *d = (uint16_t *)dst;
    for (uint32_t w = 0; w < count * 1024u; ++w) {
        if (ata_pio_wait_drq(stat) != 0 && w > 0) break;
        d[w] = inw(base);
    }
    /* Drain any leftover */
    ata_pio_wait_not_busy(stat);
    return 0;
}

/* ── Unified CD-ROM API ──────────────────────────────────────────────────── */

int disk_cdrom_available(void) {
    return atapi_ahci_ready || atapi_pio_ready;
}

int disk_cdrom_read(uint32_t lba, uint32_t count, void *buffer) {
    if (!disk_cdrom_available() || !buffer || count == 0) return -1;
    uint8_t *dst = (uint8_t *)buffer;
    uint32_t rem = count;

    while (rem > 0) {
        if (atapi_ahci_ready) {
            uint32_t chunk = rem > ATAPI_AHCI_CHUNK ? ATAPI_AHCI_CHUNK : rem;
            if (ahci_atapi_dma_read(lba, chunk, dst) != 0) return -1;
            dst += chunk * ATAPI_ISO_SECTOR_SIZE;
            lba += chunk;
            rem -= chunk;
        } else {
            uint32_t chunk = rem > ATA_PIO_CHUNK ? ATA_PIO_CHUNK : rem;
            if (ata_pio_atapi_read_chunk(lba, chunk, atapi_pio_buf) != 0) return -1;
            uint32_t nbytes = chunk * ATAPI_ISO_SECTOR_SIZE;
            for (uint32_t i = 0; i < nbytes; ++i) dst[i] = atapi_pio_buf[i];
            dst += nbytes;
            lba += chunk;
            rem -= chunk;
        }
    }
    return 0;
}

/* Called from disk_init() to probe for CD-ROM on an already-found AHCI HBA */
static void ahci_probe_atapi(ahci_hba_t *hba) {
    const uint32_t implemented = hba->pi;
    ahci_port_t *ports = (ahci_port_t *)((uint8_t *)hba + 0x100u);
    for (uint32_t i = 0; i < AHCI_PORT_COUNT; ++i) {
        if ((implemented & (1u << i)) == 0) continue;
        if (!ahci_atapi_port_present(&ports[i])) continue;
        if (ahci_atapi_init_port(&ports[i]) == 0) {
            atapi_ahci_ready = 1;
            return;
        }
    }
}

/* Called from disk_init() to probe ATA PIO ports for ATAPI */
static void ata_pio_probe_atapi(void) {
    /* Secondary IDE (0x170): VirtualBox puts CD here by default */
    static const uint16_t bases[2]  = { 0x170u, 0x1F0u };
    static const uint8_t  drives[2] = { 0xA0u, 0xB0u }; /* master, slave */

    for (int bi = 0; bi < 2; ++bi) {
        for (int di = 0; di < 2; ++di) {
            if (ata_pio_detect(bases[bi], drives[di]) == 0) {
                atapi_pio_base  = bases[bi];
                atapi_pio_drive = drives[di];
                atapi_pio_ready = 1;
                return;
            }
        }
    }
}
