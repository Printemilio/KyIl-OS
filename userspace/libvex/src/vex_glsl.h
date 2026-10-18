#ifndef VEX_GLSL_H
#define VEX_GLSL_H
#include <stddef.h>
#include <stdint.h>
/* Private frontend/compiler contract, not a native shader ABI. */
typedef struct vex_glsl_binary { uint32_t *words; size_t bytes; } vex_glsl_binary;
typedef struct vex_glsl_binding { const char *name; uint32_t location; } vex_glsl_binding;
#ifdef __cplusplus
extern "C" {
#endif
int vex_glsl_validate(const char *source, int fragment, char *log, size_t capacity);
int vex_glsl_link(const char *vertex, const char *fragment,
                  const vex_glsl_binding *bindings, size_t count,
                  vex_glsl_binary out[2], char *log, size_t capacity);
void vex_glsl_binary_free(vex_glsl_binary *binary);
void vex_glsl_release(void);
#ifdef __cplusplus
}
#endif
#endif
