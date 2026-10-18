#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "vex_syscall.h"
#include "vex_egl_backend.h"
#include <unos/vex_abi.h>
static unsigned tests;
#define CHECK(x) do{++tests;if(!(x)){fprintf(stderr,"EGL/GLES FAILED line %d: %s (GL %x EGL %x)\n",__LINE__,#x,glGetError(),eglGetError());exit(1);}}while(0)
static GLuint shader(GLenum type,const char *source){GLuint id=glCreateShader(type);glShaderSource(id,1,&source,NULL);glCompileShader(id);GLint ok;glGetShaderiv(id,GL_COMPILE_STATUS,&ok);if(!ok){char log[4096];glGetShaderInfoLog(id,sizeof(log),NULL,log);fprintf(stderr,"compile: %s\n",log);}CHECK(ok);return id;}
static GLuint program(const char *vertex,const char *fragment){GLuint v=shader(GL_VERTEX_SHADER,vertex),f=shader(GL_FRAGMENT_SHADER,fragment),p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);fprintf(stderr,"EGL/GLES stage: program linked\n");GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){char log[4096];glGetProgramInfoLog(p,sizeof(log),NULL,log);fprintf(stderr,"link: %s\n",log);}CHECK(ok);glDeleteShader(v);glDeleteShader(f);return p;}
static uint32_t pixel(int x,int y){uint32_t p=0;glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,&p);CHECK(glGetError()==GL_NO_ERROR);return p;}
static EGLDisplay display;static EGLContext context;static EGLSurface surface;
static uint64_t native_frames;
static uint64_t native_surface_cap;
static unsigned native_references;
static void native_retain(void *owner){(void)owner;++native_references;}
static void native_release(void *owner){(void)owner;--native_references;}
static int native_present(void *owner,uint64_t cap,uint64_t serial){
    native_surface_cap=cap;
    vex_egl_window *window=owner;CHECK(serial==native_frames+1);vex_gpu_surface_present_req_t p={0};p.surface_handle=cap;
    p.source=(vex_rect_t){0,0,window->width,window->height};p.destination=(vex_rect_t){16,16,window->width,window->height};p.opacity=255;
    CHECK(vex_syscall1(VEX_SYS_GPU_SURFACE_PRESENT,(uintptr_t)&p)==0);
    vex_gpu_surface_wait_req_t w={cap,p.signal_serial,0};CHECK(vex_syscall1(VEX_SYS_GPU_SURFACE_WAIT,(uintptr_t)&w)==0);
    vex_surface_query_req_t q={0};q.surface_handle=cap;CHECK(vex_syscall1(VEX_SYS_SURFACE_QUERY,(uintptr_t)&q)==0);
    CHECK(q.completed_serial>=p.signal_serial&&q.width==window->width&&q.height==window->height&&q.stride==window->width);
    vex_surface_map_req_t m={0};m.surface_handle=cap;m.length=q.backing_size;m.access=VEX_SURFACE_MAP_READ;
    CHECK(vex_syscall1(VEX_SYS_SURFACE_MAP,(uintptr_t)&m)==0);
    // Native top row is blue, bottom row red; GL uses the opposite Y origin.
    uint32_t *pixels=(void*)(uintptr_t)m.mapped_addr;CHECK(pixels[0]==0xff0000ff&&pixels[(window->height-1)*window->width]==0xffff0000);
    vex_surface_unmap_req_t u={0};u.surface_handle=cap;u.length=m.length;u.mapped_addr=m.mapped_addr;u.access=m.access;
    CHECK(vex_syscall1(VEX_SYS_SURFACE_UNMAP,(uintptr_t)&u)==0);++native_frames;return 0;
}
static void *other_thread(void *arg){(void)arg;EGLBoolean result=eglMakeCurrent(display,surface,surface,context);return (void*)(uintptr_t)(!result&&eglGetError()==EGL_BAD_ACCESS&&eglGetCurrentContext()==EGL_NO_CONTEXT);}
static void *bind_until_exit(void *arg){return (void*)(uintptr_t)eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,(EGLContext)arg);}
typedef struct {int fd;uint64_t cap;uint64_t sequence;} native_ack_test;
static void *native_frame_wait_ack(void *arg){
    native_ack_test *test=arg;
    uint64_t frame[3]={UINT64_C(0x5657504546524d31),test->sequence,0},ack[3]={0};
    vex_message_attachment_req_t attachment={test->cap,VEX_MESSAGE_OBJECT_SURFACE,VEX_SURFACE_RIGHT_QUERY};
    vex_message_io_req_t send={test->fd,0,frame,sizeof(frame),&attachment,1};
    vex_message_io_req_t receive={test->fd,0,ack,sizeof(ack),NULL,0};
    if(vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&send)!=sizeof(frame)||
       vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&receive)!=sizeof(ack)||
       memcmp(frame,ack,sizeof(frame)))return NULL;
    return (void*)1;
}
typedef struct {uint64_t inode;int64_t offset;uint16_t length;uint8_t type;char name[];} test_dirent;
static void filesystem_abi_test(int native){
    char cwd[4096];long length=syscall(SYS_getcwd,cwd,sizeof(cwd));
    CHECK(length>1&&length==(long)strlen(cwd)+1&&cwd[0]=='/');
    char *allocated=getcwd(NULL,0);CHECK(allocated&&strcmp(allocated,cwd)==0);free(allocated);
    errno=0;CHECK(syscall(SYS_getcwd,cwd,1)==-1&&errno==ERANGE);
    errno=0;CHECK(syscall(SYS_getcwd,cwd,0)==-1&&errno==ERANGE);
    errno=0;CHECK(syscall(SYS_getcwd,(void*)1,sizeof(cwd))==-1&&errno==EFAULT);
    if(!native)return;
    errno=0;CHECK(syscall(SYS_getcwd,(void*)UINT64_C(0xffff800000000000),sizeof(cwd))==-1&&errno==EFAULT);
    const char *paths[]={"/usr/share/fonts","/usr/share/fonts/truetype","/usr/share/fonts/truetype/dejavu"};
    const char *expected[]={"truetype","dejavu","DejaVuSans.ttf"};
    for(unsigned cycle=0;cycle<8;++cycle)for(unsigned directory=0;directory<3;++directory){
        int fd=open(paths[directory],O_RDONLY|O_DIRECTORY);CHECK(fd>=0);
        char small[64];errno=0;CHECK(syscall(SYS_getdents64,fd,(void*)1,sizeof(small))==-1&&errno==EFAULT);
        errno=0;CHECK(syscall(SYS_getdents64,fd,small,1)==-1&&errno==EINVAL);
        unsigned found=0,entries=0;int64_t previous=0;
        for(;;){
            long bytes=syscall(SYS_getdents64,fd,small,sizeof(small));CHECK(bytes>=0&&bytes<=(long)sizeof(small));
            if(!bytes)break;
            for(long offset=0;offset<bytes;){
                test_dirent *entry=(void*)(small+offset);
                CHECK(entry->length>=24&&!(entry->length&7)&&offset+entry->length<=bytes&&entry->offset>previous);
                previous=entry->offset;++entries;
                if(!strcmp(entry->name,expected[directory])){++found;CHECK(entry->type==(directory==2?8:4));}
                if(cycle==0&&directory==2&&entry->type==8&&strstr(entry->name,".ttf")){
                    char path[512];snprintf(path,sizeof(path),"%s/%s",paths[directory],entry->name);
                    struct stat info;CHECK(stat(path,&info)==0&&info.st_size>4);
                    int font=open(path,O_RDONLY);CHECK(font>=0);uint8_t signature[4];
                    CHECK(pread(font,signature,4,0)==4&&signature[0]==0&&signature[1]==1&&signature[2]==0&&signature[3]==0);
                    CHECK(close(font)==0);
                }
                offset+=entry->length;
            }
            CHECK(entries<1000);
        }
        CHECK(found==1&&entries>2);CHECK(close(fd)==0);
    }
    int fd=open(paths[2],O_RDONLY|O_DIRECTORY);CHECK(fd>=0);int duplicate=dup(fd);CHECK(duplicate>=0);
    char entry_buffer[64];CHECK(syscall(SYS_getdents64,fd,entry_buffer,24)==24);
    CHECK(!strcmp(((test_dirent*)entry_buffer)->name,"."));
    CHECK(syscall(SYS_getdents64,duplicate,entry_buffer,24)==24);
    CHECK(!strcmp(((test_dirent*)entry_buffer)->name,".."));
    CHECK(close(fd)==0&&close(duplicate)==0);
    fprintf(stderr,"VSL filesystem ABI: raw/glibc getcwd, ERANGE/EFAULT, ISO children, partial reads, shared FD cursor, 24 open/read/close cycles verified\n");
    /* The file/attachment owns shared bytes even between mappings. */
    fd=open("/dev/shm/WK2SharedMemory.gles-lifetime",O_RDWR|O_CREAT|O_EXCL,0600);CHECK(fd>=0);
    CHECK(unlink("/dev/shm/WK2SharedMemory.gles-lifetime")==0&&ftruncate(fd,8192)==0);
    uint32_t *shared=mmap(NULL,8192,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);CHECK(shared!=MAP_FAILED);
    shared[0]=0xdead1234;shared[1024]=0xcafe5678;
    CHECK(munmap(shared,8192)==0);
    shared=mmap(NULL,8192,PROT_READ,MAP_SHARED,fd,0);CHECK(shared!=MAP_FAILED);
    CHECK(shared[0]==0xdead1234&&shared[1024]==0xcafe5678);
    uint32_t marker=0;CHECK(pread(fd,&marker,4,4096)==4&&marker==0xcafe5678);
    uint32_t *tail=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,4096);CHECK(tail!=MAP_FAILED);
    tail[0]=0x10203040;CHECK(shared[1024]==0x10203040);
    CHECK(lseek(fd,4096,SEEK_SET)==4096);marker=0x99887766;CHECK(write(fd,&marker,4)==4&&tail[0]==marker&&shared[1024]==marker);
    CHECK(munmap(tail,4096)==0&&munmap(shared,8192)==0);
    CHECK(ftruncate(fd,4096)==0&&ftruncate(fd,8192)==0);
    shared=mmap(NULL,8192,PROT_READ,MAP_SHARED,fd,0);CHECK(shared!=MAP_FAILED);
    CHECK(shared[0]==0xdead1234&&shared[1024]==0&&shared[2047]==0);
    CHECK(munmap(shared,8192)==0);
    CHECK(lseek(fd,4096,SEEK_SET)==4096&&write(fd,&marker,4)==4);
    int pair_fds[2];vex_message_pair_req_t pair={pair_fds,0,0};CHECK(vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&pair)==0);
    uint32_t payload=123;vex_message_attachment_req_t outgoing={(uint64_t)fd,VEX_MESSAGE_OBJECT_FD,VEX_MESSAGE_ATTACHMENT_DUP},incoming={0};
    vex_message_io_req_t send={pair_fds[0],0,&payload,4,&outgoing,1},receive={pair_fds[1],0,&payload,4,&incoming,1};
    receive.flags=MSG_DONTWAIT;
    CHECK((int64_t)vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&receive)==-EAGAIN);
    int saved_fd=receive.fd;receive.fd=-1;
    CHECK((int64_t)vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&receive)==-EBADF);receive.fd=saved_fd;
    CHECK(vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&send)==4);CHECK(close(fd)==0);
    CHECK(vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&receive)==4&&receive.attachment_count==1&&incoming.type==VEX_MESSAGE_OBJECT_FD);
    shared=mmap(NULL,8192,PROT_READ,MAP_SHARED,(int)incoming.handle,0);CHECK(shared!=MAP_FAILED);
    CHECK(close((int)incoming.handle)==0&&shared[0]==0xdead1234&&shared[1024]==marker);
    CHECK(munmap(shared,8192)==0&&close(pair_fds[0])==0&&close(pair_fds[1])==0);
    fprintf(stderr,"VSL shared file: gap without mappings, offset/length aliases, read/write coherence, queued attachment after producer FD close and last-map lifetime verified\n");
}
int main(void){
    fprintf(stderr,"EGL/GLES stage: enter main\n");
    vex_bulk_result_t filesystem_bulk={0};vex_bulk_req_t filesystem_req={VEX_BULK_MEMORY,&filesystem_bulk};
    filesystem_abi_test(vex_syscall1(VEX_SYS_BULK,(uintptr_t)&filesystem_req)==0);
    display=eglGetDisplay(EGL_DEFAULT_DISPLAY);CHECK(display!=EGL_NO_DISPLAY);CHECK(eglInitialize(display,NULL,NULL));EGLConfig config;EGLint count;
    EGLint choose[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_NONE};CHECK(eglChooseConfig(display,choose,&config,1,&count)&&count==1);
    EGLint conformant=-1;CHECK(eglGetConfigAttrib(display,config,EGL_CONFORMANT,&conformant)&&conformant==0);
    CHECK(eglCreateContext(display,config,EGL_NO_CONTEXT,NULL)==EGL_NO_CONTEXT);CHECK(eglGetError()==EGL_BAD_MATCH);
    EGLint attr[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE},size[]={EGL_WIDTH,32,EGL_HEIGHT,32,EGL_NONE};context=eglCreateContext(display,config,EGL_NO_CONTEXT,attr);CHECK(context!=EGL_NO_CONTEXT);surface=eglCreatePbufferSurface(display,config,size);CHECK(surface!=EGL_NO_SURFACE);
    CHECK(eglGetCurrentContext()==EGL_NO_CONTEXT);CHECK(eglMakeCurrent(display,surface,surface,context));CHECK(eglGetCurrentContext()==context);
    fprintf(stderr,"EGL/GLES stage: context current, thread ownership test\n");
    pthread_t thread;CHECK(pthread_create(&thread,NULL,other_thread,NULL)==0);void *result;CHECK(pthread_join(thread,&result)==0&&result==(void*)1);
    fprintf(stderr,"EGL/GLES stage: thread ownership verified\n");
    CHECK(glIsEnabled(GL_DITHER));glDisable(GL_DITHER);
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);CHECK(pixel(0,0)==0xff0000ff);
    glEnable(GL_SCISSOR_TEST);glScissor(4,5,2,3);glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);CHECK(pixel(4,5)==0xff00ff00);CHECK(pixel(3,5)==0xff0000ff);glDisable(GL_SCISSOR_TEST);
    const char *vertex="attribute vec3 pos;void main(){gl_Position=vec4(pos,1.);}";
    fprintf(stderr,"EGL/GLES stage: compiling actual GLSL\n");
    const char *fragment="precision highp float;uniform vec4 color;void main(){gl_FragColor=color;}";
    GLuint p=program(vertex,fragment);glUseProgram(p);GLint location=glGetUniformLocation(p,"color"),position=glGetAttribLocation(p,"pos");CHECK(location>=0&&position>=0);
    float vertices[]={-1,-1,0, 1,-1,0, -1,1,0, 1,1,0};GLuint buffer;glGenBuffers(1,&buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);glVertexAttribPointer(position,3,GL_FLOAT,GL_FALSE,0,NULL);glEnableVertexAttribArray(position);
    glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glUniform4f(location,0,0,1,1);glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==0);CHECK(pixel(8,8)==0xffff0000);CHECK(pixel(24,24)==0xffff0000);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glUniform4f(location,1,0,0,.5);glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==0);uint32_t mixed=pixel(16,16);CHECK((mixed&255)>=127&&(mixed&255)<=128);CHECK(((mixed>>16)&255)>=127&&((mixed>>16)&255)<=128);glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);glClearDepthf(1);glClear(GL_DEPTH_BUFFER_BIT);glUniform4f(location,0,1,0,1);glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(pixel(12,12)==0xff00ff00);
    for(int i=0;i<4;++i)vertices[i*3+2]=.75;glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(vertices),vertices);glUniform4f(location,1,0,0,1);glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(pixel(12,12)==0xff00ff00);glDisable(GL_DEPTH_TEST);
    glEnable(GL_STENCIL_TEST);glClearStencil(0);glClear(GL_STENCIL_BUFFER_BIT);glStencilFunc(GL_ALWAYS,7,255);glStencilOp(GL_KEEP,GL_KEEP,GL_REPLACE);glUniform4f(location,0,0,1,1);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    glStencilFunc(GL_EQUAL,3,255);glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);glUniform4f(location,1,0,0,1);glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(pixel(12,12)==0xffff0000);glDisable(GL_STENCIL_TEST);
    GLuint texture,fbo;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,32,32,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glClearColor(1,1,0,1);glClear(GL_COLOR_BUFFER_BIT);CHECK(pixel(10,10)==0xff00ffff);
    // Binding an attachment is NOT texture feedback unless the linked
    // program actually uses a sampler referring to that texture unit.
    glUniform4f(location,0,0,1,1);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    CHECK(glGetError()==0);CHECK(pixel(10,10)==0xffff0000);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    GLuint rgb_fbo,rgb_buffer;glGenFramebuffers(1,&rgb_fbo);glGenRenderbuffers(1,&rgb_buffer);
    CHECK(strstr((const char*)glGetString(GL_EXTENSIONS),"GL_OES_rgb8_rgba8")!=NULL);
    glBindFramebuffer(GL_FRAMEBUFFER,rgb_fbo);glBindRenderbuffer(GL_RENDERBUFFER,rgb_buffer);
    for(unsigned rgba=0;rgba<2;++rgba){
        glRenderbufferStorage(GL_RENDERBUFFER,rgba?GL_RGBA8_OES:GL_RGB8_OES,32,32);
        CHECK(glGetError()==0);GLint bits=-1;glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_RED_SIZE,&bits);CHECK(bits==8);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_ALPHA_SIZE,&bits);CHECK(bits==(rgba?8:0));
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rgb_buffer);
        CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
        glGetIntegerv(GL_ALPHA_BITS,&bits);CHECK(bits==(rgba?8:0));glGetIntegerv(GL_RED_BITS,&bits);CHECK(bits==8);
        glClearColor(1,0,0,0);glClear(GL_COLOR_BUFFER_BIT);CHECK(pixel(1,1)==(rgba?0x000000ffu:0xff0000ffu));
        glUseProgram(p);glUniform4f(location,0,0,1,.5f);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
        CHECK(glGetError()==0);CHECK(pixel(16,16)==(rgba?0x80ff0000u:0xffff0000u));
        glEnable(GL_SCISSOR_TEST);glScissor(1,1,1,1);glClearColor(0,1,0,0);glClear(GL_COLOR_BUFFER_BIT);
        CHECK(pixel(1,1)==(rgba?0x0000ff00u:0xff00ff00u));CHECK(pixel(16,16)==(rgba?0x80ff0000u:0xffff0000u));glDisable(GL_SCISSOR_TEST);
    }
    glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&rgb_fbo);glDeleteRenderbuffers(1,&rgb_buffer);
    GLuint textured=program("attribute vec3 pos;varying vec2 uv;void main(){gl_Position=vec4(pos,1.);uv=pos.xy*.5+.5;}","precision mediump float;varying vec2 uv;uniform sampler2D tex;void main(){gl_FragColor=texture2D(tex,uv);}");
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);glUseProgram(textured);glUniform1i(glGetUniformLocation(textured,"tex"),0);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==GL_INVALID_OPERATION);CHECK(pixel(10,10)==0xffff0000);
    glUseProgram(p);glUniform4f(location,0,0,1,1);glActiveTexture(GL_TEXTURE7);glBindTexture(GL_TEXTURE_2D,texture);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,0);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==0);CHECK(pixel(10,10)==0xffff0000);
    glActiveTexture(GL_TEXTURE7);glBindTexture(GL_TEXTURE_2D,0);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture);glBindFramebuffer(GL_FRAMEBUFFER,0);
    glUseProgram(textured);glUniform1i(glGetUniformLocation(textured,"tex"),0);position=glGetAttribLocation(textured,"pos");glVertexAttribPointer(position,3,GL_FLOAT,0,0,NULL);glEnableVertexAttribArray(position);glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==0);CHECK(pixel(8,8)==0xffff0000);
    glUseProgram(p);glBindTexture(GL_TEXTURE_2D,0);glVertexAttribPointer(glGetAttribLocation(p,"pos"),3,GL_FLOAT,0,0,NULL);uint64_t hash=0;
    for(unsigned frame=0;frame<64;++frame){if(!(frame%16))fprintf(stderr,"EGL/GLES stage: frame %u\n",frame);glUniform4f(location,frame/63.f,0,1-frame/63.f,1);glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==0);CHECK(eglSwapBuffers(display,surface));hash=hash*131+pixel(16,16);}CHECK(hash!=0);fprintf(stderr,"EGL/GLES 64 real programmable frames hash=%llx\n",(unsigned long long)hash);
    glDrawArrays(GL_TRIANGLES,100,3);CHECK(glGetError()==GL_INVALID_OPERATION);glBindBuffer(0xffff,buffer);CHECK(glGetError()==GL_INVALID_ENUM);
    // GLSL.std.450 Length: scalar and each vector width, including a
    // magnitude whose square overflows float32 although its length does not.
    const char *length_suffix[]={".x",".xy",".xyz",""};const float length_expected[]={3,5,13,85};
    for(unsigned width=0;width<4;++width){char fragment[256];snprintf(fragment,sizeof(fragment),
        "precision highp float;uniform vec4 data;uniform float scale;void main(){gl_FragColor=vec4(length(data%s)/scale,0.,0.,1.);}",length_suffix[width]);
        GLuint length_program=program("attribute vec3 pos;void main(){gl_Position=vec4(pos,1.);}",fragment);glUseProgram(length_program);
        glVertexAttribPointer(glGetAttribLocation(length_program,"pos"),3,GL_FLOAT,0,0,NULL);glEnableVertexAttribArray(glGetAttribLocation(length_program,"pos"));
        GLint data=glGetUniformLocation(length_program,"data"),scale=glGetUniformLocation(length_program,"scale");CHECK(data>=0&&scale>=0);
        glUniform4f(data,-3,4,12,84);glUniform1f(scale,length_expected[width]);glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==0);CHECK(pixel(16,16)==0xff0000ff);
        glUniform4f(data,0,0,0,0);glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==0);CHECK(pixel(16,16)==0xff000000);
        glUniform4f(data,-1e30f,0,0,0);glUniform1f(scale,1e30f);glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==0);CHECK(pixel(16,16)==0xff0000ff);
        glDeleteProgram(length_program);
    }
    GLuint common_math=program("attribute vec3 pos;void main(){gl_Position=vec4(pos,1.);}",
        "precision highp float;uniform vec4 data;void main(){gl_FragColor=vec4(fract(data.x),inversesqrt(data.y),step(data.z,data.w),smoothstep(0.,1.,data.x-1.));}");
    glUseProgram(common_math);position=glGetAttribLocation(common_math,"pos");glVertexAttribPointer(position,3,GL_FLOAT,0,0,NULL);glEnableVertexAttribArray(position);
    GLint math_data=glGetUniformLocation(common_math,"data");CHECK(math_data>=0);glUniform4f(math_data,1.5f,4.0f,.5f,.75f);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==0);CHECK(pixel(16,16)==0x80ff8080u);
    glUniform4f(math_data,-1.25f,4.0f,.5f,.75f);glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==0);CHECK(pixel(16,16)==0x00ff80bfu);glDeleteProgram(common_math);
    GLuint unsupported_v=shader(GL_VERTEX_SHADER,"attribute vec3 pos;void main(){gl_Position=vec4(pos,1.);}"),
        unsupported_f=shader(GL_FRAGMENT_SHADER,"precision highp float;uniform float angle;void main(){gl_FragColor=vec4(sin(angle));}"),unsupported_program=glCreateProgram();
    glAttachShader(unsupported_program,unsupported_v);glAttachShader(unsupported_program,unsupported_f);glLinkProgram(unsupported_program);
    GLint unsupported_link=1;glGetProgramiv(unsupported_program,GL_LINK_STATUS,&unsupported_link);CHECK(!unsupported_link);CHECK(glGetError()==0);
    char unsupported_log[256]={0};glGetProgramInfoLog(unsupported_program,sizeof(unsupported_log),NULL,unsupported_log);CHECK(strstr(unsupported_log,"rejects")!=NULL);
    glDeleteProgram(unsupported_program);glDeleteShader(unsupported_v);glDeleteShader(unsupported_f);
    GLuint composed=program("attribute vec3 pos;uniform mat4 transform;vec4 project(vec3 p){return transform*vec4(p,1.);}void main(){gl_Position=project(pos);}",
        "precision highp float;uniform vec4 colors[2];uniform int choice;vec4 shade(vec4 c){return c*.5+c*.5;}void main(){gl_FragColor=shade(colors[choice]);}");
    glUseProgram(composed);float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};glUniformMatrix4fv(glGetUniformLocation(composed,"transform"),1,GL_FALSE,identity);
    GLint colors=glGetUniformLocation(composed,"colors"),second=glGetUniformLocation(composed,"colors[1]"),choice=glGetUniformLocation(composed,"choice");CHECK(colors>=0&&second==colors+1&&choice>=0);
    glUniform4f(colors,1,0,0,1);glUniform4f(second,0,1,0,1);float read_uniform[4]={0};glGetUniformfv(composed,second,read_uniform);CHECK(read_uniform[0]==0&&read_uniform[1]==1&&read_uniform[3]==1);
    position=glGetAttribLocation(composed,"pos");glVertexAttribPointer(position,3,GL_FLOAT,0,0,NULL);glEnableVertexAttribArray(position);
    glUniform1i(choice,1);glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==0);CHECK(pixel(15,15)==0xff00ff00);
    glUniform1i(choice,0);glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==0);CHECK(pixel(15,15)==0xff0000ff);
    glEnable(GL_DITHER);glUniform4f(colors,.5,.5,.5,1);glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==0);
    uint32_t dither_a=pixel(4,4),dither_b=pixel(5,4);CHECK(dither_a!=dither_b&&(dither_a&255)>=127&&(dither_a&255)<=128&&(dither_b&255)>=127&&(dither_b&255)<=128);
    glDisable(GL_DITHER);glUniform4f(colors,1,0,0,1);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    const uint32_t checker[4]={0xff0000ff,0xff00ff00,0xffff0000,0xffffffff};
    glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,checker);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glUseProgram(textured);glUniform1i(glGetUniformLocation(textured,"tex"),0);glVertexAttribPointer(glGetAttribLocation(textured,"pos"),3,GL_FLOAT,0,0,NULL);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    CHECK(glGetError()==0);CHECK(pixel(2,2)==checker[0]);CHECK(pixel(29,2)==checker[1]);CHECK(pixel(2,29)==checker[2]);CHECK(pixel(29,29)==checker[3]);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==0);CHECK(pixel(16,16)==0xff878780);
    glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glViewport(0,0,1,1);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    CHECK(glGetError()==0);CHECK(pixel(0,0)==checker[3]);CHECK(pixel(1,0)==0xff000000);glViewport(0,0,32,32);
    GLuint biased=program("attribute vec3 pos;varying vec2 uv;void main(){gl_Position=vec4(pos,1.);uv=pos.xy*.5+.5;}",
        "precision highp float;varying vec2 uv;uniform sampler2D tex;uniform float bias;void main(){gl_FragColor=texture2D(tex,uv,bias);}");
    glUseProgram(biased);glUniform1i(glGetUniformLocation(biased,"tex"),0);GLint bias_location=glGetUniformLocation(biased,"bias");CHECK(bias_location>=0);
    glVertexAttribPointer(glGetAttribLocation(biased,"pos"),3,GL_FLOAT,0,0,NULL);glEnableVertexAttribArray(glGetAttribLocation(biased,"pos"));
    glUniform1f(bias_location,5);glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==0);CHECK(pixel(16,16)==checker[3]);
    glUniform1f(bias_location,-.475f);glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==0);CHECK(pixel(16,16)==0xff878780);
    glViewport(0,0,1,1);glUniform1f(bias_location,-2);glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==0);CHECK(pixel(0,0)==0xff808080);
    glUniform1f(bias_location,0);glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==0);CHECK(pixel(0,0)==checker[3]);
    glViewport(0,0,32,32);glDeleteProgram(biased);
    // The exact fragment form emitted by the real Skia run, including its
    // constant negative bias, must link and execute through the same core.
    GLuint skia_sample=program("attribute vec3 pos;varying highp vec2 vlocalCoord_S0;void main(){gl_Position=vec4(pos,1.);vlocalCoord_S0=pos.xy*.5+.5;}",
        "#version 100\nprecision mediump float;precision mediump sampler2D;uniform sampler2D uTextureSampler_0_S0;varying highp vec2 vlocalCoord_S0;void main(){mediump vec4 outputColor_S0=vec4(1.0);highp vec2 texCoord=vlocalCoord_S0;outputColor_S0=texture2D(uTextureSampler_0_S0,texCoord,-0.475);{gl_FragColor=outputColor_S0;}}");
    glUseProgram(skia_sample);glUniform1i(glGetUniformLocation(skia_sample,"uTextureSampler_0_S0"),0);glVertexAttribPointer(glGetAttribLocation(skia_sample,"pos"),3,GL_FLOAT,0,0,NULL);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==0);CHECK(pixel(16,16)==0xff878780);glDeleteProgram(skia_sample);
    GLuint mirrored=program("attribute vec3 pos;void main(){gl_Position=vec4(pos,1.);}",
        "precision highp float;uniform sampler2D tex;void main(){gl_FragColor=texture2D(tex,vec2(1.25,-.25));}");
    glUseProgram(mirrored);glUniform1i(glGetUniformLocation(mirrored,"tex"),0);glVertexAttribPointer(glGetAttribLocation(mirrored,"pos"),3,GL_FLOAT,0,0,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_MIRRORED_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_MIRRORED_REPEAT);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==0);CHECK(pixel(16,16)==checker[1]);glDeleteProgram(mirrored);
    glBindTexture(GL_TEXTURE_2D,0);glUseProgram(composed);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    GLuint derivatives=program("attribute vec3 pos;varying vec2 uv;void main(){gl_Position=vec4(pos,1.);uv=pos.xy*.5+.5;}",
        "#extension GL_OES_standard_derivatives : require\nprecision highp float;varying vec2 uv;void main(){gl_FragColor=vec4(dFdx(uv.x)*32.,dFdy(uv.y)*32.,fwidth(uv.x)*32.,1.);}");
    glUseProgram(derivatives);glVertexAttribPointer(glGetAttribLocation(derivatives,"pos"),3,GL_FLOAT,0,0,NULL);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    CHECK(glGetError()==0);CHECK(pixel(8,8)==0xffffffff);CHECK(pixel(24,24)==0xffffffff);glDeleteProgram(derivatives);
    glUseProgram(composed);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    GLuint conditional=program("attribute vec2 pos;varying float y;void main(){gl_Position=vec4(pos,0.,1.);y=pos.y*.5+.5;}",
        "precision highp float;varying float y;void main(){gl_FragColor=y<.5?vec4(1.,0.,0.,1.):vec4(0.,0.,1.,1.);}");
    glUseProgram(conditional);glVertexAttribPointer(glGetAttribLocation(conditional,"pos"),2,GL_FLOAT,0,12,NULL);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    CHECK(glGetError()==0);CHECK(pixel(1,1)==0xff0000ff);CHECK(pixel(31,31)==0xffff0000);glDeleteProgram(conditional);
    glUseProgram(composed);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    GLuint copied;glGenTextures(1,&copied);glBindTexture(GL_TEXTURE_2D,copied);glCopyTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,0,0,4,4,0);CHECK(glGetError()==0);
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);glCopyTexSubImage2D(GL_TEXTURE_2D,0,1,1,0,0,2,2);CHECK(glGetError()==0);
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,copied,0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);CHECK(pixel(0,0)==0xff0000ff);CHECK(pixel(1,1)==0xffff0000);CHECK(pixel(3,3)==0xff0000ff);
    glCopyTexSubImage2D(GL_TEXTURE_2D,0,3,3,0,0,2,2);CHECK(glGetError()==GL_INVALID_VALUE);
    glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteTextures(1,&copied);glDeleteProgram(composed);glUseProgram(p);
    fprintf(stderr,"EGL/GLES stage: shared context lifecycle\n");EGLContext shared=eglCreateContext(display,config,context,attr);CHECK(shared!=EGL_NO_CONTEXT);CHECK(eglMakeCurrent(display,surface,surface,shared));CHECK(glIsBuffer(buffer));glBindBuffer(GL_ARRAY_BUFFER,buffer);glDeleteBuffers(1,&buffer);CHECK(!glIsBuffer(buffer));CHECK(eglMakeCurrent(display,surface,surface,context));glUseProgram(p);glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==0); // retained attribute binding survives deletion in another context
    CHECK(eglDestroyContext(display,shared));glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);glDeleteProgram(textured);glDeleteProgram(p);
    CHECK(eglDestroyContext(display,context));CHECK(eglGetCurrentContext()==context);CHECK(eglDestroySurface(display,surface));CHECK(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));CHECK(!eglMakeCurrent(display,surface,surface,context));CHECK(eglGetError()==EGL_BAD_CONTEXT);
    fprintf(stderr,"EGL/GLES stage: contexts destroyed\n");
    EGLContext worker_context=eglCreateContext(display,config,EGL_NO_CONTEXT,attr);CHECK(worker_context!=EGL_NO_CONTEXT);
    CHECK(pthread_create(&thread,NULL,bind_until_exit,worker_context)==0);CHECK(pthread_join(thread,&result)==0&&result==(void*)1);
    CHECK(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,worker_context));CHECK(eglDestroyContext(display,worker_context));CHECK(eglReleaseThread());
    vex_bulk_result_t bulk={0};vex_bulk_req_t bulk_req={VEX_BULK_MEMORY,&bulk};
    if(vex_syscall1(VEX_SYS_BULK,(uintptr_t)&bulk_req)==0){
        vex_egl_window window={VEX_EGL_WINDOW_MAGIC,32,32,native_present,NULL,native_retain,native_release};window.owner=&window;
        EGLContext native_context=eglCreateContext(display,config,EGL_NO_CONTEXT,attr);CHECK(native_context!=EGL_NO_CONTEXT);
        EGLSurface native=eglCreateWindowSurface(display,config,(EGLNativeWindowType)(uintptr_t)&window,NULL);CHECK(native!=EGL_NO_SURFACE);
        CHECK(eglMakeCurrent(display,native,native,native_context));
        for(unsigned i=0;i<4;++i){glDisable(GL_SCISSOR_TEST);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glEnable(GL_SCISSOR_TEST);glScissor(0,16,32,16);glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);CHECK(pixel(1,1)==0xff0000ff);CHECK(pixel(1,31)==0xffff0000);CHECK(eglSwapBuffers(display,native));}
        CHECK(native_frames==4&&native_references==1);
        window.width=48;window.height=24;
        glDisable(GL_SCISSOR_TEST);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);glScissor(0,12,48,12);glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
        CHECK(pixel(47,23)==0xffff0000);CHECK(eglSwapBuffers(display,native));CHECK(native_frames==5&&native_references==1);
        glDisable(GL_SCISSOR_TEST);glDisable(GL_DITHER);glViewport(0,0,48,24);
        GLuint native_program=program("attribute vec2 pos;varying float y;void main(){gl_Position=vec4(pos,0.,1.);y=pos.y*.5+.5;}",
            "precision highp float;varying float y;void main(){gl_FragColor=y<.5?vec4(1.,0.,0.,1.):vec4(0.,0.,1.,1.);}");
        const float native_vertices[]={-1,-1,1,-1,-1,1,1,1};GLint native_position=glGetAttribLocation(native_program,"pos");
        glUseProgram(native_program);glVertexAttribPointer(native_position,2,GL_FLOAT,0,0,native_vertices);glEnableVertexAttribArray(native_position);
        glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==0);CHECK(pixel(1,1)==0xff0000ff);CHECK(pixel(47,23)==0xffff0000);
        CHECK(eglSwapBuffers(display,native));CHECK(native_frames==6&&native_references==1);glDeleteProgram(native_program);
        int endpoints[2];vex_message_pair_req_t pair={endpoints,0,0};CHECK(vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&pair)==0);
        for(unsigned compose=0;compose<2;++compose){
            uint32_t payload=0x12345678,received=0;uint32_t rights=VEX_SURFACE_RIGHT_QUERY|VEX_SURFACE_RIGHT_MAP_READ|VEX_SURFACE_RIGHT_PRESENT|(compose?VEX_SURFACE_RIGHT_COMPOSE:0);
            vex_message_attachment_req_t outgoing={native_surface_cap,VEX_MESSAGE_OBJECT_SURFACE,rights},incoming={0};
            vex_message_io_req_t send={endpoints[0],0,&payload,sizeof(payload),&outgoing,1};
            CHECK(vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&send)==sizeof(payload));
            vex_message_io_req_t receive={endpoints[1],0,&received,sizeof(received),&incoming,1};
            CHECK(vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&receive)==sizeof(received));
            CHECK(received==payload&&receive.attachment_count==1&&incoming.type==VEX_MESSAGE_OBJECT_SURFACE&&incoming.rights==rights&&incoming.handle!=native_surface_cap);
            vex_surface_query_req_t q={0};q.surface_handle=incoming.handle;CHECK(vex_syscall1(VEX_SYS_SURFACE_QUERY,(uintptr_t)&q)==0&&q.width==48&&q.height==24);
            vex_surface_map_req_t forbidden={0};forbidden.surface_handle=incoming.handle;forbidden.length=q.backing_size;forbidden.access=VEX_SURFACE_MAP_WRITE;
            CHECK((int64_t)vex_syscall1(VEX_SYS_SURFACE_MAP,(uintptr_t)&forbidden)<0);
            vex_gpu_surface_present_req_t present={0};present.surface_handle=incoming.handle;present.source=present.destination=(vex_rect_t){0,0,48,24};present.opacity=255;
            CHECK((int64_t)vex_syscall1(VEX_SYS_GPU_SURFACE_PRESENT,(uintptr_t)&present)==(compose?0:-1));
            CHECK(vex_syscall1(VEX_SYS_GPU_SURFACE_DESTROY,incoming.handle)==0);
        }
        CHECK(close(endpoints[0])==0&&close(endpoints[1])==0);
        // Renderer-like worker sends a capability and sleeps awaiting a plain
        // ACK. The owner process's main thread must not receive its wakeup.
        for(unsigned cycle=0;cycle<16;++cycle){
            CHECK(vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&pair)==0);
            native_ack_test exchange={endpoints[0],native_surface_cap,cycle+1};
            CHECK(pthread_create(&thread,NULL,native_frame_wait_ack,&exchange)==0);
            uint64_t frame[3]={0};vex_message_attachment_req_t incoming={0};
            vex_message_io_req_t receive={endpoints[1],0,frame,sizeof(frame),&incoming,1};
            CHECK(vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&receive)==sizeof(frame));
            CHECK(frame[1]==cycle+1&&receive.attachment_count==1&&incoming.type==VEX_MESSAGE_OBJECT_SURFACE);
            // Give the worker a scheduler turn to register its blocking receive.
            CHECK(vex_syscall0(VEX_SYS_THREAD_YIELD)==0);
            vex_message_io_req_t ack={endpoints[1],0,frame,sizeof(frame),NULL,0};
            CHECK(vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&ack)==sizeof(frame));
            CHECK(pthread_join(thread,&result)==0&&result==(void*)1);
            CHECK(vex_syscall1(VEX_SYS_GPU_SURFACE_DESTROY,incoming.handle)==0);
            CHECK(close(endpoints[0])==0&&close(endpoints[1])==0);
        }
        fprintf(stderr,"EGL/GLES native IPC: 16 worker frame/capability/plain ACK wakeups verified\n");
        CHECK(eglDestroySurface(display,native));CHECK(native_references==1);CHECK(eglDestroyContext(display,native_context));CHECK(eglReleaseThread());CHECK(native_references==0);
        fprintf(stderr,"EGL/GLES native Surface: 6 real presents, programmable draw/resize/completion/readback/orientation/lifetime verified\n");
    }
    CHECK(eglTerminate(display));fprintf(stderr,"EGL/GLES PASS %u checks; clear/triangle/texture/FBO/blend/depth/stencil/share/threads/lifetime\n",tests);return 0;
}
