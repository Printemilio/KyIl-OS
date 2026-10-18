#define _POSIX_C_SOURCE 200809L
#include "vex_render_core.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include "vex_render_types.h"
static _Thread_local unsigned profile_error;
static void *vk_heap_alloc(size_t bytes){return calloc(1,bytes);}
static void vk_heap_free(void *p){free(p);}
#define VEX_RENDER_DIAGNOSTIC(msg) ((void)fprintf(stderr,"%s",msg))
#define VEX_RENDER_PROFILE_FAILURE(line) (profile_error=(line),0)
/* The hosted GL frontend supplies libm; the native Vulkan profile does not
 * acquire a new freestanding math dependency or accept unimplemented Bias. */
static int gl_lod_magnification(float squared_footprint,float bias){
    if(!isfinite(squared_footprint)||!isfinite(bias)||squared_footprint<0)return -1;
    return squared_footprint==0||(.5f*log2f(squared_footprint)+bias)<=0;
}
#define VEX_RENDER_GL_LOD_MAGNIFICATION gl_lod_magnification
#include "vex_render_engine.h"

typedef struct core_uniform { vex_render_uniform info;char name[128];uint32_t id[2]; } core_uniform;
struct vex_render_program {
    vk_shader_module_impl_t module[2];vk_spv_profile_t *profile[2];
    uint32_t (*uniform_words[2])[16];core_uniform uniforms[128];uint32_t uniform_count;
};
static const char *core_name(const vk_shader_module_impl_t *s,uint32_t id){
    for(size_t i=5;i<s->code_size/4;){uint32_t wc=s->code[i]>>16,op=s->code[i]&65535;
        if(!wc||wc>s->code_size/4-i)return "";
        if(op==5&&wc>2&&s->code[i+1]==id&&memchr(s->code+i+2,0,(wc-2)*4))return (const char *)(s->code+i+2);i+=wc;
    }return "";
}
void vex_render_program_destroy(vex_render_program *p){
    if(!p)return;for(unsigned s=0;s<2;++s){if(p->profile[s]){free(p->profile[s]->workspace);free(p->profile[s]->quad_workspace);free(p->profile[s]->prologue);}free(p->profile[s]);free(p->uniform_words[s]);free(p->module[s].code);}free(p);
}
static int core_uniform_type(vk_spv_profile_t *p,uint32_t id,vex_render_uniform *out){
    uint32_t t=p->element[p->type_of[id]];out->count=1;out->columns=1;out->components=1;
    if(p->kind[t]==VK_SPV_KIND_SAMPLED_IMAGE){out->kind=4;return 1;}
    if(p->kind[t]==VK_SPV_KIND_ARRAY){out->count=p->count[t];t=p->element[t];}
    if(p->kind[t]==VK_SPV_KIND_MATRIX){out->columns=p->count[t];t=p->element[t];}
    if(p->kind[t]==VK_SPV_KIND_VECTOR){out->components=p->count[t];t=p->element[t];}
    out->kind=p->kind[t]==VK_SPV_KIND_FLOAT?1:p->kind[t]==VK_SPV_KIND_INT?(p->signedness[t]?2:3):p->kind[t]==VK_SPV_KIND_BOOL?2:0;
    return out->kind&&out->count*out->columns*out->components<=16;
}
vex_render_program *vex_render_program_create(const vex_glsl_binary code[2],char *log,size_t capacity){
    vex_render_program *p=calloc(1,sizeof(*p));if(!p)return NULL;
    for(unsigned s=0;s<2;++s){
        p->module[s].code=malloc(code[s].bytes);p->module[s].code_size=code[s].bytes;
        p->profile[s]=calloc(1,sizeof(*p->profile[s]));if(!p->module[s].code||!p->profile[s])goto fail;
        memcpy(p->module[s].code,code[s].words,code[s].bytes);profile_error=0;
        if(!vex_spv_parse_environment(&p->module[s],1,s?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT,p->profile[s])){
            snprintf(log,capacity,"Vex shader execution profile rejects %s module at core line %u",s?"fragment":"vertex",profile_error);goto fail;
        }
        p->profile[s]->workspace=calloc(p->profile[s]->bound,sizeof(vk_spv_value_t));
        p->profile[s]->prologue=calloc(p->profile[s]->bound,sizeof(vk_spv_value_t));
        if(p->profile[s]->gl_needs_quad){p->profile[s]->quad_workspace=calloc((size_t)p->profile[s]->bound*4,sizeof(vk_spv_value_t));if(!p->profile[s]->quad_workspace)goto fail;}
        p->uniform_words[s]=calloc(p->profile[s]->bound,64);
        if(!p->profile[s]->workspace||!p->profile[s]->prologue||!p->uniform_words[s])goto fail;
        p->profile[s]->gl_uniform_words=(const uint32_t (*)[16])p->uniform_words[s];
        for(uint32_t id=1;id<p->profile[s]->bound;++id){vk_spv_profile_t *profile=p->profile[s];
            if(!profile->gl_uniform[id]&&profile->descriptor_type_by_id[id]!=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)continue;
            const char *name=core_name(&p->module[s],id);if(!*name||strlen(name)>=128)goto fail;
            int location=vex_render_uniform_location(p,name),index=location<0?-1:location/16;if(index<0){if(p->uniform_count==128)goto fail;index=p->uniform_count++;
                core_uniform *u=&p->uniforms[index];strcpy(u->name,name);u->info.name=u->name;if(!core_uniform_type(profile,id,&u->info))goto fail;
            }p->uniforms[index].id[s]=id;
        }
    }
    if(p->profile[1]->fragment_varying_mask&~p->profile[0]->vertex_varying_mask){snprintf(log,capacity,"Missing linked varying");goto fail;}
    return p;
fail:vex_render_program_destroy(p);return NULL;
}
/* ---- Band-parallel rasterization ----------------------------------------------
 * The hosted GL runtime shades big triangles on several CPUs: the rows are cut
 * into bands (boundaries on even rows, so a 2x2 quad never straddles two bands)
 * and each helper thread runs vk_raster_rows() on its own band with private
 * interpreter workspaces. Bands write disjoint rows. VEX_GL_THREADS=n sets the
 * helper count (0 = serial), default is the CPU count minus one, at most 3. */
