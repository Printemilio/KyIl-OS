static size_t scalar_size(GLenum t){return t==GL_FLOAT?4:t==GL_SHORT||t==GL_UNSIGNED_SHORT?2:t==GL_BYTE||t==GL_UNSIGNED_BYTE?1:0;}
void glEnableVertexAttribArray(GLuint i){BEGIN_VOID;if(i>=8)vex_gl_error(GL_INVALID_VALUE);else c->attrib[i].enabled=1;END;}
void glDisableVertexAttribArray(GLuint i){BEGIN_VOID;if(i>=8)vex_gl_error(GL_INVALID_VALUE);else c->attrib[i].enabled=0;END;}
void glVertexAttribPointer(GLuint i,GLint size,GLenum type,GLboolean normalized,GLsizei stride,const void *pointer){BEGIN_VOID;if(i>=8||size<1||size>4||stride<0)vex_gl_error(GL_INVALID_VALUE);else if(!scalar_size(type))vex_gl_error(GL_INVALID_ENUM);else {gl_attribute *a=&c->attrib[i];binding(&a->buffer,c->array_buffer);a->size=size;a->type=type;a->normalized=!!normalized;a->stride=stride;a->pointer=pointer;}END;}
void glVertexAttrib4f(GLuint i,GLfloat x,GLfloat y,GLfloat z,GLfloat w){BEGIN_VOID;if(i>=8)vex_gl_error(GL_INVALID_VALUE);else {float v[]={x,y,z,w};memcpy(c->attrib[i].value,v,16);}END;}
void glVertexAttrib1f(GLuint i,GLfloat x){glVertexAttrib4f(i,x,0,0,1);}void glVertexAttrib2f(GLuint i,GLfloat x,GLfloat y){glVertexAttrib4f(i,x,y,0,1);}void glVertexAttrib3f(GLuint i,GLfloat x,GLfloat y,GLfloat z){glVertexAttrib4f(i,x,y,z,1);}
void glVertexAttrib4fv(GLuint i,const GLfloat *p){if(p)glVertexAttrib4f(i,p[0],p[1],p[2],p[3]);}void glVertexAttrib3fv(GLuint i,const GLfloat *p){if(p)glVertexAttrib3f(i,p[0],p[1],p[2]);}void glVertexAttrib2fv(GLuint i,const GLfloat *p){if(p)glVertexAttrib2f(i,p[0],p[1]);}void glVertexAttrib1fv(GLuint i,const GLfloat *p){if(p)glVertexAttrib1f(i,p[0]);}
void glBindFramebuffer(GLenum t,GLuint id){BEGIN_VOID;if(t!=GL_FRAMEBUFFER)vex_gl_error(GL_INVALID_ENUM);else {gl_object *o=bind_name(c,id,OBJ_FRAMEBUFFER);if(!id||o)binding(&c->framebuffer,o);}END;}
void glBindRenderbuffer(GLenum t,GLuint id){BEGIN_VOID;if(t!=GL_RENDERBUFFER)vex_gl_error(GL_INVALID_ENUM);else {gl_object *o=bind_name(c,id,OBJ_RENDERBUFFER);if(!id||o)binding(&c->renderbuffer,o);}END;}
void glRenderbufferStorage(GLenum t,GLenum format,GLsizei w,GLsizei h){BEGIN_VOID;gl_object *o=c->renderbuffer;unsigned bytes=format==GL_RGBA4||format==GL_RGB5_A1||format==GL_RGB565||format==GL_RGB8_OES||format==GL_RGBA8_OES?4:format==GL_DEPTH_COMPONENT16||format==GL_STENCIL_INDEX8||format==GL_DEPTH24_STENCIL8_OES?8:0;
    if(t!=GL_RENDERBUFFER||!bytes)vex_gl_error(GL_INVALID_ENUM);else if(w<0||h<0||w>4096||h>4096)vex_gl_error(GL_INVALID_VALUE);else if(!o)vex_gl_error(GL_INVALID_OPERATION);else {void *p=w&&h?calloc((size_t)w*h,bytes):NULL;if(w&&h&&!p)vex_gl_error(GL_OUT_OF_MEMORY);else {if(format==GL_RGB8_OES)for(size_t i=0;i<(size_t)w*h;++i)((uint32_t*)p)[i]=0xff000000u;free(o->u.renderbuffer.pixels);o->u.renderbuffer.pixels=p;o->u.renderbuffer.width=w;o->u.renderbuffer.height=h;o->u.renderbuffer.format=format;}}END;}
