#include <unos/memory_diagnostic.h>
#include <unos/kernel_allocator.h>
#include <unos/ora.h>
void storage_memory_diagnostic(void) {
    kernel_allocator_memory_diagnostic();
    ora_memory_diagnostic();
}
