# VexOS GPU and Vulkan architecture

## Current runtime

VexOS selects one GPU driver during `gpu_init()`. Drivers are registered
through `gpu_driver_register()` and are accepted only when:

1. the driver ABI version matches;
2. all mandatory resource and submission operations are present;
3. `probe()` recognizes an available device;
4. `start()` successfully creates a usable device and queues.

Recognizing a PCI vendor or device ID is not sufficient. This prevents VexOS
from reporting Vulkan when command submission, synchronization, or scanout is
not actually operational.

VirtualBox currently selects `software-framebuffer`. It owns GPU buffer
objects, performs composition synchronously, and copies damaged regions to the
Limine framebuffer. It is the compatibility driver, not a Vulkan driver.

## Driver boundary

`gpu_driver_ops_t` is the only hardware-specific boundary. A hardware driver
must implement:

- device probing and atomic startup;
- resource allocation and destruction;
- optional CPU mappings;
- clear, fill, upload and composition submission;
- damaged presentation or scanout.

Resources are opaque `gpu_driver_resource_t` objects. Device-local or tiled
images do not need a CPU mapping or a linear stride. CPU-visible buffers must
provide both.

The compositor scene itself is a driver resource. Window surfaces are
composited by calling the active driver's `buffer_composite()` operation.
Consequently, switching to Vulkan does not change window ownership, damage
tracking, application surfaces, LVGL, or the userspace surface ABI.

## Capabilities and compatibility

The original `vex_gpu_info_t` structure remains unchanged for binary
compatibility. `VEX_SYS_GPU_QUERY_CAPS` returns the versioned
`vex_gpu_caps_t` extension:

- selected driver name and backend;
- CPU mapping and scanout support;
- accelerated composition, filtering and transforms;
- device-local memory and asynchronous submission;
- timeline fences;
- Vulkan 1.3 support.

Applications must test capability bits instead of assuming that a backend
number implies a working feature.

## Hardware Vulkan acceptance criteria

A driver may advertise `VEX_GPU_BACKEND_VULKAN_PATH` and
`VEX_GPU_CAP_VULKAN_1_3` only after all of the following succeed:

1. device and queue creation;
2. render-target and sampled-image allocation;
3. host-visible upload;
4. graphics or compute composition submission;
5. fence completion;
6. presentation of a damaged image;
7. resource teardown after process exit.

The first hardware transport can be VMSVGA/SVGA3D, virtio-gpu/Venus, or a
native PCI driver. All use the same resource and compositor contracts.

## Migration state

- Complete: persistent compositor surfaces and ownership.
- Complete: driver-owned compositor render target.
- Complete: driver-dispatched composition and presentation.
- Complete: versioned capability query.
- Complete: software compatibility driver for VirtualBox.
- Pending: a hardware command transport.
- Pending: Vulkan device/queue/image implementation over that transport.
- Pending: asynchronous fences and device-local LVGL staging.
- Pending: removal of the compatibility driver from hardware-capable builds.
