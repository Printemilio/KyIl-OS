# VexOS compositor GPU surface API

The compositor surface API is the ownership boundary between rendering
backends and the desktop compositor. Applications render into GPU buffers;
the compositor imports those buffers as surfaces and remains the only
component allowed to place them in the desktop scene.

## Object model

- A surface has a generation-tagged 32-bit handle. Reusing a freed slot never
  makes an old handle valid again.
- A surface owns or retains one backend buffer.
- The logical size is separate from allocation size. Resizing can reuse a
  larger allocation and grows allocations geometrically to avoid reallocating
  during repeated window resizing.
- Mappings are explicit and access-checked. Protected surfaces cannot be CPU
  mapped.
- Imported buffers are reference-counted. Destroying the application's buffer
  handle does not invalidate a surface that the compositor is still reading.
- Process teardown reaps window surfaces, compositor surfaces, then GPU
  buffers in dependency order.

## Presentation

`compositor_surface_present()` is a commit operation, not a direct scanout.
It:

1. waits for the requested input serial;
2. clips and coalesces damage rectangles;
3. assigns a monotonically increasing submission serial;
4. exposes the committed image to the compositor.

The software backend completes synchronously. A future Vulkan backend can
advance `completed_serial` from a GPU fence callback without changing the
public API.

Damage is bounded to 16 rectangles. Touching rectangles are merged. Overflow
collapses to one full-surface rectangle, keeping memory and composition cost
predictable.

## Composition

`compositor_surface_compose()` accepts a source rectangle, destination
rectangle, opacity, filter and transform. The current backend supports:

- XRGB8888 opaque surfaces;
- ARGB8888 straight or premultiplied alpha for compositor-owned buffers;
- identity transform;
- nearest-neighbour scaling.

An opaque, unscaled layer uses a row-copy fast path. Other supported layers
use clipped per-pixel blending. Linear filtering and rotations are represented
in the API but rejected until a backend advertises and implements them.

## Userspace ABI

The persistent userspace path is:

1. `VEX_SYS_GPU_BUFFER_CREATE`
2. `VEX_SYS_GPU_SURFACE_CREATE`
3. render and `VEX_SYS_GPU_QUEUE_SUBMIT`
4. `VEX_SYS_GPU_SURFACE_PRESENT`
5. `VEX_SYS_GPU_SURFACE_DESTROY`
6. `VEX_SYS_GPU_BUFFER_DESTROY`

`VEX_SYS_GPU_PRESENT` remains as a compatibility entry point, but it now
creates a transient imported surface and routes through the compositor. It no
longer writes directly to the framebuffer.

The framebuffer address is never exposed to userspace.

## Vulkan migration

The compositor API deliberately does not expose Vulkan objects. A Vulkan
backend plugs into `gpu_driver_ops_t`; it does not replace the surface API:

- buffers are opaque driver resources and may be device-local;
- composition is submitted through the selected driver;
- the compositor render target is itself a driver resource;
- capabilities are queried through `VEX_SYS_GPU_QUERY_CAPS`;
- completion serials can move to hardware fence callbacks.

Window management, damage semantics, handles and the userspace ABI remain
stable across that migration.
