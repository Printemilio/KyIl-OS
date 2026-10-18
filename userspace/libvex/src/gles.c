#include "vex_gl_state.h"
#include <GLES2/gl2ext.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <limits.h>
#ifdef VEX_HOSTED_BUILD
#include <malloc.h>
#endif
enum {OBJ_BUFFER=1,OBJ_TEXTURE,OBJ_SHADER,OBJ_PROGRAM,OBJ_RENDERBUFFER,OBJ_FRAMEBUFFER};
/* Bounded diagnostic: includes calls without a current context, which would
 * otherwise disappear before reaching resource/shader validation. */
static void trace_call(const char *name,void *caller){static unsigned reports;static int enabled=-1;if(enabled<0)enabled=getenv("VEX_GL_CALL_TRACE")!=NULL;if(enabled && reports++<384)fprintf(stderr,"Vex GL call: %s context=%lu thread=%lu caller=%p\n",name,vex_gl_current?(unsigned long)vex_gl_current->token:0,(unsigned long)pthread_self(),caller);}
#define BEGIN_VOID pthread_mutex_lock(&vex_gl_lock);trace_call(__func__,__builtin_return_address(0));egl_context *c=vex_gl_current;if(!c){pthread_mutex_unlock(&vex_gl_lock);return;}
#define BEGIN_RET(r) pthread_mutex_lock(&vex_gl_lock);trace_call(__func__,__builtin_return_address(0));egl_context *c=vex_gl_current;if(!c){pthread_mutex_unlock(&vex_gl_lock);return (r);}
#define END pthread_mutex_unlock(&vex_gl_lock)
void vex_gl_error_at(GLenum e,const char *function,void *caller){static unsigned reports;if(vex_gl_current&&!vex_gl_current->error)vex_gl_current->error=e;if(reports++<64)fprintf(stderr,"Vex GLES: function=%s error=%x caller=%p\n",function,e,caller);}
void vex_gl_finish(void){__atomic_thread_fence(__ATOMIC_SEQ_CST);}
void vex_gl_memory_diagnostic(egl_context *c,uint64_t frame){
    if(!getenv("VEX_MEMORY_DIAGNOSTIC"))return;
    size_t textures=0,buffers=0,renderbuffers=0;unsigned objects=0,programs=0;
    for(gl_object *o=c->share->objects;o;o=o->next){++objects;
        if(o->kind==OBJ_TEXTURE && o->u.texture.pixels)textures+=(size_t)o->u.texture.width*o->u.texture.height*4;
        if(o->kind==OBJ_BUFFER)buffers+=o->u.buffer.size;
        if(o->kind==OBJ_RENDERBUFFER && o->u.renderbuffer.pixels)renderbuffers+=(size_t)o->u.renderbuffer.width*o->u.renderbuffer.height*8;
        if(o->kind==OBJ_PROGRAM)++programs;
    }
    fprintf(stderr,"Vex GL memory: frame=%llu objects=%u programs=%u texture_bytes=%zu buffer_bytes=%zu renderbuffer_bytes_upper=%zu\n",
        (unsigned long long)frame,objects,programs,textures,buffers,renderbuffers);
#ifdef VEX_HOSTED_BUILD
    struct mallinfo2 m=mallinfo2();
    fprintf(stderr,"Vex libc memory: frame=%llu arena=%zu live=%zu reusable=%zu mmap=%zu chunks=%zu\n",
        (unsigned long long)frame,m.arena,m.uordblks,m.fordblks,m.hblkhd,m.hblks);
#endif
}
static void retain(gl_object *o){if(o)++o->refs;}
static gl_object *lookup(gl_share *g,GLuint n,unsigned kind){for(gl_object *o=g->objects;o;o=o->next)if(o->name==n&&(!o->deleted||kind==OBJ_SHADER||kind==OBJ_PROGRAM)&&o->kind==kind)return o;return NULL;}
static gl_object *allocate(gl_share *g,unsigned kind){if(!g->next_name)return NULL;gl_object *o=calloc(1,sizeof(*o));if(!o)return NULL;o->refs=1;o->name=g->next_name++;o->kind=kind;o->group=g;o->next=g->objects;g->objects=o;
    if(kind==OBJ_TEXTURE){o->u.texture.min=GL_NEAREST_MIPMAP_LINEAR;o->u.texture.mag=GL_LINEAR;o->u.texture.u=o->u.texture.v=GL_REPEAT;}return o;}
