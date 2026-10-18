/* GLES object reflection uses the linked common-core program, never source
 * matching or synthetic locations. Included by gles.c after object helpers. */
static GLenum uniform_type(const vex_render_uniform *u) {
    if(u->kind==4)return GL_SAMPLER_2D;
    if(u->columns>1)return u->columns==2?GL_FLOAT_MAT2:u->columns==3?GL_FLOAT_MAT3:GL_FLOAT_MAT4;
    if(u->kind==1)return u->components==1?GL_FLOAT:u->components==2?GL_FLOAT_VEC2:u->components==3?GL_FLOAT_VEC3:GL_FLOAT_VEC4;
    return u->components==1?GL_INT:u->components==2?GL_INT_VEC2:u->components==3?GL_INT_VEC3:GL_INT_VEC4;
}
void glGetActiveUniform(GLuint id,GLuint index,GLsizei capacity,GLsizei *length,GLint *size,GLenum *type,GLchar *name){
    BEGIN_VOID;gl_object *p=lookup(c->share,id,OBJ_PROGRAM);
    const vex_render_uniform *u=p?vex_render_uniform_info(p->u.program.executable,index):NULL;
    if(!p)vex_gl_error(GL_INVALID_VALUE);else if(!u||capacity<0)vex_gl_error(GL_INVALID_VALUE);
    else {char array_name[132];snprintf(array_name,sizeof(array_name),"%s%s",u->name,u->count>1?"[0]":"");text_copy(array_name,capacity,length,name);if(size)*size=u->count;if(type)*type=uniform_type(u);}END;
}
void glGetActiveAttrib(GLuint id,GLuint index,GLsizei capacity,GLsizei *length,GLint *size,GLenum *type,GLchar *name){
    BEGIN_VOID;gl_object *p=lookup(c->share,id,OBJ_PROGRAM);uint32_t components=0;
    const char *s=p?vex_render_attribute_info(p->u.program.executable,index,&components):NULL;
    if(!p||!s||capacity<0)vex_gl_error(GL_INVALID_VALUE);else {text_copy(s,capacity,length,name);if(size)*size=1;if(type)*type=components==1?GL_FLOAT:components==2?GL_FLOAT_VEC2:components==3?GL_FLOAT_VEC3:GL_FLOAT_VEC4;}END;
}
void glGetAttachedShaders(GLuint id,GLsizei max,GLsizei *count,GLuint *out){BEGIN_VOID;gl_object *p=lookup(c->share,id,OBJ_PROGRAM);
    if(!p||max<0||(!out&&max))vex_gl_error(GL_INVALID_VALUE);else {int n=0;for(unsigned s=0;s<2&&n<max;++s)if(p->u.program.shader[s])out[n++]=p->u.program.shader[s]->name;if(count)*count=n;}END;}
void glGetShaderSource(GLuint id,GLsizei max,GLsizei *length,GLchar *out){BEGIN_VOID;gl_object *s=lookup(c->share,id,OBJ_SHADER);if(!s)vex_gl_error(GL_INVALID_VALUE);else text_copy(s->u.shader.source?s->u.shader.source:"",max,length,out);END;}
GLboolean glIsProgram(GLuint id){return is_object(OBJ_PROGRAM,id);}
GLboolean glIsShader(GLuint id){return is_object(OBJ_SHADER,id);}
static void get_uniform(GLuint id,GLint location,void *out,int floating){
    BEGIN_VOID;gl_object *p=lookup(c->share,id,OBJ_PROGRAM);const vex_render_uniform *u=p?vex_render_uniform_at_location(p->u.program.executable,location):NULL;
    const uint32_t *words=p?vex_render_uniform_get(p->u.program.executable,location):NULL;
    if(!p)vex_gl_error(GL_INVALID_VALUE);else if(!u||!words||!out)vex_gl_error(GL_INVALID_OPERATION);
    else for(unsigned i=0;i<u->components*u->columns;++i){float f;int32_t integer;memcpy(&f,words+i,4);memcpy(&integer,words+i,4);if(floating)((float*)out)[i]=u->kind==1?f:(float)integer;else ((int*)out)[i]=u->kind==1?(int)f:integer;}END;
}
void glGetUniformfv(GLuint id,GLint location,GLfloat *out){get_uniform(id,location,out,1);}
void glGetUniformiv(GLuint id,GLint location,GLint *out){get_uniform(id,location,out,0);}
void glGetShaderPrecisionFormat(GLenum shader,GLenum precision,GLint *range,GLint *bits){BEGIN_VOID;
    if(shader!=GL_VERTEX_SHADER&&shader!=GL_FRAGMENT_SHADER)vex_gl_error(GL_INVALID_ENUM);
    else if(!range||!bits)vex_gl_error(GL_INVALID_VALUE);
    else if(precision==GL_LOW_FLOAT||precision==GL_MEDIUM_FLOAT||precision==GL_HIGH_FLOAT){range[0]=range[1]=127;*bits=23;}
    else if(precision==GL_LOW_INT||precision==GL_MEDIUM_INT||precision==GL_HIGH_INT){range[0]=range[1]=31;*bits=0;}else vex_gl_error(GL_INVALID_ENUM);END;}