static gl_object **attachment(gl_object *f,GLenum a){if(a==GL_COLOR_ATTACHMENT0)return &f->u.framebuffer.color;if(a==GL_DEPTH_ATTACHMENT)return &f->u.framebuffer.depth;if(a==GL_STENCIL_ATTACHMENT)return &f->u.framebuffer.stencil;return NULL;}
void glFramebufferTexture2D(GLenum t,GLenum a,GLenum tt,GLuint id,GLint level){BEGIN_VOID;gl_object *f=c->framebuffer,*o=lookup(c->share,id,OBJ_TEXTURE);gl_object **slot=f?attachment(f,a):NULL;
    if(t!=GL_FRAMEBUFFER||tt!=GL_TEXTURE_2D||(f&&!slot))vex_gl_error(GL_INVALID_ENUM);else if(level)vex_gl_error(GL_INVALID_VALUE);else if(!f||(id&&!o)||a!=GL_COLOR_ATTACHMENT0)vex_gl_error(GL_INVALID_OPERATION);else binding(slot,o);END;}
void glFramebufferRenderbuffer(GLenum t,GLenum a,GLenum rt,GLuint id){BEGIN_VOID;gl_object *f=c->framebuffer,*o=lookup(c->share,id,OBJ_RENDERBUFFER);gl_object **slot=f?attachment(f,a):NULL;
    if(t!=GL_FRAMEBUFFER||rt!=GL_RENDERBUFFER||(f&&!slot))vex_gl_error(GL_INVALID_ENUM);else if(!f||(id&&!o))vex_gl_error(GL_INVALID_OPERATION);else binding(slot,o);END;}
static int object_image(gl_object *o,void **p,GLsizei *w,GLsizei *h){if(!o)return 0;if(o->kind==OBJ_TEXTURE){*p=o->u.texture.pixels;*w=o->u.texture.width;*h=o->u.texture.height;}else {*p=o->u.renderbuffer.pixels;*w=o->u.renderbuffer.width;*h=o->u.renderbuffer.height;}return *p&&*w&&*h;}
static GLenum framebuffer_target(egl_context *c,vex_render_target *out,int read){
    memset(out,0,sizeof(*out));
    if(!c->framebuffer){egl_surface *s=read?vex_gl_read_surface:vex_gl_draw_surface;if(!s)return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;if(!vex_egl_refresh_surface(s)){vex_gl_error(GL_OUT_OF_MEMORY);return GL_FRAMEBUFFER_UNSUPPORTED;}*out=s->target;return GL_FRAMEBUFFER_COMPLETE;}
    gl_object *f=c->framebuffer;GLsizei w,h;void *p;if(!f->u.framebuffer.color)return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    if(!object_image(f->u.framebuffer.color,&p,&w,&h))return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    if(f->u.framebuffer.color->kind==OBJ_RENDERBUFFER&&f->u.framebuffer.color->u.renderbuffer.format!=GL_RGBA4&&f->u.framebuffer.color->u.renderbuffer.format!=GL_RGB565&&f->u.framebuffer.color->u.renderbuffer.format!=GL_RGB5_A1&&f->u.framebuffer.color->u.renderbuffer.format!=GL_RGB8_OES&&f->u.framebuffer.color->u.renderbuffer.format!=GL_RGBA8_OES)return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    *out=(vex_render_target){p,w,h,NULL,0};gl_object *ds=f->u.framebuffer.depth?f->u.framebuffer.depth:f->u.framebuffer.stencil;
    if(ds){GLsizei dw,dh;void *dp;if(ds->kind!=OBJ_RENDERBUFFER||!object_image(ds,&dp,&dw,&dh)||w!=dw||h!=dh)return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        GLenum format=ds->u.renderbuffer.format;if(format!=GL_DEPTH_COMPONENT16&&format!=GL_STENCIL_INDEX8&&format!=GL_DEPTH24_STENCIL8_OES)return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        if(f->u.framebuffer.depth&&f->u.framebuffer.stencil&&f->u.framebuffer.depth!=f->u.framebuffer.stencil)return GL_FRAMEBUFFER_UNSUPPORTED;out->depth_stencil=dp;
    }return GL_FRAMEBUFFER_COMPLETE;
}
GLenum glCheckFramebufferStatus(GLenum t){BEGIN_RET(0);vex_render_target out;GLenum status=t==GL_FRAMEBUFFER?framebuffer_target(c,&out,0):0;if(!status)vex_gl_error(GL_INVALID_ENUM);END;return status;}
/* RGB storage has no writable alpha channel. Its padded software byte stays
 * one, including during blending and partial color masks. */
