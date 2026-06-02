#include <unos/voice.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/boot_requests.h>
#include <unos/tty.h>
#include <unos/vmm.h>

#define PCI_CONFIG_ADDRESS 0xcf8u
#define PCI_CONFIG_DATA 0xcfcu
#define PCI_VENDOR_INVALID 0xffffu
#define PCI_CLASS_MULTIMEDIA 0x04u
#define PCI_SUBCLASS_HDA 0x03u
#define PAGE_SIZE 4096ull
#define VOICE_MMIO_BASE 0xffffffffc0000000ull

#define HDA_GCAP 0x00u
#define HDA_VMIN 0x02u
#define HDA_VMAJ 0x03u
#define HDA_STATESTS 0x0eu
#define HDA_GCTL 0x08u

typedef struct pci_device {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
} pci_device_t;

typedef struct hda_state {
    volatile uint8_t *regs;
    uint16_t gcap;
    uint8_t version_major;
    uint8_t version_minor;
    uint16_t statests;
    int available;
} hda_state_t;

static hda_state_t hda;
static uint64_t next_mmio_virt = VOICE_MMIO_BASE;
static const char *voice_last_status = "voice: not initialized";

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" :: "a"(value), "Nd"(port));
}

static inline uint64_t read_cr3(void) {
    uint64_t value;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(value));
    return value;
}

static uint64_t align_down(uint64_t value, uint64_t alignment) {
    return value & ~(alignment - 1u);
}

static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static void *current_pml4(void) {
    return (void *)(hhdm_request.response->offset + read_cr3());
}

static void copy_string(char *dst, size_t capacity, const char *src) {
    size_t i = 0;

    if (capacity == 0) {
        return;
    }

    while (src != NULL && src[i] != '\0' && i + 1u < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static void *map_mmio(uint64_t phys, uint64_t length) {
    const uint64_t phys_base = align_down(phys, PAGE_SIZE);
    const uint64_t phys_offset = phys - phys_base;
    const uint64_t mapped_length = align_up(length + phys_offset, PAGE_SIZE);
    const uint64_t virt_base = next_mmio_virt;

    if (phys == 0 || hhdm_request.response == NULL) {
        return NULL;
    }

    for (uint64_t offset = 0; offset < mapped_length; offset += PAGE_SIZE) {
        vmm_map_page(current_pml4(), virt_base + offset, phys_base + offset, VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE | VMM_PAGE_PCD);
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

static int pci_find_hda(pci_device_t *out) {
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
                const uint32_t class_reg = pci_read32((uint8_t)bus, device, function, 0x08u);
                const uint8_t class_code = (uint8_t)(class_reg >> 24u);
                const uint8_t subclass = (uint8_t)(class_reg >> 16u);

                if (fn_vendor != PCI_VENDOR_INVALID && class_code == PCI_CLASS_MULTIMEDIA && subclass == PCI_SUBCLASS_HDA) {
                    out->bus = (uint8_t)bus;
                    out->device = device;
                    out->function = function;
                    return 0;
                }
            }
        }
    }

    return -1;
}

static uint8_t hda_read8(uint32_t offset) {
    return hda.regs[offset];
}

static uint16_t hda_read16(uint32_t offset) {
    return (uint16_t)hda.regs[offset] | ((uint16_t)hda.regs[offset + 1u] << 8u);
}

static uint32_t hda_read32(uint32_t offset) {
    return (uint32_t)hda_read16(offset) | ((uint32_t)hda_read16(offset + 2u) << 16u);
}

void voice_init(void) {
    pci_device_t device;

    hda.regs = NULL;
    hda.available = 0;
    voice_last_status = "voice: HDA not found";

    if (pci_find_hda(&device) != 0) {
        return;
    }

    uint32_t command = pci_read32(device.bus, device.device, device.function, 0x04u);
    command |= 0x0006u;
    pci_write32(device.bus, device.device, device.function, 0x04u, command);

    hda.regs = map_mmio(pci_bar(device, 0u), 0x4000u);
    if (hda.regs == NULL) {
        voice_last_status = "voice: HDA BAR map failed";
        return;
    }

    hda.gcap = hda_read16(HDA_GCAP);
    hda.version_minor = hda_read8(HDA_VMIN);
    hda.version_major = hda_read8(HDA_VMAJ);
    hda.statests = hda_read16(HDA_STATESTS);

    if ((hda_read32(HDA_GCTL) & 0x1u) == 0) {
        volatile uint32_t *gctl = (volatile uint32_t *)(hda.regs + HDA_GCTL);
        *gctl = hda_read32(HDA_GCTL) | 0x1u;
    }

    hda.available = 1;
    voice_last_status = "voice: HDA register path ready";
    tty_puts("voice: HDA controller ready\n");
}

int voice_hda_available(void) {
    return hda.available;
}

const char *voice_status(void) {
    return voice_last_status;
}

static int text_contains(const char *text, const char *needle) {
    if (text == NULL || needle == NULL || needle[0] == '\0') {
        return 0;
    }

    for (size_t i = 0; text[i] != '\0'; ++i) {
        size_t j = 0;
        while (needle[j] != '\0' && text[i + j] == needle[j]) {
            ++j;
        }
        if (needle[j] == '\0') {
            return 1;
        }
    }

    return 0;
}

voice_command_t voice_command_from_text(const char *text) {
    if (text_contains(text, "terminal") || text_contains(text, "ouvre le terminal")) {
        return VOICE_COMMAND_OPEN_TERMINAL;
    }
    if (text_contains(text, "browser") || text_contains(text, "navigateur") || text_contains(text, "webkit")) {
        return VOICE_COMMAND_OPEN_BROWSER;
    }
    return VOICE_COMMAND_NONE;
}

int voice_listen_once(char *out_text, size_t capacity) {
    if (!hda.available) {
        copy_string(out_text, capacity, "voice hda indisponible");
        return 0;
    }

    /*
     * The fast path is intentionally narrow: HDA MMIO is discovered now,
     * stream DMA + Pocketsphinx can replace this recognizer boundary without
     * changing Spotlight.
     */
    copy_string(out_text, capacity, "ecoute activee");
    return 0;
}
