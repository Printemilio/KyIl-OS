/* Single shader/raster implementation. Included by each API adapter;
 * it contains no syscall, WSI or Vulkan entry-point calls. */
#ifndef VEX_RENDER_ENGINE_H
#define VEX_RENDER_ENGINE_H
#include "vex_render_declarations.h"
#ifndef VEX_RENDER_PROFILE_FAILURE
#define VEX_RENDER_PROFILE_FAILURE(line) 0
#endif
static uint32_t pack_rgba(const VkClearColorValue *color) {
    uint32_t channel[4] = {0u, 0u, 0u, 255u};
    if (color != 0) {
        for (uint32_t i = 0u; i < 4u; ++i) {
            float value = color->float32[i];
            if (!(value > 0.0f)) value = 0.0f; /* also maps NaN to zero */
            if (value >= 1.0f) channel[i] = 255u;
            else channel[i] = (uint32_t)(value * 255.0f + 0.5f);
        }
    }
    return (channel[3] << 24u) | (channel[0] << 16u) |
           (channel[1] << 8u) | channel[2];
}

static float vk_blend_factor_value(VkBlendFactor factor, uint32_t component,
                                   const float source[4], const float destination[4],
                                   const float constants[4]) {
    switch (factor) {
        case VK_BLEND_FACTOR_ZERO: return 0.0f;
        case VK_BLEND_FACTOR_ONE: return 1.0f;
        case VK_BLEND_FACTOR_SRC_COLOR: return source[component];
        case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR: return 1.0f - source[component];
        case VK_BLEND_FACTOR_DST_COLOR: return destination[component];
        case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR: return 1.0f - destination[component];
        case VK_BLEND_FACTOR_SRC_ALPHA: return source[3];
        case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA: return 1.0f - source[3];
        case VK_BLEND_FACTOR_DST_ALPHA: return destination[3];
        case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA: return 1.0f - destination[3];
        case VK_BLEND_FACTOR_CONSTANT_COLOR: return constants[component];
        case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR: return 1.0f - constants[component];
        case VK_BLEND_FACTOR_CONSTANT_ALPHA: return constants[3];
        case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA: return 1.0f - constants[3];
        case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE:
            return component == 3u ? 1.0f :
                (source[3] < 1.0f - destination[3] ? source[3] : 1.0f - destination[3]);
        default: return 0.0f;
    }
}

static float vk_blend_component(VkBlendOp op, float source, float destination,
                                float source_factor, float destination_factor) {
    float value;
    switch (op) {
        case VK_BLEND_OP_ADD: value = source * source_factor + destination * destination_factor; break;
        case VK_BLEND_OP_SUBTRACT: value = source * source_factor - destination * destination_factor; break;
        case VK_BLEND_OP_REVERSE_SUBTRACT: value = destination * destination_factor - source * source_factor; break;
        case VK_BLEND_OP_MIN: value = source < destination ? source : destination; break;
        case VK_BLEND_OP_MAX: value = source > destination ? source : destination; break;
        default: value = source;
    }
    if (!(value > 0.0f)) return 0.0f;
    if (value >= 1.0f) return 1.0f;
    return value;
}

static uint8_t vk_blend_channel_byte(float value) {
    if (!(value > 0.0f)) return 0u;
    if (value >= 1.0f) return 255u;
    return (uint8_t)(value * 255.0f + 0.5f);
}

static uint32_t vk_blend_pixel(const float source_color[4], uint32_t destination_pixel,
                               const vk_pipeline_impl_t *pipeline,const float constants[4],float dither) {
    const uint32_t shifts[4] = {16u, 8u, 0u, 24u};
    float source[4], destination[4], output[4];
    const VkPipelineColorBlendAttachmentState *blend = &pipeline->color_blend;
    for (uint32_t c = 0u; c < 4u; ++c) {
        source[c] = source_color[c];
        destination[c] = (float)((destination_pixel >> shifts[c]) & 0xffu) / 255.0f;
    }
    /* Blend factors can read any source/destination component (notably alpha),
       so initialize both vectors completely before evaluating a channel. */
    for (uint32_t c = 0u; c < 4u; ++c) {
        if (!blend->blendEnable) output[c]=source[c];
        else if (c < 3u) {
            float source_factor = vk_blend_factor_value(blend->srcColorBlendFactor, c,
                source, destination, constants);
            float destination_factor = vk_blend_factor_value(blend->dstColorBlendFactor, c,
                source, destination, constants);
            output[c] = vk_blend_component(blend->colorBlendOp, source[c], destination[c],
                source_factor, destination_factor);
        } else {
            output[c] = vk_blend_component(blend->alphaBlendOp, source[c], destination[c],
                vk_blend_factor_value(blend->srcAlphaBlendFactor, c, source, destination,
                                      constants),
                vk_blend_factor_value(blend->dstAlphaBlendFactor, c, source, destination,
                                      constants));
        }
    }
    for(unsigned c=0;c<3;++c)if(output[c]>0.0f&&output[c]<1.0f)output[c]+=dither;
    return ((uint32_t)vk_blend_channel_byte(output[3]) << 24u) |
           ((uint32_t)vk_blend_channel_byte(output[0]) << 16u) |
           ((uint32_t)vk_blend_channel_byte(output[1]) << 8u) |
           (uint32_t)vk_blend_channel_byte(output[2]);
}

static uint32_t vk_logic_pixel(VkLogicOp op,uint32_t s,uint32_t d){
    switch(op){
        case VK_LOGIC_OP_CLEAR:return 0u;case VK_LOGIC_OP_AND:return s&d;
        case VK_LOGIC_OP_AND_REVERSE:return s&~d;case VK_LOGIC_OP_COPY:return s;
        case VK_LOGIC_OP_AND_INVERTED:return ~s&d;case VK_LOGIC_OP_NO_OP:return d;
        case VK_LOGIC_OP_XOR:return s^d;case VK_LOGIC_OP_OR:return s|d;
        case VK_LOGIC_OP_NOR:return ~(s|d);case VK_LOGIC_OP_EQUIVALENT:return ~(s^d);
        case VK_LOGIC_OP_INVERT:return ~d;case VK_LOGIC_OP_OR_REVERSE:return s|~d;
        case VK_LOGIC_OP_COPY_INVERTED:return ~s;case VK_LOGIC_OP_OR_INVERTED:return ~s|d;
        case VK_LOGIC_OP_NAND:return ~(s&d);case VK_LOGIC_OP_SET:return UINT32_MAX;
        default:return 0u;
    }
}

static uint32_t vk_apply_color_write_mask(uint32_t source, uint32_t destination,
                                          VkColorComponentFlags write_mask) {
    uint32_t mask = 0u;
    if ((write_mask & VK_COLOR_COMPONENT_R_BIT) != 0u) mask |= 0x00ff0000u;
    if ((write_mask & VK_COLOR_COMPONENT_G_BIT) != 0u) mask |= 0x0000ff00u;
    if ((write_mask & VK_COLOR_COMPONENT_B_BIT) != 0u) mask |= 0x000000ffu;
    if ((write_mask & VK_COLOR_COMPONENT_A_BIT) != 0u) mask |= 0xff000000u;
    return (destination & ~mask) | (source & mask);
}

static int vk_format_is_color_unorm(uint32_t format) {
    return format == VK_FORMAT_B8G8R8A8_UNORM ||
           format == VK_FORMAT_R8G8B8A8_UNORM;
}

static uint32_t vk_pixel_to_canonical(uint32_t format, uint32_t pixel) {
    if (format == VK_FORMAT_R8G8B8A8_UNORM)
        return (pixel & 0xff00ff00u) | ((pixel & 0x00ff0000u) >> 16u) |
               ((pixel & 0x000000ffu) << 16u);
    return pixel;
}

static uint32_t vk_pixel_from_canonical(uint32_t format, uint32_t pixel) {
    return vk_pixel_to_canonical(format, pixel);
}

static void vk_sample_location(uint32_t n,uint32_t sample,float *x,float *y) {
    static const float locations[8][2]={{.5625f,.3125f},{.4375f,.6875f},{.8125f,.5625f},{.3125f,.1875f},{.1875f,.8125f},{.0625f,.4375f},{.6875f,.9375f},{.9375f,.0625f}};
    static const float four[4][2]={{.375f,.125f},{.875f,.375f},{.125f,.625f},{.625f,.875f}};
    if(n==1u){*x=*y=.5f;}else if(n==2u){*x=*y=sample==0u?.75f:.25f;}
    else if(n==4u){*x=four[sample][0];*y=four[sample][1];}
    else{*x=locations[sample][0];*y=locations[sample][1];}
}

static int vk_format_has_depth(uint32_t f) { return f==VK_FORMAT_D32_SFLOAT||f==VK_FORMAT_D32_SFLOAT_S8_UINT; }

static int vk_format_has_stencil(uint32_t f) { return f==VK_FORMAT_S8_UINT||f==VK_FORMAT_D32_SFLOAT_S8_UINT; }

static uint32_t vk_format_texel_bytes(VkFormat format) {
    return format==VK_FORMAT_D32_SFLOAT_S8_UINT?8u:format==VK_FORMAT_S8_UINT?1u:4u;
}

static uint32_t vk_aspect_offset(VkFormat f,VkImageAspectFlags a) {
    return f==VK_FORMAT_D32_SFLOAT_S8_UINT&&a==VK_IMAGE_ASPECT_STENCIL_BIT?4u:0u;
}

static vk_image_subresource_impl_t *vk_image_subresource(
        const vk_image_impl_t *image, uint32_t mip, uint32_t layer) {
    if (image == 0 || image->subresources == 0 || mip >= image->mip_levels ||
        layer >= image->array_layers) return 0;
    return &image->subresources[layer * image->mip_levels + mip];
}

static void *vk_image_subresource_pixels(vk_image_impl_t *image,
                                             uint32_t mip, uint32_t layer) {
    vk_image_subresource_impl_t *sub = vk_image_subresource(image, mip, layer);
    if (sub == 0) return 0;
    if (image->owned_pixels != 0)
        return (uint8_t *)image->owned_pixels + sub->offset;
    if (image->memory == 0 || image->memory->bytes == 0 ||
        image->memory_offset > image->memory->size ||
        sub->offset > image->memory->size - image->memory_offset ||
        sub->size > image->memory->size - image->memory_offset - sub->offset)
        return 0;
    return image->memory->bytes + image->memory_offset + sub->offset;
}

static int vk_image_plane(vk_image_impl_t *image, uint32_t mip, uint32_t layer,
                           vk_image_plane_t *plane) {
    vk_image_subresource_impl_t *sub = vk_image_subresource(image, mip, layer);
    if (sub == 0 || plane == 0) return 0;
    plane->pixels = vk_image_subresource_pixels(image, mip, layer);
    plane->samples = image->samples;
    plane->width = sub->width;
    plane->height = sub->height;
    plane->format = image->format;
    plane->layout = sub->layout;
    return plane->pixels != 0;
}

static int vk_view_plane(vk_image_view_impl_t *view, vk_image_plane_t *plane) {
    return view != 0 && vk_image_plane(view->image, view->range.baseMipLevel,
                                       view->range.baseArrayLayer, plane);
}

static float *vk_view_depth_values(vk_image_view_impl_t *view) {
    if (view == 0 || !vk_format_has_depth(view->image->format)) return 0;
    return (float *)(void *)vk_image_subresource_pixels(view->image,
        view->range.baseMipLevel, view->range.baseArrayLayer);
}