#include <pthread.h>
#include <unistd.h>
#define RASTER_MAX_WORKERS 3
typedef struct raster_worker {
    pthread_t thread;
    vk_spv_value_t *workspace, *quad_workspace;
    size_t workspace_count;
    int32_t y_begin, y_end;
    VkResult result;
} raster_worker_t;
static struct {
    pthread_once_t once;
    pthread_mutex_t dispatch, lock;
    pthread_cond_t wake, done;
    raster_worker_t worker[RASTER_MAX_WORKERS];
    unsigned count, pending;
    uint32_t generation;
    const vk_raster_job_t *job;
} raster_pool = { PTHREAD_ONCE_INIT, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
                  PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER };

static int raster_worker_scratch(raster_worker_t *worker, uint32_t bound) {
    if (worker->workspace_count < bound) {
        free(worker->workspace);
        free(worker->quad_workspace);
        worker->workspace = calloc(bound, sizeof(vk_spv_value_t));
        worker->quad_workspace = calloc((size_t)bound * 4u, sizeof(vk_spv_value_t));
        worker->workspace_count = worker->workspace && worker->quad_workspace ? bound : 0u;
    }
    return worker->workspace_count >= bound;
}

static void *raster_worker_main(void *argument) {
    raster_worker_t *worker = argument;
    uint32_t seen = 0u;

    pthread_mutex_lock(&raster_pool.lock);
    for (;;) {
        while (raster_pool.generation == seen) {
            pthread_cond_wait(&raster_pool.wake, &raster_pool.lock);
        }
        seen = raster_pool.generation;
        if (worker->y_begin < worker->y_end) {
            const vk_raster_job_t *job = raster_pool.job;
            pthread_mutex_unlock(&raster_pool.lock);
            worker->result = raster_worker_scratch(worker, job->fragment_profile->bound)
                ? vk_raster_rows(job, worker->y_begin, worker->y_end, worker->workspace, worker->quad_workspace)
                : VK_ERROR_OUT_OF_HOST_MEMORY;
            pthread_mutex_lock(&raster_pool.lock);
            worker->y_begin = worker->y_end;
            if (--raster_pool.pending == 0u) {
                pthread_cond_signal(&raster_pool.done);
            }
        }
    }
    return NULL;
}