void vex_gl_object_release(gl_object *o){if(!o||--o->refs)return;gl_object **p=&o->group->objects;while(*p&&*p!=o)p=&(*p)->next;if(*p)*p=o->next;
    switch(o->kind){case OBJ_BUFFER:free(o->u.buffer.bytes);break;case OBJ_TEXTURE:free(o->u.texture.pixels);break;case OBJ_SHADER:free(o->u.shader.source);break;
    case OBJ_PROGRAM:vex_render_program_destroy(o->u.program.executable);vex_gl_object_release(o->u.program.shader[0]);vex_gl_object_release(o->u.program.shader[1]);break;
    case OBJ_RENDERBUFFER:free(o->u.renderbuffer.pixels);break;case OBJ_FRAMEBUFFER:vex_gl_object_release(o->u.framebuffer.color);vex_gl_object_release(o->u.framebuffer.depth);vex_gl_object_release(o->u.framebuffer.stencil);break;}free(o);}
static void binding(gl_object **slot,gl_object *o){retain(o);vex_gl_object_release(*slot);*slot=o;}
void vex_gl_context_initialize(egl_context *c){c->pack=c->unpack=4;c->dither=1;c->sample_value=1;c->mipmap_hint=GL_DONT_CARE;c->clear_depth=1;c->state.depth_write=1;c->state.depth_compare=VK_COMPARE_OP_LESS;c->cull_face=GL_BACK;c->front_face=GL_CCW;
    c->state.blend.srcColorBlendFactor=c->state.blend.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;c->state.blend.colorWriteMask=15;
    for(unsigned i=0;i<2;++i){c->state.stencil[i].compareOp=VK_COMPARE_OP_ALWAYS;c->state.stencil[i].compareMask=c->state.stencil[i].writeMask=UINT32_MAX;c->stencil_mask[i]=UINT32_MAX;}
    for(unsigned i=0;i<4;++i)c->color_mask[i]=1;for(unsigned i=0;i<8;++i)c->attrib[i].value[3]=1;}
void vex_gl_context_cleanup(egl_context *c){vex_gl_object_release(c->array_buffer);vex_gl_object_release(c->elements);vex_gl_object_release(c->program);vex_gl_object_release(c->framebuffer);vex_gl_object_release(c->renderbuffer);
    for(unsigned i=0;i<8;++i)vex_gl_object_release(c->attrib[i].buffer);for(unsigned i=0;i<16;++i)vex_gl_object_release(c->texture[i]);
    if(--c->share->refs==0){for(;;){gl_object *o=c->share->objects;while(o&&o->deleted)o=o->next;if(!o)break;o->deleted=1;vex_gl_object_release(o);}free(c->share);}}