static int attrib_value(gl_attribute *a,GLenum property){switch(property){case GL_VERTEX_ATTRIB_ARRAY_ENABLED:return a->enabled;case GL_VERTEX_ATTRIB_ARRAY_SIZE:return a->size;case GL_VERTEX_ATTRIB_ARRAY_STRIDE:return a->stride;case GL_VERTEX_ATTRIB_ARRAY_TYPE:return a->type;case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:return a->normalized;case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:return object_name(a->buffer);default:vex_gl_error(GL_INVALID_ENUM);return 0;}}
void glGetVertexAttribiv(GLuint index,GLenum property,GLint *out){BEGIN_VOID;if(index>=8||!out)vex_gl_error(GL_INVALID_VALUE);else if(property==GL_CURRENT_VERTEX_ATTRIB){for(unsigned i=0;i<4;++i)out[i]=c->attrib[index].value[i];}else *out=attrib_value(&c->attrib[index],property);END;}
void glGetVertexAttribfv(GLuint index,GLenum property,GLfloat *out){BEGIN_VOID;if(index>=8||!out)vex_gl_error(GL_INVALID_VALUE);else if(property==GL_CURRENT_VERTEX_ATTRIB)memcpy(out,c->attrib[index].value,16);else *out=attrib_value(&c->attrib[index],property);END;}
void glGetVertexAttribPointerv(GLuint index,GLenum property,void **out){BEGIN_VOID;if(index>=8||!out)vex_gl_error(GL_INVALID_VALUE);else if(property!=GL_VERTEX_ATTRIB_ARRAY_POINTER)vex_gl_error(GL_INVALID_ENUM);else *out=(void*)c->attrib[index].pointer;END;}
void glGetTexParameterfv(GLenum target,GLenum property,GLfloat *out){GLint value;glGetTexParameteriv(target,property,&value);if(out)*out=value;}
void glTexParameteriv(GLenum target,GLenum property,const GLint *value){if(value)glTexParameteri(target,property,*value);}
void glTexParameterfv(GLenum target,GLenum property,const GLfloat *value){if(value)glTexParameterf(target,property,*value);}
void glGetRenderbufferParameteriv(GLenum target,GLenum property,GLint *out){BEGIN_VOID;gl_object *o=c->renderbuffer;
    if(target!=GL_RENDERBUFFER)vex_gl_error(GL_INVALID_ENUM);else if(!o||!out)vex_gl_error(GL_INVALID_OPERATION);else {GLenum f=o->u.renderbuffer.format;switch(property){
    case GL_RENDERBUFFER_WIDTH:*out=o->u.renderbuffer.width;break;case GL_RENDERBUFFER_HEIGHT:*out=o->u.renderbuffer.height;break;case GL_RENDERBUFFER_INTERNAL_FORMAT:*out=f;break;
    case GL_RENDERBUFFER_RED_SIZE:case GL_RENDERBUFFER_GREEN_SIZE:case GL_RENDERBUFFER_BLUE_SIZE:*out=f==GL_RGB8_OES||f==GL_RGBA8_OES?8:f==GL_RGBA4?4:f==GL_RGB565?(property==GL_RENDERBUFFER_GREEN_SIZE?6:5):f==GL_RGB5_A1?5:0;break;
    case GL_RENDERBUFFER_ALPHA_SIZE:*out=f==GL_RGBA8_OES?8:f==GL_RGBA4?4:f==GL_RGB5_A1?1:0;break;case GL_RENDERBUFFER_DEPTH_SIZE:*out=f==GL_DEPTH_COMPONENT16?16:f==GL_DEPTH24_STENCIL8_OES?24:0;break;case GL_RENDERBUFFER_STENCIL_SIZE:*out=f==GL_STENCIL_INDEX8||f==GL_DEPTH24_STENCIL8_OES?8:0;break;default:vex_gl_error(GL_INVALID_ENUM);}}END;}
void glGetFramebufferAttachmentParameteriv(GLenum target,GLenum point,GLenum property,GLint *out){BEGIN_VOID;gl_object **slot=c->framebuffer?attachment(c->framebuffer,point):NULL;
    if(target!=GL_FRAMEBUFFER||!slot)vex_gl_error(GL_INVALID_ENUM);else if(!out)vex_gl_error(GL_INVALID_VALUE);else {gl_object *o=*slot;if(property==GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE)*out=!o?GL_NONE:o->kind==OBJ_TEXTURE?GL_TEXTURE:GL_RENDERBUFFER;else if(property==GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME)*out=object_name(o);else if(o&&o->kind==OBJ_TEXTURE&&(property==GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL||property==GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE))*out=0;else vex_gl_error(GL_INVALID_ENUM);}END;}
void glValidateProgram(GLuint id){BEGIN_VOID;gl_object *p=lookup(c->share,id,OBJ_PROGRAM);if(!p)vex_gl_error(GL_INVALID_VALUE);else {p->u.program.validated=p->u.program.linked;
    for(size_t i=0;p->u.program.validated&&i<vex_render_uniform_count(p->u.program.executable);++i){const vex_render_uniform *u=vex_render_uniform_info(p->u.program.executable,i);const uint32_t *v=vex_render_uniform_get(p->u.program.executable,i*16);if(u->kind==4&&v[0]>=16){p->u.program.validated=0;snprintf(p->u.program.log,sizeof(p->u.program.log),"Sampler unit exceeds context limit");}}}END;}
/* No compressed format is advertised. Unsupported compressed uploads fail. */
void glCompressedTexImage2D(GLenum t,GLint l,GLenum f,GLsizei w,GLsizei h,GLint b,GLsizei n,const void *p){BEGIN_VOID;(void)t;(void)l;(void)f;(void)w;(void)h;(void)b;(void)n;(void)p;vex_gl_error(GL_INVALID_ENUM);END;}
void glCompressedTexSubImage2D(GLenum t,GLint l,GLint x,GLint y,GLsizei w,GLsizei h,GLenum f,GLsizei n,const void *p){BEGIN_VOID;(void)t;(void)l;(void)x;(void)y;(void)w;(void)h;(void)f;(void)n;(void)p;vex_gl_error(GL_INVALID_ENUM);END;}