static void raster_pool_init(void) {
    const char *requested = getenv("VEX_GL_THREADS");
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    long helpers = requested ? atol(requested) : cpus - 1;

    if (helpers > RASTER_MAX_WORKERS) {
        helpers = RASTER_MAX_WORKERS;
    }
    for (long i = 0; i < helpers; ++i) {
        raster_pool.worker[raster_pool.count].y_begin = raster_pool.worker[raster_pool.count].y_end = 0;
        if (pthread_create(&raster_pool.worker[raster_pool.count].thread, NULL, raster_worker_main,
                           &raster_pool.worker[raster_pool.count]) != 0) {
            break;
        }
        pthread_detach(raster_pool.worker[raster_pool.count].thread);
        ++raster_pool.count;
    }
}

static VkResult core_raster_parallel(const vk_raster_job_t *job, int32_t y_begin, int32_t y_end) {
    pthread_once(&raster_pool.once, raster_pool_init);
    if (raster_pool.count == 0u || pthread_mutex_trylock(&raster_pool.dispatch) != 0) {
        return vk_raster_rows(job, y_begin, y_end, 0, 0);   /* serial; another thread owns the pool */
    }
    const unsigned bands = raster_pool.count + 1u;
    const int32_t rows = y_end - y_begin, height = (rows + (int32_t)bands - 1) / (int32_t)bands;
    int32_t boundary[RASTER_MAX_WORKERS + 2];
    VkResult result;

    boundary[0] = y_begin;
    for (unsigned k = 1u; k <= bands; ++k) {
        int32_t edge = k == bands ? y_end : (y_begin + (int32_t)k * height + 1) & ~1;
        boundary[k] = edge > y_end ? y_end : edge < boundary[k - 1u] ? boundary[k - 1u] : edge;
    }
    pthread_mutex_lock(&raster_pool.lock);
    raster_pool.job = job;
    raster_pool.pending = 0u;
    for (unsigned w = 0u; w < raster_pool.count; ++w) {
        raster_pool.worker[w].y_begin = boundary[w + 1u];
        raster_pool.worker[w].y_end = boundary[w + 2u];
        raster_pool.worker[w].result = VK_SUCCESS;
        raster_pool.pending += raster_pool.worker[w].y_begin < raster_pool.worker[w].y_end;
    }
    ++raster_pool.generation;
    pthread_cond_broadcast(&raster_pool.wake);
    pthread_mutex_unlock(&raster_pool.lock);

    result = vk_raster_rows(job, boundary[0], boundary[1], 0, 0);
    pthread_mutex_lock(&raster_pool.lock);
    while (raster_pool.pending != 0u) {
        pthread_cond_wait(&raster_pool.done, &raster_pool.lock);
    }
    for (unsigned w = 0u; w < raster_pool.count; ++w) {
        if (result == VK_SUCCESS) {
            result = raster_pool.worker[w].result;
        }
    }
    pthread_mutex_unlock(&raster_pool.lock);
    pthread_mutex_unlock(&raster_pool.dispatch);
    return result;
}

/* ---- Native fast paths for the Skia programs that dominate compositor frames ----
 * WPE draws through Skia/Ganesh, whose generated GLSL is stable for a given
 * pipeline. A fragment program whose source (ignoring white space) equals one of
 * the fixtures below is evaluated natively; anything else runs in the
 * interpreter. userspace/linux_tests/gl_render_bench.c renders each fixture both
 * ways and requires identical pixels. VEX_GL_FAST=0 disables the fast paths. */
static const char skia_solid_source[] =
    "#version 100\nprecision mediump float;precision mediump sampler2D;varying mediump vec4 vcolor_S0;\n"
    "void main(){mediump vec4 outputColor_S0=vcolor_S0;{gl_FragColor=outputColor_S0;}}\n";
static const char skia_texture_source[] =
    "#version 100\nprecision mediump float;precision mediump sampler2D;uniform sampler2D uTextureSampler_0_S0;\n"
    "varying highp vec2 vlocalCoord_S0;\n"
    "void main(){mediump vec4 outputColor_S0=vec4(1.0);highp vec2 texCoord=vlocalCoord_S0;\n"
    "outputColor_S0=texture2D(uTextureSampler_0_S0,texCoord,-0.475);{gl_FragColor=outputColor_S0;}}\n";