GLenum glGetError(void){BEGIN_RET(GL_NO_ERROR);GLenum e=c->error;c->error=0;END;return e;}
const GLubyte *glGetString(GLenum n){BEGIN_RET(NULL);const char *s=NULL;switch(n){case GL_VENDOR:s="Vex";break;case GL_RENDERER:s="Vex shared programmable CPU core";break;case GL_VERSION:s="OpenGL ES 2.0 Vex experimental incomplete";break;case GL_SHADING_LANGUAGE_VERSION:s="OpenGL ES GLSL ES 1.00";break;case GL_EXTENSIONS:s="GL_OES_rgb8_rgba8";break;default:vex_gl_error(GL_INVALID_ENUM);}END;return (const GLubyte*)s;}
static unsigned object_name(gl_object *o){return o?o->name:0;}
static GLint framebuffer_color_bits(egl_context *c,GLenum channel){
    gl_object *o=c->framebuffer?c->framebuffer->u.framebuffer.color:NULL;
    if(!o)return 8;
    if(o->kind==OBJ_TEXTURE){GLenum f=o->u.texture.format;return channel==GL_ALPHA_BITS?(f==GL_RGB||f==GL_LUMINANCE?0:8):f==GL_ALPHA?0:8;}
    GLenum f=o->u.renderbuffer.format;
    if(channel==GL_ALPHA_BITS)return f==GL_RGBA8_OES?8:f==GL_RGBA4?4:f==GL_RGB5_A1?1:0;
    return f==GL_RGB8_OES||f==GL_RGBA8_OES?8:f==GL_RGBA4?4:f==GL_RGB565?(channel==GL_GREEN_BITS?6:5):f==GL_RGB5_A1?5:0;
}
void glGetIntegerv(GLenum n,GLint *out){BEGIN_VOID;if(!out){vex_gl_error(GL_INVALID_VALUE);END;return;}switch(n){
    case GL_ARRAY_BUFFER_BINDING:*out=object_name(c->array_buffer);break;case GL_ELEMENT_ARRAY_BUFFER_BINDING:*out=object_name(c->elements);break;
    case GL_TEXTURE_BINDING_2D:*out=object_name(c->texture[c->active_texture]);break;case GL_ACTIVE_TEXTURE:*out=GL_TEXTURE0+c->active_texture;break;
    case GL_CURRENT_PROGRAM:*out=object_name(c->program);break;case GL_FRAMEBUFFER_BINDING:*out=object_name(c->framebuffer);break;case GL_RENDERBUFFER_BINDING:*out=object_name(c->renderbuffer);break;
    case GL_PACK_ALIGNMENT:*out=c->pack;break;case GL_UNPACK_ALIGNMENT:*out=c->unpack;break;case GL_VIEWPORT:out[0]=c->state.viewport.x;out[1]=c->state.viewport.y;out[2]=c->state.viewport.width;out[3]=c->state.viewport.height;break;
    case GL_MAX_VERTEX_ATTRIBS:*out=8;break;case GL_MAX_TEXTURE_SIZE:case GL_MAX_RENDERBUFFER_SIZE:*out=4096;break;
    case GL_MAX_TEXTURE_IMAGE_UNITS:case GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS:case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:*out=16;break;
    case GL_MAX_VERTEX_UNIFORM_VECTORS:case GL_MAX_FRAGMENT_UNIFORM_VECTORS:*out=128;break;case GL_MAX_VARYING_VECTORS:*out=8;break;
    case GL_MAX_VIEWPORT_DIMS:out[0]=out[1]=4096;break;case GL_NUM_COMPRESSED_TEXTURE_FORMATS:*out=0;break;
    case GL_GENERATE_MIPMAP_HINT:*out=c->mipmap_hint;break;case GL_DITHER:*out=c->dither;break;case GL_POLYGON_OFFSET_FILL:*out=c->polygon_offset;break;case GL_SAMPLE_COVERAGE:*out=c->sample_coverage;break;case GL_SAMPLE_ALPHA_TO_COVERAGE:*out=c->sample_alpha_coverage;break;case GL_SAMPLE_COVERAGE_INVERT:*out=c->sample_invert;break;case GL_NUM_SHADER_BINARY_FORMATS:*out=0;break;case GL_IMPLEMENTATION_COLOR_READ_FORMAT:*out=GL_RGBA;break;case GL_IMPLEMENTATION_COLOR_READ_TYPE:*out=GL_UNSIGNED_BYTE;break;
    case GL_DEPTH_BITS:*out=32;break;case GL_STENCIL_BITS:*out=8;break;case GL_RED_BITS:case GL_GREEN_BITS:case GL_BLUE_BITS:case GL_ALPHA_BITS:*out=framebuffer_color_bits(c,n);break;
    case GL_SAMPLE_BUFFERS:*out=0;break;case GL_SAMPLES:*out=1;break;case GL_BLEND:*out=c->blend;break;case GL_DEPTH_TEST:*out=c->depth;break;case GL_STENCIL_TEST:*out=c->stencil;break;case GL_SCISSOR_TEST:*out=c->scissor;break;
    default:vex_gl_error(GL_INVALID_ENUM);}END;}
