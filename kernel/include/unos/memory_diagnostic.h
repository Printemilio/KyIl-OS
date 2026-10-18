#ifndef UNOS_MEMORY_DIAGNOSTIC_H
#define UNOS_MEMORY_DIAGNOSTIC_H
#include <stdint.h>
#ifndef VEX_BROWSER_MEMORY_DIAGNOSTIC
#define VEX_BROWSER_MEMORY_DIAGNOSTIC 0
#endif
/* Read-only, bounded diagnostics. Not part of a userspace ABI. */
void storage_memory_diagnostic(void);
void surface_memory_diagnostic(void);
void process_memory_diagnostic(void);
void vmm_memory_diagnostic(void *pml4);
uint64_t vmm_resident_pages(void *pml4, uint64_t start, uint64_t end);
struct process;
void process_memory_resident_diagnostic(struct process *process);
#endif
