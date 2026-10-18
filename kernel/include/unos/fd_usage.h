#ifndef UNOS_FD_USAGE_H
#define UNOS_FD_USAGE_H

#include <stdint.h>

/* Open-descriptor count per resource-owning process, for leak diagnostics. */
typedef struct vex_fd_usage {
    uint32_t pid;
    uint32_t open_descriptors;
} vex_fd_usage_t;

/* Copy up to `capacity` entries (processes that own a descriptor table).
 * Returns the number copied. */
unsigned vex_fd_usage_snapshot(vex_fd_usage_t *out, unsigned capacity);

#endif
