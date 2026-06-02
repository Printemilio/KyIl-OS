#ifndef UNOS_BOOT_REQUESTS_H
#define UNOS_BOOT_REQUESTS_H

#include <limine.h>
#include <stdint.h>

extern volatile uint64_t limine_base_revision[];
extern volatile struct limine_framebuffer_request framebuffer_request;
extern volatile struct limine_memmap_request memmap_request;
extern volatile struct limine_hhdm_request hhdm_request;
extern volatile struct limine_executable_address_request executable_address_request;
extern volatile struct limine_module_request module_request;

#endif
