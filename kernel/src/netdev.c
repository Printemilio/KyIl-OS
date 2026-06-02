#include <unos/netdev.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/boot_requests.h>
#include <unos/debug_log.h>
#include <unos/heap.h>
#include <unos/pmm.h>
#include <unos/vmm.h>

#define PCI_CONFIG_ADDRESS 0xcf8u
#define PCI_CONFIG_DATA 0xcfcu
#define PCI_VENDOR_INVALID 0xffffu
#define PCI_VENDOR_INTEL 0x8086u
#define PCI_DEVICE_E1000_82540EM 0x100eu
#define PCI_VENDOR_VIRTIO 0x1af4u

#define PAGE_SIZE 4096ull
#define PAGE_MASK 0x000ffffffffff000ull
#define NET_MMIO_BASE 0xffffffffb0000000ull
#define ETH_FRAME_MAX 1518u

#define E1000_CTRL 0x0000u
#define E1000_STATUS 0x0008u
#define E1000_EERD 0x0014u
#define E1000_ICR 0x00c0u
#define E1000_ICS 0x00c8u
#define E1000_IMS 0x00d0u
#define E1000_IMC 0x00d8u
#define E1000_RCTL 0x0100u
#define E1000_TCTL 0x0400u
#define E1000_TIPG 0x0410u
#define E1000_GPRC 0x4074u
#define E1000_BPRC 0x4078u
#define E1000_MPRC 0x407cu
#define E1000_RNBC 0x40a0u
#define E1000_MPC 0x4010u
#define E1000_TPT 0x40d4u
#define E1000_RDBAL 0x2800u
#define E1000_RDBAH 0x2804u
#define E1000_RDLEN 0x2808u
#define E1000_RDH 0x2810u
#define E1000_RDT 0x2818u
#define E1000_TDBAL 0x3800u
#define E1000_TDBAH 0x3804u
#define E1000_TDLEN 0x3808u
#define E1000_TDH 0x3810u
#define E1000_TDT 0x3818u
#define E1000_RAL 0x5400u
#define E1000_RAH 0x5404u
#define E1000_MTA 0x5200u
#define E1000_RX_DESC_COUNT 32u
#define E1000_TX_DESC_COUNT 32u
#define E1000_RX_BUFFER_SIZE 2048u
#define E1000_TX_BUFFER_SIZE 2048u
#define E1000_CTRL_FD 0x00000001u
#define E1000_CTRL_ASDE 0x00000020u
#define E1000_CTRL_SLU 0x00000040u
#define E1000_CTRL_SPEED_1000 0x00000200u
#define E1000_CTRL_FRCSPD 0x00000800u
#define E1000_CTRL_FRCDPX 0x00001000u
#define E1000_CTRL_RST 0x04000000u
#define E1000_STATUS_LU 0x00000002u
#define E1000_RAH_AV 0x80000000u
#define E1000_RCTL_EN 0x00000002u
#define E1000_RCTL_SBP 0x00000004u
#define E1000_RCTL_UPE 0x00000008u
#define E1000_RCTL_MPE 0x00000010u
#define E1000_RCTL_LPE 0x00000020u
#define E1000_RCTL_BAM 0x00008000u
#define E1000_RCTL_BSIZE_2048 0x00000000u
#define E1000_RCTL_SECRC 0x04000000u
#define E1000_TCTL_EN 0x00000002u
#define E1000_TCTL_PSP 0x00000008u
#define E1000_TCTL_CT_SHIFT 4u
#define E1000_TCTL_COLD_SHIFT 12u
#define E1000_TCTL_RTLC 0x01000000u
#define E1000_TDESC_CMD_EOP 0x01u
#define E1000_TDESC_CMD_IFCS 0x02u
#define E1000_TDESC_CMD_RS 0x08u
#define E1000_TDESC_STATUS_DD 0x01u
#define E1000_RDESC_STATUS_DD 0x01u
#define E1000_INTR_TXDW 0x00000001u
#define E1000_INTR_LSC 0x00000004u
#define E1000_INTR_RXSEQ 0x00000008u
#define E1000_INTR_RXDMT0 0x00000010u
#define E1000_INTR_RXO 0x00000040u
#define E1000_INTR_RXT0 0x00000080u