void glGetFloatv(GLenum n,GLfloat *out){BEGIN_VOID;if(!out)vex_gl_error(GL_INVALID_VALUE);else if(n==GL_COLOR_CLEAR_VALUE)memcpy(out,c->clear_color,16);else if(n==GL_SAMPLE_COVERAGE_VALUE)*out=c->sample_value;else if(n==GL_POLYGON_OFFSET_FACTOR)*out=c->state.offset_factor;else if(n==GL_POLYGON_OFFSET_UNITS)*out=c->state.offset_units;else if(n==GL_DEPTH_CLEAR_VALUE)*out=c->clear_depth;else if(n==GL_DEPTH_RANGE){out[0]=c->state.viewport.minDepth;out[1]=c->state.viewport.maxDepth;}else if(n==GL_ALIASED_LINE_WIDTH_RANGE||n==GL_ALIASED_POINT_SIZE_RANGE)out[0]=out[1]=1;else vex_gl_error(GL_INVALID_ENUM);END;}
void glGetBooleanv(GLenum n,GLboolean *out){BEGIN_VOID;if(!out)vex_gl_error(GL_INVALID_VALUE);else if(n==GL_COLOR_WRITEMASK)memcpy(out,c->color_mask,4);else if(n==GL_DEPTH_WRITEMASK)*out=c->state.depth_write;else if(n==GL_SHADER_COMPILER)*out=1;else vex_gl_error(GL_INVALID_ENUM);END;}
static GLboolean *enable_slot(egl_context *c,GLenum cap){switch(cap){case GL_SCISSOR_TEST:return &c->scissor;case GL_BLEND:return &c->blend;case GL_DEPTH_TEST:return &c->depth;case GL_STENCIL_TEST:return &c->stencil;case GL_CULL_FACE:return &c->cull;case GL_DITHER:return &c->dither;case GL_POLYGON_OFFSET_FILL:return &c->polygon_offset;case GL_SAMPLE_COVERAGE:return &c->sample_coverage;case GL_SAMPLE_ALPHA_TO_COVERAGE:return &c->sample_alpha_coverage;default:return NULL;}}
void glEnable(GLenum cap){BEGIN_VOID;GLboolean *p=enable_slot(c,cap);if(p)*p=1;else vex_gl_error(GL_INVALID_ENUM);END;}
void glDisable(GLenum cap){BEGIN_VOID;GLboolean *p=enable_slot(c,cap);if(p)*p=0;else vex_gl_error(GL_INVALID_ENUM);END;}
GLboolean glIsEnabled(GLenum cap){BEGIN_RET(0);GLboolean *p=enable_slot(c,cap);GLboolean r=p?*p:0;if(!p)vex_gl_error(GL_INVALID_ENUM);END;return r;}
void glViewport(GLint x,GLint y,GLsizei w,GLsizei h){BEGIN_VOID;if(w<0||h<0)vex_gl_error(GL_INVALID_VALUE);else c->state.viewport=(VkViewport){x,y,w,h,c->state.viewport.minDepth,c->state.viewport.maxDepth};END;}
void glScissor(GLint x,GLint y,GLsizei w,GLsizei h){BEGIN_VOID;if(w<0||h<0)vex_gl_error(GL_INVALID_VALUE);else c->state.scissor=(VkRect2D){{x,y},{w,h}};END;}
static float clamp01(float x){return x<0?0:x>1?1:x;}
void glClearColor(GLfloat r,GLfloat g,GLfloat b,GLfloat a){BEGIN_VOID;float v[]={r,g,b,a};for(unsigned i=0;i<4;++i)c->clear_color[i]=clamp01(v[i]);END;}
void glClearDepthf(GLfloat d){BEGIN_VOID;c->clear_depth=clamp01(d);END;}
void glClearStencil(GLint v){BEGIN_VOID;c->clear_stencil=v;END;}
void glColorMask(GLboolean r,GLboolean g,GLboolean b,GLboolean a){BEGIN_VOID;c->color_mask[0]=!!r;c->color_mask[1]=!!g;c->color_mask[2]=!!b;c->color_mask[3]=!!a;END;}
void glDepthMask(GLboolean v){BEGIN_VOID;c->state.depth_write=!!v;END;}
void glDepthFunc(GLenum v){BEGIN_VOID;if(v<GL_NEVER||v>GL_ALWAYS)vex_gl_error(GL_INVALID_ENUM);else c->state.depth_compare=v-GL_NEVER;END;}
void glDepthRangef(GLfloat a,GLfloat b){BEGIN_VOID;c->state.viewport.minDepth=clamp01(a);c->state.viewport.maxDepth=clamp01(b);END;}
void glCullFace(GLenum f){BEGIN_VOID;if(f!=GL_FRONT&&f!=GL_BACK&&f!=GL_FRONT_AND_BACK)vex_gl_error(GL_INVALID_ENUM);else c->cull_face=f;END;}
void glFrontFace(GLenum f){BEGIN_VOID;if(f!=GL_CW&&f!=GL_CCW)vex_gl_error(GL_INVALID_ENUM);else c->front_face=f;END;}
static int blend_factor(GLenum f,VkBlendFactor *out){switch(f){case GL_ZERO:*out=VK_BLEND_FACTOR_ZERO;break;case GL_ONE:*out=VK_BLEND_FACTOR_ONE;break;case GL_SRC_COLOR:*out=VK_BLEND_FACTOR_SRC_COLOR;break;case GL_ONE_MINUS_SRC_COLOR:*out=VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;break;case GL_DST_COLOR:*out=VK_BLEND_FACTOR_DST_COLOR;break;case GL_ONE_MINUS_DST_COLOR:*out=VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;break;case GL_SRC_ALPHA:*out=VK_BLEND_FACTOR_SRC_ALPHA;break;case GL_ONE_MINUS_SRC_ALPHA:*out=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;break;case GL_DST_ALPHA:*out=VK_BLEND_FACTOR_DST_ALPHA;break;case GL_ONE_MINUS_DST_ALPHA:*out=VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;break;case GL_CONSTANT_COLOR:*out=VK_BLEND_FACTOR_CONSTANT_COLOR;break;case GL_ONE_MINUS_CONSTANT_COLOR:*out=VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;break;case GL_CONSTANT_ALPHA:*out=VK_BLEND_FACTOR_CONSTANT_ALPHA;break;case GL_ONE_MINUS_CONSTANT_ALPHA:*out=VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;break;default:return 0;}return 1;}
void glBlendFuncSeparate(GLenum sr,GLenum dr,GLenum sa,GLenum da){BEGIN_VOID;VkBlendFactor a,b,d,e;if(!blend_factor(sr,&a)||!blend_factor(dr,&b)||!blend_factor(sa,&d)||!blend_factor(da,&e))vex_gl_error(GL_INVALID_ENUM);else {c->state.blend.srcColorBlendFactor=a;c->state.blend.dstColorBlendFactor=b;c->state.blend.srcAlphaBlendFactor=d;c->state.blend.dstAlphaBlendFactor=e;}END;}
void glBlendFunc(GLenum s,GLenum d){glBlendFuncSeparate(s,d,s,d);}
void glBlendEquationSeparate(GLenum a,GLenum b){BEGIN_VOID;if((a!=GL_FUNC_ADD&&a!=GL_FUNC_SUBTRACT&&a!=GL_FUNC_REVERSE_SUBTRACT)||(b!=GL_FUNC_ADD&&b!=GL_FUNC_SUBTRACT&&b!=GL_FUNC_REVERSE_SUBTRACT))vex_gl_error(GL_INVALID_ENUM);else {c->state.blend.colorBlendOp=a==GL_FUNC_ADD?VK_BLEND_OP_ADD:a==GL_FUNC_SUBTRACT?VK_BLEND_OP_SUBTRACT:VK_BLEND_OP_REVERSE_SUBTRACT;c->state.blend.alphaBlendOp=b==GL_FUNC_ADD?VK_BLEND_OP_ADD:b==GL_FUNC_SUBTRACT?VK_BLEND_OP_SUBTRACT:VK_BLEND_OP_REVERSE_SUBTRACT;}END;}
void glBlendEquation(GLenum v){glBlendEquationSeparate(v,v);}
void glBlendColor(GLfloat r,GLfloat g,GLfloat b,GLfloat a){BEGIN_VOID;float v[]={r,g,b,a};for(unsigned i=0;i<4;++i)c->state.blend_constants[i]=clamp01(v[i]);END;}
static int face_range(GLenum f,unsigned *a,unsigned *b){if(f==GL_FRONT){*a=0;*b=1;}else if(f==GL_BACK){*a=1;*b=2;}else if(f==GL_FRONT_AND_BACK){*a=0;*b=2;}else return 0;return 1;}
void glStencilFuncSeparate(GLenum f,GLenum op,GLint ref,GLuint mask){BEGIN_VOID;unsigned a,b;if(!face_range(f,&a,&b)||op<GL_NEVER||op>GL_ALWAYS)vex_gl_error(GL_INVALID_ENUM);else for(unsigned i=a;i<b;++i){c->state.stencil[i].compareOp=op-GL_NEVER;c->state.stencil[i].reference=ref<0?0:ref>255?255:ref;c->state.stencil[i].compareMask=mask;}END;}
void glStencilFunc(GLenum op,GLint ref,GLuint mask){glStencilFuncSeparate(GL_FRONT_AND_BACK,op,ref,mask);}
static int stencil_op(GLenum op,VkStencilOp *v){switch(op){case GL_KEEP:*v=VK_STENCIL_OP_KEEP;break;case GL_ZERO:*v=VK_STENCIL_OP_ZERO;break;case GL_REPLACE:*v=VK_STENCIL_OP_REPLACE;break;case GL_INCR:*v=VK_STENCIL_OP_INCREMENT_AND_CLAMP;break;case GL_DECR:*v=VK_STENCIL_OP_DECREMENT_AND_CLAMP;break;case GL_INVERT:*v=VK_STENCIL_OP_INVERT;break;case GL_INCR_WRAP:*v=VK_STENCIL_OP_INCREMENT_AND_WRAP;break;case GL_DECR_WRAP:*v=VK_STENCIL_OP_DECREMENT_AND_WRAP;break;default:return 0;}return 1;}
void glStencilOpSeparate(GLenum face,GLenum fail,GLenum zfail,GLenum pass){BEGIN_VOID;unsigned a,b;VkStencilOp f,z,p;if(!face_range(face,&a,&b)||!stencil_op(fail,&f)||!stencil_op(zfail,&z)||!stencil_op(pass,&p))vex_gl_error(GL_INVALID_ENUM);else for(unsigned i=a;i<b;++i){c->state.stencil[i].failOp=f;c->state.stencil[i].depthFailOp=z;c->state.stencil[i].passOp=p;}END;}
void glStencilOp(GLenum fail,GLenum zfail,GLenum pass){glStencilOpSeparate(GL_FRONT_AND_BACK,fail,zfail,pass);}
void glStencilMaskSeparate(GLenum f,GLuint m){BEGIN_VOID;unsigned a,b;if(!face_range(f,&a,&b))vex_gl_error(GL_INVALID_ENUM);else for(unsigned i=a;i<b;++i)c->state.stencil[i].writeMask=c->stencil_mask[i]=m;END;}
void glStencilMask(GLuint m){glStencilMaskSeparate(GL_FRONT_AND_BACK,m);}
#include "gles_resources.h"
#include "gles_queries.h"
void glFlush(void){vex_gl_finish();}void glFinish(void){vex_gl_finish();}
void glHint(GLenum t,GLenum m){BEGIN_VOID;if(t!=GL_GENERATE_MIPMAP_HINT||(m!=GL_DONT_CARE&&m!=GL_FASTEST&&m!=GL_NICEST))vex_gl_error(GL_INVALID_ENUM);else c->mipmap_hint=m;END;}
void glLineWidth(GLfloat w){BEGIN_VOID;if(w<=0)vex_gl_error(GL_INVALID_VALUE);else if(w!=1)vex_gl_error(GL_INVALID_OPERATION);END;}
void glPolygonOffset(GLfloat f,GLfloat u){BEGIN_VOID;c->state.offset_factor=f;c->state.offset_units=u;END;}
void glSampleCoverage(GLfloat v,GLboolean i){BEGIN_VOID;c->sample_value=clamp01(v);c->sample_invert=!!i;END;}
void glReleaseShaderCompiler(void){vex_glsl_release();}
void glShaderBinary(GLsizei n,const GLuint *s,GLenum f,const void *b,GLsizei l){BEGIN_VOID;(void)n;(void)s;(void)f;(void)b;(void)l;vex_gl_error(GL_INVALID_ENUM);END;}
void *glXGetCurrentContext(void){return NULL;}
