#ifndef KYIL_DRIVER_PCI_H
#define KYIL_DRIVER_PCI_H
#include <unos/driver_dma.h>
#include <unos/hardware_resources.h>
typedef struct drv_pci_condition {uint32_t offset,mask,value;} drv_pci_condition_t;
typedef struct drv_pci_contract {
    uint16_t vendor,device;
    uint32_t class_code,bar,bar_min,bar_max,reset_offset,reset_mask;
    unsigned quiet_count;
    uint32_t reset_settle_ns;
    drv_pci_condition_t quiet[4];
} drv_pci_contract_t;
typedef struct drv_pci {
    hardware_pci_info_t info;
    drv_pci_contract_t contract;
    drv_dma_policy_t policy;
    uint64_t owner,physical,length,virtual_base;
    volatile uint8_t *mmio;
    unsigned irq,claimed,mapped,reset_generation,reset_required,dma_started,window_slot;
    uint32_t last_offset,last_value;uint64_t reads,writes;
} drv_pci_t;
/* Authority tables contain layouts/masks, never initialization or packet code. */
int driver_pci_contract(uint16_t,uint16_t,drv_pci_contract_t *,drv_dma_policy_t *);
int driver_pci_open(drv_pci_t *,uint64_t,const drv_manifest_t *);
drv_dma_provider_t driver_pci_provider(drv_pci_t *);
void driver_pci_prepare_stop(drv_pci_t *);
int driver_pci_disable(drv_pci_t *);
int driver_pci_enable_dma(drv_pci_t *);
int driver_pci_close(drv_pci_t *);
#endif