#define VIRTIO_NET_DEVICE_LEGACY_MIN 0x1000u
#define VIRTIO_NET_DEVICE_LEGACY_MAX 0x103fu
#define VIRTIO_NET_DEVICE_TRANSITIONAL 0x1041u
#define VIRTIO_PCI_HOST_FEATURES 0x00u
#define VIRTIO_PCI_GUEST_FEATURES 0x04u
#define VIRTIO_PCI_QUEUE_PFN 0x08u
#define VIRTIO_PCI_QUEUE_NUM 0x0cu
#define VIRTIO_PCI_QUEUE_SEL 0x0eu
#define VIRTIO_PCI_QUEUE_NOTIFY 0x10u
#define VIRTIO_PCI_STATUS 0x12u
#define VIRTIO_PCI_ISR 0x13u
#define VIRTIO_NET_CONFIG_MAC 0x14u
#define VIRTIO_STATUS_ACK 0x01u
#define VIRTIO_STATUS_DRIVER 0x02u
#define VIRTIO_STATUS_DRIVER_OK 0x04u
#define VIRTIO_STATUS_FEATURES_OK 0x08u
#define VIRTIO_QUEUE_SIZE 8u
#define VIRTQ_DESC_F_NEXT 1u
#define VIRTQ_DESC_F_WRITE 2u

typedef enum {
    NETDEV_NONE,
    NETDEV_E1000,
    NETDEV_VIRTIO
} netdev_backend_t;

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
} pci_device_t;

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed, aligned(16))) e1000_rx_desc_t;

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed, aligned(16))) e1000_tx_desc_t;

_Static_assert(sizeof(e1000_rx_desc_t) == 16u, "E1000 RX descriptor must be 16 bytes");
_Static_assert(sizeof(e1000_tx_desc_t) == 16u, "E1000 TX descriptor must be 16 bytes");

typedef struct {
    volatile uint8_t *mmio;
    volatile e1000_rx_desc_t *rx_desc;
    volatile e1000_tx_desc_t *tx_desc;
    uint64_t rx_desc_phys;
    uint64_t tx_desc_phys;
    uint64_t rx_buffers_phys[E1000_RX_DESC_COUNT];
    uint64_t tx_buffers_phys[E1000_TX_DESC_COUNT];
    uint8_t *rx_buffers[E1000_RX_DESC_COUNT];
    uint8_t *tx_buffers[E1000_TX_DESC_COUNT];
    uint32_t rx_tail;
    uint32_t tx_tail;
    uint32_t rx_empty_polls;
    uint32_t rx_debug_polls;
} e1000_state_t;

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} virtq_desc_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTIO_QUEUE_SIZE];
} virtq_avail_t;

typedef struct {
    uint32_t id;
    uint32_t len;
} virtq_used_elem_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[VIRTIO_QUEUE_SIZE];
} virtq_used_t;

typedef struct {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} virtio_net_hdr_t;

typedef struct {
    uint16_t io_base;
    uint16_t queue_size;
    uint64_t queue_phys;
    uint8_t *queue_virt;
    virtq_desc_t *desc;
    virtq_avail_t *avail;
    virtq_used_t *used;
    uint8_t *buffers[VIRTIO_QUEUE_SIZE];
    uint64_t buffers_phys[VIRTIO_QUEUE_SIZE];
    uint16_t avail_idx;
    uint16_t used_idx;
} virtio_state_t;

static netdev_backend_t netdev_backend;
static int netdev_initialized;
static uint8_t local_mac[6];
static uint64_t next_mmio_virt = NET_MMIO_BASE;
static e1000_state_t e1000;
static virtio_state_t virtio;

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" :: "a"(value), "Nd"(port));
}

static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" :: "a"(value), "Nd"(port));
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

static uint64_t alloc_phys_page(void) {
    return (uint64_t)pmm_alloc_page();
}

static void zero_page(void *page) {
    uint64_t *words = page;
    for (uint64_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); ++i) {
        words[i] = 0;
    }
}

static void copy_bytes(void *dst, const void *src, size_t size) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (size_t i = 0; i < size; ++i) {
        d[i] = s[i];
    }
}