static const char skia_atlas_source[] =
    "#version 100\nprecision mediump float;precision mediump sampler2D;uniform sampler2D uTextureSampler_0_S0;\n"
    "varying highp vec2 vTextureCoords_S0;varying highp float vTexIndex_S0;varying mediump vec4 vinColor_S0;\n"
    "void main(){mediump vec4 outputColor_S0=vinColor_S0;\n"
    "mediump vec4 texColor=texture2D(uTextureSampler_0_S0,vTextureCoords_S0,-0.475).wwww;\n"
    "mediump vec4 outputCoverage_S0=texColor;{gl_FragColor=outputColor_S0*outputCoverage_S0;}}\n";

static int same_ignoring_space(const char *a,const char *b){
    for(;;){
        while(*a&&isspace((unsigned char)*a))++a;
        while(*b&&isspace((unsigned char)*b))++b;
        if(*a!=*b)return 0;
        if(!*a)return 1;
        ++a;++b;
    }
}
static int core_fragment_varying(const vex_render_program *p,const char *name){
    const vk_spv_profile_t *f=p->profile[1];
    for(unsigned location=0;location<VK_SPV_MAX_INTERFACE_LOCATIONS;++location){
        const uint32_t id=f->fragment_varying_id[location];
        if(id&&!strcmp(core_name(&p->module[1],id),name))return (int)location;
    }
    return -1;
}
static int core_fragment_sampler_binding(const vex_render_program *p,const char *name){
    const vk_spv_profile_t *f=p->profile[1];
    for(uint32_t id=1;id<f->bound;++id)
        if(f->descriptor_type_by_id[id]==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER&&!strcmp(core_name(&p->module[1],id),name))
            return (int)f->descriptor_binding[id];
    return -1;
}
void vex_render_program_classify(vex_render_program *p,const char *fragment_source){
    if(!p||!fragment_source)return;
    vk_spv_profile_t *f=p->profile[1];
    const char *switch_off=getenv("VEX_GL_FAST");
    f->fast_kind=0;
    if(switch_off&&!strcmp(switch_off,"0"))return;
    int location0=-1,location1=-1,binding=-1;
    if(same_ignoring_space(fragment_source,skia_solid_source)&&(location0=core_fragment_varying(p,"vcolor_S0"))>=0){
        f->fast_kind=VEX_FAST_SOLID_VARYING;
    }else if(same_ignoring_space(fragment_source,skia_texture_source)&&(location0=core_fragment_varying(p,"vlocalCoord_S0"))>=0&&
             (binding=core_fragment_sampler_binding(p,"uTextureSampler_0_S0"))>=0){
        f->fast_kind=VEX_FAST_TEXTURE;
    }else if(same_ignoring_space(fragment_source,skia_atlas_source)&&(location0=core_fragment_varying(p,"vTextureCoords_S0"))>=0&&
             (location1=core_fragment_varying(p,"vinColor_S0"))>=0&&
             (binding=core_fragment_sampler_binding(p,"uTextureSampler_0_S0"))>=0){
        f->fast_kind=VEX_FAST_ATLAS_COVERAGE;
    }
    if(!f->fast_kind)return;
    f->fast_location[0]=(uint8_t)location0;f->fast_location[1]=(uint8_t)(location1<0?0:location1);
    f->fast_binding=(uint32_t)(binding<0?0:binding);f->fast_bias=-0.475f;
}
void vex_render_set_fast_paths(int enabled){vex_render_fast_paths_enabled=enabled!=0;}
int vex_render_attribute(vex_render_program *p,const char *name){if(!p||!name)return -1;for(unsigned a=0;a<8;++a)if(p->profile[0]->attribute_id[a]&&!strcmp(name,core_name(&p->module[0],p->profile[0]->attribute_id[a])))return a;return -1;}
size_t vex_render_attribute_count(vex_render_program *p){size_t n=0;if(p)for(unsigned a=0;a<8;++a)n+=!!p->profile[0]->attribute_id[a];return n;}
const char *vex_render_attribute_info(vex_render_program *p,size_t index,uint32_t *components){if(!p)return NULL;for(unsigned a=0;a<8;++a){uint32_t id=p->profile[0]->attribute_id[a];if(!id)continue;if(index--==0){uint32_t t=p->profile[0]->element[p->profile[0]->type_of[id]];*components=p->profile[0]->kind[t]==VK_SPV_KIND_VECTOR?p->profile[0]->count[t]:1;return core_name(&p->module[0],id);}}return NULL;}
int vex_render_uniform_location(vex_render_program *p,const char *name){
    if(!p||!name)return -1;size_t length=strlen(name);unsigned element=0;const char *bracket=strchr(name,'[');
    if(bracket){char *end;unsigned long value=strtoul(bracket+1,&end,10);if(end==bracket+1||*end!=']'||end[1]||value>=16)return -1;element=(unsigned)value;length=bracket-name;}
    for(unsigned i=0;i<p->uniform_count;++i)if(strlen(p->uniforms[i].name)==length&&!strncmp(name,p->uniforms[i].name,length))
        return element<p->uniforms[i].info.count?(int)(i*16+element):-1;return -1;
}
size_t vex_render_uniform_count(vex_render_program *p){return p?p->uniform_count:0;}
const vex_render_uniform *vex_render_uniform_info(vex_render_program *p,size_t i){return p&&i<p->uniform_count?&p->uniforms[i].info:NULL;}
const vex_render_uniform *vex_render_uniform_at_location(vex_render_program *p,int location){
    const vex_render_uniform *u=location<0?NULL:vex_render_uniform_info(p,(unsigned)location/16);
    return u&&(unsigned)location%16<u->count?u:NULL;
}
int vex_render_uniform_set(vex_render_program *p,int location,const uint32_t *words,uint32_t n){
    const vex_render_uniform *info=vex_render_uniform_at_location(p,location);if(!info||(!words&&n))return 0;
    core_uniform *u=&p->uniforms[(unsigned)location/16];uint32_t offset=((unsigned)location%16)*info->components*info->columns;
    if(n>info->components*info->columns*info->count-offset)return 0;
    if(n)for(unsigned s=0;s<2;++s)if(u->id[s])memcpy(p->uniform_words[s][u->id[s]]+offset,words,n*4);return 1;
}
const uint32_t *vex_render_uniform_get(vex_render_program *p,int location){
    const vex_render_uniform *info=vex_render_uniform_at_location(p,location);if(!info)return NULL;
    core_uniform *u=&p->uniforms[(unsigned)location/16];unsigned s=u->id[0]?0:1;
    return p->uniform_words[s][u->id[s]]+((unsigned)location%16)*info->components*info->columns;
}
static void core_image(vk_image_impl_t *i,vk_image_subresource_impl_t *s,void *pixels,uint32_t w,uint32_t h,uint32_t format,VkImageLayout layout){
    memset(i,0,sizeof(*i));memset(s,0,sizeof(*s));i->width=w;i->height=h;i->format=format;i->samples=1;i->mip_levels=i->array_layers=1;i->subresources=s;i->owned_pixels=pixels;
    s->width=w;s->height=h;s->row_pitch=(uint64_t)w*vk_format_texel_bytes(format);s->size=s->row_pitch*h;s->layout=layout;
}
/* Diagnostics (VEX_GL_STATS): per-program draw counts, covered area and time,
 * reported every two seconds. Costs nothing unless the variable is set. */
