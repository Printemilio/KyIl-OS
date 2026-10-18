#ifndef KYIL_FILE_RANGE_ABI_H
#define KYIL_FILE_RANGE_ABI_H
#include <stdint.h>
#define VEX_SYS_FILE_RANGE 230ull
#define VEX_FILE_RANGE_RELEASE 0u
#define VEX_FILE_RANGE_READ 1u
#define VEX_FILE_RANGE_WRITE 2u
#define VEX_FILE_RANGE_WAIT 1u
#define VEX_FILE_RANGE_DESCRIPTION 2u
/* DESCRIPTION selects mandatory exact ranges owned by (Process, open file
 * description). Dup shares ownership within a Process; inherited/transferred
 * descriptions in another Process do not bypass locks. Final local descriptor
 * close or Process teardown releases its locks. Mapped views are independent.
 * Without this flag the original advisory group/split contract is unchanged. */
/* Advisory range ownership belongs to the Process resource group, not a TID.
 * A zero length means through EOF. WAIT sleeps on the existing resource wait
 * queue; no user pointers or memory transactions survive that wait. Closing
 * a descriptor of this file releases the group's ranges (including duplicates).
 * This is NOT a Windows per-handle/mandatory file locking contract. */
typedef struct {uint32_t size,mode;int32_t fd;uint32_t flags;uint64_t offset,length;} vex_file_range_req_t;
#endif
