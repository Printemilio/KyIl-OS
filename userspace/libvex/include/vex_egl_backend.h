#ifndef VEX_EGL_BACKEND_H
#define VEX_EGL_BACKEND_H
#include <stdint.h>
/* Private, versioned runtime/backend interface, not a native syscall ABI.
 * A present callback retains/consumes the Surface capability using Vex IPC.
 * It must not return until the consumer has released this version: EGL may
 * write the same backing again after eglSwapBuffers returns. */
#define VEX_EGL_WINDOW_MAGIC UINT64_C(0x5645474c57494e31)
typedef struct vex_egl_window {
    uint64_t magic;
    uint32_t width,height;
    int (*present)(void *owner,uint64_t capability,uint64_t serial);
    void *owner;
    /* Optional paired callbacks. The EGL surface owns a reference even if
     * the WPE target is destroyed first. Stack descriptors omit both. */
    void (*retain)(void *owner);
    void (*release)(void *owner);
} vex_egl_window;
#endif