static int framebuffer_rgb_only(egl_context *c){gl_object *o=c->framebuffer?c->framebuffer->u.framebuffer.color:NULL;return o&&(o->kind==OBJ_RENDERBUFFER?o->u.renderbuffer.format==GL_RGB8_OES:o->u.texture.format==GL_RGB);}
static uint32_t color_mask(egl_context *c){uint32_t m=0;for(unsigned i=0;i<4;++i)if(c->color_mask[i]&&(i!=3||!framebuffer_rgb_only(c)))m|=255u<<(8*i);return m;}
static uint32_t swap_rb(uint32_t p){return (p&0xff00ff00u)|((p&0xff0000u)>>16)|((p&255u)<<16);}
static uint32_t framebuffer_pixel(const vex_render_target *target,int64_t x,int64_t y){
    if(x<0||y<0||x>=target->width||y>=target->height)return 0;
    uint32_t pixel=target->pixels[(size_t)(target->top_left?target->height-1-y:y)*target->width+x];
    return target->format==VK_FORMAT_B8G8R8A8_UNORM?swap_rb(pixel):pixel;
}
static uint32_t copy_pixel_format(uint32_t rgba,GLenum format){
    uint8_t bytes[4];memcpy(bytes,&rgba,4);
    if(format==GL_RGBA)return rgba;if(format==GL_RGB)return rgba|0xff000000u;
    if(format==GL_ALPHA)return rgba&0xff000000u;
    uint32_t luminance=bytes[0]*0x10101u;return luminance|(format==GL_LUMINANCE_ALPHA?(rgba&0xff000000u):0xff000000u);
}
void glCopyTexImage2D(GLenum target,GLint level,GLenum format,GLint x,GLint y,GLsizei width,GLsizei height,GLint border){
    BEGIN_VOID;gl_object *texture=c->texture[c->active_texture];vex_render_target source;
    if(target!=GL_TEXTURE_2D||!pixel_components(format))vex_gl_error(GL_INVALID_ENUM);
    else if(level||border||width<0||height<0||width>4096||height>4096)vex_gl_error(GL_INVALID_VALUE);
    else if(!texture)vex_gl_error(GL_INVALID_OPERATION);
    else if(framebuffer_target(c,&source,1)!=GL_FRAMEBUFFER_COMPLETE)vex_gl_error(GL_INVALID_FRAMEBUFFER_OPERATION);
    else {uint32_t *pixels=width&&height?calloc((size_t)width*height,4):NULL;
        if(width&&height&&!pixels)vex_gl_error(GL_OUT_OF_MEMORY);
        else {for(int j=0;j<height;++j)for(int i=0;i<width;++i)pixels[(size_t)j*width+i]=copy_pixel_format(framebuffer_pixel(&source,(int64_t)x+i,(int64_t)y+j),format);
            free(texture->u.texture.pixels);texture->u.texture.pixels=pixels;texture->u.texture.width=width;texture->u.texture.height=height;texture->u.texture.format=format;}}
    END;
}
void glCopyTexSubImage2D(GLenum target,GLint level,GLint xoffset,GLint yoffset,GLint x,GLint y,GLsizei width,GLsizei height){
    BEGIN_VOID;gl_object *texture=c->texture[c->active_texture];vex_render_target source;
    if(target!=GL_TEXTURE_2D)vex_gl_error(GL_INVALID_ENUM);
    else if(level||xoffset<0||yoffset<0||width<0||height<0)vex_gl_error(GL_INVALID_VALUE);
    else if(!texture||!texture->u.texture.pixels)vex_gl_error(GL_INVALID_OPERATION);
    else if(xoffset>texture->u.texture.width||width>texture->u.texture.width-xoffset||yoffset>texture->u.texture.height||height>texture->u.texture.height-yoffset)vex_gl_error(GL_INVALID_VALUE);
    else if(framebuffer_target(c,&source,1)!=GL_FRAMEBUFFER_COMPLETE)vex_gl_error(GL_INVALID_FRAMEBUFFER_OPERATION);
    else {uint32_t *copy=width&&height?malloc((size_t)width*height*4):NULL;
        if(width&&height&&!copy)vex_gl_error(GL_OUT_OF_MEMORY);
        else {for(int j=0;j<height;++j)for(int i=0;i<width;++i)copy[(size_t)j*width+i]=copy_pixel_format(framebuffer_pixel(&source,(int64_t)x+i,(int64_t)y+j),texture->u.texture.format);
            for(int j=0;j<height;++j)if(width)memcpy(texture->u.texture.pixels+(size_t)(j+yoffset)*texture->u.texture.width+xoffset,copy+(size_t)j*width,(size_t)width*4);free(copy);}}
    END;
}
void glClear(GLbitfield mask){BEGIN_VOID;vex_render_target t;if(mask&~(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT))vex_gl_error(GL_INVALID_VALUE);else if(framebuffer_target(c,&t,0)!=GL_FRAMEBUFFER_COMPLETE)vex_gl_error(GL_INVALID_FRAMEBUFFER_OPERATION);else {
    int64_t x0=0,y0=0,x1=t.width,y1=t.height;if(c->scissor){if(c->state.scissor.offset.x>x0)x0=c->state.scissor.offset.x;if(c->state.scissor.offset.y>y0)y0=c->state.scissor.offset.y;int64_t ex=(int64_t)c->state.scissor.offset.x+c->state.scissor.extent.width,ey=(int64_t)c->state.scissor.offset.y+c->state.scissor.extent.height;if(ex<x1)x1=ex;if(ey<y1)y1=ey;}
    uint32_t color=0,m=color_mask(c);for(unsigned i=0;i<4;++i)color|=(uint32_t)(c->clear_color[i]*255.f+.5f)<<(8*i);if(t.format==VK_FORMAT_B8G8R8A8_UNORM){color=swap_rb(color);m=swap_rb(m);}
    for(int64_t y=y0;y<y1;++y)for(int64_t x=x0;x<x1;++x){size_t i=(size_t)(t.top_left?t.height-1-y:y)*t.width+x;if(mask&GL_COLOR_BUFFER_BIT)t.pixels[i]=(t.pixels[i]&~m)|(color&m);if(t.depth_stencil){uint8_t *ds=(uint8_t*)t.depth_stencil+i*8;if((mask&GL_DEPTH_BUFFER_BIT)&&c->state.depth_write)memcpy(ds,&c->clear_depth,4);if(mask&GL_STENCIL_BUFFER_BIT)ds[4]=(ds[4]&~c->stencil_mask[0])|(c->clear_stencil&c->stencil_mask[0]);}}
    }END;}
