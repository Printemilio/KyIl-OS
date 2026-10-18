#ifndef VEX_RAYLIB_CANVAS_CACHE_H
#define VEX_RAYLIB_CANVAS_CACHE_H
/* Private userspace renderer interface, UI thread only. No kernel handles.
 * Capture is transactional: failure returns NULL and preserves old_cache.
 * Restore replaces exact pixels only for a fully visible 2D rectangle.
 * No depth/partial clipping support: return false for a normal redraw fallback. */
void *vex_raylib_canvas_capture(void *old_cache, int x, int y, int w, int h);
int vex_raylib_canvas_restore(void *cache, int x, int y, int w, int h);
void vex_raylib_canvas_drop(void *cache);
#endif
