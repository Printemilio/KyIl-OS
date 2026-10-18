#ifndef UNOS_KERNEL_VIRTUAL_LAYOUT_H
#define UNOS_KERNEL_VIRTUAL_LAYOUT_H
/* Fixed privileged VA windows; none is part of a userspace ABI. Audio MMIO
 * formerly overlapped the first raw allocator chunks at c0000000. */
#define VEX_KERNEL_AUDIO_MMIO_BASE 0xffffffffc0000000ull
#define VEX_KERNEL_AUDIO_MMIO_LIMIT 0xffffffffc1000000ull
/* One dedicated upper-half PML4 branch for raw heap VA, separated from
 * the HHDM, kernel image and MMIO. Reservation alone allocates no RAM.
 * The former 752 MiB tail window exhausted VA with >3 GiB RAM still free. */
#define VEX_KERNEL_ALLOC_BASE 0xffffc00000000000ull
#define VEX_KERNEL_ALLOC_LIMIT 0xffffc08000000000ull
#endif