void glReadPixels(GLint x,GLint y,GLsizei w,GLsizei h,GLenum f,GLenum type,void *out){BEGIN_VOID;vex_render_target t;if(w<0||h<0||(!out&&w&&h))vex_gl_error(GL_INVALID_VALUE);else if(f!=GL_RGBA||type!=GL_UNSIGNED_BYTE)vex_gl_error(GL_INVALID_ENUM);else if(framebuffer_target(c,&t,1)!=GL_FRAMEBUFFER_COMPLETE)vex_gl_error(GL_INVALID_FRAMEBUFFER_OPERATION);else {size_t pitch=((size_t)w*4+c->pack-1)&~(size_t)(c->pack-1);for(int j=0;j<h;++j)for(int i=0;i<w;++i){int64_t xx=(int64_t)x+i,yy=(int64_t)y+j;if(xx<0||yy<0||xx>=t.width||yy>=t.height)continue;uint32_t p=t.pixels[(size_t)(t.top_left?t.height-1-yy:yy)*t.width+xx];if(t.format==VK_FORMAT_B8G8R8A8_UNORM)p=swap_rb(p);memcpy((uint8_t*)out+(size_t)j*pitch+i*4,&p,4);}}END;}
static float read_scalar(const uint8_t *p,GLenum t,int norm){float f;if(t==GL_FLOAT){memcpy(&f,p,4);return f;}if(t==GL_UNSIGNED_BYTE)return norm?*p/255.f:*p;if(t==GL_BYTE){int8_t v;memcpy(&v,p,1);return norm?fmaxf(-1.f,v/127.f):v;}if(t==GL_UNSIGNED_SHORT){uint16_t v;memcpy(&v,p,2);return norm?v/65535.f:v;}int16_t v;memcpy(&v,p,2);return norm?fmaxf(-1.f,v/32767.f):v;}
static VkSamplerAddressMode sampler_address(GLenum e){return e==GL_CLAMP_TO_EDGE?VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:e==GL_MIRRORED_REPEAT?VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:VK_SAMPLER_ADDRESS_MODE_REPEAT;}
static void draw(egl_context *c,GLenum mode,GLint first,GLsizei count,GLenum index_type,const void *indices){
    vex_render_target target;VkPrimitiveTopology topology;if(mode==GL_TRIANGLES)topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;else if(mode==GL_TRIANGLE_STRIP)topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;else if(mode==GL_TRIANGLE_FAN)topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;else if(mode==GL_POINTS)topology=VK_PRIMITIVE_TOPOLOGY_POINT_LIST;else if(mode==GL_LINES)topology=VK_PRIMITIVE_TOPOLOGY_LINE_LIST;else if(mode==GL_LINE_STRIP)topology=VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;else {vex_gl_error(GL_INVALID_ENUM);return;}
    if(count<0||first<0){vex_gl_error(GL_INVALID_VALUE);return;}if(!c->program||!c->program->u.program.executable){vex_gl_error(GL_INVALID_OPERATION);return;}
    if(framebuffer_target(c,&target,0)!=GL_FRAMEBUFFER_COMPLETE){vex_gl_error(GL_INVALID_FRAMEBUFFER_OPERATION);return;}if(!count)return;
    size_t index_bytes=index_type==GL_UNSIGNED_BYTE?1:index_type==GL_UNSIGNED_SHORT?2:0;const uint8_t *index_data=indices;
    if(index_type){if(!index_bytes){vex_gl_error(GL_INVALID_ENUM);return;}if(c->elements){size_t offset=(uintptr_t)indices;if(offset>c->elements->u.buffer.size||(size_t)count>(c->elements->u.buffer.size-offset)/index_bytes){vex_gl_error(GL_INVALID_OPERATION);return;}index_data=c->elements->u.buffer.bytes+offset;}else if(!indices){vex_gl_error(GL_INVALID_OPERATION);return;}}
    if((size_t)count>SIZE_MAX/128){vex_gl_error(GL_OUT_OF_MEMORY);return;}float *packed=malloc((size_t)count*128);if(!packed){vex_gl_error(GL_OUT_OF_MEMORY);return;}
    for(unsigned v=0;v<(unsigned)count;++v){size_t index=(size_t)first+v;if(index_type){index=0;memcpy(&index,index_data+v*index_bytes,index_bytes);}
        for(unsigned a=0;a<8;++a){float *dest=packed+v*32+a*4;gl_attribute *attr=&c->attrib[a];memcpy(dest,attr->value,16);if(!attr->enabled)continue;size_t scalar=scalar_size(attr->type),stride=attr->stride?attr->stride:scalar*attr->size,bytes=scalar*attr->size,offset=(uintptr_t)attr->pointer;
            if(!scalar||!bytes||index>(SIZE_MAX-offset)/stride){vex_gl_error(GL_INVALID_OPERATION);goto fail;}offset+=index*stride;const uint8_t *source;
            if(attr->buffer){if(offset>attr->buffer->u.buffer.size||bytes>attr->buffer->u.buffer.size-offset){vex_gl_error(GL_INVALID_OPERATION);goto fail;}source=attr->buffer->u.buffer.bytes+offset;}else {if(!attr->pointer){vex_gl_error(GL_INVALID_OPERATION);goto fail;}source=(void*)offset;}
            dest[0]=dest[1]=dest[2]=0;dest[3]=1;for(unsigned i=0;i<(unsigned)attr->size;++i)dest[i]=read_scalar(source+i*scalar,attr->type,attr->normalized);
        }
    }
    vex_render_state state=c->state;state.dither=c->dither;state.polygon_offset=c->polygon_offset;state.topology=topology;state.depth_test=c->depth;state.stencil_test=c->stencil;state.blend.blendEnable=c->blend;state.blend.colorWriteMask=0;for(unsigned i=0;i<4;++i)if(c->color_mask[i])state.blend.colorWriteMask|=1u<<i;
    if(framebuffer_rgb_only(c))state.blend.colorWriteMask&=7;
    if(!c->scissor)state.scissor=(VkRect2D){{0,0},{target.width,target.height}};
    state.cull=!c->cull?0:c->cull_face==GL_FRONT?VK_CULL_MODE_FRONT_BIT:c->cull_face==GL_BACK?VK_CULL_MODE_BACK_BIT:VK_CULL_MODE_FRONT_AND_BACK;state.front=c->front_face==GL_CCW?VK_FRONT_FACE_COUNTER_CLOCKWISE:VK_FRONT_FACE_CLOCKWISE;
    /* ES 2.0 section 4.4.4: a bound attachment becomes feedback only if
     * the linked shaders can sample its unit. Unused bindings must not
     * invalidate a color-only draw (or impose their filtering state). */
    uint32_t sampled_units=0;vex_render_program *program=c->program->u.program.executable;
    for(size_t u=0;u<vex_render_uniform_count(program);++u){
        const vex_render_uniform *info=vex_render_uniform_info(program,u);if(info->kind!=4)continue;
        const uint32_t *units=vex_render_uniform_get(program,(int)(u*16));
        for(unsigned k=0;k<info->count;++k){if(units[k]>=16){vex_gl_error(GL_INVALID_OPERATION);goto fail;}sampled_units|=1u<<units[k];}
    }
    vex_render_texture textures[16]={0};for(unsigned i=0;i<16;++i){if(!(sampled_units&(1u<<i)))continue;gl_object *o=c->texture[i];if(o){if(o->u.texture.min!=GL_LINEAR&&o->u.texture.min!=GL_NEAREST){vex_gl_error(GL_INVALID_OPERATION);goto fail;}textures[i]=(vex_render_texture){o->u.texture.pixels,o->u.texture.width,o->u.texture.height,o->u.texture.mag==GL_LINEAR?VK_FILTER_LINEAR:VK_FILTER_NEAREST,o->u.texture.min==GL_LINEAR?VK_FILTER_LINEAR:VK_FILTER_NEAREST,sampler_address(o->u.texture.u),sampler_address(o->u.texture.v)};if(c->framebuffer&&c->framebuffer->u.framebuffer.color==o){vex_gl_error(GL_INVALID_OPERATION);goto fail;}}}
    if(!vex_render_draw(c->program->u.program.executable,&state,&target,packed,count,textures))vex_gl_error(GL_INVALID_OPERATION);
fail:free(packed);
}
void glDrawArrays(GLenum m,GLint f,GLsizei n){BEGIN_VOID;draw(c,m,f,n,0,NULL);END;}
void glDrawElements(GLenum m,GLsizei n,GLenum t,const void *i){BEGIN_VOID;draw(c,m,0,n,t,i);END;}