#include <stdio.h>
#include <time.h>
typedef struct { uint64_t hash, draws, vertices, area_px, ns; } draw_stat_t;
static draw_stat_t draw_stats[16];
static uint64_t draw_stats_reported_ns;
static uint64_t stats_clock_ns(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000ull+(uint64_t)t.tv_nsec;}
static void draw_stats_record(const vex_render_program *program,const vex_render_state *state,uint32_t count,uint64_t ns){
    uint64_t hash=1469598103934665603ull;
    for(size_t i=0;i<program->module[1].code_size/4;++i){hash^=((const uint32_t *)program->module[1].code)[i];hash*=1099511628211ull;}
    draw_stat_t *slot=&draw_stats[hash%16];
    if(slot->hash!=hash&&slot->draws){slot=&draw_stats[15];slot->hash=0;}
    slot->hash=hash;++slot->draws;slot->vertices+=count;slot->ns+=ns;
    uint64_t w=state->scissor.extent.width<state->viewport.width?state->scissor.extent.width:(uint64_t)state->viewport.width;
    uint64_t h=state->scissor.extent.height<state->viewport.height?state->scissor.extent.height:(uint64_t)state->viewport.height;
    slot->area_px+=w*h;
    const uint64_t now=stats_clock_ns();
    if(now-draw_stats_reported_ns<2000000000ull)return;
    draw_stats_reported_ns=now;
    for(unsigned i=0;i<16;++i)if(draw_stats[i].draws){
        fprintf(stderr,"Vex GL stats: hash=%016llx draws=%llu vertices=%llu area_px=%llu time_ms=%llu\n",
            (unsigned long long)draw_stats[i].hash,(unsigned long long)draw_stats[i].draws,(unsigned long long)draw_stats[i].vertices,
            (unsigned long long)draw_stats[i].area_px,(unsigned long long)(draw_stats[i].ns/1000000ull));
        draw_stats[i]=(draw_stat_t){0};
    }
}
static int vex_render_draw_impl(vex_render_program *program,const vex_render_state *state,vex_render_target *target,const float *vertices,uint32_t count,const vex_render_texture textures[16]);
int vex_render_draw(vex_render_program *program,const vex_render_state *state,vex_render_target *target,const float *vertices,uint32_t count,const vex_render_texture textures[16]){
    static int enabled=-1;
    if(enabled<0)enabled=getenv("VEX_GL_STATS")!=NULL;
    if(!enabled)return vex_render_draw_impl(program,state,target,vertices,count,textures);
    const uint64_t started=stats_clock_ns();
    const int result=vex_render_draw_impl(program,state,target,vertices,count,textures);
    if(program&&state)draw_stats_record(program,state,count,stats_clock_ns()-started);
    return result;
}
static int vex_render_draw_impl(vex_render_program *program,const vex_render_state *state,vex_render_target *target,const float *vertices,uint32_t count,const vex_render_texture textures[16]){
    if(!program||!state||!target||!target->pixels||!vertices)return 0;
    vex_render_state oriented=*state;
    program->profile[0]->gl_top_left_height=program->profile[1]->gl_top_left_height=target->top_left?target->height:0;
    if(target->top_left){
        oriented.viewport.y=(float)target->height-state->viewport.y-state->viewport.height;
        int64_t y=(int64_t)target->height-state->scissor.offset.y-state->scissor.extent.height;
        if(y<INT32_MIN||y>INT32_MAX)return 0;
        oriented.scissor.offset.y=(int32_t)y;
        oriented.front=state->front==VK_FRONT_FACE_CLOCKWISE?VK_FRONT_FACE_COUNTER_CLOCKWISE:VK_FRONT_FACE_CLOCKWISE;
    }state=&oriented;
    vk_pipeline_impl_t pipeline={0};vk_render_pass_impl_t pass={0};vk_framebuffer_impl_t framebuffer={0};vk_recorded_command_t draw={0};
    vk_image_impl_t image,ds;vk_image_subresource_impl_t sub,ds_sub;vk_image_view_impl_t color_view={0},ds_view={0};
    core_image(&image,&sub,target->pixels,target->width,target->height,target->format?target->format:VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    color_view.image=&image;color_view.range.levelCount=color_view.range.layerCount=1;
    if(target->depth_stencil){core_image(&ds,&ds_sub,target->depth_stencil,target->width,target->height,VK_FORMAT_D32_SFLOAT_S8_UINT,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);ds_view.image=&ds;ds_view.range.levelCount=ds_view.range.layerCount=1;pass.has_depth=1;}
    pass.subpass_layout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;pass.depth_subpass_layout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    framebuffer.render_pass=&pass;framebuffer.attachment0=&color_view;framebuffer.depth_attachment=target->depth_stencil?&ds_view:NULL;framebuffer.width=target->width;framebuffer.height=target->height;
    pipeline.render_pass=&pass;pipeline.vertex_shader=&program->module[0];pipeline.fragment_shader=&program->module[1];pipeline.vertex_profile=program->profile[0];pipeline.fragment_profile=program->profile[1];
    pipeline.topology=state->topology;pipeline.cull_mode=state->cull;pipeline.front_face=state->front;pipeline.samples=pipeline.sample_mask=1;
    pipeline.depth_test_enable=state->depth_test;pipeline.depth_write_enable=state->depth_write;pipeline.depth_compare_op=state->depth_compare;pipeline.stencil_test_enable=state->stencil_test;
    pipeline.color_blend=state->blend;pipeline.vertex_attribute_mask=program->profile[0]->attribute_mask;pipeline.vertex_binding.stride=128;
    pipeline.gl_quad=program->profile[1]->gl_needs_quad;pipeline.dither_enable=state->dither;pipeline.depth_bias_enable=state->polygon_offset;
    draw.args.draw.state.depth_bias_constant=state->offset_units;draw.args.draw.state.depth_bias_slope=state->offset_factor;
    for(unsigned a=0;a<8;++a){pipeline.vertex_attributes[a].format=VK_FORMAT_R32G32B32A32_SFLOAT;pipeline.vertex_attributes[a].offset=a*16;}
    vk_memory_impl_t vm={0};vm.bytes=(void*)vertices;vm.size=(uint64_t)count*128;vk_buffer_impl_t vb={0};vb.memory=&vm;vb.size=vm.size;vb.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    draw.args.draw.pipeline=&pipeline;draw.args.draw.framebuffer=&framebuffer;draw.args.draw.vertex_buffer=&vb;draw.args.draw.vertex_count=count;draw.args.draw.instance_count=1;
    draw.args.draw.viewport=state->viewport;draw.args.draw.scissor=state->scissor;draw.args.draw.render_area=(VkRect2D){{0,0},{target->width,target->height}};
    memcpy(draw.args.draw.state.stencil,state->stencil,sizeof(state->stencil));memcpy(draw.args.draw.state.blend_constants,state->blend_constants,16);
    vk_image_impl_t images[16];vk_image_subresource_impl_t subs[16];vk_image_view_impl_t views[16];vk_sampler_impl_t samplers[16];
    vk_descriptor_value_impl_t values[16]={0};vk_descriptor_layout_binding_impl_t bindings[16]={0};vk_descriptor_set_layout_impl_t layout={0};vk_descriptor_set_impl_t set={0};unsigned bindings_count=0;
    layout.bindings=bindings;set.layout=&layout;set.values=values;draw.args.draw.descriptor_sets[0]=&set;
    for(unsigned u=0;u<program->uniform_count;++u){core_uniform *uniform=&program->uniforms[u];if(uniform->info.kind!=4)continue;
        unsigned s=uniform->id[0]?0:1,id=uniform->id[s],binding=program->profile[s]->descriptor_binding[id],unit=program->uniform_words[s][id][0];
        if(binding>=16||unit>=16||!textures[unit].pixels||!textures[unit].width||!textures[unit].height)return 0;
        const vex_render_texture *tex=&textures[unit];core_image(&images[binding],&subs[binding],(void*)tex->pixels,tex->width,tex->height,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        images[binding].usage=VK_IMAGE_USAGE_SAMPLED_BIT;memset(&views[binding],0,sizeof(views[binding]));views[binding].image=&images[binding];views[binding].type=VK_IMAGE_VIEW_TYPE_2D;views[binding].range.levelCount=views[binding].range.layerCount=1;
        memset(&samplers[binding],0,sizeof(samplers[binding]));samplers[binding].mag_filter=tex->mag;samplers[binding].min_filter=tex->min;samplers[binding].address_mode_u=tex->u;samplers[binding].address_mode_v=tex->v;
        values[binding].type=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;values[binding].initialized=1;values[binding].resource.image.view=&views[binding];values[binding].resource.image.sampler=&samplers[binding];values[binding].resource.image.layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bindings[bindings_count].binding=(VkDescriptorSetLayoutBinding){binding,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_ALL,NULL};bindings[bindings_count++].value_offset=binding;
    }layout.binding_count=bindings_count;set.value_count=16;
    program->profile[0]->prologue_ready=program->profile[1]->prologue_ready=0u;   /* uniforms may have changed */
    program->profile[0]->prologue_filling=program->profile[1]->prologue_filling=0u;
    vk_raster_parallel=core_raster_parallel;
    return vk_execute_draw(&draw)==VK_SUCCESS;
}