static int mac_is_zero(const uint8_t mac[6]) {
    return mac[0] == 0 && mac[1] == 0 && mac[2] == 0 &&
           mac[3] == 0 && mac[4] == 0 && mac[5] == 0;
}

static void ensure_local_mac(const char *source) {
    if (!mac_is_zero(local_mac)) {
        return;
    }

    static const uint8_t fallback_mac[6] = {0x02, 0x56, 0x45, 0x58, 0x00, 0x01};
    copy_bytes(local_mac, fallback_mac, sizeof(local_mac));
    debug_log_puts("netdev: fallback mac after ");
    debug_log_puts(source);
    debug_log_puts("\n");
}

static void *map_mmio(uint64_t phys, uint64_t length) {
    const uint64_t phys_base = align_down(phys, PAGE_SIZE);
    const uint64_t phys_offset = phys - phys_base;
    const uint64_t mapped_length = align_up(length + phys_offset, PAGE_SIZE);
    const uint64_t virt_base = next_mmio_virt;

    for (uint64_t offset = 0; offset < mapped_length; offset += PAGE_SIZE) {
        vmm_map_page(
            current_pml4(),
            virt_base + offset,
            phys_base + offset,
            VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE | VMM_PAGE_PWT | VMM_PAGE_PCD
        );
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

static int pci_find_e1000(pci_device_t *out) {
    for (uint16_t bus = 0; bus < 256u; ++bus) {
        for (uint8_t device = 0; device < 32u; ++device) {
            for (uint8_t function = 0; function < 8u; ++function) {
                const uint32_t id = pci_read32((uint8_t)bus, device, function, 0x00u);
                const uint16_t vendor = (uint16_t)id;
                const uint16_t device_id = (uint16_t)(id >> 16u);
                if (vendor == PCI_VENDOR_INTEL && device_id == PCI_DEVICE_E1000_82540EM) {
                    out->bus = (uint8_t)bus;
                    out->device = device;
                    out->function = function;
                    out->vendor_id = vendor;
                    out->device_id = device_id;
                    return 0;
                }
                if (function == 0 && vendor == PCI_VENDOR_INVALID) {
                    break;
                }
            }
        }
    }

    return -1;
}

static int pci_find_virtio_net(pci_device_t *out) {
    for (uint16_t bus = 0; bus < 256u; ++bus) {
        for (uint8_t device = 0; device < 32u; ++device) {
            for (uint8_t function = 0; function < 8u; ++function) {
                const uint32_t id = pci_read32((uint8_t)bus, device, function, 0x00u);
                const uint16_t vendor = (uint16_t)id;
                const uint16_t device_id = (uint16_t)(id >> 16u);
                if (vendor == PCI_VENDOR_VIRTIO &&
                    ((device_id >= VIRTIO_NET_DEVICE_LEGACY_MIN && device_id <= VIRTIO_NET_DEVICE_LEGACY_MAX) ||
                     device_id == VIRTIO_NET_DEVICE_TRANSITIONAL)) {
                    out->bus = (uint8_t)bus;
                    out->device = device;
                    out->function = function;
                    out->vendor_id = vendor;
                    out->device_id = device_id;
                    return 0;
                }
                if (function == 0 && vendor == PCI_VENDOR_INVALID) {
                    break;
                }
            }
        }
    }

    return -1;
}

static uint32_t e1000_read(uint32_t reg) {
    return *(volatile uint32_t *)(e1000.mmio + reg);
}

static void e1000_write(uint32_t reg, uint32_t value) {
    *(volatile uint32_t *)(e1000.mmio + reg) = value;
}

static void e1000_flush(void) {
    (void)e1000_read(E1000_STATUS);
}

static void dma_barrier(void) {
    __asm__ volatile ("mfence" ::: "memory");
}

static void dma_read_barrier(void) {
    __asm__ volatile ("lfence" ::: "memory");
}

static void cache_flush_line(const volatile void *addr) {
    __asm__ volatile ("clflush (%0)" :: "r"(addr) : "memory");
}

static void cache_flush_range(const volatile void *addr, size_t size) {
    const uintptr_t start = (uintptr_t)addr & ~63ull;
    const uintptr_t end = ((uintptr_t)addr + size + 63ull) & ~63ull;

    for (uintptr_t line = start; line < end; line += 64u) {
        cache_flush_line((const void *)line);
    }
}

static int e1000_read_eeprom(uint8_t addr, uint16_t *value) {
    e1000_write(E1000_EERD, ((uint32_t)addr << 8u) | 1u);
    for (uint32_t i = 0; i < 100000u; ++i) {
        const uint32_t eerd = e1000_read(E1000_EERD);
        if ((eerd & 0x10u) != 0) {
            *value = (uint16_t)(eerd >> 16u);
            return 0;
        }
    }
    return -1;
}

static void e1000_load_mac(void) {
    uint16_t word;
    if (e1000_read_eeprom(0, &word) == 0) {
        local_mac[0] = (uint8_t)word;
        local_mac[1] = (uint8_t)(word >> 8u);
        e1000_read_eeprom(1, &word);
        local_mac[2] = (uint8_t)word;
        local_mac[3] = (uint8_t)(word >> 8u);
        e1000_read_eeprom(2, &word);
        local_mac[4] = (uint8_t)word;
        local_mac[5] = (uint8_t)(word >> 8u);
        ensure_local_mac("e1000 eeprom");
        return;
    }

    const uint32_t ral = e1000_read(E1000_RAL);
    const uint32_t rah = e1000_read(E1000_RAH);
    local_mac[0] = (uint8_t)ral;
    local_mac[1] = (uint8_t)(ral >> 8u);
    local_mac[2] = (uint8_t)(ral >> 16u);
    local_mac[3] = (uint8_t)(ral >> 24u);
    local_mac[4] = (uint8_t)rah;
    local_mac[5] = (uint8_t)(rah >> 8u);
    ensure_local_mac("e1000 ral/rah");
}

static void e1000_program_receive_address(void) {
    const uint32_t ral =
        ((uint32_t)local_mac[0]) |
        ((uint32_t)local_mac[1] << 8u) |
        ((uint32_t)local_mac[2] << 16u) |
        ((uint32_t)local_mac[3] << 24u);
    const uint32_t rah =
        ((uint32_t)local_mac[4]) |
        ((uint32_t)local_mac[5] << 8u) |
        E1000_RAH_AV;

    e1000_write(E1000_RAL, ral);
    e1000_write(E1000_RAH, rah);
    e1000_flush();
}

static void e1000_clear_multicast_table(void) {
    for (uint32_t i = 0; i < 128u; ++i) {
        e1000_write(E1000_MTA + i * sizeof(uint32_t), 0);
    }
    e1000_flush();
}

static void e1000_reset_controller(void) {
    e1000_write(E1000_IMC, 0xffffffffu);
    e1000_write(E1000_RCTL, 0);
    e1000_write(E1000_TCTL, 0);
    e1000_flush();

    e1000_write(E1000_CTRL, e1000_read(E1000_CTRL) | E1000_CTRL_RST);
    e1000_flush();
    for (uint32_t i = 0; i < 100000u; ++i) {
        __asm__ volatile ("pause");
    }

    e1000_write(E1000_IMC, 0xffffffffu);
    e1000_flush();
}

static void short_io_delay(void) {
    for (uint32_t i = 0; i < 1000u; ++i) {
        __asm__ volatile ("" ::: "memory");
    }
}

static int e1000_wait_for_link(void) {
    for (uint32_t i = 0; i < 1000000u; ++i) {
        e1000_write(
            E1000_CTRL,
            e1000_read(E1000_CTRL) |
                E1000_CTRL_FD |
                E1000_CTRL_ASDE |
                E1000_CTRL_SLU |
                E1000_CTRL_SPEED_1000 |
                E1000_CTRL_FRCSPD |
                E1000_CTRL_FRCDPX
        );
        e1000_flush();
        if ((e1000_read(E1000_STATUS) & E1000_STATUS_LU) != 0) {
            return 0;
        }
        __asm__ volatile ("pause");
    }

    return -1;
}

static int e1000_init(pci_device_t device) {
    pci_enable_bus_mastering(device);
    e1000.mmio = map_mmio(pci_bar(device, 0), 0x20000u);
    if (e1000.mmio == NULL) {
        return -1;
    }

    e1000_reset_controller();
    e1000_load_mac();
    e1000_write(
        E1000_CTRL,
        e1000_read(E1000_CTRL) |
            E1000_CTRL_FD |
            E1000_CTRL_ASDE |
            E1000_CTRL_SLU |
            E1000_CTRL_SPEED_1000 |
            E1000_CTRL_FRCSPD |
            E1000_CTRL_FRCDPX
    );
    const int link_result = e1000_wait_for_link();
    e1000_program_receive_address();
    e1000_clear_multicast_table();

    e1000.rx_desc_phys = alloc_phys_page();
    e1000.tx_desc_phys = alloc_phys_page();
    if (e1000.rx_desc_phys == 0 || e1000.tx_desc_phys == 0) {
        return -1;
    }

    e1000.rx_desc = phys_to_virt(e1000.rx_desc_phys);
    e1000.tx_desc = phys_to_virt(e1000.tx_desc_phys);
    zero_page((void *)e1000.rx_desc);
    zero_page((void *)e1000.tx_desc);

    for (uint32_t i = 0; i < E1000_RX_DESC_COUNT; ++i) {
        e1000.rx_buffers_phys[i] = alloc_phys_page();
        if (e1000.rx_buffers_phys[i] == 0) {
            return -1;
        }
        e1000.rx_buffers[i] = phys_to_virt(e1000.rx_buffers_phys[i]);
        e1000.rx_desc[i].addr = e1000.rx_buffers_phys[i];
    }

    for (uint32_t i = 0; i < E1000_TX_DESC_COUNT; ++i) {
        e1000.tx_buffers_phys[i] = alloc_phys_page();
        if (e1000.tx_buffers_phys[i] == 0) {
            return -1;
        }
        e1000.tx_buffers[i] = phys_to_virt(e1000.tx_buffers_phys[i]);
        e1000.tx_desc[i].addr = e1000.tx_buffers_phys[i];
        e1000.tx_desc[i].status = E1000_TDESC_STATUS_DD;
    }

    e1000_write(E1000_RCTL, 0);
    e1000_flush();
    e1000_write(E1000_RDBAL, (uint32_t)e1000.rx_desc_phys);
    e1000_write(E1000_RDBAH, (uint32_t)(e1000.rx_desc_phys >> 32u));
    e1000_write(E1000_RDLEN, E1000_RX_DESC_COUNT * sizeof(e1000_rx_desc_t));
    e1000_write(E1000_RDH, 0);
    e1000_write(E1000_RDT, E1000_RX_DESC_COUNT - 1u);
    cache_flush_range(e1000.rx_desc, E1000_RX_DESC_COUNT * sizeof(e1000_rx_desc_t));
    dma_barrier();
    short_io_delay();
    e1000.rx_tail = 0;
    e1000.rx_empty_polls = 0;
    e1000.rx_debug_polls = 0;

    e1000_write(E1000_RCTL,
        E1000_RCTL_EN |
        E1000_RCTL_SBP |
        E1000_RCTL_UPE |
        E1000_RCTL_MPE |
        E1000_RCTL_LPE |
        E1000_RCTL_BAM |
        E1000_RCTL_BSIZE_2048 |
        E1000_RCTL_SECRC
    );
    e1000_flush();

    e1000_write(E1000_TDBAL, (uint32_t)e1000.tx_desc_phys);
    e1000_write(E1000_TDBAH, (uint32_t)(e1000.tx_desc_phys >> 32u));
    e1000_write(E1000_TDLEN, E1000_TX_DESC_COUNT * sizeof(e1000_tx_desc_t));
    e1000_write(E1000_TDH, 0);
    e1000_write(E1000_TDT, 0);
    e1000.tx_tail = 0;

    (void)link_result;

    e1000_write(E1000_TIPG, 10u | (8u << 10u) | (6u << 20u));
    e1000_write(E1000_TCTL,
        E1000_TCTL_EN |
        E1000_TCTL_PSP |
        E1000_TCTL_RTLC |
        (15u << E1000_TCTL_CT_SHIFT) |
        (64u << E1000_TCTL_COLD_SHIFT)
    );
    e1000_flush();
    (void)e1000_read(E1000_GPRC);
    (void)e1000_read(E1000_BPRC);
    (void)e1000_read(E1000_MPRC);
    (void)e1000_read(E1000_RNBC);
    (void)e1000_read(E1000_MPC);
    (void)e1000_read(E1000_TPT);
    (void)e1000_read(E1000_ICR);
    e1000_write(E1000_IMS,
        E1000_INTR_TXDW |
        E1000_INTR_LSC |
        E1000_INTR_RXSEQ |
        E1000_INTR_RXDMT0 |
        E1000_INTR_RXO |
        E1000_INTR_RXT0
    );
    e1000_flush();
    return 0;
}

static int e1000_send(const void *data, size_t size) {
    if (size == 0 || size > ETH_FRAME_MAX) {
        return -1;
    }

    const uint32_t tail = e1000.tx_tail;
    volatile e1000_tx_desc_t *desc = &e1000.tx_desc[tail];
    if ((desc->status & E1000_TDESC_STATUS_DD) == 0) {
        return -1;
    }

    copy_bytes(e1000.tx_buffers[tail], data, size);
    desc->length = (uint16_t)size;
    desc->cso = 0;
    desc->cmd = E1000_TDESC_CMD_EOP | E1000_TDESC_CMD_IFCS | E1000_TDESC_CMD_RS;
    desc->css = 0;
    desc->special = 0;
    desc->status = 0;

    e1000.tx_tail = (tail + 1u) % E1000_TX_DESC_COUNT;
    dma_barrier();
    e1000_write(E1000_TDT, e1000.tx_tail);
    e1000_flush();
    for (uint32_t i = 0; i < 100000u; ++i) {
        if ((desc->status & E1000_TDESC_STATUS_DD) != 0) {
            return 0;
        }
    }

    return 0;
}

static int e1000_recv(void *buffer, size_t *size) {
    uint32_t index = e1000.rx_tail;
    volatile e1000_rx_desc_t *desc = &e1000.rx_desc[index];

    cache_flush_line(desc);
    dma_read_barrier();
    if ((desc->status & E1000_RDESC_STATUS_DD) == 0) {
        for (uint32_t i = 0; i < E1000_RX_DESC_COUNT; ++i) {
            volatile e1000_rx_desc_t *candidate = &e1000.rx_desc[i];
            cache_flush_line(candidate);
            dma_read_barrier();
            if ((candidate->status & E1000_RDESC_STATUS_DD) != 0) {
                index = i;
                desc = candidate;
                e1000.rx_tail = i;
                break;
            }
        }
    }

    if ((desc->status & E1000_RDESC_STATUS_DD) == 0) {
        ++e1000.rx_empty_polls;
        ++e1000.rx_debug_polls;
        return -1;
    }

    cache_flush_line(desc);
    dma_read_barrier();
    e1000.rx_empty_polls = 0;

    if (*size < desc->length) {
        return -1;
    }

    copy_bytes(buffer, e1000.rx_buffers[index], desc->length);
    *size = desc->length;
    desc->status = 0;
    desc->errors = 0;
    desc->length = 0;
    cache_flush_line(desc);
    dma_barrier();

    e1000.rx_tail = (index + 1u) % E1000_RX_DESC_COUNT;
    e1000_write(E1000_RDT, index);
    return 0;
}

static uint16_t virtio_io_bar(pci_device_t device) {
    return (uint16_t)pci_bar(device, 0);
}

static int virtio_alloc_queue(void) {
    virtio.queue_phys = alloc_phys_page();
    if (virtio.queue_phys == 0) {
        return -1;
    }

    virtio.queue_virt = phys_to_virt(virtio.queue_phys);
    zero_page(virtio.queue_virt);
    virtio.desc = (virtq_desc_t *)virtio.queue_virt;
    virtio.avail = (virtq_avail_t *)(virtio.queue_virt + sizeof(virtq_desc_t) * VIRTIO_QUEUE_SIZE);
    virtio.used = (virtq_used_t *)(virtio.queue_virt + PAGE_SIZE / 2u);

    for (uint16_t i = 0; i < VIRTIO_QUEUE_SIZE; ++i) {
        virtio.buffers_phys[i] = alloc_phys_page();
        if (virtio.buffers_phys[i] == 0) {
            return -1;
        }
        virtio.buffers[i] = phys_to_virt(virtio.buffers_phys[i]);
    }

    return 0;
}

static void virtio_post_rx_buffer(uint16_t id) {
    virtio_net_hdr_t zero_header = {0};
    copy_bytes(virtio.buffers[id], &zero_header, sizeof(zero_header));

    virtio.desc[id].addr = (uint64_t)virtio.buffers_phys[id];
    virtio.desc[id].len = PAGE_SIZE;
    virtio.desc[id].flags = VIRTQ_DESC_F_WRITE;
    virtio.desc[id].next = 0;
    virtio.avail->ring[virtio.avail_idx % VIRTIO_QUEUE_SIZE] = id;
    ++virtio.avail_idx;
    virtio.avail->idx = virtio.avail_idx;
}

static int virtio_init(pci_device_t device) {
    pci_enable_bus_mastering(device);
    virtio.io_base = virtio_io_bar(device);
    if (virtio.io_base == 0) {
        return -1;
    }

    outb(virtio.io_base + VIRTIO_PCI_STATUS, 0);
    outb(virtio.io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK);
    outb(virtio.io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);
    outl(virtio.io_base + VIRTIO_PCI_GUEST_FEATURES, 0);
    outb(virtio.io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);

    for (uint8_t i = 0; i < 6u; ++i) {
        local_mac[i] = inb((uint16_t)(virtio.io_base + VIRTIO_NET_CONFIG_MAC + i));
    }
    ensure_local_mac("virtio config");

    outw(virtio.io_base + VIRTIO_PCI_QUEUE_SEL, 0);
    virtio.queue_size = inw(virtio.io_base + VIRTIO_PCI_QUEUE_NUM);
    if (virtio.queue_size == 0 || virtio.queue_size > VIRTIO_QUEUE_SIZE || virtio_alloc_queue() != 0) {
        return -1;
    }

    outl(virtio.io_base + VIRTIO_PCI_QUEUE_PFN, (uint32_t)(virtio.queue_phys >> 12u));
    virtio.avail_idx = 0;
    virtio.used_idx = virtio.used->idx;
    for (uint16_t i = 1; i < VIRTIO_QUEUE_SIZE; ++i) {
        virtio_post_rx_buffer(i);
    }
    outb(virtio.io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);
    outw(virtio.io_base + VIRTIO_PCI_QUEUE_NOTIFY, 0);
    (void)inb(virtio.io_base + VIRTIO_PCI_ISR);
    return 0;
}

static int virtio_send(const void *data, size_t size) {
    if (size == 0 || size > ETH_FRAME_MAX) {
        return -1;
    }

    virtio_net_hdr_t *header = (virtio_net_hdr_t *)virtio.buffers[0];
    uint8_t *payload = virtio.buffers[0] + sizeof(virtio_net_hdr_t);
    copy_bytes(header, &(virtio_net_hdr_t){0}, sizeof(virtio_net_hdr_t));
    copy_bytes(payload, data, size);

    virtio.desc[0].addr = (uint64_t)virtio.buffers_phys[0];
    virtio.desc[0].len = (uint32_t)(sizeof(virtio_net_hdr_t) + size);
    virtio.desc[0].flags = 0;
    virtio.desc[0].next = 0;
    virtio.avail->ring[virtio.avail_idx % VIRTIO_QUEUE_SIZE] = 0;
    ++virtio.avail_idx;
    virtio.avail->idx = virtio.avail_idx;
    outw(virtio.io_base + VIRTIO_PCI_QUEUE_NOTIFY, 0);
    return 0;
}

static int virtio_recv(void *buffer, size_t *size) {
    if (virtio.used_idx == virtio.used->idx) {
        return -1;
    }

    const virtq_used_elem_t used = virtio.used->ring[virtio.used_idx % VIRTIO_QUEUE_SIZE];
    ++virtio.used_idx;
    if (used.len <= sizeof(virtio_net_hdr_t)) {
        return -1;
    }

    const uint32_t frame_len = used.len - sizeof(virtio_net_hdr_t);
    if (*size < frame_len) {
        return -1;
    }

    copy_bytes(buffer, virtio.buffers[used.id] + sizeof(virtio_net_hdr_t), frame_len);
    *size = frame_len;
    virtio_post_rx_buffer((uint16_t)used.id);
    outw(virtio.io_base + VIRTIO_PCI_QUEUE_NOTIFY, 0);
    return 0;
}

void netdev_init(void) {
    pci_device_t device;

    if (netdev_initialized) {
        return;
    }

    netdev_initialized = 1;
    netdev_backend = NETDEV_NONE;
    if (pci_find_e1000(&device) == 0 && e1000_init(device) == 0) {
        netdev_backend = NETDEV_E1000;
        debug_log_puts("netdev: backend e1000\n");
        return;
    }

    if (pci_find_virtio_net(&device) == 0 && virtio_init(device) == 0) {
        netdev_backend = NETDEV_VIRTIO;
        debug_log_puts("netdev: backend virtio\n");
        return;
    }

    debug_log_puts("netdev: backend none\n");
}

int netdev_link_up(void) {
    if (netdev_backend == NETDEV_E1000) {
        return (e1000_read(E1000_STATUS) & E1000_STATUS_LU) != 0;
    }

    if (netdev_backend == NETDEV_VIRTIO) {
        return 1;
    }

    return 0;
}

int netdev_send_frame(const void *data, size_t size) {
    if (netdev_backend == NETDEV_E1000) {
        return e1000_send(data, size);
    }

    if (netdev_backend == NETDEV_VIRTIO) {
        return virtio_send(data, size);
    }

    return -1;
}

void netdev_irq_handler(void) {
    if (netdev_backend == NETDEV_E1000) {
        (void)e1000_read(E1000_ICR);
        return;
    }
}

int netdev_recv_frame(void *buffer, size_t *size) {
    if (buffer == NULL || size == NULL) {
        return -1;
    }

    if (netdev_backend == NETDEV_E1000) {
        return e1000_recv(buffer, size);
    }

    if (netdev_backend == NETDEV_VIRTIO) {
        return virtio_recv(buffer, size);
    }

    return -1;
}

const uint8_t *netdev_mac(void) {
    return local_mac;
}

const char *netdev_type(void) {
    if (netdev_backend == NETDEV_E1000) {
        return "E1000";
    }

    if (netdev_backend == NETDEV_VIRTIO) {
        return "virtio-net";
    }

    return "aucun";
}

void netdev_stats(netdev_stats_t *stats) {
    if (stats == NULL) {
        return;
    }

    stats->link_up = 0;
    stats->tx_packets = 0;
    stats->rx_packets = 0;
    stats->rx_broadcast = 0;
    stats->rx_multicast = 0;
    stats->rx_no_buffer = 0;
    stats->missed_packets = 0;
    stats->rx_head = 0;
    stats->rx_tail = 0;
    stats->rx_control = 0;
    for (size_t i = 0; i < 4u; ++i) {
        stats->rx_descriptor_status[i] = 0;
    }

    if (netdev_backend == NETDEV_E1000) {
        stats->link_up = (e1000_read(E1000_STATUS) & E1000_STATUS_LU) != 0 ? 1u : 0u;
        stats->tx_packets = e1000_read(E1000_TPT);
        stats->rx_packets = e1000_read(E1000_GPRC);
        stats->rx_broadcast = e1000_read(E1000_BPRC);
        stats->rx_multicast = e1000_read(E1000_MPRC);
        stats->rx_no_buffer = e1000_read(E1000_RNBC);
        stats->missed_packets = e1000_read(E1000_MPC);
        stats->rx_head = e1000_read(E1000_RDH);
        stats->rx_tail = e1000_read(E1000_RDT);
        stats->rx_control = e1000_read(E1000_RCTL);
        dma_read_barrier();
        for (size_t i = 0; i < 4u; ++i) {
            cache_flush_line(&e1000.rx_desc[i]);
            dma_read_barrier();
            stats->rx_descriptor_status[i] = e1000.rx_desc[i].status;
        }
    }
}
