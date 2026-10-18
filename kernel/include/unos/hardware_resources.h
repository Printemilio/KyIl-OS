#ifndef KYIL_HARDWARE_RESOURCES_H
#define KYIL_HARDWARE_RESOURCES_H
#include <stddef.h>
#include <stdint.h>
enum {HW_PIO=1,HW_MMIO=2,HW_IRQ=3,HW_PCI=4};
/* Exclusive host leases. Owner tokens are kernel-local, never archive IDs. */
int hardware_claim(unsigned kind,uint64_t base,uint64_t size,uint64_t owner);
int hardware_claim_shared_irq(uint8_t irq,uint64_t owner);
void hardware_release_owner(uint64_t owner);
int hardware_release(unsigned kind,uint64_t base,uint64_t size,uint64_t owner);
/* Probe BAR only while a caller owns the BDF and decoding/BME are disabled.
 * Every config register is restored before return, under the PCI transaction. */
int hardware_pci_bar(uint8_t bus,uint8_t slot,uint8_t function,unsigned bar,uint64_t owner,uint64_t *base,uint64_t *size);
int hardware_native_pci(uint8_t bus,uint8_t device,uint8_t function);
int hardware_native_mmio(uint64_t base,uint64_t size);
uint32_t hardware_pci_read32(uint8_t bus,uint8_t device,uint8_t function,uint8_t offset);
void hardware_pci_write32(uint8_t bus,uint8_t device,uint8_t function,uint8_t offset,uint32_t value);
void hardware_pci_write_width(uint8_t bus,uint8_t device,uint8_t function,uint8_t offset,unsigned width,uint32_t value);
typedef struct hardware_pci_info {uint8_t bus,slot,function;uint16_t vendor,device;uint32_t class_code;} hardware_pci_info_t;
void hardware_pci_enumerate(void (*visit)(const hardware_pci_info_t *,void *),void *);
#endif