static int32_t clamp_i32(int32_t value, int32_t min_value, int32_t max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void fill_image_rect(vk_image_plane_t *image, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    uint32_t *pixels = image->pixels;
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;

    if (pixels == 0 || w <= 0 || h <= 0) {
        return;
    }
    color = vk_pixel_from_canonical(image->format, color);

    x0 = clamp_i32(x, 0, (int32_t)image->width);
    y0 = clamp_i32(y, 0, (int32_t)image->height);
    x1 = clamp_i32(x + w, 0, (int32_t)image->width);
    y1 = clamp_i32(y + h, 0, (int32_t)image->height);
    if (x1 <= x0 || y1 <= y0) {
        return;
    }

    for (int32_t py = y0; py < y1; ++py) {
        uint32_t *row = pixels + (uint32_t)py * image->width * image->samples;
        for (int32_t px = x0; px < x1; ++px) {
            for (uint32_t sample=0u; sample<image->samples; ++sample) row[(uint32_t)px*image->samples+sample] = color;
        }
    }
}

static void fill_depth_rect(vk_image_plane_t *image, int32_t x, int32_t y,
                            int32_t w, int32_t h, float depth) {
    float *values = (float *)(void *)image->pixels;
    int32_t x0, y0, x1, y1;
    if (values == 0 || w <= 0 || h <= 0) return;
    x0 = clamp_i32(x, 0, (int32_t)image->width);
    y0 = clamp_i32(y, 0, (int32_t)image->height);
    x1 = clamp_i32(x + w, 0, (int32_t)image->width);
    y1 = clamp_i32(y + h, 0, (int32_t)image->height);
    for (int32_t py = y0; py < y1; ++py)
        for (int32_t px = x0; px < x1; ++px)
            for (uint32_t sample=0u; sample<image->samples; ++sample)
                values[(((uint32_t)py * image->width + (uint32_t)px)*image->samples+sample)*(image->format==VK_FORMAT_D32_SFLOAT_S8_UINT?2u:1u)] = depth;
}

static void fill_stencil_rect(vk_image_plane_t *plane, int32_t x, int32_t y,
        int32_t width, int32_t height, uint32_t value) {
    uint8_t *bytes = (uint8_t *)plane->pixels;
    int32_t x0 = clamp_i32(x, 0, (int32_t)plane->width), y0 = clamp_i32(y, 0, (int32_t)plane->height);
    int32_t x1 = clamp_i32(x + width, 0, (int32_t)plane->width), y1 = clamp_i32(y + height, 0, (int32_t)plane->height);
    for (int32_t row = y0; row < y1; ++row)
        for (int32_t col = x0; col < x1; ++col)
            for (uint32_t sample=0u; sample<plane->samples; ++sample) bytes[(((uint32_t)row * plane->width + (uint32_t)col)*plane->samples+sample)*vk_format_texel_bytes(plane->format)+vk_aspect_offset(plane->format,VK_IMAGE_ASPECT_STENCIL_BIT)]=(uint8_t)value;
}

static int vk_vertex_format_info(VkFormat format, uint32_t *components_out,
                                 uint32_t *size_out, uint8_t *encoding_out,
                                 uint8_t *bgra_out) {
    uint32_t components, size;
    uint8_t encoding, bgra = 0u;
    switch (format) {
        case VK_FORMAT_R32_SFLOAT: components = 1u; size = 4u; encoding = VK_VERTEX_FORMAT_FLOAT32; break;
        case VK_FORMAT_R32G32_SFLOAT: components = 2u; size = 8u; encoding = VK_VERTEX_FORMAT_FLOAT32; break;
        case VK_FORMAT_R32G32B32_SFLOAT: components = 3u; size = 12u; encoding = VK_VERTEX_FORMAT_FLOAT32; break;
        case VK_FORMAT_R32G32B32A32_SFLOAT: components = 4u; size = 16u; encoding = VK_VERTEX_FORMAT_FLOAT32; break;
        case VK_FORMAT_R8_UNORM: components = 1u; size = 1u; encoding = VK_VERTEX_FORMAT_UNORM8; break;
        case VK_FORMAT_R8G8_UNORM: components = 2u; size = 2u; encoding = VK_VERTEX_FORMAT_UNORM8; break;
        case VK_FORMAT_R8G8B8_UNORM: components = 3u; size = 3u; encoding = VK_VERTEX_FORMAT_UNORM8; break;
        case VK_FORMAT_B8G8R8_UNORM: components = 3u; size = 3u; encoding = VK_VERTEX_FORMAT_UNORM8; bgra = 1u; break;
        case VK_FORMAT_R8G8B8A8_UNORM: components = 4u; size = 4u; encoding = VK_VERTEX_FORMAT_UNORM8; break;
        case VK_FORMAT_B8G8R8A8_UNORM: components = 4u; size = 4u; encoding = VK_VERTEX_FORMAT_UNORM8; bgra = 1u; break;
        case VK_FORMAT_R8_SNORM: components = 1u; size = 1u; encoding = VK_VERTEX_FORMAT_SNORM8; break;
        case VK_FORMAT_R8G8_SNORM: components = 2u; size = 2u; encoding = VK_VERTEX_FORMAT_SNORM8; break;
        case VK_FORMAT_R8G8B8_SNORM: components = 3u; size = 3u; encoding = VK_VERTEX_FORMAT_SNORM8; break;
        case VK_FORMAT_B8G8R8_SNORM: components = 3u; size = 3u; encoding = VK_VERTEX_FORMAT_SNORM8; bgra = 1u; break;
        case VK_FORMAT_R8G8B8A8_SNORM: components = 4u; size = 4u; encoding = VK_VERTEX_FORMAT_SNORM8; break;
        case VK_FORMAT_B8G8R8A8_SNORM: components = 4u; size = 4u; encoding = VK_VERTEX_FORMAT_SNORM8; bgra = 1u; break;
        default: return 0;
    }
    if (components_out != 0) *components_out = components;
    if (size_out != 0) *size_out = size;
    if (encoding_out != 0) *encoding_out = encoding;
    if (bgra_out != 0) *bgra_out = bgra;
    return 1;
}

static uint32_t vk_spv_popcount8(uint8_t value) {
    uint32_t count = 0u;
    for (uint32_t bit = 0u; bit < VK_SPV_MAX_INTERFACE_LOCATIONS; ++bit)
        count += (value >> bit) & 1u;
    return count;
}

static uint32_t vk_spv_shared_bytes(const vk_spv_profile_t *p,uint32_t t,uint32_t depth){
    if(t>=p->bound||depth>8u)return 0;
    if(p->kind[t]==VK_SPV_KIND_BOOL||((p->kind[t]==VK_SPV_KIND_INT||p->kind[t]==VK_SPV_KIND_FLOAT)&&p->width[t]==32u))return 4u;
    uint32_t bytes=0u;
    if(p->kind[t]==VK_SPV_KIND_VECTOR||p->kind[t]==VK_SPV_KIND_MATRIX||p->kind[t]==VK_SPV_KIND_ARRAY){
        uint32_t child=vk_spv_shared_bytes(p,p->element[t],depth+1u);
        if(!child||!p->count[t]||p->count[t]>16384u/child)return 0;bytes=child*p->count[t];
    }else if(p->kind[t]==VK_SPV_KIND_STRUCT){
        for(uint32_t m=0u;m<p->count[t];++m){uint32_t child=vk_spv_shared_bytes(p,p->member_type[t][m],depth+1u);if(!child||child>16384u-bytes)return 0;bytes+=child;}
    }
    return bytes;
}

static uint32_t vk_spv_value_words(const vk_spv_profile_t *p,uint32_t type,uint32_t depth){
    if(type>=p->bound||depth>8u)return 0u;
    if(p->kind[type]==VK_SPV_KIND_BOOL||((p->kind[type]==VK_SPV_KIND_INT||p->kind[type]==VK_SPV_KIND_FLOAT)&&p->width[type]==32u))return 1u;
    uint32_t words=0u;
    if(p->kind[type]==VK_SPV_KIND_VECTOR||p->kind[type]==VK_SPV_KIND_MATRIX||p->kind[type]==VK_SPV_KIND_ARRAY){
        uint32_t child=vk_spv_value_words(p,p->element[type],depth+1u);
        if(!child||!p->count[type]||p->count[type]>16u/child)return 0u;words=child*p->count[type];
    }else if(p->kind[type]==VK_SPV_KIND_STRUCT){
        if(p->count[type]>VK_SPV_MAX_BLOCK_MEMBERS)return 0u;
        for(uint32_t i=0u;i<p->count[type];++i){uint32_t child=vk_spv_value_words(p,p->member_type[type][i],depth+1u);if(!child||child>16u-words)return 0u;words+=child;}
    }
    return words;
}

static int vk_spv_composite_child(const vk_spv_profile_t *p,uint32_t type,uint32_t index,uint32_t *child,uint32_t *offset){
    if(type>=p->bound||index>=p->count[type])return 0;*offset=0u;
    if(p->kind[type]==VK_SPV_KIND_STRUCT){
        if(index>=VK_SPV_MAX_BLOCK_MEMBERS)return 0;
        for(uint32_t i=0u;i<index;++i){uint32_t count=vk_spv_value_words(p,p->member_type[type][i],0u);if(!count||count>16u-*offset)return 0;*offset+=count;}
        *child=p->member_type[type][index];
    }else if(p->kind[type]==VK_SPV_KIND_VECTOR||p->kind[type]==VK_SPV_KIND_MATRIX||p->kind[type]==VK_SPV_KIND_ARRAY){
        *child=p->element[type];uint32_t count=vk_spv_value_words(p,*child,0u);if(!count||index>16u/count)return 0;*offset=index*count;
    }else return 0;
    return vk_spv_value_words(p,*child,0u)!=0u;
}

static int vk_spv_call_graph(const vk_spv_profile_t *p,const uint32_t *code,uint32_t function,uint32_t depth,uint8_t *state){
    if(function>=p->bound||!p->function_start[function]||depth>=32u||state[function]==1u)return 0;
    if(state[function]>=2u)return depth+state[function]-2u<32u;state[function]=1u;uint32_t height=0u;
    for(uint32_t off=p->function_start[function];off<p->function_end[function];){const uint32_t *w=code+off;uint32_t wc=w[0]>>16u;
        if((w[0]&65535u)==57u){
            uint32_t target=w[3];if(target>=p->bound||!p->function_type[target])return 0;
            uint32_t signature=p->function_type[target];
            if(wc!=4u+p->count[signature]||w[1]!=p->element[signature]||p->function_parameters[target]!=p->count[signature])return 0;
            for(uint32_t i=0u;i<p->count[signature];++i)if(w[4u+i]>=p->bound||p->type_of[w[4u+i]]!=p->member_type[signature][i])return 0;
            if(!vk_spv_call_graph(p,code,target,depth+1u,state))return 0;
            uint32_t child=state[target]-1u;if(child>height)height=child;
        }
        off+=wc;
    }
    if(depth+height>=32u)return 0;state[function]=(uint8_t)(height+2u);return 1;
}

static uint32_t vk_spv_interface_f32_components(const vk_spv_profile_t *profile,
                                                uint32_t variable,
                                                uint32_t storage) {
    uint32_t pointer, vector, scalar;
    if (profile == 0 || variable == 0u || variable >= profile->bound ||
        (pointer = profile->type_of[variable]) >= profile->bound ||
        profile->kind[pointer] != VK_SPV_KIND_POINTER ||
        profile->storage[pointer] != storage ||
        (vector = profile->element[pointer]) >= profile->bound) return 0u;
    if (profile->kind[vector] == VK_SPV_KIND_FLOAT && profile->width[vector] == 32u)
        return 1u;
    if (profile->kind[vector] != VK_SPV_KIND_VECTOR ||
        profile->count[vector] < 2u || profile->count[vector] > 4u ||
        (scalar = profile->element[vector]) >= profile->bound ||
        profile->kind[scalar] != VK_SPV_KIND_FLOAT || profile->width[scalar] != 32u)
        return 0u;
    return profile->count[vector];
}

static int vk_spv_interface_vec4_f32(const vk_spv_profile_t *profile,
                                     uint32_t variable, uint32_t storage) {
    return vk_spv_interface_f32_components(profile, variable, storage) == 4u;
}

static int vk_spv_define(vk_spv_profile_t *profile, uint32_t id) {
    if (id == 0u || id >= profile->bound || profile->defined[id] != 0u) return 0;
    profile->defined[id] = 1u;
    return 1;
}

static int vk_spv_block_layout(const vk_spv_profile_t *p, uint32_t type,
        uint32_t depth, uint32_t *alignment, uint32_t *size, int std140) {
    if (type >= p->bound || depth > 8u) return 0;
    uint32_t a = 0u, bytes = 0u;
    if ((p->kind[type] == VK_SPV_KIND_FLOAT || p->kind[type] == VK_SPV_KIND_INT) && p->width[type] == 32u) {
        a = bytes = 4u;
    } else if (p->kind[type] == VK_SPV_KIND_VECTOR) {
        uint32_t scalar = p->element[type], count = p->count[type];
        if (scalar >= p->bound || (p->kind[scalar] != VK_SPV_KIND_FLOAT && p->kind[scalar] != VK_SPV_KIND_INT) ||
            p->width[scalar] != 32u || count < 2u || count > 4u) return 0;
        a = count == 2u ? 8u : 16u; bytes = count * 4u;
    } else if (p->kind[type] == VK_SPV_KIND_MATRIX) {
        uint32_t column = p->element[type];
        if (p->count[type] != 4u || column >= p->bound || p->kind[column] != VK_SPV_KIND_VECTOR ||
            p->count[column] != 4u || p->element[column] >= p->bound ||
            p->kind[p->element[column]] != VK_SPV_KIND_FLOAT) return 0;
        a = 16u; bytes = 64u;
    } else if(p->kind[type]==VK_SPV_KIND_RUNTIME_ARRAY) {
        uint32_t child_size;
        if(std140||!p->array_stride[type]||!vk_spv_block_layout(p,p->element[type],depth+1u,&a,&child_size,0)||
           p->array_stride[type]%a||p->array_stride[type]<child_size)return 0;
        *alignment=a;*size=0u;return 1;
    } else if (p->kind[type] == VK_SPV_KIND_ARRAY) {
        uint32_t stride = p->array_stride[type], child_size;
        /* Matrix arrays require matrix-layout propagation not yet implemented. */
        if (p->element[type] >= p->bound || p->kind[p->element[type]] == VK_SPV_KIND_MATRIX ||
            !vk_spv_block_layout(p, p->element[type], depth + 1u, &a, &child_size, std140)) return 0;
        if (std140 && a < 16u) a = 16u;
        if (stride == 0u || stride % a || stride < child_size ||
            p->count[type] == 0u || p->count[type] > 65536u / stride) return 0;
        bytes = p->count[type] * stride;
    } else if (p->kind[type] == VK_SPV_KIND_STRUCT) {
        if (!p->count[type] || p->count[type] > VK_SPV_MAX_BLOCK_MEMBERS) return 0;
        a = std140?16u:4u;
        for (uint32_t m = 0u; m < p->count[type]; ++m) {
            uint32_t child = p->member_type[type][m], child_alignment, child_size;
            if(child<p->bound&&p->kind[child]==VK_SPV_KIND_RUNTIME_ARRAY&&m+1u!=p->count[type])return 0;
            if (!p->member_offset_set[type][m] || child >= p->bound ||
                !vk_spv_block_layout(p, child, depth + 1u, &child_alignment, &child_size, std140)) return 0;
            if (p->kind[child] == VK_SPV_KIND_MATRIX) {
                if (!p->member_col_major[type][m] || !p->member_matrix_stride_set[type][m] ||
                    p->member_matrix_stride[type][m] != 16u) return 0;
            } else if (p->member_col_major[type][m] || p->member_matrix_stride_set[type][m]) return 0;
            uint32_t offset = p->member_offset[type][m];
            if (offset < bytes || offset % child_alignment || offset > 65536u || child_size > 65536u - offset) return 0;
            bytes = offset + child_size;
            if (child_alignment > a) a = child_alignment;
        }
        if (bytes > 65536u - (a - 1u)) return 0;
        bytes = (bytes + a - 1u) & ~(a - 1u);
    } else return 0;
    *alignment = a; *size = bytes;
    return bytes <= 65536u;
}

static int vk_spv_push_words(const vk_spv_profile_t *p,uint32_t type,uint32_t offset,uint32_t depth,uint32_t *mask){
    uint32_t a,size;if(depth>8u||!vk_spv_block_layout(p,type,depth,&a,&size,0)||offset>128u||size>128u-offset)return 0;
    if(p->kind[type]==VK_SPV_KIND_STRUCT){
        for(uint32_t m=0u;m<p->count[type];++m)if(!vk_spv_push_words(p,p->member_type[type][m],offset+p->member_offset[type][m],depth+1u,mask))return 0;
    }else if(p->kind[type]==VK_SPV_KIND_ARRAY){
        for(uint32_t i=0u;i<p->count[type];++i)if(!vk_spv_push_words(p,p->element[type],offset+i*p->array_stride[type],depth+1u,mask))return 0;
    }else{
        for(uint32_t word=offset/4u;word<(offset+size)/4u;++word)*mask|=1u<<word;
    }
    return 1;
}

static int vk_spv_descriptor_type_for_variable(vk_spv_profile_t *profile,
                                               uint32_t variable,
                                               VkDescriptorType *type_out) {
    uint32_t pointer;
    if (profile == 0 || type_out == 0 || variable == 0u ||
        variable >= profile->bound || profile->variable[variable] == 0u ||
        (pointer = profile->type_of[variable]) >= profile->bound ||
        profile->kind[pointer] != VK_SPV_KIND_POINTER ||
        profile->descriptor_set[variable] == UINT32_MAX ||
        profile->descriptor_binding[variable] == UINT32_MAX ||
        profile->element[pointer] >= profile->bound) return 0;
    uint32_t resource_type = profile->element[pointer];
    uint32_t elements = 1u;
    if (profile->kind[resource_type] == VK_SPV_KIND_ARRAY) {
        elements = profile->count[resource_type];
        resource_type = profile->element[resource_type];
        if (elements == 0u || elements > VK_SPV_MAX_DESCRIPTORS || resource_type >= profile->bound)
            return 0;
    }
    profile->descriptor_elements[variable] = elements;
    if (profile->storage[variable] == 2u) {
        const uint32_t structure = resource_type;
        uint32_t required_size = 0u, alignment;
        int storage=profile->buffer_block[structure]!=0u;
        if ((!storage&&!profile->block[structure]) || profile->kind[structure] != VK_SPV_KIND_STRUCT ||
            !vk_spv_block_layout(profile,structure,0u,&alignment,&required_size,!storage)) return 0;
        profile->descriptor_required_size[variable] = required_size;
        *type_out = storage?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        return 1;
    }
    if (profile->storage[variable] == 0u) {
        const uint32_t sampled_image=resource_type;
        uint32_t image_type=profile->kind[resource_type]==VK_SPV_KIND_SAMPLED_IMAGE?profile->element[resource_type]:resource_type;
        if(image_type<profile->bound&&profile->kind[image_type]==VK_SPV_KIND_IMAGE&&profile->image_dim[image_type]==5u){
            uint32_t scalar=profile->element[image_type];
            if(scalar>=profile->bound||profile->width[scalar]!=32u||
               (profile->kind[scalar]!=VK_SPV_KIND_FLOAT&&profile->kind[scalar]!=VK_SPV_KIND_INT))return 0;
            uint32_t fmt=profile->image_format[image_type];
            if(profile->image_sampled[image_type]==2u&&
               !((fmt==3u&&profile->kind[scalar]==VK_SPV_KIND_FLOAT)||
                 (fmt==24u&&profile->kind[scalar]==VK_SPV_KIND_INT&&profile->signedness[scalar])||
                 (fmt==33u&&profile->kind[scalar]==VK_SPV_KIND_INT&&!profile->signedness[scalar])))return 0;
            *type_out=profile->image_sampled[image_type]==2u?VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
            return 1;
        }
        uint32_t image, sampled_type;
        if(profile->kind[resource_type]==VK_SPV_KIND_IMAGE&&profile->image_sampled[resource_type]==2u&&
           profile->kind[profile->element[resource_type]]==VK_SPV_KIND_FLOAT){
            *type_out=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;return 1;
        }
        if (profile->kind[sampled_image] != VK_SPV_KIND_SAMPLED_IMAGE ||
            (image = profile->element[sampled_image]) >= profile->bound ||
            profile->kind[image] != VK_SPV_KIND_IMAGE ||
            (sampled_type = profile->element[image]) >= profile->bound ||
            profile->kind[sampled_type] != VK_SPV_KIND_FLOAT ||
            profile->width[sampled_type] != 32u) return 0;
        *type_out = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        return 1;
    }
    return 0;
}

static int vk_spv_parse_string(const uint32_t *words, uint32_t word_count,
                               uint32_t start, const char *expected,
                               uint32_t *consumed_words) {
    uint32_t used = 0u;
    size_t char_index = 0u;
    const size_t expected_length = expected == 0 ? 0u : strlen(expected);
    int matches = expected != 0;
    int terminated = 0;
    for (uint32_t wi = start; wi < word_count && !terminated; ++wi) {
        uint32_t word = words[wi];
        ++used;
        for (uint32_t byte_index = 0u; byte_index < 4u; ++byte_index) {
            char value = (char)(word >> (byte_index * 8u));
            if (value == '\0') {
                terminated = 1;
                if (expected != 0 && char_index != expected_length) matches = 0;
                break;
            }
            if (expected == 0 || char_index >= expected_length ||
                expected[char_index] != value) matches = 0;
            ++char_index;
        }
    }
    if (consumed_words != 0) *consumed_words = used;
    return terminated && matches;
}

static int vk_spv_instruction_supported_in_function(uint32_t opcode, uint32_t word_count) {
    if(opcode==95u||opcode==98u)return word_count==5u;
    if(opcode==100u)return word_count==4u;
    if(opcode==99u)return word_count==4u;
    if(opcode==224u)return word_count==4u;
    if(opcode==225u)return word_count==3u;
    if(opcode==227u)return word_count==6u;
    if(opcode==228u)return word_count==5u;
    if(opcode==229u||(opcode>=234u&&opcode<=242u))return word_count==7u;
    if(opcode==230u||opcode==231u)return word_count==9u;
    if(opcode==232u||opcode==233u)return word_count==6u;
    if (opcode == 0u) return word_count == 1u;             /* OpNop */
    if (opcode == 8u) return word_count == 4u;             /* OpLine */
    if (opcode == 55u) return word_count==3u;
    if (opcode == 57u) return word_count>=4u&&word_count<=4u+VK_SPV_MAX_BLOCK_MEMBERS;
    if (opcode == 59u) return word_count==4u||word_count==5u;
    if (opcode == 254u) return word_count==2u;
    if (opcode == 252u||opcode==255u) return word_count==1u;
    if (opcode == 77u||opcode==83u) return word_count==5u-(opcode==83u);
    if (opcode == 78u) return word_count==6u;
    if (opcode == 84u) return word_count==4u;
    if (opcode == 143u||opcode==147u) return word_count==5u;
    if (opcode == 82u) return word_count>=6u&&word_count<=13u;
    if (opcode == 54u) return word_count == 5u;            /* OpFunction */
    if (opcode == 56u || opcode == 253u) return word_count == 1u; /* End/Return */
    if (opcode == 245u) return word_count >= 5u && (word_count & 1u) != 0u; /* OpPhi */
    if (opcode == 247u) return word_count == 3u;            /* OpSelectionMerge */
    if (opcode == 246u) return word_count == 4u;            /* OpLoopMerge */
    if (opcode == 248u) return word_count == 2u;           /* OpLabel */
    if (opcode == 249u) return word_count == 2u;           /* OpBranch */
    if (opcode == 250u) return word_count == 4u;           /* OpBranchConditional, no weights */
    if (opcode == 61u) return word_count == 4u;            /* OpLoad */
    if (opcode == 62u) return word_count == 3u;            /* OpStore */
    if(opcode==63u)return word_count==3u;
    if(opcode==68u)return word_count==5u;
    if(opcode==251u)return word_count>=3u&&(word_count&1u)==1u;
    if (opcode == 65u||opcode==66u) return word_count >= 5u && word_count <= 12u; /* bounded OpAccessChain */
    if (opcode == 80u) return word_count >= 4u;            /* OpCompositeConstruct */
    if (opcode == 81u) return word_count >= 5u && word_count <= 12u;             /* OpCompositeExtract */
    if (opcode == 79u) return word_count >= 7u && word_count <= 9u; /* OpVectorShuffle */
    if (opcode == 87u) return word_count == 5u || word_count == 7u; /* optional Bias, validated by environment */
    if (opcode == 88u) return word_count == 7u;             /* explicit Lod operand */
    if (opcode == 126u || opcode == 127u) return word_count == 4u; /* Negate */
    if (opcode == 169u) return word_count == 6u;           /* OpSelect */
    if (opcode >= 164u && opcode <= 168u)
        return opcode == 168u ? word_count == 4u : word_count == 5u; /* logical ops */
    if (opcode >= 170u && opcode <= 179u) return word_count == 5u; /* integer comparisons */
    if ((opcode >= 180u && opcode <= 191u)) return word_count == 5u; /* float comparisons */
    if (opcode == 142u || opcode == 148u) return word_count == 5u; /* VectorTimesScalar / Dot */
    if (opcode == 144u || opcode == 145u || opcode == 146u)
        return word_count == 5u;                           /* vector/matrix products */
    if (opcode == 184u) return word_count == 5u; /* FOrdLessThan */
    if (opcode == 12u) return word_count >= 6u && word_count <= 8u; /* GLSL.std.450 subset */
    if ((opcode >= 109u && opcode <= 115u) || opcode == 124u)
        return word_count == 4u; /* 32-bit scalar/vector conversion */
    if (opcode == 128u || opcode == 129u || opcode == 130u || opcode == 131u ||
        opcode == 132u || opcode == 133u || opcode == 134u || opcode == 135u ||
        opcode == 136u || opcode == 137u || opcode == 138u || opcode == 139u)
        return word_count == 5u; /* integer/float scalar-vector arithmetic */
    if (opcode >= 194u && opcode <= 199u) return word_count == 5u; /* bitwise/shift */
    if (opcode == 200u) return word_count == 4u; /* OpNot */
    return 0;
}

static int vk_spv_scalar_and_components(const vk_spv_profile_t *profile,
                                        uint32_t type_id,
                                        uint32_t *scalar_type,
                                        uint32_t *components) {
    uint32_t scalar;
    if (profile == 0 || type_id == 0u || type_id >= profile->bound) return 0;
    if (profile->kind[type_id] == VK_SPV_KIND_VECTOR) {
        scalar = profile->element[type_id];
        if (scalar >= profile->bound || profile->count[type_id] == 0u ||
            profile->count[type_id] > 4u) return 0;
        *components = profile->count[type_id];
    } else {
        scalar = type_id;
        *components = 1u;
    }
    if (profile->kind[scalar] != VK_SPV_KIND_INT &&
        profile->kind[scalar] != VK_SPV_KIND_FLOAT &&
        profile->kind[scalar] != VK_SPV_KIND_BOOL) return 0;
    *scalar_type = scalar;
    return 1;
}

static int vk_spv_arithmetic_type(const vk_spv_profile_t *p,uint32_t a,uint32_t b){
    uint32_t as,bs,ac,bc;
    if(a==b)return 1;
    return vk_spv_scalar_and_components(p,a,&as,&ac)&&vk_spv_scalar_and_components(p,b,&bs,&bc)&&ac==bc&&
        p->kind[as]==VK_SPV_KIND_INT&&p->kind[bs]==VK_SPV_KIND_INT&&p->width[as]==p->width[bs];
}

static int vex_spv_parse_environment(const vk_shader_module_impl_t *shader, uint8_t gl_environment,
                                VkShaderStageFlagBits stage,
                                vk_spv_profile_t *profile) {
    const uint32_t *code;
    uint32_t word_count;
    uint32_t entry_id = 0u;
    uint32_t function_id = 0u;
    uint32_t in_function = 0u;
    uint32_t function_end = 0u;
    uint32_t label_count = 0u;
    uint32_t input_count = 0u;
    uint32_t output_count = 0u;
    uint32_t vertex_model = stage == VK_SHADER_STAGE_VERTEX_BIT ? 0u : stage==VK_SHADER_STAGE_COMPUTE_BIT?5u:4u;
    if (shader == 0 || profile == 0 || shader->code == 0 ||
        shader->code_size < 5u * sizeof(uint32_t) ||
        (shader->code_size & 3u) != 0u) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
    code = shader->code;
    word_count = (uint32_t)(shader->code_size / sizeof(uint32_t));
    if (code[0] != 0x07230203u || code[1] != 0x00010000u ||
        code[3] == 0u || code[3] > VK_SPV_MAX_IDS || code[4] != 0u) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
    memset(profile, 0, sizeof(*profile));
    profile->gl_environment = gl_environment;
    profile->bound = code[3];
    profile->stage = (uint8_t)stage;
    for (uint32_t i = 0u; i < profile->bound; ++i) {
        profile->builtin[i] = UINT32_MAX;
        profile->workgroup_offset[i]=UINT32_MAX;
        profile->location[i] = UINT32_MAX;
        profile->descriptor_set[i] = UINT32_MAX;
        profile->descriptor_binding[i] = UINT32_MAX;
    }
    for (uint32_t offset = 5u; offset < word_count;) {
        uint32_t first = code[offset];
        uint32_t wc = first >> 16u;
        uint32_t op = first & 0xffffu;
        const uint32_t *w = &code[offset];
        if (wc == 0u || wc > word_count - offset) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
        if (in_function != 0u) {
            if (op==54u||(!vk_spv_instruction_supported_in_function(op, wc)&&!(gl_environment&&stage==VK_SHADER_STAGE_FRAGMENT_BIT&&op>=207u&&op<=209u&&wc==4u))) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            if(op==12u){
                /* Keep load-time support in agreement with execution. */
                uint32_t arguments=w[4]==4u||w[4]==6u||w[4]==8u||w[4]==9u||w[4]==10u||
                    w[4]==31u||w[4]==32u||w[4]==66u?1u:
                    w[4]==37u||w[4]==40u||w[4]==48u?2u:
                    w[4]==43u||w[4]==46u||w[4]==49u?3u:0u;
                if(!arguments||wc!=5u+arguments||!profile->glsl_std450_import_id||w[3]!=profile->glsl_std450_import_id)
                    return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            }
            if(op==55u){
                uint32_t signature=profile->function_type[function_id],index=profile->function_parameters[function_id];
                if(index>=profile->count[signature]||w[1]!=profile->member_type[signature][index]||!vk_spv_define(profile,w[2]))return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                profile->function_parameter[function_id][index]=w[2];++profile->function_parameters[function_id];
                profile->owner[w[2]]=function_id;profile->type_of[w[2]]=w[1];
            }else if(op==59u){
                if(w[1]>=profile->bound||profile->kind[w[1]]!=VK_SPV_KIND_POINTER||profile->storage[w[1]]!=7u||w[3]!=7u||
                   !vk_spv_value_words(profile,profile->element[w[1]],0u)||!vk_spv_define(profile,w[2])||
                   (wc==5u&&(w[4]>=profile->bound||profile->type_of[w[4]]!=profile->element[w[1]])))return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                profile->owner[w[2]]=function_id;profile->type_of[w[2]]=w[1];profile->variable[w[2]]=1u;profile->storage[w[2]]=7u;
            }else if(op!=0u&&op!=8u&&op!=56u&&op!=62u&&op!=246u&&op!=247u&&op!=248u&&op!=249u&&op!=250u&&op!=252u&&op!=253u&&op!=254u&&op!=255u&&op!=224u&&op!=225u&&op!=228u&&op!=99u&&op!=63u&&op!=251u){
                if(wc<3u||w[1]>=profile->bound||!vk_spv_define(profile,w[2]))return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                profile->owner[w[2]]=function_id;profile->type_of[w[2]]=w[1];
            }
            if(op==252u&&stage!=VK_SHADER_STAGE_FRAGMENT_BIT)return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            if(op==253u&&profile->kind[profile->element[profile->function_type[function_id]]]!=VK_SPV_KIND_VOID)return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            if(op==254u&&(w[1]>=profile->bound||profile->type_of[w[1]]!=profile->element[profile->function_type[function_id]]))return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            if(gl_environment&&stage==VK_SHADER_STAGE_FRAGMENT_BIT&&(op==87u||(op>=207u&&op<=209u)))profile->gl_needs_quad=1;
            if (op == 87u) {
                if (stage != VK_SHADER_STAGE_FRAGMENT_BIT) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                if(wc==7u){
                    const uint32_t bias=w[6];
                    if(!gl_environment||w[5]!=1u||bias>=profile->bound||profile->type_of[bias]>=profile->bound||
                       profile->kind[profile->type_of[bias]]!=VK_SPV_KIND_FLOAT||profile->width[profile->type_of[bias]]!=32u)
                        return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                }
                profile->implicit_samples = 1u;
            }
            if (op == 88u && w[5] != 2u) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            if (op == 248u) {
                const uint32_t label = w[1];
                if (label == 0u || label >= profile->bound ||
                    profile->label_word[label] != 0u || ++label_count > VK_SPV_MAX_BLOCKS)
                    return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                if(!vk_spv_define(profile,label))return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                profile->owner[label]=function_id;
                profile->label_word[label] = offset + 1u;
            }
            if (op == 56u) {
                in_function = 0u;
                function_end = 1u;
                profile->function_end[function_id]=offset;
                if(function_id==entry_id)profile->function_end_word=offset;
            }
            offset += wc;
            continue;
        }
        if (op == 17u) { /* Implemented Vulkan shader capabilities only. */
            if (wc != 2u) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            if (w[1] == 1u) profile->shader_capability = 1u;
            else if (w[1] == 28u) profile->uniform_array_dynamic = 1u;
            else if (w[1] == 29u) profile->sampled_array_dynamic = 1u;
            else if(w[1]==30u)profile->storage_buffer_array_dynamic=1u;
            else if(w[1]==31u)profile->storage_image_array_dynamic=1u;
            else if(w[1]==46u||w[1]==47u){} /* SampledBuffer / ImageBuffer */
            else return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
        } else if (op == 14u) { /* OpMemoryModel Logical GLSL450 */
            if (wc != 3u || w[1] != 0u || w[2] != 1u) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            profile->memory_model = 1u;
        } else if (op == 11u) { /* OpExtInstImport, private GLSL.std.450 subset */
            uint32_t string_words = 0u;
            if (wc < 3u || profile->glsl_std450_import_id != 0u ||
                w[1] >= profile->bound ||
                !vk_spv_parse_string(w, wc, 2u, "GLSL.std.450", &string_words) ||
                2u + string_words != wc || !vk_spv_define(profile, w[1])) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            profile->glsl_std450_import_id = w[1];
        } else if (op == 15u) { /* OpEntryPoint */
            uint32_t name_words = 0u;
            uint32_t name_matches;
            if (wc < 4u) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            name_matches = (uint32_t)vk_spv_parse_string(w, wc, 3u, "main", &name_words);
            if (w[1] == vertex_model && name_matches != 0u) {
                if (entry_id != 0u) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                entry_id = w[2];
            }
            if (name_words == 0u || 3u + name_words > wc) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
        } else if(op==16u) {
            if(w[1]!=entry_id)return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            if(stage==VK_SHADER_STAGE_COMPUTE_BIT&&wc==6u&&w[2]==17u){
                if(profile->local_size[0])return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                uint64_t count=1u;
                for(uint32_t axis=0u;axis<3u;++axis){if(!w[3u+axis]||w[3u+axis]>(axis==2u?64u:128u))return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                    profile->local_size[axis]=w[3u+axis];count*=w[3u+axis];}
                if(count>128u)return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            }else if(stage==VK_SHADER_STAGE_FRAGMENT_BIT&&wc==3u&&w[2]==7u)profile->fragment_origin_upper_left=1u;
            else if(gl_environment&&stage==VK_SHADER_STAGE_FRAGMENT_BIT&&wc==3u&&w[2]==8u){}
            else return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
        } else if (op == 71u) { /* OpDecorate */
            if (wc < 3u || w[1] == 0u || w[1] >= profile->bound) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            if (w[2] == 0u && wc == 3u) { /* RelaxedPrecision: CPU float32 provides at least the required precision. */
            } else if (w[2] == 2u || w[2]==3u) {
                if (wc != 3u) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                if(w[2]==2u)profile->block[w[1]]=1u;else profile->buffer_block[w[1]]=1u;
            } else if(w[2]==24u||w[2]==25u) {
                if(wc!=3u)return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;profile->access[w[1]]|=w[2]==24u?2u:1u;
            } else if(w[2]==19u||w[2]==20u||w[2]==21u||w[2]==23u) {
                if(wc!=3u)return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            } else if (w[2] == 6u) { /* ArrayStride */
                if (wc != 4u || w[3] == 0u || w[3] > 65536u || profile->array_stride[w[1]]) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                profile->array_stride[w[1]] = w[3];
            } else if (w[2] == 11u || w[2] == 30u || w[2] == 33u || w[2] == 34u) {
                if (wc != 4u) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                if (w[2] == 11u) profile->builtin[w[1]] = w[3];
                else if (w[2] == 30u) profile->location[w[1]] = w[3];
                else if (w[2] == 33u) profile->descriptor_binding[w[1]] = w[3];
                else profile->descriptor_set[w[1]] = w[3];
            } else return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
        } else if (op == 72u) { /* Bounded UBO member layout metadata */
            if (wc < 4u || w[1] == 0u || w[1] >= profile->bound ||
                w[2] >= VK_SPV_MAX_BLOCK_MEMBERS) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            if(w[3]==24u||w[3]==25u){
                if(wc!=4u)return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;profile->member_access[w[1]][w[2]]|=w[3]==24u?2u:1u;
            }else if(w[3]==19u||w[3]==20u||w[3]==21u||w[3]==23u){if(wc!=4u)return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            }else if (w[3] == 35u) { /* Offset */
                if (wc != 5u ||
                    profile->member_offset_set[w[1]][w[2]] != 0u) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                profile->member_offset_set[w[1]][w[2]] = 1u;
                profile->member_offset[w[1]][w[2]] = w[4];
                if (w[2] == 0u && w[4] == 0u)
                    profile->member0_offset_zero[w[1]] = 1u;
            } else if (w[3] == 5u) { /* ColMajor */
                if (wc != 4u || profile->member_col_major[w[1]][w[2]] != 0u)
                    return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                profile->member_col_major[w[1]][w[2]] = 1u;
            } else if (w[3] == 7u) { /* MatrixStride */
                if (wc != 5u || w[4] != 16u ||
                    profile->member_matrix_stride_set[w[1]][w[2]] != 0u) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                profile->member_matrix_stride_set[w[1]][w[2]] = 1u;
                profile->member_matrix_stride[w[1]][w[2]] = w[4];
            } else return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
        } else if (op == 19u || op == 20u) { /* Void / Bool */
            if (wc != 2u || !vk_spv_define(profile, w[1])) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            profile->kind[w[1]] = op == 19u ? VK_SPV_KIND_VOID : VK_SPV_KIND_BOOL;
            profile->width[w[1]] = op == 20u ? 1u : 0u;
        } else if (op == 21u) { /* OpTypeInt */
            if (wc != 4u || w[2] != 32u || w[3] > 1u || !vk_spv_define(profile, w[1])) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            profile->kind[w[1]] = VK_SPV_KIND_INT;
            profile->width[w[1]] = w[2];
            profile->signedness[w[1]] = w[3];
        } else if (op == 22u) { /* OpTypeFloat */
            if (wc != 3u || w[2] != 32u || !vk_spv_define(profile, w[1])) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            profile->kind[w[1]] = VK_SPV_KIND_FLOAT;
            profile->width[w[1]] = w[2];
        } else if (op == 23u) { /* OpTypeVector */
            if (wc != 4u || w[3] == 0u || w[3] > 4u || w[2] >= profile->bound ||
                !vk_spv_define(profile, w[1])) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            profile->kind[w[1]] = VK_SPV_KIND_VECTOR;
            profile->element[w[1]] = w[2];
            profile->count[w[1]] = w[3];
        } else if (op == 24u) { /* bounded float32 mat4 */
            if (wc != 4u || w[2] >= profile->bound || w[3] < 2u || w[3] > 4u ||
                profile->kind[w[2]] != VK_SPV_KIND_VECTOR ||
                profile->count[w[2]] < 2u || profile->count[w[2]] > 4u ||
                profile->element[w[2]] >= profile->bound ||
                profile->kind[profile->element[w[2]]] != VK_SPV_KIND_FLOAT ||
                profile->width[profile->element[w[2]]] != 32u ||
                !vk_spv_define(profile, w[1])) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            profile->kind[w[1]] = VK_SPV_KIND_MATRIX;
            profile->element[w[1]] = w[2];
            profile->count[w[1]] = w[3];
        } else if (op == 25u) { /* restricted 2D sampled image */
            if(wc!=9u||w[2]>=profile->bound||w[4]||w[5]>1u||w[6]||
               !((w[3]==1u&&((w[7]==1u&&w[8]==0u&&w[5]==0u)||(w[7]==2u&&w[8]==4u)))||
                 (w[3]==5u&&!w[5]&&((w[7]==1u&&!w[8])||(w[7]==2u&&(w[8]==3u||w[8]==24u||w[8]==33u)))))||
               !vk_spv_define(profile,w[1]))return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            profile->kind[w[1]] = VK_SPV_KIND_IMAGE;
            profile->image_dim[w[1]]=(uint8_t)w[3];profile->image_format[w[1]]=(uint8_t)w[8];
            profile->image_arrayed[w[1]]=(uint8_t)w[5];profile->image_sampled[w[1]]=(uint8_t)w[7];
            profile->element[w[1]] = w[2];
        } else if (op == 27u) { /* OpTypeSampledImage */
            if (wc != 3u || w[2] >= profile->bound ||
                profile->kind[w[2]] != VK_SPV_KIND_IMAGE ||
                !vk_spv_define(profile, w[1])) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            profile->kind[w[1]] = VK_SPV_KIND_SAMPLED_IMAGE;
            profile->element[w[1]] = w[2];
        } else if (op == 28u) { /* Fixed-size descriptor arrays. */
            if (wc != 4u || w[2] >= profile->bound || w[3] >= profile->bound ||
                profile->defined[w[2]] == 0u || profile->constant_integer[w[3]] == 0u ||
                profile->constant_value[w[3]] == 0u ||
                profile->constant_value[w[3]] > 16384u ||
                !vk_spv_define(profile, w[1])) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            profile->kind[w[1]] = VK_SPV_KIND_ARRAY;
            profile->element[w[1]] = w[2];
            profile->count[w[1]] = profile->constant_value[w[3]];
        }else if(op==29u){
            if(wc!=3u||w[2]>=profile->bound||!vk_spv_define(profile,w[1]))return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            profile->kind[w[1]]=VK_SPV_KIND_RUNTIME_ARRAY;profile->element[w[1]]=w[2];
        } else if (op == 30u) { /* bounded vec4-block OpTypeStruct */
            if (wc < 3u || wc > 2u + VK_SPV_MAX_BLOCK_MEMBERS ||
                !vk_spv_define(profile, w[1])) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            profile->kind[w[1]] = VK_SPV_KIND_STRUCT;
            profile->count[w[1]] = wc - 2u;
            for (uint32_t member = 0u; member < wc - 2u; ++member) {
                if (w[2u + member] >= profile->bound) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                profile->member_type[w[1]][member] = w[2u + member];
            }
            profile->element[w[1]] = w[2];
        } else if (op == 32u) { /* OpTypePointer */
            if (wc != 4u || w[2] > 12u || w[3] >= profile->bound ||
                !vk_spv_define(profile, w[1])) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            profile->kind[w[1]] = VK_SPV_KIND_POINTER;
            profile->storage[w[1]] = (uint8_t)w[2];
            profile->element[w[1]] = w[3];
        } else if(op==33u){
            if(wc<3u||wc>3u+VK_SPV_MAX_BLOCK_MEMBERS||w[2]>=profile->bound||!vk_spv_define(profile,w[1]))return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            profile->kind[w[1]]=VK_SPV_KIND_FUNCTION;profile->element[w[1]]=w[2];profile->count[w[1]]=wc-3u;
            for(uint32_t i=0u;i<wc-3u;++i){if(w[3u+i]>=profile->bound)return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;profile->member_type[w[1]][i]=w[3u+i];}
        } else if (op == 41u || op == 42u || op == 43u || op == 44u || op == 46u) {
            uint32_t id;
            if (wc < 3u || w[1] >= profile->bound) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            id = w[2];
            if (!vk_spv_define(profile, id)) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            profile->type_of[id]=w[1];
            if (op == 43u && (wc != 4u || w[1] >= profile->bound ||
                (profile->kind[w[1]] != VK_SPV_KIND_INT &&
                 profile->kind[w[1]] != VK_SPV_KIND_FLOAT))) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            if (op == 43u && profile->kind[w[1]] == VK_SPV_KIND_INT) {
                profile->constant_integer[id] = 1u;
                profile->constant_value[id] = w[3];
            }
            if(op==44u&&(!vk_spv_value_words(profile,w[1],0u)||wc!=3u+profile->count[w[1]]))return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
        } else if (op == 59u) { /* Global Input/Output/Uniform/PushConstant OpVariable */
            if ((wc != 4u&&wc!=5u) || (wc==5u&&w[3]!=6u) || w[1] >= profile->bound || w[2] == 0u ||
                w[2] >= profile->bound || profile->kind[w[1]] != VK_SPV_KIND_POINTER ||
                profile->storage[w[1]] != (uint8_t)w[3] ||
                (w[3] != 0u && w[3] != 1u && w[3] != 2u && w[3] != 3u && w[3] != 4u && w[3] != 6u && w[3] != 9u) ||
                !vk_spv_define(profile, w[2])) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            profile->storage[w[2]] = (uint8_t)w[3];
            profile->variable[w[2]] = 1u;
            profile->type_of[w[2]] = w[1];
        }else if(op==54u){
            if(wc!=5u||entry_id==0u||(w[3]&~15u)||((w[3]&3u)==3u)||w[4]>=profile->bound||profile->kind[w[4]]!=VK_SPV_KIND_FUNCTION||
               w[1]!=profile->element[w[4]]||!vk_spv_define(profile,w[2]))return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            if(w[2]==entry_id&&(profile->kind[w[1]]!=VK_SPV_KIND_VOID||profile->count[w[4]]))return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            function_id=w[2];profile->function_start[function_id]=offset;profile->function_type[function_id]=w[4];
            if(function_id==entry_id){profile->function_id=entry_id;profile->function_word=offset;}
            in_function=1u;function_end=0u;
        } else if (op == 2u || op == 3u || op == 4u || op == 5u || op == 6u ||
                   op == 7u || op == 8u || op == 10u) {
            /* Source/debug text is inert metadata. */
        } else {
            return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
        }
        offset += wc;
    }
    if (in_function != 0u || function_end == 0u || label_count == 0u ||
        profile->function_id != entry_id || profile->shader_capability == 0u ||
        profile->memory_model == 0u) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
    /* Accept only structured selections and zero-control-mask loops. Every
       back-edge must be a loop continue->header edge or a branch to that
       loop's continue block; execution is separately instruction-budgeted. */
    {
        uint32_t source_label = 0u;
        uint32_t source_word = 0u;
        for (uint32_t offset = 5u;offset < word_count;) {
            const uint32_t first = code[offset];
            const uint32_t wc = first >> 16u;
            const uint32_t op = first & 0xffffu;
            const uint32_t *w = &code[offset];
            if (wc == 0u || wc > word_count - offset) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            if(op==54u){source_label=0u;function_id=w[2];}
            if(op==56u){source_label=0u;function_id=0u;}
            if (op == 248u) {
                source_label = w[1];
                source_word = offset;
            } else if (op == 246u) {
                uint32_t merge, continuing;
                if (source_label == 0u || wc != 4u || w[3] != 0u ||
                    profile->loop_header[source_label] != 0u ||
                    (merge = w[1]) == 0u || merge >= profile->bound ||
                    (continuing = w[2]) == 0u || continuing >= profile->bound ||
                    merge == source_label || continuing == source_label ||
                    merge == continuing || profile->owner[merge]!=function_id || profile->owner[continuing]!=function_id || profile->label_word[merge] == 0u ||
                    profile->label_word[continuing] == 0u ||
                    profile->label_word[continuing] - 1u <= source_word ||
                    profile->label_word[merge] - 1u <= profile->label_word[continuing] - 1u)
                    return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                profile->loop_header[source_label] = 1u;
                profile->loop_merge_label[source_label] = merge;
                profile->loop_continue_label[source_label] = continuing;
            } else if (op == 247u) {
                uint32_t merge = w[1];
                if (source_label == 0u || wc != 3u || w[2] != 0u ||
                    merge == 0u || merge >= profile->bound ||
                    profile->label_word[merge] == 0u || profile->owner[merge]!=function_id ||
                    profile->label_word[merge] - 1u <= source_word) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            } else if(op==251u){
                if(!source_label||w[1]>=profile->bound||profile->kind[profile->type_of[w[1]]]!=VK_SPV_KIND_INT)return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                for(uint32_t i=2u;i<wc;i+=2u){uint32_t target=w[i];
                    if(!target||target>=profile->bound||!profile->label_word[target]||profile->owner[target]!=function_id)return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                }
            } else if (op == 249u || op == 250u) {
                uint32_t target_count = op == 250u ? 2u : 1u;
                uint32_t target_start = op == 250u ? 2u : 1u;
                if (source_label == 0u) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                for (uint32_t i = 0u; i < target_count; ++i) {
                    const uint32_t target = w[target_start + i];
                    if (target == 0u || target >= profile->bound ||
                        profile->label_word[target] == 0u || profile->owner[target]!=function_id) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                    if (profile->label_word[target] - 1u <= source_word) {
                        int loop_edge = 0;
                        for (uint32_t header = 1u; header < profile->bound; ++header) {
                            uint32_t merge, continuing, header_word;
                            if (profile->loop_header[header] == 0u || profile->owner[header]!=function_id) continue;
                            merge = profile->loop_merge_label[header];
                            continuing = profile->loop_continue_label[header];
                            header_word = profile->label_word[header] - 1u;
                            if (source_word > header_word &&
                                source_word < profile->label_word[merge] - 1u &&
                                ((target == continuing) ||
                                 (target == header && source_label == continuing))) {
                                loop_edge = 1;
                                break;
                            }
                        }
                        if (!loop_edge) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                    }
                }
            } else if (op == 245u) {
                for (uint32_t i = 4u; i < wc; i += 2u) {
                    const uint32_t predecessor = w[i];
                    if (predecessor == 0u || predecessor >= profile->bound ||
                        profile->label_word[predecessor] == 0u || profile->owner[predecessor]!=function_id) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                }
            }
            offset += wc;
        }
    }
    uint8_t call_state[VK_SPV_MAX_IDS]={0};
    for(uint32_t id=1u;id<profile->bound;++id)if(profile->function_start[id]){
        uint32_t signature=profile->function_type[id];
        if(profile->function_parameters[id]!=profile->count[signature]||!profile->function_end[id]||
           !vk_spv_call_graph(profile,code,id,0u,call_state))return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
    }
    for (uint32_t id = 1u; id < profile->bound; ++id) {
        if (profile->variable[id] != 0u && profile->storage[id] == 1u) {
            ++input_count;
            if (stage == VK_SHADER_STAGE_VERTEX_BIT) {
                if (profile->builtin[id] == 42u) {
                    if (profile->location[id] != UINT32_MAX || profile->input_id != 0u) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                    profile->input_id = id;
                } else if(profile->builtin[id]==43u){
                    if(profile->location[id]!=UINT32_MAX||profile->instance_id)return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;profile->instance_id=id;
                } else if (profile->builtin[id] == UINT32_MAX &&
                           profile->location[id] < VK_SPV_MAX_INTERFACE_LOCATIONS) {
                    const uint32_t location = profile->location[id];
                    if (profile->attribute_id[location] != 0u) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                    profile->attribute_id[location] = id;
                    profile->attribute_mask |= (uint8_t)(1u << location);
                } else {
                    return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                }
            }
            if(stage==VK_SHADER_STAGE_COMPUTE_BIT){
                uint32_t builtin=profile->builtin[id];
                if(builtin<24u||builtin>29u||profile->location[id]!=UINT32_MAX||profile->compute_builtin[builtin-24u])return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                uint32_t pointer=profile->type_of[id],type=profile->element[pointer],scalar=type;
                if(builtin!=29u){if(profile->kind[type]!=VK_SPV_KIND_VECTOR||profile->count[type]!=3u)return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;scalar=profile->element[type];}
                if(scalar>=profile->bound||profile->kind[scalar]!=VK_SPV_KIND_INT||profile->width[scalar]!=32u||profile->signedness[scalar])return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                profile->compute_builtin[builtin-24u]=id;
            }
            if (stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
                if (profile->builtin[id] == 17u) { /* FrontFacing */
                    if (profile->front_facing_id || profile->location[id] != UINT32_MAX) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                    uint32_t pointer=profile->type_of[id],type=profile->element[pointer];
                    if(profile->kind[pointer]!=VK_SPV_KIND_POINTER || profile->storage[pointer]!=1u ||
                       type>=profile->bound || profile->kind[type]!=VK_SPV_KIND_BOOL) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                    profile->front_facing_id=id;
                } else if (profile->builtin[id] == 15u) { /* FragCoord */
                    if (profile->frag_coord_id || profile->location[id] != UINT32_MAX ||
                        !vk_spv_interface_vec4_f32(profile,id,1u)) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                    profile->frag_coord_id=id;
                } else {
                    if (profile->builtin[id] != UINT32_MAX ||
                        profile->location[id] >= VK_SPV_MAX_INTERFACE_LOCATIONS) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                    const uint32_t location = profile->location[id];
                    if (profile->fragment_varying_id[location] != 0u) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                    profile->fragment_varying_id[location] = id;
                    profile->fragment_varying_mask |= (uint8_t)(1u << location);
                }
            }
        } else if(profile->variable[id]&&profile->storage[id]==4u){
            uint32_t pointer=profile->type_of[id],bytes=vk_spv_shared_bytes(profile,profile->element[pointer],0u);
            if(stage!=VK_SHADER_STAGE_COMPUTE_BIT||!bytes||bytes>16384u-profile->workgroup_size)return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            profile->workgroup_offset[id]=profile->workgroup_size;profile->workgroup_size+=bytes;
        } else if(profile->variable[id]&&profile->storage[id]==6u){
            if(!vk_spv_value_words(profile,profile->element[profile->type_of[id]],0u))return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
        } else if (profile->variable[id] != 0u && profile->storage[id] == 3u) {
            ++output_count;
            if (stage == VK_SHADER_STAGE_VERTEX_BIT && profile->builtin[id] == 0u) {
                if (profile->position_id != 0u) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                profile->position_id = id;
            }
            if (stage == VK_SHADER_STAGE_VERTEX_BIT && profile->builtin[id] == UINT32_MAX) {
                if (profile->location[id] >= VK_SPV_MAX_INTERFACE_LOCATIONS) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                const uint32_t location = profile->location[id];
                if (profile->vertex_varying_id[location] != 0u) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                profile->vertex_varying_id[location] = id;
                profile->vertex_varying_mask |= (uint8_t)(1u << location);
            }
            if (stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
                if (profile->builtin[id] != UINT32_MAX || profile->location[id] != 0u ||
                    profile->output_id != 0u) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                profile->output_id = id;
            }
        } else if (profile->variable[id] != 0u && profile->storage[id] == 2u) {
            VkDescriptorType descriptor_type;
            if (profile->descriptor_count >= VK_SPV_MAX_DESCRIPTORS ||
                !vk_spv_descriptor_type_for_variable(profile, id, &descriptor_type)) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            for (uint32_t prior = 1u; prior < id; ++prior) {
                if (profile->descriptor_type_by_id[prior] != 0u &&
                    profile->descriptor_set[prior] == profile->descriptor_set[id] &&
                    profile->descriptor_binding[prior] == profile->descriptor_binding[id])
                    return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            }
            profile->descriptor_type_by_id[id] = (uint8_t)descriptor_type;
            if (profile->descriptor_elements[id] > VK_SPV_MAX_DESCRIPTORS - profile->descriptor_count) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            profile->descriptor_count += (uint8_t)profile->descriptor_elements[id];
        } else if(gl_environment && profile->variable[id] && profile->storage[id]==0u && vk_spv_value_words(profile,profile->element[profile->type_of[id]],0u)) {
            profile->gl_uniform[id]=1u;
        } else if (profile->variable[id] != 0u && profile->storage[id] == 0u) {
            VkDescriptorType descriptor_type;
            if (profile->descriptor_count >= VK_SPV_MAX_DESCRIPTORS ||
                !vk_spv_descriptor_type_for_variable(profile, id, &descriptor_type)) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            for (uint32_t prior = 1u; prior < id; ++prior) {
                if (profile->descriptor_type_by_id[prior] != 0u &&
                    profile->descriptor_set[prior] == profile->descriptor_set[id] &&
                    profile->descriptor_binding[prior] == profile->descriptor_binding[id])
                    return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            }
            profile->descriptor_type_by_id[id] = (uint8_t)descriptor_type;
            if (profile->descriptor_elements[id] > VK_SPV_MAX_DESCRIPTORS - profile->descriptor_count) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            profile->descriptor_count += (uint8_t)profile->descriptor_elements[id];
        } else if (profile->variable[id] != 0u && profile->storage[id] == 9u) {
            if (profile->push_constant_count != 0u) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            profile->push_constant_count = 1u;
            profile->push_constant_variable_id = id;
        }
    }
    if(profile->push_constant_count){
        uint32_t variable=profile->push_constant_variable_id,pointer=profile->type_of[variable];
        if(pointer>=profile->bound||profile->kind[pointer]!=VK_SPV_KIND_POINTER||profile->storage[pointer]!=9u)return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
        uint32_t structure=profile->element[pointer];
        if(structure>=profile->bound||profile->kind[structure]!=VK_SPV_KIND_STRUCT||!profile->block[structure]||
           !vk_spv_push_words(profile,structure,0u,0u,&profile->push_word_mask))return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
    }
    if (stage == VK_SHADER_STAGE_VERTEX_BIT) {
        if (input_count > VK_SPV_MAX_INTERFACE_LOCATIONS + 1u ||
            input_count != (uint32_t)(profile->input_id != 0u) + (uint32_t)(profile->instance_id != 0u) +
                           vk_spv_popcount8(profile->attribute_mask) ||
            output_count != 1u + vk_spv_popcount8(profile->vertex_varying_mask) ||
            profile->position_id == 0u) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
        for (uint32_t location = 0u;
             location < VK_SPV_MAX_INTERFACE_LOCATIONS; ++location) {
            if ((profile->attribute_mask & (1u << location)) != 0u) {
                uint32_t components = vk_spv_interface_f32_components(profile,
                    profile->attribute_id[location], 1u);
                if (components == 0u) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                profile->attribute_components[location] = (uint8_t)components;
            }
            if ((profile->vertex_varying_mask & (1u << location)) != 0u) {
                uint32_t components = vk_spv_interface_f32_components(profile,
                    profile->vertex_varying_id[location], 3u);
                if (components == 0u) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
                profile->vertex_varying_components[location] = (uint8_t)components;
            }
        }
        if (!vk_spv_interface_vec4_f32(profile, profile->position_id, 3u)) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
        if(profile->instance_id){uint32_t pointer=profile->type_of[profile->instance_id],type=profile->element[pointer];
            if(profile->kind[pointer]!=VK_SPV_KIND_POINTER||profile->storage[pointer]!=1u||type>=profile->bound||
               profile->kind[type]!=VK_SPV_KIND_INT||profile->width[type]!=32u)return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;}
        if (profile->input_id != 0u) {
            uint32_t input_pointer = profile->type_of[profile->input_id];
            if (input_pointer >= profile->bound ||
                profile->kind[input_pointer] != VK_SPV_KIND_POINTER ||
                profile->storage[input_pointer] != 1u ||
                profile->element[input_pointer] >= profile->bound ||
                profile->kind[profile->element[input_pointer]] != VK_SPV_KIND_INT ||
                profile->width[profile->element[input_pointer]] != 32u) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
        }
    } else if (stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
        if (input_count != vk_spv_popcount8(profile->fragment_varying_mask)+(uint32_t)(profile->front_facing_id!=0u)+(uint32_t)(profile->frag_coord_id!=0u) ||
            output_count != 1u || profile->output_id == 0u ||
            (!gl_environment && profile->fragment_origin_upper_left == 0u) ||
            !vk_spv_interface_vec4_f32(profile, profile->output_id, 3u)) return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
        for (uint32_t location = 0u;
             location < VK_SPV_MAX_INTERFACE_LOCATIONS; ++location) {
            if ((profile->fragment_varying_mask & (1u << location)) != 0u &&
                (profile->fragment_varying_components[location] =
                    (uint8_t)vk_spv_interface_f32_components(profile,
                        profile->fragment_varying_id[location], 1u)) == 0u)
                return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
        }
    } else if(stage==VK_SHADER_STAGE_COMPUTE_BIT){
        if(output_count||!profile->local_size[0])return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
    } else return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
    /* Features also follow actual indexing, not only optional module
       capability declarations emitted by a particular compiler. */
    for(uint32_t off=5u;off<word_count;){uint32_t op=code[off]&65535u,wc=code[off]>>16u;
        if((op==65u||op==66u)&&wc>=5u){uint32_t base=code[off+3u],index=code[off+4u];
            if(base<profile->bound&&index<profile->bound&&profile->descriptor_elements[base]>1u&&!profile->constant_integer[index]){
                uint32_t type=profile->descriptor_type_by_id[base];
                if(type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)profile->storage_buffer_array_dynamic=1u;
                else if(type==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)profile->storage_image_array_dynamic=1u;
                else if(type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)profile->uniform_array_dynamic=1u;
                else if(type==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)profile->sampled_array_dynamic=1u;
                else if(type==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER||type==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)return gl_environment ? VEX_RENDER_PROFILE_FAILURE(__LINE__) : 0;
            }
        }off+=wc;
    }
    return vk_spv_static_memory_valid(shader,profile);
}

static int vk_spv_profile_layout_supported(const vk_spv_profile_t *profile,
                                           vk_pipeline_layout_impl_t *layout,
                                           VkShaderStageFlagBits stage) {
    int descriptor_supported = 1;
    int push_supported = profile != 0 && profile->push_constant_count == 0u;
    if (profile == 0 || layout == 0) return 0;
    if ((profile->uniform_array_dynamic && !layout->device->uniform_array_dynamic) ||
        (profile->sampled_array_dynamic && !layout->device->sampled_array_dynamic)||
        (profile->storage_buffer_array_dynamic&&!layout->device->storage_buffer_array_dynamic)||
        (profile->storage_image_array_dynamic&&!layout->device->storage_image_array_dynamic)) return 0;
    if (profile->push_constant_count > 1u) return 0;
    for (uint32_t id = 1u; id < profile->bound; ++id) {
        if (profile->descriptor_type_by_id[id] != 0u) {
            vk_descriptor_set_layout_impl_t *set_layout;
            uint32_t set_number = profile->descriptor_set[id];
            int binding_found = 0;
            if (set_number >= layout->set_layout_count) return 0;
            set_layout = (vk_descriptor_set_layout_impl_t *)(uintptr_t)
                layout->set_layouts[set_number];
            if (set_layout == 0 || set_layout->destroy_requested != 0u) return 0;
            for (uint32_t binding_index = 0u;
                 binding_index < set_layout->binding_count; ++binding_index) {
                const VkDescriptorSetLayoutBinding *binding =
                    &set_layout->bindings[binding_index].binding;
                if (binding->binding == profile->descriptor_binding[id]) {
                    binding_found = binding->descriptorType ==
                            (VkDescriptorType)profile->descriptor_type_by_id[id] &&
                        binding->descriptorCount >= profile->descriptor_elements[id] &&
                        (binding->stageFlags & stage) != 0u;
                    break;
                }
            }
            if (!binding_found) descriptor_supported = 0;
        }
    }
    if(profile->push_constant_count){
        uint32_t covered=0u;
        for(uint32_t i=0u;i<layout->push_constant_range_count;++i){const VkPushConstantRange *range=&layout->push_ranges[i];
            if(range->stageFlags&stage)for(uint32_t word=range->offset/4u;word<(range->offset+range->size)/4u;++word)covered|=1u<<word;
        }
        push_supported=(covered&profile->push_word_mask)==profile->push_word_mask;
    }
    return descriptor_supported && push_supported;
}

static int vk_spv_stage_index(VkShaderStageFlagBits stage) {
    if (stage == VK_SHADER_STAGE_VERTEX_BIT) return 0;
    if (stage == VK_SHADER_STAGE_FRAGMENT_BIT) return 1;
    if (stage == VK_SHADER_STAGE_COMPUTE_BIT) return 2;
    return -1;
}

static int vk_spv_profile_push_initialized(const vk_spv_profile_t *profile,
                                           VkShaderStageFlagBits stage,
                                           const uint32_t initialized[3]) {
    int index;
    if (profile == 0 || initialized == 0) return 0;
    if (profile->push_constant_count == 0u) return 1;
    index = vk_spv_stage_index(stage);
    return index >= 0 && (initialized[index] & profile->push_word_mask) == profile->push_word_mask;
}

static float vk_spv_word_float(uint32_t word) {
    float value;
    memcpy(&value, &word, sizeof(value));
    return value;
}

/* GLSL.std.450 operations used by ordinary Skia-generated ESSL shaders.
 * Implement the exponent/fraction cases from IEEE-754 bits so the native
 * software renderer does not acquire a libm dependency. */
static float vk_spv_floor_f32(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    uint32_t exponent = (bits >> 23u) & 0xffu;
    if (exponent == 0xffu) return value; /* NaN and infinities are integral here. */
    int unbiased = (int)exponent - 127;
    if (unbiased < 0) {
        if ((bits & 0x7fffffffu) == 0u) return value;
        return (bits >> 31u) ? -1.0f : 0.0f;
    }
    if (unbiased >= 23) return value;
    uint32_t fractional_mask = (1u << (23u - (uint32_t)unbiased)) - 1u;
    if ((bits & fractional_mask) == 0u) return value;
    bits &= ~fractional_mask;
    memcpy(&value, &bits, sizeof(value));
    return (bits >> 31u) ? value - 1.0f : value;
}

static float vk_spv_sqrt_f32(float value) {
    float result;
    __asm__("sqrtss %1,%0" : "=x"(result) : "x"(value));
    return result;
}

static uint32_t vk_spv_float_word(float value) {
    uint32_t word;
    memcpy(&word, &value, sizeof(word));
    return word;
}

static int vk_spv_get_combined_image(
    vk_descriptor_set_impl_t *const bound_sets[8], uint32_t set_number,
    uint32_t binding_number, uint32_t element_index, vk_image_view_impl_t **view_out,
    vk_sampler_impl_t **sampler_out, VkImageLayout *descriptor_layout_out) {
    vk_descriptor_set_impl_t *set;
    vk_descriptor_value_impl_t *value = 0;
    vk_image_view_impl_t *view;
    vk_image_impl_t *image;
    vk_sampler_impl_t *sampler;
    if (view_out == 0 || sampler_out == 0 || descriptor_layout_out == 0 ||
        bound_sets == 0 || set_number >= 8u ||
        (set = bound_sets[set_number]) == 0 || set->layout == 0) return 0;
    for (uint32_t i = 0u; i < set->layout->binding_count; ++i) {
        const vk_descriptor_layout_binding_impl_t *binding = &set->layout->bindings[i];
        if (binding->binding.binding == binding_number &&
            binding->binding.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
            element_index < binding->binding.descriptorCount &&
            binding->value_offset + element_index < set->value_count) {
            value = &set->values[binding->value_offset + element_index];
            break;
        }
    }
    if (value == 0 || value->initialized == 0u ||
        value->type != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
        (view = value->resource.image.view) == 0 || view->type != VK_IMAGE_VIEW_TYPE_2D ||
        (image = view->image) == 0 ||
        (sampler = value->resource.image.sampler) == 0 ||
        (image->usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0u ||
        !vk_format_is_color_unorm(image->format) || image->width == 0u || image->height == 0u ||
        (value->resource.image.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
         value->resource.image.layout != VK_IMAGE_LAYOUT_GENERAL) ||
        (sampler->mag_filter!=VK_FILTER_NEAREST&&sampler->mag_filter!=VK_FILTER_LINEAR) ||
        (sampler->min_filter != VK_FILTER_NEAREST &&
         sampler->min_filter != VK_FILTER_LINEAR) ||
        (sampler->address_mode_u != VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE &&
         sampler->address_mode_u != VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT &&
         sampler->address_mode_u != VK_SAMPLER_ADDRESS_MODE_REPEAT) ||
        (sampler->address_mode_v != VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE &&
         sampler->address_mode_v != VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT &&
         sampler->address_mode_v != VK_SAMPLER_ADDRESS_MODE_REPEAT) ||
        vk_image_subresource_pixels(image, view->range.baseMipLevel,
                                     view->range.baseArrayLayer) == 0) return 0;
    *view_out = view;
    *sampler_out = sampler;
    *descriptor_layout_out = value->resource.image.layout;
    return 1;
}

static uint32_t vk_sampler_address_index(int32_t index, uint32_t extent,
                                         uint32_t address_mode) {
    if (address_mode == VK_SAMPLER_ADDRESS_MODE_REPEAT) {
        int32_t wrapped = index % (int32_t)extent;
        if (wrapped < 0) wrapped += (int32_t)extent;
        return (uint32_t)wrapped;
    }
    if (address_mode == VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT) {
        int32_t period=(int32_t)extent*2,wrapped=index%period;
        if(wrapped<0)wrapped+=period;
        return (uint32_t)(wrapped<(int32_t)extent?wrapped:period-1-wrapped);
    }
    if (index < 0) return 0u;
    if ((uint32_t)index >= extent) return extent - 1u;
    return (uint32_t)index;
}

static uint32_t vk_sample_linear_pixel(const uint32_t *pixels, uint32_t format,
                                      uint32_t width, uint32_t height,
                                      float u, float v,
                                      uint32_t address_u, uint32_t address_v) {
    const float x = u * (float)width - 0.5f;
    const float y = v * (float)height - 0.5f;
    int32_t x0 = (int32_t)x;
    int32_t y0 = (int32_t)y;
    int32_t x1, y1;
    float tx, ty;
    uint32_t p00, p10, p01, p11;
    uint32_t output = 0u;
    if ((float)x0 > x) --x0;
    if ((float)y0 > y) --y0;
    x1 = x0 + 1;
    y1 = y0 + 1;
    tx = x - (float)x0;
    ty = y - (float)y0;
    p00 = pixels[(uint64_t)vk_sampler_address_index(y0, height, address_v) * width +
                 vk_sampler_address_index(x0, width, address_u)];
    p10 = pixels[(uint64_t)vk_sampler_address_index(y0, height, address_v) * width +
                 vk_sampler_address_index(x1, width, address_u)];
    p01 = pixels[(uint64_t)vk_sampler_address_index(y1, height, address_v) * width +
                 vk_sampler_address_index(x0, width, address_u)];
    p11 = pixels[(uint64_t)vk_sampler_address_index(y1, height, address_v) * width +
                 vk_sampler_address_index(x1, width, address_u)];
    p00 = vk_pixel_to_canonical(format, p00);
    p10 = vk_pixel_to_canonical(format, p10);
    p01 = vk_pixel_to_canonical(format, p01);
    p11 = vk_pixel_to_canonical(format, p11);
    for (uint32_t shift = 0u; shift < 32u; shift += 8u) {
        const float c00 = (float)((p00 >> shift) & 0xffu);
        const float c10 = (float)((p10 >> shift) & 0xffu);
        const float c01 = (float)((p01 >> shift) & 0xffu);
        const float c11 = (float)((p11 >> shift) & 0xffu);
        const float top = c00 + (c10 - c00) * tx;
        const float bottom = c01 + (c11 - c01) * tx;
        const uint32_t channel = (uint32_t)(top + (bottom - top) * ty + 0.5f);
        output |= (channel & 0xffu) << shift;
    }
    return output;
}

static int vk_spv_sample_plane(vk_image_view_impl_t *view,
    vk_sampler_impl_t *sampler, VkImageLayout descriptor_layout, uint32_t mip,
    const uint32_t coordinate_words[4], uint32_t result_words[4]) {
    vk_image_plane_t plane;
    vk_image_plane_t *image = &plane;
    uint32_t *pixels;
    float coordinates[2];
    uint32_t texel_x, texel_y, pixel;
    if (mip >= view->range.levelCount ||
        !vk_image_plane(view->image, view->range.baseMipLevel + mip,
            view->range.baseArrayLayer, image) || image->layout != descriptor_layout ||
        (pixels = image->pixels) == 0) return 0;
    coordinates[0] = vk_spv_word_float(coordinate_words[0]);
    coordinates[1] = vk_spv_word_float(coordinate_words[1]);
    for (uint32_t axis = 0u; axis < 2u; ++axis) {
        uint32_t address_mode = axis == 0u ? sampler->address_mode_u : sampler->address_mode_v;
        float coordinate = coordinates[axis];
        uint32_t extent = axis == 0u ? image->width : image->height;
        if (!(coordinate == coordinate)) coordinate = 0.0f; /* deterministic NaN handling */
        if (address_mode == VK_SAMPLER_ADDRESS_MODE_REPEAT || address_mode == VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT) {
            if (coordinate <= -2147483000.0f || coordinate >= 2147483000.0f) {
                coordinate = 0.0f;
            } else {
                int32_t whole = (int32_t)coordinate;
                if (coordinate < 0.0f && (float)whole != coordinate) --whole;
                coordinate -= (float)whole;
                if(address_mode==VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT && (whole&1))coordinate=1.0f-coordinate;
            }
        } else {
            if (coordinate < 0.0f) coordinate = 0.0f;
            if (coordinate > 1.0f) coordinate = 1.0f;
        }
        if (coordinate >= 1.0f && address_mode == VK_SAMPLER_ADDRESS_MODE_REPEAT)
            coordinate = 0.0f;
        coordinates[axis] = coordinate;
        uint32_t texel = (uint32_t)(coordinate * (float)extent);
        if (texel >= extent) texel = extent - 1u;
        if (axis == 0u) texel_x = texel;
        else texel_y = texel;
    }
    if (sampler->min_filter == VK_FILTER_LINEAR) {
        pixel = vk_sample_linear_pixel(pixels, image->format,
                                       image->width, image->height,
                                       coordinates[0], coordinates[1],
                                       sampler->address_mode_u,
                                       sampler->address_mode_v);
    } else {
        pixel = vk_pixel_to_canonical(image->format,
            pixels[(uint64_t)texel_y * image->width + texel_x]);
    }
    result_words[0] = vk_spv_float_word((float)((pixel >> 16u) & 0xffu) / 255.0f);
    result_words[1] = vk_spv_float_word((float)((pixel >> 8u) & 0xffu) / 255.0f);
    result_words[2] = vk_spv_float_word((float)(pixel & 0xffu) / 255.0f);
    result_words[3] = vk_spv_float_word((float)((pixel >> 24u) & 0xffu) / 255.0f);
    return 1;
}

static int vk_spv_sample_combined_image(
    vk_descriptor_set_impl_t *const bound_sets[8], uint32_t set_number,
    uint32_t binding_number, uint32_t element_index, const uint32_t coordinate_words[4],
    int explicit_lod, float lod, uint32_t result_words[4]) {
    vk_image_view_impl_t *view; vk_sampler_impl_t *sampler; VkImageLayout layout;
    if (!vk_spv_get_combined_image(bound_sets, set_number, binding_number, element_index,
            &view, &sampler, &layout) || (!explicit_lod && sampler->max_lod != 0.0f)) return 0;
    if (!(lod == lod)) lod = 0.0f;
    if (lod < sampler->min_lod) lod = sampler->min_lod;
    if (lod > sampler->max_lod) lod = sampler->max_lod;
    const float max_mip = (float)(view->range.levelCount - 1u);
    if (lod > max_mip) lod = max_mip;
    if (lod < 0.0f) lod = 0.0f;
    if (sampler->mipmap_mode == VK_SAMPLER_MIPMAP_MODE_NEAREST)
        return vk_spv_sample_plane(view, sampler, layout, (uint32_t)(lod + 0.5f), coordinate_words, result_words);
    uint32_t low = (uint32_t)lod, high = low + 1u, upper[4];
    if (high >= view->range.levelCount) high = low;
    if (!vk_spv_sample_plane(view, sampler, layout, low, coordinate_words, result_words) ||
        !vk_spv_sample_plane(view, sampler, layout, high, coordinate_words, upper)) return 0;
    const float weight = lod - (float)low;
    for (uint32_t c = 0u; c < 4u; ++c) {
        float a = vk_spv_word_float(result_words[c]), b = vk_spv_word_float(upper[c]);
        result_words[c] = vk_spv_float_word(a + (b - a) * weight);
    }
    return 1;
}

static int vk_spv_run(const vk_shader_module_impl_t *shader,
                      const vk_spv_profile_t *profile_data,
                      VkShaderStageFlagBits stage, uint32_t vertex_index,uint32_t instance_index,int front_facing,
                      const uint32_t vertex_attribute_words[VK_SPV_MAX_INTERFACE_LOCATIONS][4],
                      vk_descriptor_set_impl_t *const bound_sets[8],
                      const uint8_t push_constants[128],
                      const uint32_t fragment_input_words[VK_SPV_MAX_INTERFACE_LOCATIONS][4],
                      const float fragment_coord[4],
                      uint32_t output_words[4],
                      uint32_t varying_words[VK_SPV_MAX_INTERFACE_LOCATIONS][4],
                      vk_spv_invocation_t *invocation) {
    const uint32_t *code;
    uint32_t word_count;
    uint32_t in_function = 0u;
    uint32_t returned = 0u;
    uint32_t current_label = 0u;
    uint32_t predecessor_label = 0u;
    uint32_t executed_instructions = 0u;
    uint32_t instruction_budget;
    if (output_words == 0 || profile_data == 0 ||
        profile_data->stage != (uint8_t)stage || profile_data->bound == 0u ||
        profile_data->bound > VK_SPV_MAX_IDS) return vk_spv_failure(invocation,__LINE__);
    /* A graphics invocation runs to completion in one call, so only its scalar
     * progress fields need a defined start (`calls` is written before read). */
    vk_spv_invocation_t graphics_state;
    if(!invocation){
        graphics_state.pc=graphics_state.label=graphics_state.predecessor=graphics_state.function=0u;
        graphics_state.call_depth=graphics_state.instructions=graphics_state.barrier=0u;
        graphics_state.fault_pc=graphics_state.fault_opcode=0u;
        graphics_state.started=graphics_state.done=graphics_state.graphics_quad=0u;
    }
    vk_spv_invocation_t *execution=invocation?invocation:&graphics_state;
    vk_spv_profile_t *snapshot=(vk_spv_profile_t *)profile_data;
    vk_spv_value_t *values=invocation?invocation->values:profile_data->workspace;
    if(values==0)return vk_spv_failure(invocation,__LINE__);
#define profile (*profile_data)
    code = shader->code;
    word_count = (uint32_t)(shader->code_size / sizeof(uint32_t));
    instruction_budget = word_count > VK_SPV_MAX_EXECUTION_INSTRUCTIONS / 16u
        ? VK_SPV_MAX_EXECUTION_INSTRUCTIONS : word_count * 16u;

    if(!execution->started){
    if(snapshot->prologue&&__atomic_load_n(&snapshot->prologue_ready,__ATOMIC_ACQUIRE))
        memcpy(values,snapshot->prologue,profile_data->bound*sizeof(*values));
    else{
    memset(values, 0, profile_data->bound*sizeof(*values));
    /* Materialize immutable module constants and bounded stage inputs before
       interpreting the entry function. */
    for (uint32_t offset = 5u; offset < word_count;) {
        uint32_t first = code[offset];
        uint32_t wc = first >> 16u;
        uint32_t op = first & 0xffffu;
        const uint32_t *w = &code[offset];
        if (op == 54u) break;
        if (op == 41u || op == 42u) {
            if (wc != 3u || w[1] >= profile.bound || w[2] >= profile.bound) return vk_spv_failure(invocation,__LINE__);
            values[w[2]].type = w[1];
            values[w[2]].word[0] = op == 41u ? 1u : 0u;
            values[w[2]].initialized = 1u;
        } else if (op == 43u) {
            if (wc != 4u || w[1] >= profile.bound || w[2] >= profile.bound) return vk_spv_failure(invocation,__LINE__);
            values[w[2]].type = w[1];
            values[w[2]].word[0] = w[3];
            values[w[2]].initialized = 1u;
        }else if(op==44u){
            uint32_t type=w[1],id=w[2],written=0u;
            if(type>=profile.bound||id>=profile.bound||!vk_spv_value_words(&profile,type,0u)||wc!=3u+profile.count[type])return vk_spv_failure(invocation,__LINE__);
            for(uint32_t c=0u;c<wc-3u;++c){uint32_t child,offset;
                if(!vk_spv_composite_child(&profile,type,c,&child,&offset)||w[3u+c]>=profile.bound||!values[w[3u+c]].initialized||values[w[3u+c]].type!=child)return vk_spv_failure(invocation,__LINE__);
                uint32_t count=vk_spv_value_words(&profile,child,0u);memcpy(values[id].word+offset,values[w[3u+c]].word,count*4u);written+=count;
            }
            values[id].type=type;values[id].initialized=1u;
        } else if (op == 46u) {
            if (wc != 3u || w[1] >= profile.bound || w[2] >= profile.bound) return vk_spv_failure(invocation,__LINE__);
            values[w[2]].type = w[1];
            values[w[2]].initialized = 1u;
        } else if (op == 59u) {
            uint32_t type_id = w[1];
            uint32_t id = w[2];
            if ((wc != 4u&&wc!=5u) || type_id >= profile.bound || id >= profile.bound ||
                profile.kind[type_id] != VK_SPV_KIND_POINTER) return vk_spv_failure(invocation,__LINE__);
            values[id].type=type_id;
            values[id].initialized=1u;
            if(profile.gl_uniform[id]){
                if(!profile.gl_uniform_words)return vk_spv_failure(invocation,__LINE__);
                values[id].memory_pointer=1u;values[id].memory_base=id;values[id].access=2u;
                memcpy(values[id].word,profile.gl_uniform_words[id],64u);
            }else if(w[3]==1u||w[3]==3u||w[3]==6u||w[3]==4u){
                values[id].memory_pointer=1u;values[id].memory_base=id;values[id].access=profile.access[id];
                if(wc==5u){uint32_t initializer=w[4],n=vk_spv_value_words(&profile,profile.element[type_id],0u);
                    if(w[3]!=6u||initializer>=profile.bound||!values[initializer].initialized||values[initializer].type!=profile.element[type_id]||!n)return vk_spv_failure(invocation,__LINE__);
                    memcpy(values[id].word,values[initializer].word,n*4u);
                }
                if(w[3]==4u)values[id].descriptor_byte_offset=profile.workgroup_offset[id];
            }else if(w[3]==2u){values[id].descriptor_pointer=1u;values[id].descriptor_set=profile.descriptor_set[id];values[id].descriptor_binding=profile.descriptor_binding[id];
                values[id].access=profile.access[id];if(profile.descriptor_type_by_id[id]!=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)values[id].access|=2u;
            }else values[id].type=profile.element[type_id];
            if(wc==5u){uint32_t source=w[4],n=vk_spv_value_words(&profile,profile.element[type_id],0u);
                if(source>=profile.bound||!n||!values[source].initialized||values[source].type!=profile.element[type_id])return vk_spv_failure(invocation,__LINE__);
                memcpy(values[id].word,values[source].word,n*4u);
            }
        }
        offset += wc;
    }
    if(snapshot->prologue&&!__atomic_exchange_n(&snapshot->prologue_filling,1u,__ATOMIC_ACQ_REL)){
        memcpy(snapshot->prologue,values,profile_data->bound*sizeof(*values));
        __atomic_store_n(&snapshot->prologue_ready,1u,__ATOMIC_RELEASE);
    }
    }
    if (stage == VK_SHADER_STAGE_VERTEX_BIT) {
        if(profile.instance_id)values[profile.instance_id].word[0]=instance_index;
        if (profile.input_id != 0u) {
            if (profile.input_id >= profile.bound ||
                values[profile.input_id].initialized == 0u) return vk_spv_failure(invocation,__LINE__);
            values[profile.input_id].word[0] = vertex_index;
        }
        for (uint32_t location = 0u;
             location < VK_SPV_MAX_INTERFACE_LOCATIONS; ++location) {
            const uint32_t id = profile.attribute_id[location];
            if (id == 0u) continue;
            if (vertex_attribute_words == 0 || id >= profile.bound ||
                values[id].initialized == 0u) return vk_spv_failure(invocation,__LINE__);
            memcpy(values[id].word, vertex_attribute_words[location],
                   sizeof(uint32_t) * 4u);
        }
    }else if(stage==VK_SHADER_STAGE_COMPUTE_BIT){
        if(!invocation)return vk_spv_failure(invocation,__LINE__);
        for(uint32_t b=0u;b<6u;++b){uint32_t id=profile.compute_builtin[b];if(!id)continue;
            const uint32_t *input=b==0u?invocation->groups:b==1u?profile.local_size:b==2u?invocation->group:b==3u?invocation->local:invocation->global;
            if(b==5u)values[id].word[0]=invocation->local[0]+profile.local_size[0]*(invocation->local[1]+profile.local_size[1]*invocation->local[2]);
            else memcpy(values[id].word,input,12u);
        }
    } else {
        if(profile.front_facing_id)values[profile.front_facing_id].word[0]=front_facing!=0;
        if(profile.frag_coord_id){
            if(fragment_coord==0)return vk_spv_failure(invocation,__LINE__);
            memcpy(values[profile.frag_coord_id].word,fragment_coord,16u);
            if(profile.gl_top_left_height)
                values[profile.frag_coord_id].word[1]=vk_spv_float_word(
                    profile.gl_top_left_height-fragment_coord[1]);
        }
        for (uint32_t location = 0u;
             location < VK_SPV_MAX_INTERFACE_LOCATIONS; ++location) {
            const uint32_t id = profile.fragment_varying_id[location];
            if (id == 0u) continue;
            if (fragment_input_words == 0 || id >= profile.bound ||
                values[id].initialized == 0u) return vk_spv_failure(invocation,__LINE__);
            memcpy(values[id].word, fragment_input_words[location],
                   sizeof(uint32_t) * 4u);
        }
    }
    execution->started=1u;
    }
#define calls execution->calls
    uint32_t call_depth=execution->call_depth,current_function=execution->started&&execution->pc?execution->function:profile.function_id;
    current_label=execution->label;predecessor_label=execution->predecessor;
    executed_instructions=execution->instructions;
    in_function=execution->pc!=0u;
    uint32_t first_offset=execution->pc?execution->pc:5u;
    if(!execution->pc&&snapshot->prologue&&snapshot->entry_offset){first_offset=snapshot->entry_offset;in_function=1u;}
    for (uint32_t offset = first_offset; offset < word_count;) {
        uint32_t first = code[offset];
        uint32_t wc = first >> 16u;
        uint32_t op = first & 0xffffu;
        const uint32_t *w = &code[offset];
        if (wc == 0u || wc > word_count - offset) return vk_spv_failure(invocation,__LINE__);
        if (op == 54u) {
            in_function = w[2] == profile.function_id;
            if(in_function&&snapshot->prologue&&!snapshot->entry_offset)snapshot->entry_offset=offset+wc;
            offset += wc;
            continue;
        }
        if (in_function == 0u) { offset += wc; continue; }
        if (op == 56u) break;
        if (op == 0u || op == 8u || op == 246u || op == 247u) {
            offset += wc;
            continue;
        }
        if (op == 248u) {
            current_label = w[1];
            offset += wc;
            continue;
        }
        execution->fault_pc=offset;execution->fault_opcode=op;
        if (++executed_instructions > instruction_budget) return vk_spv_failure(invocation,__LINE__);
        /* Fragment quads suspend at operations requiring neighboring lanes.
         * This reuses the same continuation engine as compute barriers. */
        if(invocation&&invocation->graphics_quad&&(op==87u||(op>=207u&&op<=209u))){
            execution->pc=offset;execution->barrier=offset;
            execution->label=current_label;execution->predecessor=predecessor_label;execution->function=current_function;
            execution->call_depth=call_depth;execution->instructions=executed_instructions;
            return 4;
        }
        if(op==224u||op==225u){
            if(!vk_spv_barrier_valid(&profile,w,op==224u))return vk_spv_failure(invocation,__LINE__);
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            if(op==224u){
                if(!invocation)return vk_spv_failure(invocation,__LINE__);
                execution->pc=offset+wc;execution->barrier=offset;
                execution->label=current_label;execution->predecessor=predecessor_label;execution->function=current_function;
                execution->call_depth=call_depth;execution->instructions=executed_instructions;
                return 3;
            }
            offset+=wc;continue;
        }
        if(op>=227u&&op<=242u){
            if(!vk_spv_atomic(&profile,values,bound_sets,invocation,w,op))return vk_spv_failure(invocation,__LINE__);
            offset+=wc;continue;
        }
        if(op==251u){uint32_t selector=w[1],target=w[2];
            if(selector>=profile.bound||!values[selector].initialized)return vk_spv_failure(invocation,__LINE__);
            for(uint32_t i=3u;i+1u<wc;i+=2u)if(values[selector].word[0]==w[i]){target=w[i+1u];break;}
            if(target>=profile.bound||!profile.label_word[target])return vk_spv_failure(invocation,__LINE__);
            predecessor_label=current_label;offset=profile.label_word[target]-1u;continue;
        }
        if(op==68u){
            uint32_t base=w[3],type,result=w[2],member=w[4];
            if(base>=profile.bound||!values[base].initialized||!values[base].descriptor_pointer)return vk_spv_failure(invocation,__LINE__);
            type=profile.element[values[base].type];
            if(type>=profile.bound||profile.kind[type]!=VK_SPV_KIND_STRUCT||member>=profile.count[type])return vk_spv_failure(invocation,__LINE__);
            uint32_t array=profile.member_type[type][member],begin=profile.member_offset[type][member];
            vk_descriptor_value_impl_t *d=vk_spv_descriptor_value(bound_sets,values[base].descriptor_set,values[base].descriptor_binding,values[base].descriptor_index);
            if(!d||d->type!=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER||profile.kind[array]!=VK_SPV_KIND_RUNTIME_ARRAY||!profile.array_stride[array]||begin>d->resource.buffer.range)return vk_spv_failure(invocation,__LINE__);
            values[result].type=w[1];values[result].initialized=1u;values[result].word[0]=(uint32_t)((d->resource.buffer.range-begin)/profile.array_stride[array]);
            offset+=wc;continue;
        }
        if(op==63u){
            uint32_t dst=w[1],src=w[2];uint8_t *to,*from;
            if(dst>=profile.bound||src>=profile.bound||values[dst].type>=profile.bound||values[src].type>=profile.bound)return vk_spv_failure(invocation,__LINE__);
            uint32_t type=profile.element[values[dst].type],bytes=vk_spv_value_words(&profile,type,0u)*4u;
            if(!bytes||profile.element[values[src].type]!=type||
               !vk_spv_pointer_address(&profile,values,src,bound_sets,invocation,bytes,0,&from)||
               !vk_spv_pointer_address(&profile,values,dst,bound_sets,invocation,bytes,1,&to))return vk_spv_failure(invocation,__LINE__);
            memmove(to,from,bytes);offset+=wc;continue;
        }
        if (op == 249u || op == 250u) {
            uint32_t target;
            if (current_label == 0u) return vk_spv_failure(invocation,__LINE__);
            if (op == 249u) {
                target = w[1];
            } else {
                uint32_t condition = w[1];
                if (wc != 4u || condition >= profile.bound ||
                    values[condition].initialized == 0u ||
                    values[condition].type >= profile.bound ||
                    profile.kind[values[condition].type] != VK_SPV_KIND_BOOL) return vk_spv_failure(invocation,__LINE__);
                target = values[condition].word[0] != 0u ? w[2] : w[3];
            }
            if (target >= profile.bound || profile.label_word[target] == 0u) return vk_spv_failure(invocation,__LINE__);
            predecessor_label = current_label;
            offset = profile.label_word[target] - 1u;
            continue;
        }
        if (op == 245u) { /* OpPhi selects the incoming value for the taken edge */
            uint32_t result_type = w[1], result_id = w[2];
            uint32_t incoming_value = UINT32_MAX;
            if (wc < 5u || (wc & 1u) == 0u || result_type >= profile.bound ||
                result_id >= profile.bound || predecessor_label == 0u) return vk_spv_failure(invocation,__LINE__);
            for (uint32_t i = 3u; i + 1u < wc; i += 2u) {
                if (w[i + 1u] == predecessor_label) {
                    if (incoming_value != UINT32_MAX) return vk_spv_failure(invocation,__LINE__);
                    incoming_value = w[i];
                }
            }
            if (incoming_value >= profile.bound ||
                values[incoming_value].initialized == 0u ||
                values[incoming_value].type != result_type) return vk_spv_failure(invocation,__LINE__);
            values[result_id] = values[incoming_value];
            offset += wc;
            continue;
        }
        if(op==252u)return stage==VK_SHADER_STAGE_FRAGMENT_BIT?2:0;
        if(op==255u)return vk_spv_failure(invocation,__LINE__);
        if(op==253u||op==254u){
            if(!call_depth){if(op!=253u)return vk_spv_failure(invocation,__LINE__);returned=1u;break;}
            --call_depth;
            if(op==254u){uint32_t value=w[1];if(value>=profile.bound||!values[value].initialized||values[value].type!=calls[call_depth].type)return vk_spv_failure(invocation,__LINE__);values[calls[call_depth].result]=values[value];}
            else if(profile.kind[calls[call_depth].type]!=VK_SPV_KIND_VOID)return vk_spv_failure(invocation,__LINE__);
            offset=calls[call_depth].offset;current_label=calls[call_depth].label;predecessor_label=calls[call_depth].predecessor;current_function=calls[call_depth].function;continue;
        }
        if(op==55u){offset+=wc;continue;}
        if(op==57u){
            uint32_t target=w[3];if(call_depth>=32u||target>=profile.bound||!profile.function_start[target]||wc!=4u+profile.function_parameters[target])return vk_spv_failure(invocation,__LINE__);
            for(uint32_t id=1u;id<profile.bound;++id)if(profile.owner[id]==target)memset(&values[id],0,sizeof(values[id]));
            for(uint32_t i=0u;i<profile.function_parameters[target];++i){uint32_t arg=w[4u+i],param=profile.function_parameter[target][i];
                if(arg>=profile.bound||!values[arg].initialized||values[arg].type!=profile.type_of[param])return vk_spv_failure(invocation,__LINE__);values[param]=values[arg];}
            calls[call_depth].offset=offset+wc;calls[call_depth].label=current_label;calls[call_depth].predecessor=predecessor_label;
            calls[call_depth].function=current_function;calls[call_depth].result=w[2];calls[call_depth].type=w[1];++call_depth;
            current_function=target;current_label=predecessor_label=0u;offset=profile.function_start[target]+5u;continue;
        }
        if(op==59u){
            uint32_t id=w[2],type=w[1];if(type>=profile.bound||id>=profile.bound||profile.storage[type]!=7u)return vk_spv_failure(invocation,__LINE__);
            memset(&values[id],0,sizeof(values[id]));values[id].type=type;values[id].initialized=values[id].local_pointer=1u;values[id].local_base=id;
            if(wc==5u){uint32_t source=w[4],count=vk_spv_value_words(&profile,profile.element[type],0u);
                if(!count||source>=profile.bound||!values[source].initialized||values[source].type!=profile.element[type])return vk_spv_failure(invocation,__LINE__);
                memcpy(values[id].word,values[source].word,count*4u);values[id].local_written=(uint16_t)((1u<<count)-1u);}
        }else if(op==83u){
            if(w[3]>=profile.bound||!values[w[3]].initialized||values[w[3]].type!=w[1])return vk_spv_failure(invocation,__LINE__);values[w[2]]=values[w[3]];
        }else if(op==77u||op==78u){
            uint32_t result=w[2],type=w[1],vector=w[3],index=w[op==77u?4u:5u];
            if(vector>=profile.bound||index>=profile.bound||!values[vector].initialized||!values[index].initialized||profile.kind[values[index].type]!=VK_SPV_KIND_INT)return vk_spv_failure(invocation,__LINE__);
            uint32_t vt=values[vector].type,i=values[index].word[0];if(vt>=profile.bound||profile.kind[vt]!=VK_SPV_KIND_VECTOR||i>=profile.count[vt])return vk_spv_failure(invocation,__LINE__);
            if(op==77u){if(type!=profile.element[vt])return vk_spv_failure(invocation,__LINE__);values[result].word[0]=values[vector].word[i];}
            else {uint32_t component=w[4];if(type!=vt||component>=profile.bound||!values[component].initialized||values[component].type!=profile.element[vt])return vk_spv_failure(invocation,__LINE__);values[result]=values[vector];values[result].word[i]=values[component].word[0];}
            values[result].type=type;values[result].initialized=1u;
        }else if (op == 61u) { /* OpLoad */
            uint32_t result_type = w[1], result_id = w[2], pointer = w[3];
            if (result_type >= profile.bound || result_id >= profile.bound ||
                pointer >= profile.bound || values[pointer].initialized == 0u) return vk_spv_failure(invocation,__LINE__);
            if(values[pointer].memory_pointer){
                uint32_t count=vk_spv_value_words(&profile,result_type,0u);uint8_t *address;
                if(!count||profile.element[values[pointer].type]!=result_type||
                   !vk_spv_pointer_address(&profile,values,pointer,bound_sets,invocation,count*4u,0,&address))return vk_spv_failure(invocation,__LINE__);
                values[result_id].type=result_type;memcpy(values[result_id].word,address,count*4u);values[result_id].initialized=1u;
            }else if(values[pointer].local_pointer){
                uint32_t base=values[pointer].local_base,count=vk_spv_value_words(&profile,result_type,0u),off=values[pointer].local_offset;
                if(base>=profile.bound||!count||off>16u||count>16u-off||profile.element[values[pointer].type]!=result_type)return vk_spv_failure(invocation,__LINE__);
                uint32_t mask=((1u<<count)-1u)<<off;if((values[base].local_written&mask)!=mask)return vk_spv_failure(invocation,__LINE__);
                values[result_id].type=result_type;memcpy(values[result_id].word,values[base].word+off,count*4u);values[result_id].initialized=1u;
            }else if (values[pointer].push_constant_pointer != 0u) {
                uint32_t bytes;
                if (push_constants == 0 || result_type >= profile.bound ||
                    values[pointer].type >= profile.bound ||
                    profile.element[values[pointer].type] != result_type) return vk_spv_failure(invocation,__LINE__);
                if ((profile.kind[result_type] == VK_SPV_KIND_FLOAT || profile.kind[result_type] == VK_SPV_KIND_INT) && profile.width[result_type] == 32u) bytes = 4u;
                else if (profile.kind[result_type] == VK_SPV_KIND_VECTOR && profile.count[result_type] <= 4u &&
                         (profile.kind[profile.element[result_type]] == VK_SPV_KIND_FLOAT || profile.kind[profile.element[result_type]] == VK_SPV_KIND_INT)) bytes = 4u * profile.count[result_type];
                else if(profile.kind[result_type]==VK_SPV_KIND_MATRIX&&profile.count[result_type]==4u)bytes=64u;
                else return vk_spv_failure(invocation,__LINE__);
                if (values[pointer].descriptor_byte_offset > 128u ||
                    bytes > 128u - values[pointer].descriptor_byte_offset) return vk_spv_failure(invocation,__LINE__);
                values[result_id].type = result_type;
                memcpy(values[result_id].word, push_constants + values[pointer].descriptor_byte_offset, bytes);
                values[result_id].initialized = 1u;
            } else if (values[pointer].descriptor_pointer == 3u) {
                if ((profile.kind[result_type] != VK_SPV_KIND_SAMPLED_IMAGE&&profile.kind[result_type]!=VK_SPV_KIND_IMAGE) ||
                    values[pointer].type >= profile.bound ||
                    profile.kind[values[pointer].type] != VK_SPV_KIND_POINTER ||
                    profile.element[values[pointer].type] != result_type) return vk_spv_failure(invocation,__LINE__);
                values[result_id] = values[pointer];
                values[result_id].type = result_type;
                values[result_id].descriptor_pointer = 2u;
            } else if (values[pointer].descriptor_pointer != 0u) {
                uint32_t byte_count;
                if (values[pointer].type >= profile.bound ||
                    profile.kind[values[pointer].type] != VK_SPV_KIND_POINTER ||
                    profile.element[values[pointer].type] != result_type) return vk_spv_failure(invocation,__LINE__);
                if ((profile.kind[result_type] == VK_SPV_KIND_FLOAT || profile.kind[result_type] == VK_SPV_KIND_INT) &&
                    profile.width[result_type] == 32u) {
                    byte_count = sizeof(uint32_t);
                } else if (profile.kind[result_type] == VK_SPV_KIND_VECTOR &&
                           profile.count[result_type] >= 2u &&
                           profile.count[result_type] <= 4u &&
                           profile.element[result_type] < profile.bound &&
                           (profile.kind[profile.element[result_type]] == VK_SPV_KIND_FLOAT ||
                            profile.kind[profile.element[result_type]] == VK_SPV_KIND_INT) &&
                           profile.width[profile.element[result_type]] == 32u) {
                    byte_count = profile.count[result_type] * sizeof(uint32_t);
                } else if (profile.kind[result_type] == VK_SPV_KIND_MATRIX &&
                           profile.count[result_type] == 4u &&
                           profile.element[result_type] < profile.bound &&
                           profile.kind[profile.element[result_type]] == VK_SPV_KIND_VECTOR &&
                           profile.count[profile.element[result_type]] == 4u) {
                    byte_count = 16u * sizeof(uint32_t);
                } else return vk_spv_failure(invocation,__LINE__);
                uint8_t *address;
                if(!vk_spv_pointer_address(&profile,values,pointer,bound_sets,invocation,byte_count,0,&address))return vk_spv_failure(invocation,__LINE__);
                memcpy(values[result_id].word,address,byte_count);
                values[result_id].type = result_type;
                values[result_id].initialized = 1u;
            } else if (profile.variable[pointer] != 0u && profile.storage[pointer] == 0u) {
                if ((profile.descriptor_type_by_id[pointer] != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER&&
                     profile.descriptor_type_by_id[pointer]!=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE&&
                     profile.descriptor_type_by_id[pointer]!=VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER&&
                     profile.descriptor_type_by_id[pointer]!=VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER) ||
                    values[pointer].type != result_type || result_type >= profile.bound ||
                    (profile.kind[result_type] != VK_SPV_KIND_SAMPLED_IMAGE&&profile.kind[result_type]!=VK_SPV_KIND_IMAGE) ||
                    profile.descriptor_set[pointer] == UINT32_MAX ||
                    profile.descriptor_binding[pointer] == UINT32_MAX) return vk_spv_failure(invocation,__LINE__);
                values[result_id].type = result_type;
                values[result_id].descriptor_set = profile.descriptor_set[pointer];
                values[result_id].descriptor_binding = profile.descriptor_binding[pointer];
                values[result_id].descriptor_pointer = 2u;
                values[result_id].access=profile.access[pointer]|values[pointer].access;
                values[result_id].initialized = 1u;
            } else {
                if (values[pointer].type != result_type) return vk_spv_failure(invocation,__LINE__);
                values[result_id] = values[pointer];
            }
        } else if(op==100u){ /* Extract the image from samplerBuffer for OpImageFetch. */
            uint32_t id=w[2],source=w[3],type=w[1];
            if(id>=profile.bound||source>=profile.bound||type>=profile.bound||!values[source].initialized||
               profile.kind[type]!=VK_SPV_KIND_IMAGE||profile.kind[values[source].type]!=VK_SPV_KIND_SAMPLED_IMAGE||
               profile.element[values[source].type]!=type)return vk_spv_failure(invocation,__LINE__);
            values[id]=values[source];values[id].type=type;
        } else if(op==95u||op==98u||op==99u){
            if(!vk_spv_storage_image(&profile,values,bound_sets,w,op))return vk_spv_failure(invocation,__LINE__);
        } else if (op == 87u || op == 88u) { /* combined 2D sample, implicit-zero/explicit LOD */
            uint32_t result_type = w[1], result_id = w[2];
            uint32_t sampled_image = w[3], coordinate = w[4];
            uint32_t coordinate_type;
            float lod = 0.0f;
            if (op == 88u) {
                uint32_t lod_id = w[6];
                if (wc != 7u || w[5] != 2u || lod_id >= profile.bound || !values[lod_id].initialized ||
                    values[lod_id].type >= profile.bound ||
                    profile.kind[values[lod_id].type] != VK_SPV_KIND_FLOAT) return vk_spv_failure(invocation,__LINE__);
                lod = vk_spv_word_float(values[lod_id].word[0]);
            }
            if ((wc != 5u && op != 88u) || result_type >= profile.bound || result_id >= profile.bound ||
                sampled_image >= profile.bound || coordinate >= profile.bound ||
                values[sampled_image].initialized == 0u ||
                values[sampled_image].descriptor_pointer != 2u ||
                profile.kind[result_type] != VK_SPV_KIND_VECTOR ||
                profile.count[result_type] != 4u || profile.element[result_type] >= profile.bound ||
                profile.kind[profile.element[result_type]] != VK_SPV_KIND_FLOAT ||
                profile.width[profile.element[result_type]] != 32u ||
                values[coordinate].initialized == 0u ||
                (coordinate_type = values[coordinate].type) >= profile.bound ||
                profile.kind[coordinate_type] != VK_SPV_KIND_VECTOR ||
                profile.count[coordinate_type] != 2u || profile.element[coordinate_type] >= profile.bound ||
                profile.kind[profile.element[coordinate_type]] != VK_SPV_KIND_FLOAT ||
                profile.width[profile.element[coordinate_type]] != 32u ||
                !vk_spv_sample_combined_image(bound_sets,
                    values[sampled_image].descriptor_set,
                    values[sampled_image].descriptor_binding,
                    values[sampled_image].descriptor_index, values[coordinate].word,
                    op == 88u, lod, values[result_id].word)) return vk_spv_failure(invocation,__LINE__);
            values[result_id].type = result_type;
            values[result_id].initialized = 1u;
        } else if (op == 65u||op==66u) { /* Descriptor selection + typed block/component traversal. */
            uint32_t result_type = w[1], result_id = w[2], base = w[3];
            if (result_type >= profile.bound || result_id >= profile.bound || base >= profile.bound ||
                values[base].initialized == 0u || profile.kind[result_type] != VK_SPV_KIND_POINTER) return vk_spv_failure(invocation,__LINE__);
            if(values[base].memory_pointer){
                if(!vk_spv_memory_chain(&profile,values,base,result_type,result_id,w+4u,wc-4u))return vk_spv_failure(invocation,__LINE__);
                offset+=wc;continue;
            }
            if(values[base].local_pointer){
                uint32_t type=profile.element[values[base].type],off=values[base].local_offset;
                for(uint32_t operand=4u;operand<wc;++operand){uint32_t id=w[operand],child,delta;
                    if(id>=profile.bound||!values[id].initialized||profile.kind[values[id].type]!=VK_SPV_KIND_INT||
                       (profile.kind[type]==VK_SPV_KIND_STRUCT&&!profile.constant_integer[id])||
                       !vk_spv_composite_child(&profile,type,values[id].word[0],&child,&delta)||delta>16u-off)return vk_spv_failure(invocation,__LINE__);
                    off+=delta;type=child;
                }
                if(profile.storage[result_type]!=7u||profile.element[result_type]!=type)return vk_spv_failure(invocation,__LINE__);
                values[result_id]=values[base];values[result_id].type=result_type;values[result_id].local_offset=(uint16_t)off;
                offset+=wc;continue;
            }
            uint32_t type, storage, descriptor_index = 0u, byte_offset = 0u;
            uint8_t access=values[base].access;
            uint32_t set_number = UINT32_MAX, binding_number = UINT32_MAX;
            int push = 0;
            if (profile.variable[base]) {
                uint32_t pointer = profile.type_of[base];
                if (pointer >= profile.bound || profile.kind[pointer] != VK_SPV_KIND_POINTER) return vk_spv_failure(invocation,__LINE__);
                type = profile.element[pointer]; storage = profile.storage[base];
                set_number = profile.descriptor_set[base]; binding_number = profile.descriptor_binding[base];
                push = storage == 9u && base == profile.push_constant_variable_id;
                if (!push && profile.descriptor_type_by_id[base] == 0u) return vk_spv_failure(invocation,__LINE__);
            } else {
                uint32_t pointer = values[base].type;
                if (pointer >= profile.bound || profile.kind[pointer] != VK_SPV_KIND_POINTER ||
                    (values[base].descriptor_pointer != 1u && values[base].descriptor_pointer != 3u &&
                     values[base].push_constant_pointer == 0u)) return vk_spv_failure(invocation,__LINE__);
                type = profile.element[pointer]; storage = profile.storage[pointer];
                set_number = values[base].descriptor_set; binding_number = values[base].descriptor_binding;
                descriptor_index = values[base].descriptor_index;
                byte_offset = values[base].descriptor_byte_offset;
                push = values[base].push_constant_pointer != 0u;
            }
            for (uint32_t operand = 4u; operand < wc; ++operand) {
                uint32_t id = w[operand];
                if (id >= profile.bound || values[id].initialized == 0u ||
                    values[id].type >= profile.bound || profile.kind[values[id].type] != VK_SPV_KIND_INT ||
                    type >= profile.bound) return vk_spv_failure(invocation,__LINE__);
                uint32_t index = values[id].word[0], offset = 0u;
                if (profile.kind[type] == VK_SPV_KIND_ARRAY || profile.kind[type]==VK_SPV_KIND_RUNTIME_ARRAY) {
                    if (profile.kind[type]!=VK_SPV_KIND_RUNTIME_ARRAY && index >= profile.count[type]) return vk_spv_failure(invocation,__LINE__);
                    if (operand == 4u && profile.variable[base] && !push) {
                        /* The pointee of a global resource array selects an
                           object. Nested arrays instead select bytes in it. */
                        if (!profile.constant_integer[id] &&
                            ((storage == 2u && !(profile.descriptor_type_by_id[base]==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER?profile.storage_buffer_array_dynamic:profile.uniform_array_dynamic)) ||
                             (storage == 0u && !(profile.descriptor_type_by_id[base]==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE?profile.storage_image_array_dynamic:profile.sampled_array_dynamic)))) return vk_spv_failure(invocation,__LINE__);
                        descriptor_index = index;
                    } else {
                        if ((storage != 2u && storage != 9u) || profile.array_stride[type] == 0u ||
                            index > UINT32_MAX / profile.array_stride[type]) return vk_spv_failure(invocation,__LINE__);
                        offset = index * profile.array_stride[type];
                    }
                    type = profile.element[type];
                } else if (profile.kind[type] == VK_SPV_KIND_STRUCT) {
                    if (!profile.constant_integer[id] || index >= profile.count[type] ||
                        index >= VK_SPV_MAX_BLOCK_MEMBERS || !profile.member_offset_set[type][index]) return vk_spv_failure(invocation,__LINE__);
                    access|=profile.member_access[type][index];
                    offset = profile.member_offset[type][index];
                    type = profile.member_type[type][index];
                } else if (profile.kind[type] == VK_SPV_KIND_VECTOR || profile.kind[type] == VK_SPV_KIND_MATRIX) {
                    if (index >= profile.count[type]) return vk_spv_failure(invocation,__LINE__);
                    offset = index * (profile.kind[type] == VK_SPV_KIND_MATRIX ? 16u : 4u);
                    type = profile.element[type];
                } else return vk_spv_failure(invocation,__LINE__);
                if (byte_offset > UINT32_MAX - offset) return vk_spv_failure(invocation,__LINE__);
                byte_offset += offset;
            }
            if (profile.element[result_type] != type || profile.storage[result_type] != storage) return vk_spv_failure(invocation,__LINE__);
            memset(&values[result_id], 0, sizeof(values[result_id]));
            values[result_id].type = result_type;
            values[result_id].access=access;
            values[result_id].descriptor_set = set_number;
            values[result_id].descriptor_binding = binding_number;
            values[result_id].descriptor_index = descriptor_index;
            values[result_id].descriptor_byte_offset = byte_offset;
            values[result_id].push_constant_pointer = push;
            values[result_id].descriptor_pointer = push ? 0u : (storage == 0u ? 3u : 1u);
            values[result_id].initialized = 1u;
        } else if (op == 62u) { /* OpStore */
            uint32_t pointer = w[1], object = w[2];
            if(pointer>=profile.bound||object>=profile.bound||!values[pointer].initialized||!values[object].initialized)return vk_spv_failure(invocation,__LINE__);
            if(values[pointer].descriptor_pointer==1u||values[pointer].memory_pointer){
                uint32_t count=vk_spv_value_words(&profile,values[object].type,0u);uint8_t *address;
                if(!count||profile.element[values[pointer].type]!=values[object].type||
                   !vk_spv_pointer_address(&profile,values,pointer,bound_sets,invocation,count*4u,1,&address))return vk_spv_failure(invocation,__LINE__);
                memcpy(address,values[object].word,count*4u);
            }else if(values[pointer].local_pointer){
                uint32_t base=values[pointer].local_base,off=values[pointer].local_offset,count=vk_spv_value_words(&profile,values[object].type,0u);
                if(base>=profile.bound||!count||off>16u||count>16u-off||profile.element[values[pointer].type]!=values[object].type)return vk_spv_failure(invocation,__LINE__);
                memcpy(values[base].word+off,values[object].word,count*4u);values[base].local_written|=(uint16_t)(((1u<<count)-1u)<<off);
            }else {if(values[pointer].type!=values[object].type||profile.storage[pointer]==1u)return vk_spv_failure(invocation,__LINE__);values[pointer]=values[object];}
        }else if(op==80u){
            uint32_t type=w[1],result=w[2],written=0u,count=vk_spv_value_words(&profile,type,0u);
            if(!count)return vk_spv_failure(invocation,__LINE__);
            for(uint32_t operand=3u;operand<wc;++operand){uint32_t id=w[operand];if(id>=profile.bound||!values[id].initialized)return vk_spv_failure(invocation,__LINE__);
                uint32_t child_words=vk_spv_value_words(&profile,values[id].type,0u);
                if(!child_words||child_words>count-written)return vk_spv_failure(invocation,__LINE__);
                if(profile.kind[type]==VK_SPV_KIND_VECTOR){uint32_t scalar=values[id].type;if(profile.kind[scalar]==VK_SPV_KIND_VECTOR)scalar=profile.element[scalar];if(scalar!=profile.element[type])return vk_spv_failure(invocation,__LINE__);}
                else {uint32_t child,delta;if(!vk_spv_composite_child(&profile,type,operand-3u,&child,&delta)||child!=values[id].type||delta!=written)return vk_spv_failure(invocation,__LINE__);}
                memcpy(values[result].word+written,values[id].word,child_words*4u);written+=child_words;
            }
            if(written!=count)return vk_spv_failure(invocation,__LINE__);values[result].type=type;values[result].initialized=1u;
        }else if(op==81u||op==82u){
            uint32_t result=w[2],composite=w[op==81u?3u:4u],type,off=0u;
            if(composite>=profile.bound||!values[composite].initialized)return vk_spv_failure(invocation,__LINE__);type=values[composite].type;
            for(uint32_t operand=op==81u?4u:5u;operand<wc;++operand){uint32_t child,delta;
                if(!vk_spv_composite_child(&profile,type,w[operand],&child,&delta)||delta>16u-off)return vk_spv_failure(invocation,__LINE__);off+=delta;type=child;}
            uint32_t count=vk_spv_value_words(&profile,type,0u);if(!count||count>16u-off)return vk_spv_failure(invocation,__LINE__);
            if(op==81u){if(w[1]!=type)return vk_spv_failure(invocation,__LINE__);values[result].type=type;memcpy(values[result].word,values[composite].word+off,count*4u);}
            else {uint32_t object=w[3];if(w[1]!=values[composite].type||object>=profile.bound||!values[object].initialized||values[object].type!=type)return vk_spv_failure(invocation,__LINE__);values[result]=values[composite];memcpy(values[result].word+off,values[object].word,count*4u);}
            values[result].initialized=1u;
        } else if (op == 79u) { /* OpVectorShuffle */
            uint32_t result_type = w[1], result_id = w[2];
            uint32_t first_vector = w[3], second_vector = w[4];
            uint32_t component_count;
            if (result_type >= profile.bound || result_id >= profile.bound ||
                first_vector >= profile.bound || second_vector >= profile.bound ||
                values[first_vector].initialized == 0u ||
                values[second_vector].initialized == 0u ||
                values[first_vector].type >= profile.bound ||
                values[second_vector].type >= profile.bound ||
                profile.kind[values[first_vector].type] != VK_SPV_KIND_VECTOR ||
                profile.kind[values[second_vector].type] != VK_SPV_KIND_VECTOR ||
                profile.kind[result_type] != VK_SPV_KIND_VECTOR ||
                profile.element[values[first_vector].type] != profile.element[result_type] ||
                profile.element[values[second_vector].type] != profile.element[result_type] ||
                (component_count = profile.count[result_type]) > 4u ||
                wc != 5u + component_count) return vk_spv_failure(invocation,__LINE__);
            values[result_id].type = result_type;
            for (uint32_t component = 0u; component < component_count; ++component) {
                const uint32_t selector = w[5u + component];
                const uint32_t first_count =
                    profile.count[values[first_vector].type];
                if (selector == UINT32_MAX) {
                    /* SPIR-V leaves this lane undefined; choosing zero is a
                       permitted deterministic implementation choice. */
                    values[result_id].word[component] = 0u;
                } else if (selector < first_count) {
                    values[result_id].word[component] =
                        values[first_vector].word[selector];
                } else if (selector - first_count <
                           profile.count[values[second_vector].type]) {
                    values[result_id].word[component] =
                        values[second_vector].word[selector - first_count];
                } else {
                    return vk_spv_failure(invocation,__LINE__);
                }
            }
            values[result_id].initialized = 1u;
        } else if (op == 169u) { /* OpSelect */
            uint32_t result_type = w[1], result_id = w[2], condition = w[3];
            uint32_t yes = w[4], no = w[5];
            if (result_type >= profile.bound || result_id >= profile.bound ||
                condition >= profile.bound || yes >= profile.bound || no >= profile.bound ||
                values[condition].initialized == 0u || values[yes].initialized == 0u ||
                values[no].initialized == 0u || values[condition].type >= profile.bound ||
                values[yes].type != result_type || values[no].type != result_type) return vk_spv_failure(invocation,__LINE__);
            uint32_t condition_type=values[condition].type;
            if(profile.kind[condition_type]==VK_SPV_KIND_BOOL){
                values[result_id] = values[values[condition].word[0] != 0u ? yes : no];
            }else{
                uint32_t scalar,components;
                if(profile.kind[condition_type]!=VK_SPV_KIND_VECTOR||profile.element[condition_type]>=profile.bound||
                    profile.kind[profile.element[condition_type]]!=VK_SPV_KIND_BOOL||profile.kind[result_type]!=VK_SPV_KIND_VECTOR||
                    !vk_spv_scalar_and_components(&profile,result_type,&scalar,&components)||components!=profile.count[condition_type])return vk_spv_failure(invocation,__LINE__);
                values[result_id]=values[yes];
                for(uint32_t component=0;component<components;++component)
                    values[result_id].word[component]=values[values[condition].word[component]?yes:no].word[component];
            }
        } else if (op >= 164u && op <= 168u) { /* scalar/vector logical operations */
            const uint32_t result_type = w[1], result_id = w[2], left = w[3];
            uint32_t scalar_type, components;
            if (result_type >= profile.bound || result_id >= profile.bound ||
                left >= profile.bound || values[left].initialized == 0u ||
                values[left].type != result_type ||
                !vk_spv_scalar_and_components(&profile, result_type,
                                              &scalar_type, &components) ||
                profile.kind[scalar_type] != VK_SPV_KIND_BOOL ||
                (op != 168u && (w[4] >= profile.bound ||
                 values[w[4]].initialized == 0u || values[w[4]].type != result_type)))
                return vk_spv_failure(invocation,__LINE__);
            values[result_id].type = result_type;
            for (uint32_t c = 0u; c < components; ++c) {
                const uint32_t a = values[left].word[c] != 0u;
                const uint32_t b = op == 168u ? 0u : values[w[4]].word[c] != 0u;
                if (op == 164u) values[result_id].word[c] = a == b;
                else if (op == 165u) values[result_id].word[c] = a != b;
                else if (op == 166u) values[result_id].word[c] = a || b;
                else if (op == 167u) values[result_id].word[c] = a && b;
                else values[result_id].word[c] = !a;
            }
            values[result_id].initialized = 1u;
        } else if (op >= 170u && op <= 179u) { /* scalar/vector integer comparisons */
            const uint32_t result_type = w[1], result_id = w[2], left = w[3], right = w[4];
            uint32_t input_scalar, input_components, result_scalar, result_components;
            if (result_type >= profile.bound || result_id >= profile.bound ||
                left >= profile.bound || right >= profile.bound ||
                values[left].initialized == 0u || values[right].initialized == 0u ||
                !vk_spv_arithmetic_type(&profile,values[left].type,values[right].type) ||
                !vk_spv_scalar_and_components(&profile, values[left].type,
                                              &input_scalar, &input_components) ||
                profile.kind[input_scalar] != VK_SPV_KIND_INT ||
                profile.width[input_scalar] != 32u ||
                !vk_spv_scalar_and_components(&profile, result_type,
                                              &result_scalar, &result_components) ||
                profile.kind[result_scalar] != VK_SPV_KIND_BOOL ||
                input_components != result_components ||
                (input_components == 1u && profile.kind[result_type] != VK_SPV_KIND_BOOL) ||
                (input_components > 1u &&
                 (profile.kind[result_type] != VK_SPV_KIND_VECTOR ||
                  profile.element[result_type] != result_scalar ||
                  profile.kind[result_scalar] != VK_SPV_KIND_BOOL))) return vk_spv_failure(invocation,__LINE__);
            values[result_id].type = result_type;
            for (uint32_t c = 0u; c < input_components; ++c) {
                const uint32_t a = values[left].word[c], b = values[right].word[c];
                int32_t sa, sb;
                int result;
                memcpy(&sa, &a, sizeof(sa));
                memcpy(&sb, &b, sizeof(sb));
                if (op == 170u) result = a == b;
                else if (op == 171u) result = a != b;
                else if (op == 172u) result = a > b;
                else if (op == 173u) result = sa > sb;
                else if (op == 174u) result = a >= b;
                else if (op == 175u) result = sa >= sb;
                else if (op == 176u) result = a < b;
                else if (op == 177u) result = sa < sb;
                else if (op == 178u) result = a <= b;
                else result = sa <= sb;
                values[result_id].word[c] = (uint32_t)result;
            }
            values[result_id].initialized = 1u;
        } else if ((op >= 109u && op <= 115u) || op == 124u) {
            const uint32_t result_type = w[1], result_id = w[2], source = w[3];
            uint32_t result_scalar, result_components, source_scalar, source_components;
            if (result_type >= profile.bound || result_id >= profile.bound ||
                source >= profile.bound || values[source].initialized == 0u ||
                !vk_spv_scalar_and_components(&profile, result_type,
                                              &result_scalar, &result_components) ||
                !vk_spv_scalar_and_components(&profile, values[source].type,
                                              &source_scalar, &source_components) ||
                result_components != source_components ||
                profile.width[result_scalar] != 32u || profile.width[source_scalar] != 32u)
                return vk_spv_failure(invocation,__LINE__);
            if ((op == 109u && (profile.kind[source_scalar] != VK_SPV_KIND_FLOAT ||
                                profile.kind[result_scalar] != VK_SPV_KIND_INT ||
                                profile.signedness[result_scalar] != 0u)) ||
                (op == 110u && (profile.kind[source_scalar] != VK_SPV_KIND_FLOAT ||
                                profile.kind[result_scalar] != VK_SPV_KIND_INT ||
                                profile.signedness[result_scalar] != 1u)) ||
                (op == 111u && (profile.kind[source_scalar] != VK_SPV_KIND_INT ||
                                profile.signedness[source_scalar] != 1u ||
                                profile.kind[result_scalar] != VK_SPV_KIND_FLOAT)) ||
                (op == 112u && (profile.kind[source_scalar] != VK_SPV_KIND_INT ||
                                profile.signedness[source_scalar] != 0u ||
                                profile.kind[result_scalar] != VK_SPV_KIND_FLOAT)) ||
                (op == 113u && (profile.kind[source_scalar] != VK_SPV_KIND_INT ||
                                profile.kind[result_scalar] != VK_SPV_KIND_INT ||
                                profile.signedness[result_scalar] != 0u)) ||
                (op == 114u && (profile.kind[source_scalar] != VK_SPV_KIND_INT ||
                                profile.kind[result_scalar] != VK_SPV_KIND_INT ||
                                profile.signedness[result_scalar] != 1u)) ||
                (op == 115u && (profile.kind[source_scalar] != VK_SPV_KIND_FLOAT ||
                                profile.kind[result_scalar] != VK_SPV_KIND_FLOAT)) ||
                (op == 124u && ((profile.kind[source_scalar] != VK_SPV_KIND_FLOAT &&
                                 profile.kind[source_scalar] != VK_SPV_KIND_INT) ||
                                (profile.kind[result_scalar] != VK_SPV_KIND_FLOAT &&
                                 profile.kind[result_scalar] != VK_SPV_KIND_INT)))) return vk_spv_failure(invocation,__LINE__);
            values[result_id].type = result_type;
            for (uint32_t c = 0u; c < result_components; ++c) {
                const uint32_t bits = values[source].word[c];
                if (op == 109u || op == 110u) {
                    const float f = vk_spv_word_float(bits);
                    if (f != f) return vk_spv_failure(invocation,__LINE__);
                    if (op == 109u) {
                        if (f < 0.0f || f >= 4294967296.0f) return vk_spv_failure(invocation,__LINE__);
                        values[result_id].word[c] = (uint32_t)f;
                    } else {
                        int32_t converted;
                        if (f < -2147483648.0f || f >= 2147483648.0f) return vk_spv_failure(invocation,__LINE__);
                        converted = (int32_t)f;
                        memcpy(&values[result_id].word[c], &converted, sizeof(converted));
                    }
                } else if (op == 111u) {
                    int32_t signed_value;
                    memcpy(&signed_value, &bits, sizeof(signed_value));
                    values[result_id].word[c] = vk_spv_float_word((float)signed_value);
                } else if (op == 112u) {
                    values[result_id].word[c] = vk_spv_float_word((float)bits);
                } else values[result_id].word[c] = bits;
            }
            values[result_id].initialized = 1u;
        } else if (op >= 180u && op <= 191u) { /* scalar/vector ordered/unordered float comparisons */
            const uint32_t result_type = w[1], result_id = w[2], left = w[3], right = w[4];
            uint32_t input_scalar, input_components, result_scalar, result_components;
            if (result_type >= profile.bound || result_id >= profile.bound ||
                left >= profile.bound || right >= profile.bound ||
                values[left].initialized == 0u || values[right].initialized == 0u ||
                values[left].type != values[right].type ||
                !vk_spv_scalar_and_components(&profile, values[left].type,
                                              &input_scalar, &input_components) ||
                profile.kind[input_scalar] != VK_SPV_KIND_FLOAT ||
                profile.width[input_scalar] != 32u ||
                !vk_spv_scalar_and_components(&profile, result_type,
                                              &result_scalar, &result_components) ||
                profile.kind[result_scalar] != VK_SPV_KIND_BOOL ||
                input_components != result_components ||
                (input_components == 1u && profile.kind[result_type] != VK_SPV_KIND_BOOL) ||
                (input_components > 1u &&
                 (profile.kind[result_type] != VK_SPV_KIND_VECTOR ||
                  profile.element[result_type] != result_scalar))) return vk_spv_failure(invocation,__LINE__);
            values[result_id].type = result_type;
            for (uint32_t c = 0u; c < input_components; ++c) {
                const float a = vk_spv_word_float(values[left].word[c]);
                const float b = vk_spv_word_float(values[right].word[c]);
                const int unordered = a != a || b != b;
                int predicate;
                if (op == 180u || op == 181u) predicate = a == b;
                else if (op == 182u || op == 183u) predicate = a != b;
                else if (op == 184u || op == 185u) predicate = a < b;
                else if (op == 186u || op == 187u) predicate = a > b;
                else if (op == 188u || op == 189u) predicate = a <= b;
                else predicate = a >= b;
                values[result_id].word[c] = (uint32_t)(
                    (op & 1u) != 0u ? (unordered || predicate) : (!unordered && predicate));
            }
            values[result_id].initialized = 1u;
        } else if (op == 12u) { /* GLSL.std.450 scalar/vector float subset */
            const uint32_t result_type = w[1], result_id = w[2];
            const uint32_t instruction = w[4];
            uint32_t components;
            uint32_t argument_count;
            if (wc < 6u || wc > 8u || result_type >= profile.bound ||
                result_id >= profile.bound || profile.glsl_std450_import_id == 0u ||
                w[3] != profile.glsl_std450_import_id) return vk_spv_failure(invocation,__LINE__);
            if (instruction == 66u) { /* Length, scalar result from scalar/vector float32 */
                uint32_t input=wc==6u?w[5]:profile.bound;
                if(input>=profile.bound||!values[input].initialized||profile.kind[result_type]!=VK_SPV_KIND_FLOAT||profile.width[result_type]!=32u)
                    return vk_spv_failure(invocation,__LINE__);
                uint32_t type=values[input].type,scalar=type,n=1u;
                if(type>=profile.bound)return vk_spv_failure(invocation,__LINE__);
                if(profile.kind[type]==VK_SPV_KIND_VECTOR){n=profile.count[type];scalar=profile.element[type];}
                if(!n||n>4u||scalar!=result_type)return vk_spv_failure(invocation,__LINE__);
                float maximum=0.0f;
                for(uint32_t c=0;c<n;++c){float a=vk_spv_word_float(values[input].word[c]&0x7fffffffu);
                    if(a!=a){maximum=a;break;}if(a>maximum)maximum=a;}
                float length=maximum;
                if(maximum>0.0f&&vk_clip_float_is_finite(maximum)){
                    float squared=0.0f;for(uint32_t c=0;c<n;++c){float a=vk_spv_word_float(values[input].word[c])/maximum;squared+=a*a;}
                    /* Shared freestanding x86 renderer: real IEEE square root,
                     * no host libm dependency for native Vulkan. Scaling avoids
                     * intermediate overflow/underflow for ordinary vectors. */
                    float root;__asm__("sqrtss %1,%0":"=x"(root):"x"(squared));length=maximum*root;
                }
                values[result_id].type=result_type;values[result_id].word[0]=vk_spv_float_word(length);values[result_id].initialized=1u;
                offset+=wc;continue;
            }
            if (instruction == 4u || instruction == 6u || instruction == 8u ||
                instruction == 9u || instruction == 10u || instruction == 31u ||
                instruction == 32u) argument_count = 1u;
            else if (instruction == 37u || instruction == 40u || instruction == 48u)
                argument_count = 2u;
            else if (instruction == 43u || instruction == 46u || instruction == 49u)
                argument_count = 3u;
            else return vk_spv_failure(invocation,__LINE__);
            if (wc != 5u + argument_count) return vk_spv_failure(invocation,__LINE__);
            if (profile.kind[result_type] == VK_SPV_KIND_FLOAT) {
                if (profile.width[result_type] != 32u) return vk_spv_failure(invocation,__LINE__);
                components = 1u;
            } else if (profile.kind[result_type] == VK_SPV_KIND_VECTOR &&
                       profile.element[result_type] < profile.bound &&
                       profile.kind[profile.element[result_type]] == VK_SPV_KIND_FLOAT &&
                       profile.width[profile.element[result_type]] == 32u) {
                components = profile.count[result_type];
                if (components == 0u || components > 4u) return vk_spv_failure(invocation,__LINE__);
            } else return vk_spv_failure(invocation,__LINE__);
            for (uint32_t argument = 0u; argument < argument_count; ++argument) {
                const uint32_t id = w[5u + argument];
                if (id >= profile.bound || values[id].initialized == 0u ||
                    values[id].type != result_type) return vk_spv_failure(invocation,__LINE__);
            }
            values[result_id].type = result_type;
            for (uint32_t component = 0u; component < components; ++component) {
                float a = vk_spv_word_float(values[w[5]].word[component]);
                float value;
                const float b = argument_count > 1u
                    ? vk_spv_word_float(values[w[6]].word[component]) : 0.0f;
                const float c = argument_count > 2u
                    ? vk_spv_word_float(values[w[7]].word[component]) : 0.0f;
                switch (instruction) {
                    case 4u: value = vk_spv_word_float(vk_spv_float_word(a) & 0x7fffffffu); break; /* FAbs */
                    case 6u: value = a < 0.0f ? -1.0f : a > 0.0f ? 1.0f : 0.0f; break; /* FSign */
                    case 8u: value = vk_spv_floor_f32(a); break; /* Floor */
                    case 9u: value = -vk_spv_floor_f32(-a); break; /* Ceil */
                    case 10u: value = a - vk_spv_floor_f32(a); break; /* Fract */
                    case 31u: value = vk_spv_sqrt_f32(a); break; /* Sqrt */
                    case 32u: value = 1.0f / vk_spv_sqrt_f32(a); break; /* InverseSqrt */
                    case 37u: value = a < b ? a : b; break; /* FMin */
                    case 40u: value = a > b ? a : b; break; /* FMax */
                    case 43u: value = a < b ? b : (a > c ? c : a); break; /* FClamp */
                    case 46u: value = a * (1.0f - c) + b * c; break; /* FMix */
                    case 48u: value = b < a ? 0.0f : 1.0f; break; /* Step */
                    case 49u: { /* SmoothStep */
                        float t = b == a ? (c < a ? 0.0f : 1.0f) : (c - a) / (b - a);
                        if (t < 0.0f) t = 0.0f;
                        if (t > 1.0f) t = 1.0f;
                        value = t * t * (3.0f - 2.0f * t);
                        break;
                    }
                    default: return vk_spv_failure(invocation,__LINE__);
                }
                values[result_id].word[component] = vk_spv_float_word(value);
            }
            values[result_id].initialized = 1u;
        } else if(op==84u||op==143u||(op>=144u&&op<=147u)){
            uint32_t type=w[1],result=w[2],a=w[3],b=op==84u?0u:w[4];
            if(type>=profile.bound||result>=profile.bound||a>=profile.bound||!values[a].initialized||
               (op!=84u&&(b>=profile.bound||!values[b].initialized)))return vk_spv_failure(invocation,__LINE__);
            uint32_t at=values[a].type,bt=op==84u?0u:values[b].type;
            if(at>=profile.bound||(op!=84u&&bt>=profile.bound))return vk_spv_failure(invocation,__LINE__);
            uint32_t ac=profile.count[at],bc=profile.count[bt],rc=profile.count[type];
            uint32_t ar=profile.kind[at]==VK_SPV_KIND_MATRIX?profile.count[profile.element[at]]:0u;
            uint32_t br=profile.kind[bt]==VK_SPV_KIND_MATRIX?profile.count[profile.element[bt]]:0u;
            uint32_t rr=profile.kind[type]==VK_SPV_KIND_MATRIX?profile.count[profile.element[type]]:0u;
            if(!vk_spv_value_words(&profile,type,0u))return vk_spv_failure(invocation,__LINE__);
            if(op==84u){
                if(profile.kind[at]!=VK_SPV_KIND_MATRIX||profile.kind[type]!=VK_SPV_KIND_MATRIX||ac!=rr||ar!=rc)return vk_spv_failure(invocation,__LINE__);
                for(uint32_t c=0u;c<rc;++c)for(uint32_t r=0u;r<rr;++r)values[result].word[c*rr+r]=values[a].word[r*ar+c];
            }else if(op==143u){
                if(type!=at||profile.kind[at]!=VK_SPV_KIND_MATRIX||profile.kind[bt]!=VK_SPV_KIND_FLOAT)return vk_spv_failure(invocation,__LINE__);
                float factor=vk_spv_word_float(values[b].word[0]);
                for(uint32_t i=0u;i<ac*ar;++i)values[result].word[i]=vk_spv_float_word(vk_spv_word_float(values[a].word[i])*factor);
            }else if(op==144u||op==145u){
                uint32_t matrix=op==144u?b:a,vector=op==144u?a:b,mt=values[matrix].type,vt=values[vector].type;
                uint32_t cols=profile.count[mt],rows=profile.count[profile.element[mt]],output=op==144u?cols:rows,inner=op==144u?rows:cols;
                if(profile.kind[mt]!=VK_SPV_KIND_MATRIX||profile.kind[vt]!=VK_SPV_KIND_VECTOR||profile.kind[type]!=VK_SPV_KIND_VECTOR||
                   profile.count[vt]!=inner||rc!=output||profile.element[vt]!=profile.element[profile.element[mt]]||profile.element[type]!=profile.element[vt])return vk_spv_failure(invocation,__LINE__);
                for(uint32_t i=0u;i<output;++i){float sum=0.0f;
                    for(uint32_t k=0u;k<inner;++k)sum+=vk_spv_word_float(values[vector].word[k])*vk_spv_word_float(values[matrix].word[op==144u?i*rows+k:k*rows+i]);
                    values[result].word[i]=vk_spv_float_word(sum);
                }
            }else if(op==146u){
                if(profile.kind[at]!=VK_SPV_KIND_MATRIX||profile.kind[bt]!=VK_SPV_KIND_MATRIX||profile.kind[type]!=VK_SPV_KIND_MATRIX||
                   ac!=br||ar!=rr||bc!=rc||profile.element[profile.element[at]]!=profile.element[profile.element[bt]]||
                   profile.element[profile.element[type]]!=profile.element[profile.element[at]])return vk_spv_failure(invocation,__LINE__);
                for(uint32_t c=0u;c<rc;++c)for(uint32_t r=0u;r<rr;++r){float sum=0.0f;
                    for(uint32_t k=0u;k<ac;++k)sum+=vk_spv_word_float(values[a].word[k*ar+r])*vk_spv_word_float(values[b].word[c*br+k]);
                    values[result].word[c*rr+r]=vk_spv_float_word(sum);
                }
            }else{
                if(profile.kind[at]!=VK_SPV_KIND_VECTOR||profile.kind[bt]!=VK_SPV_KIND_VECTOR||profile.kind[type]!=VK_SPV_KIND_MATRIX||
                   ac!=rr||bc!=rc||profile.element[at]!=profile.element[bt]||profile.element[profile.element[type]]!=profile.element[at])return vk_spv_failure(invocation,__LINE__);
                for(uint32_t c=0u;c<rc;++c)for(uint32_t r=0u;r<rr;++r)
                    values[result].word[c*rr+r]=vk_spv_float_word(vk_spv_word_float(values[a].word[r])*vk_spv_word_float(values[b].word[c]));
            }
            values[result].type=type;values[result].initialized=1u;
        } else if (op == 142u) { /* OpVectorTimesScalar for float vectors */
            uint32_t result_type = w[1], result_id = w[2];
            uint32_t vector = w[3], scalar = w[4];
            uint32_t components;
            float scale;
            if (wc != 5u || result_type >= profile.bound || result_id >= profile.bound ||
                vector >= profile.bound || scalar >= profile.bound ||
                profile.kind[result_type] != VK_SPV_KIND_VECTOR ||
                profile.element[result_type] >= profile.bound ||
                profile.kind[profile.element[result_type]] != VK_SPV_KIND_FLOAT ||
                profile.width[profile.element[result_type]] != 32u ||
                values[scalar].initialized == 0u || values[vector].initialized == 0u ||
                values[vector].type != result_type || values[scalar].type >= profile.bound ||
                profile.kind[values[scalar].type] != VK_SPV_KIND_FLOAT ||
                profile.width[values[scalar].type] != 32u) return vk_spv_failure(invocation,__LINE__);
            components = profile.count[result_type];
            if (components == 0u || components > 4u) return vk_spv_failure(invocation,__LINE__);
            scale = vk_spv_word_float(values[scalar].word[0]);
            values[result_id].type = result_type;
            for (uint32_t component = 0u; component < components; ++component)
                values[result_id].word[component] = vk_spv_float_word(
                    vk_spv_word_float(values[vector].word[component]) * scale);
            values[result_id].initialized = 1u;
        } else if (op == 148u) { /* OpDot for float vectors */
            uint32_t result_type = w[1], result_id = w[2];
            uint32_t left = w[3], right = w[4];
            uint32_t vector_type, components;
            float sum = 0.0f;
            if (wc != 5u || result_type >= profile.bound || result_id >= profile.bound ||
                left >= profile.bound || right >= profile.bound ||
                profile.kind[result_type] != VK_SPV_KIND_FLOAT || profile.width[result_type] != 32u ||
                values[left].initialized == 0u || values[right].initialized == 0u ||
                values[left].type >= profile.bound || values[left].type != values[right].type ||
                (vector_type = values[left].type) >= profile.bound ||
                profile.kind[vector_type] != VK_SPV_KIND_VECTOR ||
                profile.element[vector_type] >= profile.bound ||
                profile.kind[profile.element[vector_type]] != VK_SPV_KIND_FLOAT ||
                profile.width[profile.element[vector_type]] != 32u) return vk_spv_failure(invocation,__LINE__);
            components = profile.count[vector_type];
            if (components < 2u || components > 4u) return vk_spv_failure(invocation,__LINE__);
            for (uint32_t component = 0u; component < components; ++component)
                sum += vk_spv_word_float(values[left].word[component]) *
                       vk_spv_word_float(values[right].word[component]);
            values[result_id].type = result_type;
            values[result_id].word[0] = vk_spv_float_word(sum);
            values[result_id].initialized = 1u;
        } else if ((op >= 126u && op <= 139u)) {
            uint32_t result_type = w[1], result_id = w[2];
            uint32_t scalar_type, components;
            if (result_type >= profile.bound || result_id >= profile.bound ||
                !vk_spv_scalar_and_components(&profile, result_type,
                                              &scalar_type, &components) ||
                profile.width[scalar_type] != 32u ||
                (profile.kind[scalar_type] != VK_SPV_KIND_FLOAT &&
                 profile.kind[scalar_type] != VK_SPV_KIND_INT)) return vk_spv_failure(invocation,__LINE__);
            values[result_id].type = result_type;
            if (op == 126u || op == 127u) {
                uint32_t a = w[3];
                if (a >= profile.bound || values[a].initialized == 0u ||
                    values[a].type != result_type ||
                    (op == 126u && profile.kind[scalar_type] != VK_SPV_KIND_INT) ||
                    (op == 127u && profile.kind[scalar_type] != VK_SPV_KIND_FLOAT)) return vk_spv_failure(invocation,__LINE__);
                for (uint32_t c = 0u; c < components; ++c) {
                    if (op == 126u) values[result_id].word[c] = 0u - values[a].word[c];
                    else values[result_id].word[c] = vk_spv_float_word(-vk_spv_word_float(values[a].word[c]));
                }
            } else {
                uint32_t a = w[3], b = w[4];
                if (a >= profile.bound || b >= profile.bound ||
                    values[a].initialized == 0u || values[b].initialized == 0u ||
                    !vk_spv_arithmetic_type(&profile,values[a].type,result_type) || !vk_spv_arithmetic_type(&profile,values[b].type,result_type) ||
                    ((op == 128u || op == 130u || op == 132u || op == 134u ||
                      op == 135u || op == 137u || op == 138u || op == 139u) &&
                     profile.kind[scalar_type] != VK_SPV_KIND_INT) ||
                    ((op == 129u || op == 131u || op == 133u || op == 136u) &&
                     profile.kind[scalar_type] != VK_SPV_KIND_FLOAT)) return vk_spv_failure(invocation,__LINE__);
                for (uint32_t c = 0u; c < components; ++c) {
                    uint32_t av = values[a].word[c], bv = values[b].word[c];
                    if (op == 128u) values[result_id].word[c] = av + bv;
                    else if (op == 130u) values[result_id].word[c] = av - bv;
                    else if (op == 132u) values[result_id].word[c] = av * bv;
                    else if (op == 129u) values[result_id].word[c] = vk_spv_float_word(vk_spv_word_float(av) + vk_spv_word_float(bv));
                    else if (op == 131u) values[result_id].word[c] = vk_spv_float_word(vk_spv_word_float(av) - vk_spv_word_float(bv));
                    else if (op == 133u) values[result_id].word[c] = vk_spv_float_word(vk_spv_word_float(av) * vk_spv_word_float(bv));
                    else if (op == 136u) values[result_id].word[c] = vk_spv_float_word(vk_spv_word_float(av) / vk_spv_word_float(bv));
                    else if (op == 134u) { if (bv == 0u) return vk_spv_failure(invocation,__LINE__); values[result_id].word[c] = av / bv; }
                    else if (op == 135u) {
                        int32_t sa, sb, sr;
                        if (bv == 0u) return vk_spv_failure(invocation,__LINE__);
                        memcpy(&sa, &av, sizeof(sa)); memcpy(&sb, &bv, sizeof(sb));
                        if (sa == INT32_MIN && sb == -1) sr = INT32_MIN; else sr = sa / sb;
                        memcpy(&values[result_id].word[c], &sr, sizeof(sr));
                    } else if (op == 137u) { if (bv == 0u) return vk_spv_failure(invocation,__LINE__); values[result_id].word[c] = av % bv; }
                    else if (op == 138u || op == 139u) {
                        int32_t sa, sb;
                        int64_t remainder;
                        if (bv == 0u) return vk_spv_failure(invocation,__LINE__);
                        memcpy(&sa, &av, sizeof(sa)); memcpy(&sb, &bv, sizeof(sb));
                        if (sa == INT32_MIN && sb == -1) remainder = 0;
                        else remainder = (int64_t)(sa % sb);
                        if (op == 139u && remainder != 0 &&
                            ((remainder < 0) != (sb < 0))) remainder += sb;
                        values[result_id].word[c] = (uint32_t)(int32_t)remainder;
                    }
                    else return vk_spv_failure(invocation,__LINE__);
                }
            }
            values[result_id].initialized = 1u;
        } else if (op >= 194u && op <= 200u) { /* scalar/vector bitwise and shifts */
            const uint32_t result_type = w[1], result_id = w[2], left = w[3];
            uint32_t scalar_type, components;
            if (result_type >= profile.bound || result_id >= profile.bound ||
                left >= profile.bound || values[left].initialized == 0u ||
                values[left].type != result_type ||
                !vk_spv_scalar_and_components(&profile, result_type,
                                              &scalar_type, &components) ||
                profile.kind[scalar_type] != VK_SPV_KIND_INT ||
                profile.width[scalar_type] != 32u ||
                (op != 200u && (w[4] >= profile.bound ||
                 values[w[4]].initialized == 0u || values[w[4]].type != result_type)) ||
                (op == 195u && profile.signedness[scalar_type] != 1u)) return vk_spv_failure(invocation,__LINE__);
            values[result_id].type = result_type;
            for (uint32_t c = 0u; c < components; ++c) {
                const uint32_t a = values[left].word[c];
                const uint32_t b = op == 200u ? 0u : values[w[4]].word[c];
                if (op == 194u) values[result_id].word[c] = b >= 32u ? 0u : a >> b;
                else if (op == 195u) {
                    if (b >= 32u) values[result_id].word[c] =
                        (a & 0x80000000u) != 0u ? UINT32_MAX : 0u;
                    else if (b == 0u) values[result_id].word[c] = a;
                    else values[result_id].word[c] =
                        (a >> b) | ((a & 0x80000000u) != 0u ? (UINT32_MAX << (32u - b)) : 0u);
                } else if (op == 196u) values[result_id].word[c] = b >= 32u ? 0u : a << b;
                else if (op == 197u) values[result_id].word[c] = a | b;
                else if (op == 198u) values[result_id].word[c] = a ^ b;
                else if (op == 199u) values[result_id].word[c] = a & b;
                else values[result_id].word[c] = ~a;
            }
            values[result_id].initialized = 1u;
        } else return vk_spv_failure(invocation,__LINE__);
        offset += wc;
    }
    if(stage==VK_SHADER_STAGE_COMPUTE_BIT){execution->done=returned!=0u;return returned?1:0;}
    uint32_t output_id = stage == VK_SHADER_STAGE_VERTEX_BIT
        ? profile.position_id : profile.output_id;
    if (returned == 0u || output_id >= profile.bound || output_id == 0u ||
        values[output_id].initialized == 0u) return vk_spv_failure(invocation,__LINE__);
    for (uint32_t c = 0u; c < 4u; ++c) output_words[c] = values[output_id].word[c];
    if (stage == VK_SHADER_STAGE_VERTEX_BIT) {
        for (uint32_t location = 0u;
             location < VK_SPV_MAX_INTERFACE_LOCATIONS; ++location) {
            const uint32_t id = profile.vertex_varying_id[location];
            if (id == 0u) continue;
            if (varying_words == 0 || id >= profile.bound ||
                values[id].initialized == 0u) return vk_spv_failure(invocation,__LINE__);
            memcpy(varying_words[location], values[id].word,
                   sizeof(uint32_t) * 4u);
        }
    }
    return 1;
#undef calls
#undef profile
}

static float vk_edge(float ax, float ay, float bx, float by, float px, float py) {
    /* Coverage uses the advertised 8-bit subpixel grid. Integer products
       preserve exact edge zero/sign even near the 4096px viewport limit;
       float32 products would lose low bits and break shared-edge ownership. */
    int64_t a_x=(int64_t)(ax*256.0f),a_y=(int64_t)(ay*256.0f);
    int64_t b_x=(int64_t)(bx*256.0f),b_y=(int64_t)(by*256.0f);
    int64_t p_x=(int64_t)(px*256.0f),p_y=(int64_t)(py*256.0f);
    return (float)((b_x-a_x)*(p_y-a_y)-(b_y-a_y)*(p_x-a_x))/65536.0f;
}

static int vk_edge_covers_sample(float edge, float area,
                                 float ax, float ay, float bx, float by) {
    float dx, dy;
    if (area < 0.0f) {
        edge = -edge;
        dx = ax - bx;
        dy = ay - by;
    } else {
        dx = bx - ax;
        dy = by - ay;
    }
    if (edge > 0.0f) return 1;
    if (edge < 0.0f) return 0;
    /* Framebuffer coordinates grow downward.  Of two oppositely directed
       copies of a shared edge, exactly the top/left-facing one owns a sample
       on the edge.  This prevents adjacent triangles from shading it twice. */
    return dy < 0.0f || (dy == 0.0f && dx > 0.0f);
}

static int vk_depth_compare(VkCompareOp operation, float incoming, float stored) {
    switch (operation) {
        case VK_COMPARE_OP_NEVER: return 0;
        case VK_COMPARE_OP_LESS: return incoming < stored;
        case VK_COMPARE_OP_EQUAL: return incoming == stored;
        case VK_COMPARE_OP_LESS_OR_EQUAL: return incoming <= stored;
        case VK_COMPARE_OP_GREATER: return incoming > stored;
        case VK_COMPARE_OP_NOT_EQUAL: return incoming != stored;
        case VK_COMPARE_OP_GREATER_OR_EQUAL: return incoming >= stored;
        case VK_COMPARE_OP_ALWAYS: return 1;
        default: return 0;
    }
}

static int vk_draw_vertex_index(const vk_recorded_command_t *record,
                                uint32_t ordinal, uint32_t *vertex_index_out) {
    const vk_buffer_impl_t *buffer;
    const vk_memory_impl_t *memory;
    uint64_t index_size;
    uint64_t index_number;
    uint64_t relative;
    uint64_t absolute;
    uint32_t raw_index;
    int64_t adjusted;

    if (record == 0 || vertex_index_out == 0) return 0;
    if (record->args.draw.indexed == 0u) {
        if (ordinal > UINT32_MAX - record->args.draw.first_vertex) return 0;
        *vertex_index_out = record->args.draw.first_vertex + ordinal;
        return 1;
    }
    buffer = record->args.draw.index_buffer;
    if (buffer == 0 || buffer->memory == 0 || buffer->memory->bytes == 0 ||
        buffer->memory_offset > buffer->memory->size ||
        buffer->size > buffer->memory->size - buffer->memory_offset) return 0;
    memory = buffer->memory;
    index_size = record->args.draw.index_type == VK_INDEX_TYPE_UINT16 ? 2u :
                 record->args.draw.index_type == VK_INDEX_TYPE_UINT32 ? 4u : 0u;
    if (index_size == 0u ||
        ordinal > UINT32_MAX - record->args.draw.first_index) return 0;
    index_number = (uint64_t)record->args.draw.first_index + ordinal;
    if (index_number > (UINT64_MAX - record->args.draw.index_buffer_offset) /
                       index_size) return 0;
    relative = record->args.draw.index_buffer_offset + index_number * index_size;
    if (relative > buffer->size || index_size > buffer->size - relative ||
        buffer->memory_offset > UINT64_MAX - relative) return 0;
    absolute = buffer->memory_offset + relative;
    if (absolute > memory->size || index_size > memory->size - absolute ||
        absolute > SIZE_MAX) return 0;
    if (index_size == 2u) {
        uint16_t index16;
        memcpy(&index16, memory->bytes + (size_t)absolute, sizeof(index16));
        raw_index = index16;
    } else {
        memcpy(&raw_index, memory->bytes + (size_t)absolute, sizeof(raw_index));
    }
    adjusted = (int64_t)raw_index + record->args.draw.vertex_offset;
    /* The software profile performs a bounded vertex fetch and exposes the
       adjusted value as a 32-bit gl_VertexIndex. Negative/out-of-range fetches
       are rejected before any render-target writes. */
    if (adjusted < 0 || adjusted > INT32_MAX) return 0;
    *vertex_index_out = (uint32_t)adjusted;
    return 1;
}

static int vk_validate_vertex_index(const vk_recorded_command_t *record,
                                    uint32_t vertex_index) {
    const vk_pipeline_impl_t *pipeline = record->args.draw.pipeline;
    const vk_buffer_impl_t *buffer = record->args.draw.vertex_buffer;
    const vk_memory_impl_t *memory;

    if (pipeline->vertex_attribute_mask == 0u) return 1;
    if (buffer == 0 || buffer->memory == 0 || buffer->memory->bytes == 0 ||
        buffer->device != pipeline->device ||
        (buffer->usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) == 0u ||
        buffer->memory_offset > buffer->memory->size ||
        buffer->size > buffer->memory->size - buffer->memory_offset ||
        record->args.draw.vertex_buffer_offset > buffer->size) return 0;
    memory = buffer->memory;
    if(pipeline->vertex_binding.inputRate==VK_VERTEX_INPUT_RATE_INSTANCE)vertex_index=record->args.draw.first_instance+(record->args.draw.instance_count?record->args.draw.instance_count-1u:0u);
    for (uint32_t location = 0u;
         location < VK_SPV_MAX_INTERFACE_LOCATIONS; ++location) {
        uint64_t relative, absolute;
        uint32_t attribute_size;
        if ((pipeline->vertex_attribute_mask & (1u << location)) == 0u) continue;
        if (!vk_vertex_format_info(pipeline->vertex_attributes[location].format,
                                   0, &attribute_size, 0, 0)) return 0;
        if (record->args.draw.vertex_buffer_offset >
                UINT64_MAX - pipeline->vertex_attributes[location].offset)
            return 0;
        relative = record->args.draw.vertex_buffer_offset +
                   pipeline->vertex_attributes[location].offset;
        if (pipeline->vertex_binding.stride && (uint64_t)vertex_index >
            (UINT64_MAX - relative) / pipeline->vertex_binding.stride) return 0;
        relative += (uint64_t)vertex_index * pipeline->vertex_binding.stride;
        if (relative > buffer->size || attribute_size > buffer->size - relative ||
            buffer->memory_offset > UINT64_MAX - relative) return 0;
        absolute = buffer->memory_offset + relative;
        if (absolute > memory->size ||
            attribute_size > memory->size - absolute || absolute > SIZE_MAX) return 0;
    }
    return 1;
}

static int vk_validate_draw_vertex_buffer(const vk_recorded_command_t *record) {
    const vk_pipeline_impl_t *pipeline;
    const vk_buffer_impl_t *buffer;
    const vk_memory_impl_t *memory;
    uint64_t last_index;
    if (record == 0 || (pipeline = record->args.draw.pipeline) == 0) return 0;
    if (record->args.draw.indexed != 0u) {
        const vk_buffer_impl_t *indices = record->args.draw.index_buffer;
        uint64_t index_size = record->args.draw.index_type == VK_INDEX_TYPE_UINT16 ? 2u :
                              record->args.draw.index_type == VK_INDEX_TYPE_UINT32 ? 4u : 0u;
        uint64_t first_index = record->args.draw.first_index;
        if (record->args.draw.vertex_count == 0u) return 1;
        if (indices == 0 || indices->device != pipeline->device ||
            (indices->usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) == 0u ||
            index_size == 0u || indices->memory == 0 ||
            indices->memory->bytes == 0 ||
            indices->memory_offset > indices->memory->size ||
            indices->size > indices->memory->size - indices->memory_offset ||
            record->args.draw.index_buffer_offset > indices->size ||
            first_index > UINT64_MAX / index_size ||
            first_index * index_size > indices->size -
                record->args.draw.index_buffer_offset ||
            record->args.draw.vertex_count >
                (indices->size - record->args.draw.index_buffer_offset -
                 first_index * index_size) / index_size) return 0;
        if (record->args.draw.index_buffer_offset > UINT64_MAX - indices->memory_offset)
            return 0;
        for (uint32_t i = 0u; i < record->args.draw.vertex_count; ++i) {
            uint32_t vertex_index;
            if (!vk_draw_vertex_index(record, i, &vertex_index) ||
                !vk_validate_vertex_index(record, vertex_index)) return 0;
        }
        return 1;
    }
    if (pipeline->vertex_attribute_mask == 0u || record->args.draw.vertex_count == 0u)
        return 1;
    buffer = record->args.draw.vertex_buffer;
    if (buffer == 0 || buffer->device != pipeline->device ||
        (buffer->usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) == 0u ||
        (memory = buffer->memory) == 0 || memory->bytes == 0 ||
        buffer->memory_offset > memory->size ||
        buffer->size > memory->size - buffer->memory_offset ||
        record->args.draw.vertex_buffer_offset > buffer->size)
        return 0;
    last_index = (uint64_t)record->args.draw.first_vertex +
                 (uint64_t)record->args.draw.vertex_count - 1u;
    if(pipeline->vertex_binding.inputRate==VK_VERTEX_INPUT_RATE_INSTANCE)last_index=(uint64_t)record->args.draw.first_instance+record->args.draw.instance_count-1u;
    for (uint32_t location = 0u;
         location < VK_SPV_MAX_INTERFACE_LOCATIONS; ++location) {
        uint64_t base, end;
        uint32_t attribute_size;
        if ((pipeline->vertex_attribute_mask & (1u << location)) == 0u) continue;
        if (!vk_vertex_format_info(pipeline->vertex_attributes[location].format,
                                   0, &attribute_size, 0, 0)) return 0;
        base = record->args.draw.vertex_buffer_offset;
        if (pipeline->vertex_attributes[location].offset > UINT64_MAX - base)
            return 0;
        base += pipeline->vertex_attributes[location].offset;
        if (base > UINT64_MAX - attribute_size ||
            (pipeline->vertex_binding.stride && last_index > (UINT64_MAX - base - attribute_size) /
                         pipeline->vertex_binding.stride)) return 0;
        end = base + last_index * pipeline->vertex_binding.stride + attribute_size;
        if (end > buffer->size || end > SIZE_MAX ||
            buffer->memory_offset > SIZE_MAX ||
            end > memory->size - buffer->memory_offset) return 0;
    }
    return 1;
}

static int vk_read_vertex_attribute(const vk_recorded_command_t *record,
                                    uint32_t vertex_index, uint32_t location,
                                    uint32_t words[4]) {
    const vk_pipeline_impl_t *pipeline;
    const vk_buffer_impl_t *buffer;
    const vk_memory_impl_t *memory;
    uint64_t relative, absolute;
    uint32_t attribute_size, component_count;
    uint8_t encoding, bgra;
    if (words == 0 || record == 0 ||
        (pipeline = record->args.draw.pipeline) == 0 ||
        location >= VK_SPV_MAX_INTERFACE_LOCATIONS ||
        (pipeline->vertex_attribute_mask & (1u << location)) == 0u ||
        !vk_vertex_format_info(pipeline->vertex_attributes[location].format,
            &component_count, &attribute_size, &encoding, &bgra) ||
        (buffer = record->args.draw.vertex_buffer) == 0 ||
        (memory = buffer->memory) == 0 || memory->bytes == 0 ||
        (pipeline->vertex_binding.inputRate==VK_VERTEX_INPUT_RATE_VERTEX && record->args.draw.indexed == 0u &&
         vertex_index < record->args.draw.first_vertex) ||
        record->args.draw.vertex_buffer_offset >
            UINT64_MAX - pipeline->vertex_attributes[location].offset)
        return 0;
    if(pipeline->vertex_binding.inputRate==VK_VERTEX_INPUT_RATE_INSTANCE)vertex_index=record->args.draw.first_instance;
    relative = record->args.draw.vertex_buffer_offset +
               pipeline->vertex_attributes[location].offset;
    if (pipeline->vertex_binding.stride && (uint64_t)vertex_index > (UINT64_MAX - relative) / pipeline->vertex_binding.stride)
        return 0;
    relative += (uint64_t)vertex_index * pipeline->vertex_binding.stride;
    if (relative > buffer->size || attribute_size > buffer->size - relative ||
        buffer->memory_offset > memory->size ||
        relative > memory->size - buffer->memory_offset ||
        attribute_size > memory->size - buffer->memory_offset - relative ||
        buffer->memory_offset > SIZE_MAX || relative > SIZE_MAX - buffer->memory_offset)
        return 0;
    absolute = buffer->memory_offset + relative;
    if (absolute > SIZE_MAX - attribute_size) return 0;
    words[0] = 0u;
    words[1] = 0u;
    words[2] = 0u;
    words[3] = 0x3f800000u; /* Vulkan fills omitted color components with 0,0,0,1. */
    if (encoding == VK_VERTEX_FORMAT_FLOAT32) {
        memcpy(words, memory->bytes + (size_t)absolute, attribute_size);
    } else {
        const uint8_t *source = memory->bytes + (size_t)absolute;
        for (uint32_t component = 0u; component < component_count; ++component) {
            uint32_t source_component = bgra != 0u && component_count >= 3u &&
                (component == 0u || component == 2u) ? 2u - component : component;
            float converted;
            if (encoding == VK_VERTEX_FORMAT_UNORM8) {
                converted = (float)source[source_component] / 255.0f;
            } else {
                int8_t signed_value;
                memcpy(&signed_value, &source[source_component], sizeof(signed_value));
                converted = (float)signed_value / 127.0f;
                if (converted < -1.0f) converted = -1.0f;
            }
            words[component] = vk_spv_float_word(converted);
        }
    }
    return 1;
}

static int vk_clip_float_is_finite(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7f800000u) != 0x7f800000u;
}

static float vk_clip_distance(const vk_clip_vertex_t *vertex, uint32_t plane) {
    float x = vk_spv_word_float(vertex->position[0]);
    float y = vk_spv_word_float(vertex->position[1]);
    float z = vk_spv_word_float(vertex->position[2]);
    float w = vk_spv_word_float(vertex->position[3]);
    switch (plane) {
        case 0u: return x + w; /* left */
        case 1u: return w - x; /* right */
        case 2u: return y + w; /* bottom */
        case 3u: return w - y; /* top */
        case 4u: return z;     /* Vulkan near plane: 0 <= z */
        default: return w - z;/* Vulkan far plane: z <= w */
    }
}

static int vk_clip_vertex_lerp(const vk_clip_vertex_t *a,
                               const vk_clip_vertex_t *b,
                               float t, vk_clip_vertex_t *out) {
    for (uint32_t i = 0u; i < 4u; ++i) {
        float ap = vk_spv_word_float(a->position[i]);
        float bp = vk_spv_word_float(b->position[i]);
        float position = ap + (bp - ap) * t;
        if (!vk_clip_float_is_finite(position)) return 0;
        out->position[i] = vk_spv_float_word(position);
    }
    for (uint32_t location = 0u;
         location < VK_SPV_MAX_INTERFACE_LOCATIONS; ++location) {
        for (uint32_t component = 0u; component < 4u; ++component) {
            float av = vk_spv_word_float(a->varying[location][component]);
            float bv = vk_spv_word_float(b->varying[location][component]);
            float varying = av + (bv - av) * t;
            if (!vk_clip_float_is_finite(varying)) return 0;
            out->varying[location][component] = vk_spv_float_word(varying);
        }
    }
    return 1;
}

static void vk_stencil_update(uint8_t *byte, const VkStencilOpState *state, VkStencilOp op) {
    uint32_t old = *byte, next;
    switch (op) {
    case VK_STENCIL_OP_KEEP: next = old; break;
    case VK_STENCIL_OP_ZERO: next = 0u; break;
    case VK_STENCIL_OP_REPLACE: next = state->reference; break;
    case VK_STENCIL_OP_INCREMENT_AND_CLAMP: next = old < 255u ? old + 1u : 255u; break;
    case VK_STENCIL_OP_DECREMENT_AND_CLAMP: next = old > 0u ? old - 1u : 0u; break;
    case VK_STENCIL_OP_INVERT: next = ~old; break;
    case VK_STENCIL_OP_INCREMENT_AND_WRAP: next = old + 1u; break;
    default: next = old - 1u; break; /* validated DECREMENT_AND_WRAP */
    }
    *byte = (uint8_t)((old & ~state->writeMask) | (next & state->writeMask));
}

#include "vex_render_quad.h"

static VkResult vk_shade_sample(const vk_recorded_command_t *record,
                                  vk_pipeline_impl_t *pipeline,
                                  vk_image_plane_t *image, uint32_t *pixels,
                                  float *depth_values, int32_t x, int32_t y,
                                  float depth,
                                  const float fragment_coord[4],
                                  const uint32_t varyings[VK_SPV_MAX_INTERFACE_LOCATIONS][4], int front_facing, uint32_t sample) {
    const vk_spv_profile_t *fragment_profile = pipeline->fragment_profile;
    uint32_t fragment_words[4];
    VkClearColorValue fragment_color;
    uint32_t color;
    const uint32_t pixel_index = ((uint32_t)y * image->width + (uint32_t)x)*image->samples+sample;
    if ((pipeline->sample_mask & (1u<<sample)) == 0u) return VK_SUCCESS;
    uint32_t depth_index = 0u;
    if (depth_values != 0) {
        vk_image_view_impl_t *view = record->args.draw.framebuffer->depth_attachment;
        vk_image_subresource_impl_t *sub = vk_image_subresource(view->image,
            view->range.baseMipLevel, view->range.baseArrayLayer);
        depth_index = (((uint32_t)y * sub->width + (uint32_t)x)*image->samples+sample)*(view->image->format==VK_FORMAT_D32_SFLOAT_S8_UINT?2u:1u);
    }
    if (fragment_profile == 0 || fragment_profile->stage != VK_SHADER_STAGE_FRAGMENT_BIT)
        return VK_ERROR_INITIALIZATION_FAILED;
    int fragment_result;
    vk_spv_invocation_t band_invocation={0};
    if(fragment_profile->fast_kind==VEX_FAST_SOLID_VARYING&&vex_render_fast_paths_enabled&&varyings){
        memcpy(fragment_words,varyings[fragment_profile->fast_location[0]],sizeof(fragment_words));fragment_result=1;
    }else if(pipeline->gl_quad)
        fragment_result=vex_spv_run_quad(record,pipeline,x,y,front_facing,fragment_coord,varyings,fragment_words);
    else{
        /* A concurrent band runs the interpreter on its own workspace. */
        if(pipeline->workspace_override)band_invocation.values=pipeline->workspace_override;
        fragment_result=vk_spv_run(pipeline->fragment_shader, fragment_profile,
                    VK_SHADER_STAGE_FRAGMENT_BIT, 0u, 0u, front_facing, 0u,
                    record->args.draw.descriptor_sets,
                    record->args.draw.push_constants[1],
                    fragment_profile->fragment_varying_mask != 0u
                        ? varyings : 0u,
                    fragment_coord,fragment_words, 0u, pipeline->workspace_override?&band_invocation:0);
    }
    if(!fragment_result){
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if(fragment_result==2)return VK_SUCCESS;
    memcpy(fragment_color.float32, fragment_words, sizeof(fragment_words));
    if (pipeline->alpha_to_coverage) {
        float alpha=fragment_color.float32[3];
        uint32_t covered=!(alpha>0.0f)?0u:alpha>=1.0f?image->samples:(uint32_t)(alpha*image->samples+0.5f);
        if (sample>=covered) return VK_SUCCESS;
    }
    uint8_t *stencil = 0;
    const VkStencilOpState *stencil_state = front_facing ? &record->args.draw.state.stencil[0] : &record->args.draw.state.stencil[1];
    vk_image_view_impl_t *stencil_view = record->args.draw.framebuffer->depth_attachment;
    if (pipeline->stencil_test_enable && stencil_view != 0 && vk_format_has_stencil(stencil_view->image->format)) {
        vk_image_subresource_impl_t *sub = vk_image_subresource(stencil_view->image,
            stencil_view->range.baseMipLevel, stencil_view->range.baseArrayLayer);
        stencil = (uint8_t *)vk_image_subresource_pixels(stencil_view->image,
            stencil_view->range.baseMipLevel, stencil_view->range.baseArrayLayer);
        if (stencil == 0) return VK_ERROR_DEVICE_LOST;
        stencil += (((uint32_t)y * sub->width + (uint32_t)x)*image->samples+sample)*vk_format_texel_bytes(stencil_view->image->format)+vk_aspect_offset(stencil_view->image->format,VK_IMAGE_ASPECT_STENCIL_BIT);
        uint32_t mask = stencil_state->compareMask & 0xffu;
        if (!vk_depth_compare(stencil_state->compareOp, (float)(stencil_state->reference & mask), (float)(*stencil & mask))) {
            vk_stencil_update(stencil, stencil_state, stencil_state->failOp);
            return VK_SUCCESS;
        }
    }
    if (depth_values != 0 &&
        !vk_depth_compare(pipeline->depth_compare_op, depth,
                          depth_values[depth_index])) {
        if (stencil != 0) vk_stencil_update(stencil, stencil_state, stencil_state->depthFailOp);
        return VK_SUCCESS;
    }
    if(record->args.draw.query_pool){
        vk_query_slot_t *slot=&record->args.draw.query_pool->slots[record->args.draw.query_index];
        if(__atomic_load_n(&slot->state,__ATOMIC_ACQUIRE)!=2u)return VK_ERROR_DEVICE_LOST;
        __atomic_fetch_add(&slot->samples,1u,__ATOMIC_RELAXED);
    }
    color = pack_rgba(&fragment_color);
    {
        uint32_t destination = vk_pixel_to_canonical(image->format,
            pixels[pixel_index]);
        static const uint8_t bayer[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
        float dither=pipeline->dither_enable?(((float)bayer[((unsigned)y&3)*4+((unsigned)x&3)]+.5f)/16.f-.5f)/255.f:0.f;
        uint32_t output = (pipeline->color_blend.blendEnable != VK_FALSE||pipeline->dither_enable)
            ? vk_blend_pixel(fragment_color.float32, destination, pipeline,record->args.draw.state.blend_constants,dither) : color;
        if(pipeline->logic_op_enable)output=vk_logic_pixel(pipeline->logic_op,color,destination);
        output = vk_apply_color_write_mask(output, destination,
            pipeline->color_blend.colorWriteMask);
        pixels[pixel_index] = vk_pixel_from_canonical(image->format, output);
    }
    if (depth_values != 0 && pipeline->depth_write_enable != VK_FALSE)
        depth_values[depth_index] = depth;
    if (stencil != 0) vk_stencil_update(stencil, stencil_state, stencil_state->passOp);
    return VK_SUCCESS;
}

static VkResult vk_shade_fragment(const vk_recorded_command_t *record,
        vk_pipeline_impl_t *pipeline, vk_image_plane_t *image, uint32_t *pixels,
        float *depth_values, int32_t x, int32_t y, float depth,
        const uint32_t varyings[VK_SPV_MAX_INTERFACE_LOCATIONS][4], int front_facing,const float coord[4]) {
    for (uint32_t sample=0u;sample<image->samples;++sample) {
        VkResult r=vk_shade_sample(record,pipeline,image,pixels,depth_values,x,y,depth,coord,varyings,front_facing,sample);
        if(r!=VK_SUCCESS)return r;
    }
    return VK_SUCCESS;
}

static int vk_run_draw_vertex(const vk_recorded_command_t *record,
                              vk_pipeline_impl_t *pipeline,
                              uint32_t vertex_index, vk_clip_vertex_t *out) {
    const vk_spv_profile_t *profile = pipeline->vertex_profile;
    uint32_t attributes[VK_SPV_MAX_INTERFACE_LOCATIONS][4] = {{0u}};
    const uint32_t (*attribute_words)[4] = 0;
    if (profile == 0 || profile->stage != VK_SHADER_STAGE_VERTEX_BIT || out == 0)
        return 0;
    memset(out, 0, sizeof(*out));
    if (profile->attribute_mask != 0u) {
        for (uint32_t location = 0u;
             location < VK_SPV_MAX_INTERFACE_LOCATIONS; ++location) {
            if ((profile->attribute_mask & (1u << location)) != 0u &&
                !vk_read_vertex_attribute(record, vertex_index, location,
                                          attributes[location])) return 0;
        }
        attribute_words = attributes;
    }
    int result = vk_spv_run(pipeline->vertex_shader, profile,
        VK_SHADER_STAGE_VERTEX_BIT, vertex_index,record->args.draw.first_instance, 0, attribute_words,
        record->args.draw.descriptor_sets,
        record->args.draw.push_constants[0], 0u,0u,
        out->position, out->varying,0);
    if(result && pipeline->vertex_profile->gl_environment){float z=vk_spv_word_float(out->position[2]);float w=vk_spv_word_float(out->position[3]);out->position[2]=vk_spv_float_word((z+w)*.5f);}
    if(result && profile->gl_top_left_height)
        out->position[1]=vk_spv_float_word(-vk_spv_word_float(out->position[1]));
    return result;
}

static void vk_draw_raster_bounds(const vk_recorded_command_t *record,
                                  const vk_image_plane_t *image,
                                  int32_t *x0, int32_t *y0,
                                  int32_t *x1, int32_t *y1) {
    const VkRect2D *scissor = &record->args.draw.scissor;
    *x0 = record->args.draw.render_area.offset.x;
    *y0 = record->args.draw.render_area.offset.y;
    *x1 = *x0 + (int32_t)record->args.draw.render_area.extent.width;
    *y1 = *y0 + (int32_t)record->args.draw.render_area.extent.height;
    if (scissor->offset.x > *x0) *x0 = scissor->offset.x;
    if (scissor->offset.y > *y0) *y0 = scissor->offset.y;
    if ((int64_t)scissor->offset.x + scissor->extent.width < *x1)
        *x1 = scissor->offset.x + (int32_t)scissor->extent.width;
    if ((int64_t)scissor->offset.y + scissor->extent.height < *y1)
        *y1 = scissor->offset.y + (int32_t)scissor->extent.height;
    if (*x1 > (int32_t)image->width) *x1 = (int32_t)image->width;
    if (*y1 > (int32_t)image->height) *y1 = (int32_t)image->height;
}

static VkResult vk_rasterize_point(const vk_recorded_command_t *record,
                                   vk_pipeline_impl_t *pipeline,
                                   vk_image_plane_t *image, uint32_t *pixels,
                                   uint32_t vertex_index) {
    pipeline->quad_cache_valid=0u;
    const VkViewport *viewport = &record->args.draw.viewport;
    vk_clip_vertex_t vertex;
    float x, y, z, w, sx, sy, depth;
    float *depth_values = 0;
    int32_t clip_x0, clip_y0, clip_x1, clip_y1, px, py;
    if (!vk_run_draw_vertex(record, pipeline, vertex_index, &vertex))
        return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t plane = 0u; plane < 6u; ++plane) {
        const float distance = vk_clip_distance(&vertex, plane);
        if (!vk_clip_float_is_finite(distance)) return VK_ERROR_INITIALIZATION_FAILED;
        if (distance < 0.0f) return VK_SUCCESS;
    }
    x = vk_spv_word_float(vertex.position[0]);
    y = vk_spv_word_float(vertex.position[1]);
    z = vk_spv_word_float(vertex.position[2]);
    w = vk_spv_word_float(vertex.position[3]);
    if (!(w > 0.0f)) return VK_SUCCESS;
    sx = viewport->x + ((x / w) + 1.0f) * viewport->width * 0.5f;
    sy = viewport->y + ((y / w) + 1.0f) * viewport->height * 0.5f;
    depth = viewport->minDepth + (z / w) * (viewport->maxDepth - viewport->minDepth);
    if (!vk_clip_float_is_finite(sx) || !vk_clip_float_is_finite(sy) ||
        !vk_clip_float_is_finite(depth)) return VK_ERROR_INITIALIZATION_FAILED;
    vk_draw_raster_bounds(record, image, &clip_x0, &clip_y0, &clip_x1, &clip_y1);
    if (pipeline->depth_test_enable != VK_FALSE) {
        vk_framebuffer_impl_t *framebuffer = record->args.draw.framebuffer;
        if (framebuffer == 0 || framebuffer->depth_attachment == 0 ||
            (depth_values = vk_view_depth_values(framebuffer->depth_attachment)) == 0)
            return VK_ERROR_INITIALIZATION_FAILED;
    }
    /* PointSize=1: test the same native sample grid as triangles, including
       fractional coverage at framebuffer boundaries. No nearest-pixel proxy. */
    int32_t x0=(int32_t)(sx-.5f),y0=(int32_t)(sy-.5f);
    for(py=y0;py<=y0+1;++py)for(px=x0;px<=x0+1;++px){
        if(px<clip_x0||px>=clip_x1||py<clip_y0||py>=clip_y1)continue;
        for(uint32_t sample=0u;sample<image->samples;++sample){
            float ox,oy;vk_sample_location(image->samples,sample,&ox,&oy);
            float x=(float)px+ox,y=(float)py+oy;
            if(x<sx-.5f||x>=sx+.5f||y<sy-.5f||y>=sy+.5f)continue;
            const float coord[4]={(float)px+.5f,(float)py+.5f,depth,1.0f/w};
            VkResult r=vk_shade_sample(record,pipeline,image,pixels,depth_values,px,py,depth,coord,vertex.varying,1,sample);
            if(r!=VK_SUCCESS)return r;
        }
    }
    return VK_SUCCESS;
}

static VkResult vk_rasterize_line(const vk_recorded_command_t *record,
                                 vk_pipeline_impl_t *pipeline,
                                 vk_image_plane_t *image, uint32_t *pixels,
                                 uint32_t vertex_index0, uint32_t vertex_index1) {
    pipeline->quad_cache_valid=0u;
    const VkViewport *viewport = &record->args.draw.viewport;
    vk_clip_vertex_t first, second;
    float *depth_values = 0;
    int32_t clip_x0, clip_y0, clip_x1, clip_y1;
    float sx[2], sy[2], ndc_depth[2], inverse_w[2];
    float dx, dy, major;
    uint32_t steps;
    if (!vk_run_draw_vertex(record, pipeline, vertex_index0, &first) ||
        !vk_run_draw_vertex(record, pipeline, vertex_index1, &second))
        return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t plane = 0u; plane < 6u; ++plane) {
        const float first_distance = vk_clip_distance(&first, plane);
        const float second_distance = vk_clip_distance(&second, plane);
        if (!vk_clip_float_is_finite(first_distance) ||
            !vk_clip_float_is_finite(second_distance))
            return VK_ERROR_INITIALIZATION_FAILED;
        if (first_distance < 0.0f && second_distance < 0.0f) return VK_SUCCESS;
        if ((first_distance < 0.0f) != (second_distance < 0.0f)) {
            const float denominator = first_distance - second_distance;
            const float t = first_distance / denominator;
            vk_clip_vertex_t clipped;
            if (denominator == 0.0f || !vk_clip_float_is_finite(t) ||
                !vk_clip_vertex_lerp(&first, &second, t, &clipped))
                return VK_ERROR_INITIALIZATION_FAILED;
            if (first_distance < 0.0f) first = clipped;
            else second = clipped;
        }
    }
    for (uint32_t i = 0u; i < 2u; ++i) {
        const vk_clip_vertex_t *vertex = i == 0u ? &first : &second;
        const float x = vk_spv_word_float(vertex->position[0]);
        const float y = vk_spv_word_float(vertex->position[1]);
        const float z = vk_spv_word_float(vertex->position[2]);
        const float w = vk_spv_word_float(vertex->position[3]);
        if (!(w > 0.0f)) return VK_SUCCESS;
        inverse_w[i] = 1.0f / w;
        ndc_depth[i] = z / w;
        sx[i] = viewport->x + ((x / w) + 1.0f) * viewport->width * 0.5f;
        sx[i]=(float)(int32_t)(sx[i]*256.0f+.5f)/256.0f;
        sy[i] = viewport->y + ((y / w) + 1.0f) * viewport->height * 0.5f;
        sy[i]=(float)(int32_t)(sy[i]*256.0f+.5f)/256.0f;
        if (!vk_clip_float_is_finite(sx[i]) || !vk_clip_float_is_finite(sy[i]) ||
            !vk_clip_float_is_finite(ndc_depth[i]))
            return VK_ERROR_INITIALIZATION_FAILED;
    }
    dx = sx[1] - sx[0];
    dy = sy[1] - sy[0];
    major = dx < 0.0f ? -dx : dx;
    if ((dy < 0.0f ? -dy : dy) > major) major = dy < 0.0f ? -dy : dy;
    if (major > 4096.0f) return VK_ERROR_INITIALIZATION_FAILED;
    steps = (uint32_t)major + ((float)(uint32_t)major < major ? 1u : 0u);
    if (steps == 0u) steps = 1u;
    vk_draw_raster_bounds(record, image, &clip_x0, &clip_y0, &clip_x1, &clip_y1);
    if (clip_x0 >= clip_x1 || clip_y0 >= clip_y1) return VK_SUCCESS;
    if (pipeline->depth_test_enable != VK_FALSE) {
        vk_framebuffer_impl_t *framebuffer = record->args.draw.framebuffer;
        if (framebuffer == 0 || framebuffer->depth_attachment == 0 ||
            (depth_values = vk_view_depth_values(framebuffer->depth_attachment)) == 0)
            return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (uint32_t i = 0u; i < steps; ++i) {
        const float t = (float)i / (float)steps;
        const float sx_sample = sx[0] + dx * t;
        const float sy_sample = sy[0] + dy * t;
        /* Window coordinates are measured from the framebuffer's upper-left
           edge; pixel (x,y) occupies [x,x+1) x [y,y+1). Keep each line
           sample in that pixel instead of shifting samples left/up. */
        /* Viewports are validated non-negative and clipped coordinates stay
           within the viewport, so truncation is floor here without pulling
           libm into the userspace renderer. */
        const int32_t px = (int32_t)sx_sample;
        const int32_t py = (int32_t)sy_sample;
        const float q0 = (1.0f - t) * inverse_w[0];
        const float q1 = t * inverse_w[1];
        const float denominator = q0 + q1;
        uint32_t interpolated[VK_SPV_MAX_INTERFACE_LOCATIONS][4] = {{0u}};
        const float depth = viewport->minDepth +
            (ndc_depth[0] + (ndc_depth[1] - ndc_depth[0]) * t) *
            (viewport->maxDepth - viewport->minDepth);
        if (px < clip_x0 || px >= clip_x1 || py < clip_y0 || py >= clip_y1)
            continue;
        if (!vk_clip_float_is_finite(depth)) return VK_ERROR_INITIALIZATION_FAILED;
        if (denominator == 0.0f) continue;
        for (uint32_t location = 0u;
             location < VK_SPV_MAX_INTERFACE_LOCATIONS; ++location) {
            if ((pipeline->fragment_profile->fragment_varying_mask &
                 (1u << location)) == 0u) continue;
            for (uint32_t component = 0u; component < 4u; ++component) {
                const float a = vk_spv_word_float(first.varying[location][component]);
                const float b = vk_spv_word_float(second.varying[location][component]);
                interpolated[location][component] = vk_spv_float_word(
                    (q0 * a + q1 * b) / denominator);
            }
        }
        {
            float squared_length=dx*dx+dy*dy;
            float center_t=squared_length>0.0f?
                (((float)px+.5f-sx[0])*dx+((float)py+.5f-sy[0])*dy)/squared_length:0.0f;
            const float coord[4]={(float)px+.5f,(float)py+.5f,
                viewport->minDepth+(ndc_depth[0]+(ndc_depth[1]-ndc_depth[0])*center_t)*(viewport->maxDepth-viewport->minDepth),
                (1.0f-center_t)*inverse_w[0]+center_t*inverse_w[1]};
            VkResult result = vk_shade_fragment(record, pipeline, image, pixels,
                depth_values, px, py, depth, interpolated, 1,coord);
            if (result != VK_SUCCESS) return result;
        }
    }
    return VK_SUCCESS;
}

/* Everything a band of rows needs to rasterize one clipped triangle. */
typedef struct vk_raster_job {
    const vk_recorded_command_t *record;
    vk_pipeline_impl_t *pipeline;
    vk_image_plane_t *image;
    uint32_t *pixels;
    float *depth_values;
    const VkViewport *viewport;
    const vk_spv_profile_t *fragment_profile;
    const uint32_t (*varying)[VK_SPV_MAX_INTERFACE_LOCATIONS][4];
    float sx[3], sy[3], inverse_w[3], ndc_depth[3];
    float area, depth_bias;
    int32_t x0, x1;
} vk_raster_job_t;

/* Set by a frontend that can run bands concurrently (the hosted GL runtime). */
#define VK_RASTER_PARALLEL_MIN_PIXELS 16384
static VkResult (*vk_raster_parallel)(const vk_raster_job_t *job, int32_t y_begin, int32_t y_end);

/* Rasterize rows [y_begin, y_end). `workspace`/`quad_workspace` replace the
 * profile's shared interpreter workspaces for a concurrent band (0 = use them);
 * each band also works on its own copy of the pipeline, which carries the
 * per-quad result cache. */
static VkResult vk_raster_rows(const vk_raster_job_t *job, int32_t y_begin, int32_t y_end,
                               vk_spv_value_t *workspace, vk_spv_value_t *quad_workspace) {
    const vk_recorded_command_t *record = job->record;
    vk_pipeline_impl_t band_pipeline = *job->pipeline;
    vk_pipeline_impl_t *pipeline = &band_pipeline;
    vk_image_plane_t *image = job->image;
    uint32_t *pixels = job->pixels;
    float *depth_values = job->depth_values;
    const VkViewport *viewport = job->viewport;
    const vk_spv_profile_t *fragment_profile = job->fragment_profile;
    const uint32_t (*varying)[VK_SPV_MAX_INTERFACE_LOCATIONS][4] = job->varying;
    const float *sx = job->sx, *sy = job->sy, *inverse_w = job->inverse_w, *ndc_depth = job->ndc_depth;
    const float area = job->area, depth_bias = job->depth_bias;
    const int32_t x0 = job->x0, x1 = job->x1;

    pipeline->workspace_override = workspace;
    pipeline->quad_workspace_override = quad_workspace;
    pipeline->quad_cache_valid = 0u;
    for (int32_t py = y_begin; py < y_end; ++py) {
        for (int32_t px = x0; px < x1; ++px) {
            /* No sample shading is exposed. FragCoord is evaluated at the
               fragment/pixel center even when coverage/depth are per sample. */
            /* Quad programs get their four lanes' coordinates from the quad inputs below. */
            float coord[4]={0.0f,0.0f,0.0f,0.0f};
            if(!pipeline->gl_quad){
                float center_b[3]={vk_edge(sx[1],sy[1],sx[2],sy[2],(float)px+.5f,(float)py+.5f)/area,
                                   vk_edge(sx[2],sy[2],sx[0],sy[0],(float)px+.5f,(float)py+.5f)/area,
                                   vk_edge(sx[0],sy[0],sx[1],sy[1],(float)px+.5f,(float)py+.5f)/area};
                coord[0]=(float)px+.5f;coord[1]=(float)py+.5f;
                coord[2]=viewport->minDepth+(center_b[0]*ndc_depth[0]+center_b[1]*ndc_depth[1]+center_b[2]*ndc_depth[2])*(viewport->maxDepth-viewport->minDepth);
                coord[3]=center_b[0]*inverse_w[0]+center_b[1]*inverse_w[1]+center_b[2]*inverse_w[2];
            }
            for(uint32_t sample=0u;sample<image->samples;++sample) {
            float ox,oy;vk_sample_location(image->samples,sample,&ox,&oy);
            float cx = (float)px + ox;
            float cy = (float)py + oy;
            float e0 = vk_edge(sx[0], sy[0], sx[1], sy[1], cx, cy);
            float e1 = vk_edge(sx[1], sy[1], sx[2], sy[2], cx, cy);
            float e2 = vk_edge(sx[2], sy[2], sx[0], sy[0], cx, cy);
            if (vk_edge_covers_sample(e0, area, sx[0], sy[0], sx[1], sy[1]) &&
                vk_edge_covers_sample(e1, area, sx[1], sy[1], sx[2], sy[2]) &&
                vk_edge_covers_sample(e2, area, sx[2], sy[2], sx[0], sy[0])) {
                float fragment_depth = viewport->minDepth +
                    (e1 / area * ndc_depth[0] + e2 / area * ndc_depth[1] +
                     e0 / area * ndc_depth[2]) * (viewport->maxDepth - viewport->minDepth);
                if(pipeline->depth_bias_enable){fragment_depth+=depth_bias;if(fragment_depth<0.0f)fragment_depth=0.0f;if(fragment_depth>1.0f)fragment_depth=1.0f;}
                uint32_t interpolated[VK_SPV_MAX_INTERFACE_LOCATIONS][4];
                if (!vk_clip_float_is_finite(fragment_depth))
                    return VK_ERROR_INITIALIZATION_FAILED;
                if (fragment_profile->fragment_varying_mask != 0u) {
                    /* Opposite edges correspond to vertices 0,1,2 as e1,e2,e0.
                       The old cyclic assignment interpolated the wrong vertex. */
                    float q[3] = {e1 / area * inverse_w[0],
                                  e2 / area * inverse_w[1],
                                  e0 / area * inverse_w[2]};
                    float denominator = q[0] + q[1] + q[2];
                    if (denominator == 0.0f) continue;
                    /* A quad program reads its varyings from the quad inputs, and
                       only when the quad is not already cached. */
                    if (!pipeline->gl_quad) memset(interpolated, 0, sizeof(interpolated));
                    for (uint32_t location = 0u;
                         location < VK_SPV_MAX_INTERFACE_LOCATIONS && !pipeline->gl_quad; ++location) {
                        if ((fragment_profile->fragment_varying_mask &
                             (1u << location)) == 0u) continue;
                        for (uint32_t c = 0u; c < 4u; ++c) {
                            float value = q[0] * vk_spv_word_float(varying[0][location][c]) +
                                          q[1] * vk_spv_word_float(varying[1][location][c]) +
                                          q[2] * vk_spv_word_float(varying[2][location][c]);
                            interpolated[location][c] =
                                vk_spv_float_word(value / denominator);
                        }
                    }
                }
                {
                    pipeline->quad_inputs=NULL;pipeline->quad_coords=NULL;
                    const int quad_cache_hit=pipeline->gl_quad&&pipeline->quad_cache_valid&&
                        pipeline->quad_cache_x==(px&~1)&&pipeline->quad_cache_y==(py&~1);
                    /* The shader reads these through the pipeline after the block below
                       ends, so they must outlive it (they used to be declared inside it:
                       a dangling pointer that made textured draws fail at random). */
                    uint32_t quad_inputs[4][VK_SPV_MAX_INTERFACE_LOCATIONS][4];
                    float quad_coords[4][4];
                    if(pipeline->gl_quad&&!quad_cache_hit){
                        memset(quad_inputs,0,sizeof(quad_inputs));
                        for(unsigned lane=0;lane<4;++lane){
                            float qx=(float)(px&~1)+(lane&1u)+.5f,qy=(float)(py&~1)+(lane>>1u)+.5f;
                            float b[3]={vk_edge(sx[1],sy[1],sx[2],sy[2],qx,qy)/area,
                                        vk_edge(sx[2],sy[2],sx[0],sy[0],qx,qy)/area,
                                        vk_edge(sx[0],sy[0],sx[1],sy[1],qx,qy)/area};
                            float q[3]={b[0]*inverse_w[0],b[1]*inverse_w[1],b[2]*inverse_w[2]},denominator=q[0]+q[1]+q[2];
                            if(denominator==0.f)return VK_ERROR_INITIALIZATION_FAILED;
                            quad_coords[lane][0]=qx;quad_coords[lane][1]=qy;
                            quad_coords[lane][2]=viewport->minDepth+(b[0]*ndc_depth[0]+b[1]*ndc_depth[1]+b[2]*ndc_depth[2])*(viewport->maxDepth-viewport->minDepth);
                            quad_coords[lane][3]=denominator;
                            for(unsigned location=0;location<VK_SPV_MAX_INTERFACE_LOCATIONS;++location)if(fragment_profile->fragment_varying_mask&(1u<<location))
                                for(unsigned component=0;component<4;++component){
                                    float value=(q[0]*vk_spv_word_float(varying[0][location][component])+q[1]*vk_spv_word_float(varying[1][location][component])+q[2]*vk_spv_word_float(varying[2][location][component]))/denominator;
                                    quad_inputs[lane][location][component]=vk_spv_float_word(value);
                                }
                        }
                        pipeline->quad_inputs=quad_inputs;pipeline->quad_coords=quad_coords;
                    }
                    VkResult result = vk_shade_sample(record, pipeline, image,
                        pixels, depth_values, px, py, fragment_depth,coord,
                        fragment_profile->fragment_varying_mask != 0u
                            ? interpolated : 0u,
                        pipeline->front_face == VK_FRONT_FACE_COUNTER_CLOCKWISE ? area > 0.0f : area < 0.0f, sample);
                    pipeline->quad_inputs=NULL;pipeline->quad_coords=NULL;
                    if (result != VK_SUCCESS) return result;
                }
            }
        }
    }
    }
    return VK_SUCCESS;
}

static VkResult vk_rasterize_clip_triangle(const vk_recorded_command_t *record,
                                           vk_pipeline_impl_t *pipeline,
                                           vk_image_plane_t *image, uint32_t *pixels,
                                           const uint32_t pos[3][4],
                                           const uint32_t varying[3][VK_SPV_MAX_INTERFACE_LOCATIONS][4]) {
    const vk_spv_profile_t *fragment_profile = pipeline->fragment_profile;
    const VkViewport *viewport = &record->args.draw.viewport;
    const VkRect2D *scissor = &record->args.draw.scissor;
    float sx[3], sy[3], area;
    float inverse_w[3];
    float ndc_depth[3];
    float *depth_values = 0;
    float min_x, max_x, min_y, max_y;
    int32_t x0, x1, y0, y1;
    int32_t clip_x0, clip_y0, clip_x1, clip_y1;
    if (fragment_profile == 0 || fragment_profile->stage != VK_SHADER_STAGE_FRAGMENT_BIT)
        return VK_ERROR_INITIALIZATION_FAILED;
    pipeline->quad_cache_valid=0u;
    if (pipeline->depth_test_enable != VK_FALSE) {
        vk_framebuffer_impl_t *framebuffer = record->args.draw.framebuffer;
        if (framebuffer == 0 || framebuffer->depth_attachment == 0 ||
            (depth_values = vk_view_depth_values(framebuffer->depth_attachment)) == 0)
            return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (uint32_t i = 0u; i < 3u; ++i) {
        float x = vk_spv_word_float(pos[i][0]);
        float y = vk_spv_word_float(pos[i][1]);
        float z = vk_spv_word_float(pos[i][2]);
        float w = vk_spv_word_float(pos[i][3]);
        if (!(w > 0.0f)) return VK_SUCCESS;
        inverse_w[i] = 1.0f / w;
        ndc_depth[i] = z / w;
        sx[i] = viewport->x + ((x / w) + 1.0f) * viewport->width * 0.5f;
        sx[i]=(float)(int32_t)(sx[i]*256.0f+.5f)/256.0f;
        sy[i] = viewport->y + ((y / w) + 1.0f) * viewport->height * 0.5f;
        sy[i]=(float)(int32_t)(sy[i]*256.0f+.5f)/256.0f;
    }
    area = vk_edge(sx[0], sy[0], sx[1], sy[1], sx[2], sy[2]);
    if (area == 0.0f) return VK_SUCCESS;
    if (pipeline->cull_mode != VK_CULL_MODE_NONE) {
        const int front_facing = pipeline->front_face == VK_FRONT_FACE_COUNTER_CLOCKWISE
            ? area > 0.0f : area < 0.0f;
        if (((pipeline->cull_mode & VK_CULL_MODE_FRONT_BIT) != 0u && front_facing) ||
            ((pipeline->cull_mode & VK_CULL_MODE_BACK_BIT) != 0u && !front_facing))
            return VK_SUCCESS;
    }
    float depth_bias=0.0f;
    if(pipeline->depth_bias_enable){
        float z[3],largest=0.0f;
        for(uint32_t i=0u;i<3u;++i){z[i]=viewport->minDepth+ndc_depth[i]*(viewport->maxDepth-viewport->minDepth);if(z[i]>largest)largest=z[i];}
        float dx=((z[1]-z[0])*(sy[2]-sy[0])-(z[2]-z[0])*(sy[1]-sy[0]))/area;
        float dy=((sx[1]-sx[0])*(z[2]-z[0])-(sx[2]-sx[0])*(z[1]-z[0]))/area;
        if(dx<0.0f)dx=-dx;if(dy<0.0f)dy=-dy;
        uint32_t bits;memcpy(&bits,&largest,4u);uint32_t exponent=(bits>>23u)&255u;
        uint32_t rbits=exponent>23u?(exponent-23u)<<23u:1u<<(exponent?exponent-1u:0u);float r;memcpy(&r,&rbits,4u);
        depth_bias=record->args.draw.state.depth_bias_constant*r+record->args.draw.state.depth_bias_slope*(dx>dy?dx:dy);
    }
    min_x = sx[0]; max_x = sx[0]; min_y = sy[0]; max_y = sy[0];
    for (uint32_t i = 1u; i < 3u; ++i) {
        if (sx[i] < min_x) min_x = sx[i];
        if (sx[i] > max_x) max_x = sx[i];
        if (sy[i] < min_y) min_y = sy[i];
        if (sy[i] > max_y) max_y = sy[i];
    }
    x0 = (int32_t)min_x; y0 = (int32_t)min_y;
    x1 = (int32_t)max_x + ((float)(int32_t)max_x < max_x ? 1 : 0);
    y1 = (int32_t)max_y + ((float)(int32_t)max_y < max_y ? 1 : 0);
    clip_x0 = record->args.draw.render_area.offset.x;
    clip_y0 = record->args.draw.render_area.offset.y;
    clip_x1 = clip_x0 + (int32_t)record->args.draw.render_area.extent.width;
    clip_y1 = clip_y0 + (int32_t)record->args.draw.render_area.extent.height;
    if (scissor->offset.x > clip_x0) clip_x0 = scissor->offset.x;
    if (scissor->offset.y > clip_y0) clip_y0 = scissor->offset.y;
    if (scissor->offset.x + (int64_t)scissor->extent.width < clip_x1)
        clip_x1 = scissor->offset.x + (int32_t)scissor->extent.width;
    if (scissor->offset.y + (int64_t)scissor->extent.height < clip_y1)
        clip_y1 = scissor->offset.y + (int32_t)scissor->extent.height;
    if (clip_x1 > (int32_t)image->width) clip_x1 = (int32_t)image->width;
    if (clip_y1 > (int32_t)image->height) clip_y1 = (int32_t)image->height;
    if (x0 < clip_x0) x0 = clip_x0;
    if (y0 < clip_y0) y0 = clip_y0;
    if (x1 > clip_x1) x1 = clip_x1;
    if (y1 > clip_y1) y1 = clip_y1;
    if (x0 >= x1 || y0 >= y1) return VK_SUCCESS;
    {
        vk_raster_job_t job;
        job.record = record; job.pipeline = pipeline; job.image = image; job.pixels = pixels;
        job.depth_values = depth_values; job.viewport = viewport; job.fragment_profile = fragment_profile;
        job.varying = varying; job.area = area; job.depth_bias = depth_bias;
        memcpy(job.sx, sx, sizeof(sx)); memcpy(job.sy, sy, sizeof(sy));
        memcpy(job.inverse_w, inverse_w, sizeof(inverse_w)); memcpy(job.ndc_depth, ndc_depth, sizeof(ndc_depth));
        job.x0 = x0; job.x1 = x1;
        /* Big triangles are split into bands of rows shaded on several CPUs. */
        if (vk_raster_parallel != 0 && image->samples == 1u &&
            (int64_t)(x1 - x0) * (int64_t)(y1 - y0) >= VK_RASTER_PARALLEL_MIN_PIXELS && y1 - y0 >= 8)
            return vk_raster_parallel(&job, y0, y1);
        return vk_raster_rows(&job, y0, y1, 0, 0);
    }

    return VK_SUCCESS;
}

static VkResult vk_rasterize_triangle(const vk_recorded_command_t *record,
                                      vk_pipeline_impl_t *pipeline,
                                      vk_image_plane_t *image, uint32_t *pixels,
                                      const uint32_t vertex_indices[3]) {
    enum { VK_CLIP_POLYGON_CAPACITY = 12u };
    const vk_spv_profile_t *vertex_profile = pipeline->vertex_profile;
    vk_clip_vertex_t first[VK_CLIP_POLYGON_CAPACITY];
    vk_clip_vertex_t second[VK_CLIP_POLYGON_CAPACITY];
    vk_clip_vertex_t *input = first;
    vk_clip_vertex_t *output = second;
    uint32_t count = 3u;
    if (vertex_profile == 0 || vertex_profile->stage != VK_SHADER_STAGE_VERTEX_BIT)
        return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0u; i < 3u; ++i) {
        uint32_t attributes[VK_SPV_MAX_INTERFACE_LOCATIONS][4] = {{0u}};
        const uint32_t (*attribute_words)[4] = 0;
        memset(&first[i], 0, sizeof(first[i]));
        if (vertex_profile->attribute_mask != 0u) {
            for (uint32_t location = 0u;
                 location < VK_SPV_MAX_INTERFACE_LOCATIONS; ++location) {
                if ((vertex_profile->attribute_mask & (1u << location)) != 0u &&
                    !vk_read_vertex_attribute(record, vertex_indices[i], location,
                                              attributes[location]))
                    return VK_ERROR_INITIALIZATION_FAILED;
            }
            attribute_words = attributes;
        }
        if (!vk_run_draw_vertex(record, pipeline, vertex_indices[i], &first[i]))
            return VK_ERROR_INITIALIZATION_FAILED;
    }

    /* Clip in homogeneous coordinates before perspective division.  This
       preserves partially visible primitives and interpolates varyings at
       the generated clip vertices, rather than dropping the whole triangle
       when only one input vertex lies outside the frustum. */
    for (uint32_t plane = 0u; plane < 6u && count >= 3u; ++plane) {
        uint32_t output_count = 0u;
        vk_clip_vertex_t previous = input[count - 1u];
        float previous_distance = vk_clip_distance(&previous, plane);
        if (!vk_clip_float_is_finite(previous_distance))
            return VK_ERROR_INITIALIZATION_FAILED;
        for (uint32_t i = 0u; i < count; ++i) {
            const vk_clip_vertex_t *current = &input[i];
            float current_distance = vk_clip_distance(current, plane);
            int previous_inside, current_inside;
            if (!vk_clip_float_is_finite(current_distance))
                return VK_ERROR_INITIALIZATION_FAILED;
            previous_inside = previous_distance >= 0.0f;
            current_inside = current_distance >= 0.0f;
            if (previous_inside != current_inside) {
                float denominator = previous_distance - current_distance;
                float t;
                if (denominator == 0.0f || !vk_clip_float_is_finite(denominator) ||
                    output_count >= VK_CLIP_POLYGON_CAPACITY)
                    return VK_ERROR_INITIALIZATION_FAILED;
                t = previous_distance / denominator;
                if (!vk_clip_float_is_finite(t)) return VK_ERROR_INITIALIZATION_FAILED;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                if (!vk_clip_vertex_lerp(&previous, current, t,
                                         &output[output_count++]))
                    return VK_ERROR_INITIALIZATION_FAILED;
            }
            if (current_inside) {
                if (output_count >= VK_CLIP_POLYGON_CAPACITY)
                    return VK_ERROR_INITIALIZATION_FAILED;
                output[output_count++] = *current;
            }
            previous = *current;
            previous_distance = current_distance;
        }
        count = output_count;
        {
            vk_clip_vertex_t *temporary = input;
            input = output;
            output = temporary;
        }
    }
    if (count < 3u) return VK_SUCCESS;
    for (uint32_t i = 1u; i + 1u < count; ++i) {
        uint32_t triangle_position[3][4];
        uint32_t triangle_varying[3][VK_SPV_MAX_INTERFACE_LOCATIONS][4];
        const vk_clip_vertex_t *vertices[3] = {&input[0], &input[i], &input[i + 1u]};
        for (uint32_t v = 0u; v < 3u; ++v) {
            memcpy(triangle_position[v], vertices[v]->position, sizeof(triangle_position[v]));
            memcpy(triangle_varying[v], vertices[v]->varying,
                   sizeof(triangle_varying[v]));
        }
        {
            VkResult result = vk_rasterize_clip_triangle(record, pipeline, image, pixels,
                triangle_position, triangle_varying);
            if (result != VK_SUCCESS) return result;
        }
    }
    return VK_SUCCESS;
}

static VkResult vk_execute_draw(const vk_recorded_command_t *record) {
    vk_pipeline_impl_t *pipeline = record->args.draw.pipeline;
    vk_framebuffer_impl_t *framebuffer = record->args.draw.framebuffer;
    vk_render_pass_impl_t *render_pass;
    vk_image_plane_t plane;
    vk_image_plane_t *image = &plane;
    uint32_t *pixels;
    if (pipeline == 0 || framebuffer == 0 ||
        (render_pass = framebuffer->render_pass) == 0 ||
        pipeline->render_pass != render_pass || framebuffer->attachment0 == 0 ||
        !vk_view_plane(framebuffer->attachment0, image) ||
        image->layout != render_pass->subpass_layout ||
        (pixels = image->pixels) == 0 ||
        (record->args.draw.instance_count && record->args.draw.first_instance>UINT32_MAX-(record->args.draw.instance_count-1u)))
        return VK_ERROR_DEVICE_LOST;
    if(!record->args.draw.instance_count||!record->args.draw.vertex_count)return VK_SUCCESS;
    if(record->args.draw.instance_count>1u){
        /* Invocation state is worker-local; the recorded command is immutable.
           All resources were retained and validated for the complete range. */
        vk_recorded_command_t invocation=*record;invocation.args.draw.instance_count=1u;
        for(uint32_t i=0u;i<record->args.draw.instance_count;++i){
            invocation.args.draw.first_instance=record->args.draw.first_instance+i;
            VkResult result=vk_execute_draw(&invocation);if(result!=VK_SUCCESS)return result;
        }
        return VK_SUCCESS;
    }
    if (pipeline->depth_test_enable != VK_FALSE) {
        vk_image_plane_t depth_plane;
        if (!render_pass->has_depth ||
            !vk_view_plane(framebuffer->depth_attachment, &depth_plane) ||
            depth_plane.layout != render_pass->depth_subpass_layout)
            return VK_ERROR_DEVICE_LOST;
    }
    if (!vk_validate_draw_vertex_buffer(record))
        return VK_ERROR_INITIALIZATION_FAILED;
    if (pipeline->topology == VK_PRIMITIVE_TOPOLOGY_POINT_LIST) {
        for (uint32_t primitive = 0u;
             primitive < record->args.draw.vertex_count; ++primitive) {
            uint32_t vertex_index;
            VkResult result;
            if (!vk_draw_vertex_index(record, primitive, &vertex_index))
                return VK_ERROR_INITIALIZATION_FAILED;
            result = vk_rasterize_point(record, pipeline, image, pixels,
                                        vertex_index);
            if (result != VK_SUCCESS) return result;
        }
        return VK_SUCCESS;
    }
    if (pipeline->topology == VK_PRIMITIVE_TOPOLOGY_LINE_LIST ||
        pipeline->topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP) {
        const uint32_t step = pipeline->topology == VK_PRIMITIVE_TOPOLOGY_LINE_LIST
            ? 2u : 1u;
        for (uint32_t primitive = 0u;
             record->args.draw.vertex_count - primitive >= 2u;
             primitive += step) {
            uint32_t vertex_indices[2];
            VkResult result;
            if (!vk_draw_vertex_index(record, primitive, &vertex_indices[0]) ||
                !vk_draw_vertex_index(record, primitive + 1u, &vertex_indices[1]))
                return VK_ERROR_INITIALIZATION_FAILED;
            result = vk_rasterize_line(record, pipeline, image, pixels,
                vertex_indices[0], vertex_indices[1]);
            if (result != VK_SUCCESS) return result;
        }
        return VK_SUCCESS;
    }
    for (uint32_t primitive = 0u;
         record->args.draw.vertex_count - primitive >= 3u;
         primitive += pipeline->topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST ? 3u : 1u) {
        uint32_t vertex_indices[3];
        uint32_t ordinal0 = primitive;
        uint32_t ordinal1 = primitive + 1u;
        uint32_t ordinal2 = primitive + 2u;
        if (pipeline->topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN) {
            ordinal0 = 0u;
            ordinal1 = primitive + 1u;
            ordinal2 = primitive + 2u;
        } else if (pipeline->topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP &&
                   (primitive & 1u) != 0u) {
            /* The odd strip triangles reverse the first two indices to keep
               their winding consistent with the preceding triangle. */
            ordinal0 = primitive + 1u;
            ordinal1 = primitive;
            ordinal2 = primitive + 2u;
        }
        if (!vk_draw_vertex_index(record, ordinal0, &vertex_indices[0]) ||
            !vk_draw_vertex_index(record, ordinal1, &vertex_indices[1]) ||
            !vk_draw_vertex_index(record, ordinal2, &vertex_indices[2]))
            return VK_ERROR_INITIALIZATION_FAILED;
        VkResult result = vk_rasterize_triangle(record, pipeline, image, pixels,
            vertex_indices);
        if (result != VK_SUCCESS) return result;
    }
    return VK_SUCCESS;
}

static int vk_spv_failure(vk_spv_invocation_t *inv,uint32_t line){
    if(!inv){
        static unsigned reports;
        if(__atomic_fetch_add(&reports,1u,__ATOMIC_RELAXED)<16u){
            char message[]="graphics SPIR-V failure line=00000000\n";
            const char *digits="0123456789abcdef";
            for(unsigned byte=0;byte<8;++byte)message[sizeof("graphics SPIR-V failure line=")-1u+byte]=digits[(line>>((7u-byte)*4u))&15u];
            VEX_RENDER_DIAGNOSTIC(message);
        }
    }
    if(inv){uint32_t numbers[3]={line,inv->fault_pc,inv->fault_opcode};char msg[]="compute SPIR-V failure line/word/op=00000000/00000000/00000000\n";
        const char *digits="0123456789abcdef";for(uint32_t n=0;n<3;++n)for(uint32_t b=0;b<8;++b)msg[sizeof("compute SPIR-V failure line/word/op=")-1u+9u*n+b]=digits[(numbers[n]>>((7u-b)*4u))&15u];
        VEX_RENDER_DIAGNOSTIC(msg);
    }return 0;
}

static vk_descriptor_value_impl_t *vk_spv_descriptor_value(
        vk_descriptor_set_impl_t *const sets[8],uint32_t set,uint32_t binding,uint32_t element){
    if(!sets||set>=8u||!sets[set]||!sets[set]->layout)return 0;
    vk_descriptor_set_impl_t *s=sets[set];
    for(uint32_t i=0u;i<s->layout->binding_count;++i){
        const vk_descriptor_layout_binding_impl_t *b=&s->layout->bindings[i];
        if(b->binding.binding==binding&&element<b->binding.descriptorCount&&
           b->value_offset+element<s->value_count){
            vk_descriptor_value_impl_t *v=&s->values[b->value_offset+element];
            return v->initialized?v:0;
        }
    }
    return 0;
}

static int vk_spv_resources_supported(const vk_spv_profile_t *p,
        vk_descriptor_set_impl_t *const sets[8]){
    if(!p)return 0;
    for(uint32_t id=1u;id<p->bound;++id)if(p->descriptor_type_by_id[id]){
        for(uint32_t e=0u;e<p->descriptor_elements[id];++e){
            vk_descriptor_value_impl_t *v=vk_spv_descriptor_value(sets,p->descriptor_set[id],p->descriptor_binding[id],e);
            if(!v||v->type!=p->descriptor_type_by_id[id])return 0;
            if(v->type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER||v->type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER){
                vk_buffer_impl_t *b=v->resource.buffer.buffer;
                if(!b||!b->memory||!b->memory->bytes||v->resource.buffer.range<p->descriptor_required_size[id])return 0;
            }else if(v->type==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER){
                if(!v->resource.image.view||!v->resource.image.sampler)return 0;
            }else if(v->type==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER||v->type==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER){
                vk_buffer_view_impl_t *view=v->resource.texel;uint32_t scalar_kind;
                if(!view||!view->buffer->memory||!vk_texel_format(view->format,&scalar_kind,0))return 0;
                uint32_t image=p->element[p->type_of[id]];
                if(p->kind[image]==VK_SPV_KIND_ARRAY)image=p->element[image];
                if(p->kind[image]==VK_SPV_KIND_SAMPLED_IMAGE)image=p->element[image];
                if(image>=p->bound||p->kind[image]!=VK_SPV_KIND_IMAGE||p->image_dim[image]!=5u)return 0;
                uint32_t scalar=p->element[image];
                if(scalar>=p->bound||(scalar_kind==1u?p->kind[scalar]!=VK_SPV_KIND_FLOAT:
                   (p->kind[scalar]!=VK_SPV_KIND_INT||p->signedness[scalar]!=(scalar_kind==3u))))return 0;
            }else if(v->type==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE){
                if(!v->resource.image.view||v->resource.image.layout!=VK_IMAGE_LAYOUT_GENERAL)return 0;
            }else return 0;
        }
    }
    return 1;
}

static int vk_spv_pointer_address(const vk_spv_profile_t *p,vk_spv_value_t *values,
        uint32_t pointer,vk_descriptor_set_impl_t *const sets[8],vk_spv_invocation_t *inv,
        uint32_t bytes,int write,uint8_t **out){
    if(pointer>=p->bound||!bytes||!values[pointer].initialized)return 0;
    const vk_spv_value_t *v=&values[pointer];
    if(v->access&(write?2u:1u))return 0;
    uint64_t offset=v->descriptor_byte_offset;
    if(v->local_pointer){
        uint32_t base=v->local_base,off=v->local_offset;
        if(base>=p->bound||(bytes&3u)||!bytes||off>16u||bytes/4u>16u-off)return 0;
        uint32_t mask=((1u<<(bytes/4u))-1u)<<off;
        if(!write&&(values[base].local_written&mask)!=mask)return 0;
        if(write)values[base].local_written|=(uint16_t)mask;
        *out=(uint8_t *)(values[base].word+off);return 1;
    }
    if(v->descriptor_pointer==1u){
        vk_descriptor_value_impl_t *d=vk_spv_descriptor_value(sets,v->descriptor_set,v->descriptor_binding,v->descriptor_index);
        if(!d||(d->type!=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER&&d->type!=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)||
           (write&&d->type!=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER))return 0;
        vk_buffer_impl_t *b=d->resource.buffer.buffer;
        if(!b||!b->memory||!b->memory->bytes||offset>d->resource.buffer.range||bytes>d->resource.buffer.range-offset||
           d->resource.buffer.offset>b->size||d->resource.buffer.range>b->size-d->resource.buffer.offset||
           b->memory_offset>b->memory->size||b->size>b->memory->size-b->memory_offset)return 0;
        *out=b->memory->bytes+(size_t)(b->memory_offset+d->resource.buffer.offset+offset);
        return 1;
    }
    if(!v->memory_pointer||v->type>=p->bound||p->kind[v->type]!=VK_SPV_KIND_POINTER)return 0;
    uint32_t storage=p->storage[v->type],base=v->memory_base;
    if(base>=p->bound||!p->variable[base]||p->storage[base]!=storage)return 0;
    if(storage==4u){
        uint32_t start=p->workgroup_offset[base],size=vk_spv_shared_bytes(p,p->element[p->type_of[base]],0u);
        if(!inv||!inv->shared||start==UINT32_MAX||offset<start||offset-start>size||bytes>size-(offset-start))return 0;
        *out=inv->shared+(size_t)offset;return 1;
    }
    if(storage!=1u&&storage!=3u&&storage!=6u&&!(storage==0u&&p->gl_uniform[base]))return 0;
    if(write&&(storage==1u||storage==0u))return 0;
    uint32_t size=vk_spv_value_words(p,p->element[p->type_of[base]],0u)*4u;
    if(!size||offset>size||bytes>size-offset)return 0;
    *out=(uint8_t *)values[base].word+(size_t)offset;return 1;
}

static int vk_spv_memory_chain(const vk_spv_profile_t *p,vk_spv_value_t *v,uint32_t base,
        uint32_t result_type,uint32_t result,const uint32_t *indices,uint32_t count){
    uint32_t type=p->element[v[base].type],offset=v[base].descriptor_byte_offset;
    for(uint32_t i=0u;i<count;++i){
        uint32_t id=indices[i],index,child,delta=0u;
        if(id>=p->bound||!v[id].initialized||v[id].type>=p->bound||p->kind[v[id].type]!=VK_SPV_KIND_INT)return 0;
        index=v[id].word[0];
        if(type>=p->bound||index>=p->count[type])return 0;
        if(p->kind[type]==VK_SPV_KIND_STRUCT){
            if(!p->constant_integer[id]||index>=VK_SPV_MAX_BLOCK_MEMBERS)return 0;
            for(uint32_t m=0u;m<index;++m){uint32_t n=vk_spv_shared_bytes(p,p->member_type[type][m],0u);if(!n||n>UINT32_MAX-delta)return 0;delta+=n;}
            child=p->member_type[type][index];
        }else if(p->kind[type]==VK_SPV_KIND_ARRAY||p->kind[type]==VK_SPV_KIND_VECTOR||p->kind[type]==VK_SPV_KIND_MATRIX){
            child=p->element[type];uint32_t n=vk_spv_shared_bytes(p,child,0u);
            if(!n||index>UINT32_MAX/n)return 0;delta=index*n;
        }else return 0;
        if(delta>UINT32_MAX-offset)return 0;offset+=delta;type=child;
    }
    if(p->element[result_type]!=type||p->storage[result_type]!=p->storage[v[base].type])return 0;
    v[result]=v[base];v[result].type=result_type;v[result].descriptor_byte_offset=offset;return 1;
}

static int vk_spv_memory_semantics(uint32_t scope,uint32_t semantics){
    uint32_t order=semantics&0x1eu;
    if(scope!=1u&&scope!=2u&&scope!=4u)return 0;
    if(order!=0u&&order!=2u&&order!=4u&&order!=8u&&order!=16u)return 0;
    return (semantics&~(0x1eu|0x40u|0x100u|0x800u))==0u;
}

static int vk_spv_barrier_valid(const vk_spv_profile_t *p,const uint32_t *w,int control){
    uint32_t first=control?2u:1u;
    if(control&&(p->stage!=VK_SHADER_STAGE_COMPUTE_BIT||w[1]>=p->bound||
                 !p->constant_integer[w[1]]||p->constant_value[w[1]]!=2u))return 0;
    if(w[first]>=p->bound||w[first+1u]>=p->bound||!p->constant_integer[w[first]]||!p->constant_integer[w[first+1u]])return 0;
    uint32_t scope=p->constant_value[w[first]],sem=p->constant_value[w[first+1u]];
    return vk_spv_memory_semantics(scope,sem)&&(!control||(sem&0x1eu)==0u||(sem&0x1eu)==8u||(sem&0x1eu)==16u);
}

static int vk_spv_atomic(const vk_spv_profile_t *p,vk_spv_value_t *v,
        vk_descriptor_set_impl_t *const sets[8],vk_spv_invocation_t *inv,const uint32_t *w,uint32_t op){
    uint32_t pointer=w[op==228u?1u:3u],scope_id=w[op==228u?2u:4u],sem_id=w[op==228u?3u:5u];
    if(pointer>=p->bound||v[pointer].type>=p->bound||scope_id>=p->bound||sem_id>=p->bound||
       !p->constant_integer[scope_id]||!p->constant_integer[sem_id]||
       !vk_spv_memory_semantics(p->constant_value[scope_id],p->constant_value[sem_id]))return 0;
    uint32_t type=p->element[v[pointer].type],storage=p->storage[v[pointer].type];
    if(p->kind[v[pointer].type]!=VK_SPV_KIND_POINTER||type>=p->bound||p->kind[type]!=VK_SPV_KIND_INT||
       p->width[type]!=32u||(storage!=2u&&storage!=4u))return 0;
    uint8_t *address;
    if(!vk_spv_pointer_address(p,v,pointer,sets,inv,4u,op!=227u,&address)||((uintptr_t)address&3u))return 0;
    if(op!=228u&&(w[1]!=type||w[2]>=p->bound))return 0;
    uint32_t *cell=(uint32_t *)(void *)address,old,value=0u;
    uint32_t operand=op==228u?w[4]:(op>=229u&&op!=232u&&op!=233u?w[op==230u||op==231u?7u:6u]:UINT32_MAX);
    if(operand!=UINT32_MAX){if(operand>=p->bound||!v[operand].initialized||v[operand].type!=type)return 0;value=v[operand].word[0];}
    uint32_t order=p->constant_value[sem_id]&0x1eu;
    if((op==227u&&(order==4u||order==8u))||(op==228u&&(order==2u||order==8u)))return 0;
    if(op==227u)old=__atomic_load_n(cell,__ATOMIC_SEQ_CST);
    else if(op==228u){__atomic_store_n(cell,value,__ATOMIC_SEQ_CST);return 1;}
    else if(op==229u)old=__atomic_exchange_n(cell,value,__ATOMIC_SEQ_CST);
    else if(op==230u||op==231u){
        uint32_t unequal=w[6],compare=w[8];
        if(unequal>=p->bound||!p->constant_integer[unequal]||compare>=p->bound||!v[compare].initialized||v[compare].type!=type||
           !vk_spv_memory_semantics(p->constant_value[scope_id],p->constant_value[unequal])||
           (p->constant_value[unequal]&0x1eu)==4u||(p->constant_value[unequal]&0x1eu)==8u)return 0;
        old=v[compare].word[0];(void)__atomic_compare_exchange_n(cell,&old,value,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
    }else if(op==232u)old=__atomic_fetch_add(cell,1u,__ATOMIC_SEQ_CST);
    else if(op==233u)old=__atomic_fetch_sub(cell,1u,__ATOMIC_SEQ_CST);
    else if(op==234u)old=__atomic_fetch_add(cell,value,__ATOMIC_SEQ_CST);
    else if(op==235u)old=__atomic_fetch_sub(cell,value,__ATOMIC_SEQ_CST);
    else if(op==240u)old=__atomic_fetch_and(cell,value,__ATOMIC_SEQ_CST);
    else if(op==241u)old=__atomic_fetch_or(cell,value,__ATOMIC_SEQ_CST);
    else if(op==242u)old=__atomic_fetch_xor(cell,value,__ATOMIC_SEQ_CST);
    else if(op>=236u&&op<=239u){
        old=__atomic_load_n(cell,__ATOMIC_SEQ_CST);
        for(;;){uint32_t desired=old;
            int less=op==236u||op==238u?(int32_t)value<(int32_t)old:value<old;
            if((op<=237u&&less)||(op>=238u&&!less))desired=value;
            uint32_t expected=old;if(__atomic_compare_exchange_n(cell,&expected,desired,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST))break;old=expected;
        }
    }else return 0;
    v[w[2]].type=type;v[w[2]].initialized=1u;v[w[2]].word[0]=old;return 1;
}

static int vk_spv_texel(const vk_spv_profile_t *p,vk_spv_value_t *v,
        vk_descriptor_set_impl_t *const sets[8],const uint32_t *w,uint32_t op){
    uint32_t image=op==99u?w[1]:w[3],coord=op==99u?w[2]:w[4];
    if(image>=p->bound||coord>=p->bound||!v[image].initialized||!v[coord].initialized||
       v[image].descriptor_pointer!=2u||(v[image].access&(op==99u?2u:1u)))return 0;
    uint32_t t=v[image].type,ct=v[coord].type;
    if(t>=p->bound||p->kind[t]!=VK_SPV_KIND_IMAGE||p->image_dim[t]!=5u||
       ct>=p->bound||p->kind[ct]!=VK_SPV_KIND_INT||p->width[ct]!=32u||
       (op==95u?p->image_sampled[t]!=1u:p->image_sampled[t]!=2u))return 0;
    vk_descriptor_value_impl_t *d=vk_spv_descriptor_value(sets,v[image].descriptor_set,v[image].descriptor_binding,v[image].descriptor_index);
    if(!d||d->type!=(op==95u?VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER))return 0;
    vk_buffer_view_impl_t *view=d->resource.texel;uint32_t kind,components;
    if(!view||!vk_texel_format(view->format,&kind,&components)||v[coord].word[0]>=view->range/4u)return 0;
    uint32_t scalar=p->element[t];
    if(scalar>=p->bound||p->width[scalar]!=32u||
       (kind==1u?p->kind[scalar]!=VK_SPV_KIND_FLOAT:
         (p->kind[scalar]!=VK_SPV_KIND_INT||p->signedness[scalar]!=(kind==3u))))return 0;
    if(op!=95u&&p->image_format[t]!=(kind==1u?3u:kind==2u?33u:24u))return 0;
    vk_buffer_impl_t *b=view->buffer;
    if(!b||!b->memory||!b->memory->bytes||b->memory_offset>b->memory->size||
       b->size>b->memory->size-b->memory_offset||view->offset>b->size||view->range>b->size-view->offset)return 0;
    uint32_t *cell=(void *)(b->memory->bytes+b->memory_offset+view->offset+(uint64_t)v[coord].word[0]*4u);
    uint32_t rt=op==99u?(w[3]<p->bound?v[w[3]].type:0u):w[1],id=op==99u?w[3]:w[2];
    if(rt>=p->bound||id>=p->bound||p->kind[rt]!=VK_SPV_KIND_VECTOR||p->count[rt]!=4u||p->element[rt]!=scalar)return 0;
    if(op==99u){if(!v[id].initialized)return 0;*cell=v[id].word[0];}
    else{v[id].type=rt;v[id].initialized=1u;v[id].word[0]=*cell;v[id].word[1]=v[id].word[2]=0u;v[id].word[3]=kind==1u?vk_spv_float_word(1.0f):1u;}
    return 1;
}

static int vk_spv_storage_image(const vk_spv_profile_t *p,vk_spv_value_t *values,
        vk_descriptor_set_impl_t *const sets[8],const uint32_t *w,uint32_t op){
    uint32_t image=op==99u?w[1]:w[3],coord=op==99u?w[2]:w[4];
    if(image<p->bound&&values[image].type<p->bound&&p->image_dim[values[image].type]==5u)
        return vk_spv_texel(p,values,sets,w,op);
    if(op==95u)return 0; /* 2D ImageFetch is not implemented. */
    if(image>=p->bound||coord>=p->bound||!values[image].initialized||!values[coord].initialized||
       values[image].descriptor_pointer!=2u||(op==98u?(values[image].access&1u):(values[image].access&2u)))return 0;
    uint32_t type=values[image].type,ct=values[coord].type;
    if(type>=p->bound||p->kind[type]!=VK_SPV_KIND_IMAGE||p->image_sampled[type]!=2u||
       ct>=p->bound||p->kind[ct]!=VK_SPV_KIND_VECTOR||p->count[ct]!=(p->image_arrayed[type]?3u:2u)||
       p->kind[p->element[ct]]!=VK_SPV_KIND_INT)return 0;
    vk_descriptor_value_impl_t *d=vk_spv_descriptor_value(sets,values[image].descriptor_set,
        values[image].descriptor_binding,values[image].descriptor_index);
    if(!d||d->type!=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE||!d->resource.image.view)return 0;
    vk_image_view_impl_t *view=d->resource.image.view;
    if((p->image_arrayed[type]&&view->type!=VK_IMAGE_VIEW_TYPE_2D_ARRAY)||
       (!p->image_arrayed[type]&&view->type!=VK_IMAGE_VIEW_TYPE_2D)||view->range.levelCount!=1u)return 0;
    int32_t x=(int32_t)values[coord].word[0],y=(int32_t)values[coord].word[1],layer=p->image_arrayed[type]?(int32_t)values[coord].word[2]:0;
    if(x<0||y<0||layer<0||(uint32_t)layer>=view->range.layerCount)return 0;
    vk_image_plane_t plane;
    if(!vk_image_plane(view->image,view->range.baseMipLevel,view->range.baseArrayLayer+(uint32_t)layer,&plane)||
       plane.layout!=VK_IMAGE_LAYOUT_GENERAL||plane.format!=VK_FORMAT_R8G8B8A8_UNORM||
       (uint32_t)x>=plane.width||(uint32_t)y>=plane.height)return 0;
    uint32_t *pixels=(uint32_t *)plane.pixels;uint64_t index=(uint64_t)(uint32_t)y*plane.width+(uint32_t)x;
    if(op==98u){uint32_t rt=w[1],id=w[2];
        if(rt>=p->bound||id>=p->bound||p->kind[rt]!=VK_SPV_KIND_VECTOR||p->count[rt]!=4u||p->kind[p->element[rt]]!=VK_SPV_KIND_FLOAT)return 0;
        uint32_t pixel=pixels[index];values[id].type=rt;values[id].initialized=1u;
        for(uint32_t c=0u;c<4u;++c)values[id].word[c]=vk_spv_float_word((float)((pixel>>(8u*c))&255u)/255.0f);
    }else{uint32_t id=w[3];if(id>=p->bound||!values[id].initialized)return 0;uint32_t t=values[id].type;
        if(t>=p->bound||p->kind[t]!=VK_SPV_KIND_VECTOR||p->count[t]!=4u||p->kind[p->element[t]]!=VK_SPV_KIND_FLOAT)return 0;
        uint32_t pixel=0u;for(uint32_t c=0u;c<4u;++c){float f=vk_spv_word_float(values[id].word[c]);
            if(!vk_clip_float_is_finite(f))return 0;if(f<0.0f)f=0.0f;if(f>1.0f)f=1.0f;
            pixel|=((uint32_t)(f*255.0f+0.5f))<<(8u*c);
        }pixels[index]=pixel;
    }return 1;
}

static int vk_spv_static_memory_valid(const vk_shader_module_impl_t *shader,const vk_spv_profile_t *p){
    for(uint32_t offset=5u;offset<shader->code_size/4u;){
        const uint32_t *w=shader->code+offset;uint32_t wc=w[0]>>16u,op=w[0]&65535u;
        if(op==224u||op==225u){if(!vk_spv_barrier_valid(p,w,op==224u))return 0;}
        if(op>=227u&&op<=242u){
            uint32_t pointer=w[op==228u?1u:3u],sid=w[op==228u?2u:4u],mid=w[op==228u?3u:5u];
            if(p->stage!=VK_SHADER_STAGE_COMPUTE_BIT||pointer>=p->bound||sid>=p->bound||mid>=p->bound||
               !p->constant_integer[sid]||!p->constant_integer[mid]||!vk_spv_memory_semantics(p->constant_value[sid],p->constant_value[mid]))return 0;
            uint32_t pt=p->type_of[pointer],type=pt<p->bound?p->element[pt]:0u;
            if(!type||p->kind[pt]!=VK_SPV_KIND_POINTER||p->kind[type]!=VK_SPV_KIND_INT||p->width[type]!=32u||
               (p->storage[pt]!=2u&&p->storage[pt]!=4u))return 0;
            uint32_t order=p->constant_value[mid]&0x1eu;
            if((op==227u&&(order==4u||order==8u))||(op==228u&&(order==2u||order==8u)))return 0;
            if(op==230u||op==231u){uint32_t id=w[6];
                if(id>=p->bound||!p->constant_integer[id]||!vk_spv_memory_semantics(p->constant_value[sid],p->constant_value[id])||
                   (p->constant_value[id]&0x1eu)==4u||(p->constant_value[id]&0x1eu)==8u)return 0;
            }
        }
        if(p->stage!=VK_SHADER_STAGE_COMPUTE_BIT){
            if(op==99u)return 0;
            if(op==62u||op==63u){uint32_t ptr=w[1],t=ptr<p->bound?p->type_of[ptr]:0u;
                if(t<p->bound&&p->kind[t]==VK_SPV_KIND_POINTER&&p->storage[t]==2u)return 0;
            }
        }
        offset+=wc;
    }
    return 1;
}

static int vk_texel_format(VkFormat format,uint32_t *kind,uint32_t *components){
    uint32_t k,n;
    switch(format){
    case VK_FORMAT_R32_SFLOAT:k=1u;n=1u;break;
    case VK_FORMAT_R32_UINT:k=2u;n=1u;break;
    case VK_FORMAT_R32_SINT:k=3u;n=1u;break;
    default:return 0;
    }
    if(kind)*kind=k;if(components)*components=n;return 1;
}
static int vk_spv_parse_profile(const vk_shader_module_impl_t *shader, VkShaderStageFlagBits stage, vk_spv_profile_t *profile) {
    return vex_spv_parse_environment(shader,0u,stage,profile);
}
#endif
