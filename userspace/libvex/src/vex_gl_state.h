#ifndef VEX_GL_STATE_H
#define VEX_GL_STATE_H
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include "vex_render_core.h"
#include <pthread.h>
#include "vex_egl_backend.h"
typedef struct gl_object gl_object;
typedef struct egl_surface egl_surface;
typedef struct egl_context egl_context;
typedef struct gl_share gl_share;
typedef struct gl_attribute { gl_object *buffer;const void *pointer;GLint size;GLenum type;GLsizei stride;GLboolean normalized,enabled;GLfloat value[4]; } gl_attribute;
struct gl_share { unsigned refs;GLuint next_name;gl_object *objects; };
struct gl_object {
    gl_object *next;gl_share *group;GLuint name;unsigned refs;int deleted,instantiated;unsigned kind;
    union {
        struct { uint8_t *bytes;size_t size;GLenum usage; } buffer;
        struct { uint32_t *pixels;GLsizei width,height;GLenum format,min,mag,u,v; } texture;
        struct { GLenum type;char *source;int compiled;char log[4096]; } shader;
        struct { gl_object *shader[2];vex_render_program *executable;int linked,validated;char log[4096];vex_glsl_binding attributes[8];char names[8][128];unsigned attribute_count; } program;
        struct { void *pixels;GLsizei width,height;GLenum format; } renderbuffer;
        struct { gl_object *color,*depth,*stencil; } framebuffer;
    } u;
};
struct egl_surface {
    egl_surface *next;uintptr_t token;unsigned refs;int deleted,bound;pthread_t owner;
    vex_render_target target;void *window;unsigned native;uint64_t capability,mapped,length,queue,serial;
};
struct egl_context {
    egl_context *next;uintptr_t token;unsigned refs;int deleted,bound;pthread_t owner;gl_share *share;
    GLenum error;gl_object *array_buffer,*elements,*texture[16],*program,*framebuffer,*renderbuffer;
    unsigned active_texture;gl_attribute attrib[8];vex_render_state state;
    GLfloat clear_color[4],clear_depth;GLint clear_stencil,pack,unpack;GLboolean scissor,depth,stencil,blend,cull;
    GLboolean color_mask[4];GLuint stencil_mask[2];GLenum cull_face,front_face;
    GLboolean dither,polygon_offset,sample_coverage,sample_alpha_coverage,sample_invert;
    GLfloat sample_value;GLenum mipmap_hint;
};
extern pthread_mutex_t vex_gl_lock;
extern _Thread_local egl_context *vex_gl_current;
extern _Thread_local egl_surface *vex_gl_draw_surface,*vex_gl_read_surface;
void vex_gl_context_initialize(egl_context *context);
void vex_gl_context_cleanup(egl_context *context);
void vex_gl_error_at(GLenum error,const char *function,void *caller);
#define vex_gl_error(error) vex_gl_error_at((error),__func__,__builtin_return_address(0))
void vex_gl_finish(void);
int vex_egl_refresh_surface(egl_surface *surface);
void vex_gl_object_release(gl_object *object);
void vex_gl_memory_diagnostic(egl_context *context,uint64_t frame);
#endif
