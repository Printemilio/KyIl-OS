#ifndef VEX_RENDER_CORE_H
#define VEX_RENDER_CORE_H
#include "vex_glsl.h"
#include <vulkan/vulkan_core.h>
/* Internal common-core interface. No Vulkan entry points, handles or queues.
 * Frontends retain resources and serialize access before submitting a draw. */
typedef struct vex_render_program vex_render_program;
typedef struct vex_render_target { uint32_t *pixels;uint32_t width,height;void *depth_stencil;uint32_t format;uint32_t top_left; } vex_render_target;
typedef struct vex_render_texture { const uint32_t *pixels;uint32_t width,height;VkFilter mag,min;VkSamplerAddressMode u,v; } vex_render_texture;
typedef struct vex_render_state {
    VkViewport viewport;VkRect2D scissor;VkPrimitiveTopology topology;
    VkCullModeFlags cull;VkFrontFace front;
    uint32_t depth_test,depth_write;VkCompareOp depth_compare;
    uint32_t stencil_test;VkStencilOpState stencil[2];
    VkPipelineColorBlendAttachmentState blend;float blend_constants[4];
    uint32_t dither,polygon_offset;float offset_factor,offset_units;
} vex_render_state;
typedef struct vex_render_uniform { const char *name;uint32_t components,columns,count,kind; } vex_render_uniform;
vex_render_program *vex_render_program_create(const vex_glsl_binary code[2],char *log,size_t capacity);
void vex_render_program_destroy(vex_render_program *program);
/* Recognise the Skia fragment programs that have a native implementation. */
void vex_render_program_classify(vex_render_program *program,const char *fragment_source);
void vex_render_set_fast_paths(int enabled);
int vex_render_attribute(vex_render_program *program,const char *name);
size_t vex_render_attribute_count(vex_render_program *program);
const char *vex_render_attribute_info(vex_render_program *program,size_t index,uint32_t *components);
int vex_render_uniform_location(vex_render_program *program,const char *name);
size_t vex_render_uniform_count(vex_render_program *program);
const vex_render_uniform *vex_render_uniform_info(vex_render_program *program,size_t index);
const vex_render_uniform *vex_render_uniform_at_location(vex_render_program *program,int location);
int vex_render_uniform_set(vex_render_program *program,int location,const uint32_t *words,uint32_t count);
const uint32_t *vex_render_uniform_get(vex_render_program *program,int location);
int vex_render_draw(vex_render_program *program,const vex_render_state *state,
                    vex_render_target *target,const float *vertices,uint32_t count,
                    const vex_render_texture textures[16]);
#endif
