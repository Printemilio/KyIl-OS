/* Opt-in, transparent FreeType diagnostics. No font/result substitution. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SIZES_H
#include FT_TRUETYPE_TABLES_H
#include <math.h>
#include <stdint.h>
#include <string.h>
static void report(const char *op,FT_Face f,int result){static unsigned count;if(count++<48 && f)fprintf(stderr,"font-trace: %s result=%d family=%s units=%u asc=%d desc=%d height=%d scaled=%ld/%ld/%ld\n",op,result,f->family_name?f->family_name:"?",f->units_per_EM,f->ascender,f->descender,f->height,f->size?f->size->metrics.ascender:0,f->size?f->size->metrics.descender:0,f->size?f->size->metrics.height:0);}
FT_Error FT_Set_Char_Size(FT_Face f,FT_F26Dot6 w,FT_F26Dot6 h,FT_UInt x,FT_UInt y){static FT_Error(*real)(FT_Face,FT_F26Dot6,FT_F26Dot6,FT_UInt,FT_UInt);if(!real)real=dlsym(RTLD_NEXT,"FT_Set_Char_Size");int r=real(f,w,h,x,y);report("char-size",f,r);return r;}
FT_Error FT_Set_Pixel_Sizes(FT_Face f,FT_UInt w,FT_UInt h){static FT_Error(*real)(FT_Face,FT_UInt,FT_UInt);if(!real)real=dlsym(RTLD_NEXT,"FT_Set_Pixel_Sizes");int r=real(f,w,h);report("pixel-size",f,r);return r;}
FT_Error FT_Request_Size(FT_Face f,FT_Size_Request q){static FT_Error(*real)(FT_Face,FT_Size_Request);if(!real)real=dlsym(RTLD_NEXT,"FT_Request_Size");int r=real(f,q);report("request-size",f,r);return r;}
void *FT_Get_Sfnt_Table(FT_Face f,FT_Sfnt_Tag tag){
    static void *(*real)(FT_Face,FT_Sfnt_Tag);if(!real)real=dlsym(RTLD_NEXT,"FT_Get_Sfnt_Table");
    void *p=real(f,tag);static unsigned count;
    if(count++<128){
        if(tag==ft_sfnt_os2 && p){TT_OS2 *t=p;fprintf(stderr,"font-trace: OS2 avg=%d asc=%d desc=%d gap=%d xheight=%d\n",t->xAvgCharWidth,t->sTypoAscender,t->sTypoDescender,t->sTypoLineGap,t->sxHeight);}
        else if(tag==ft_sfnt_hhea && p){TT_HoriHeader *t=p;fprintf(stderr,"font-trace: hhea asc=%d desc=%d gap=%d advance=%u\n",t->Ascender,t->Descender,t->Line_Gap,t->advance_Width_Max);}
        else fprintf(stderr,"font-trace: table=%d present=%d\n",tag,p!=NULL);
    }return p;
}
FT_Error FT_Activate_Size(FT_Size s){static FT_Error(*real)(FT_Size);if(!real)real=dlsym(RTLD_NEXT,"FT_Activate_Size");int r=real(s);if(r)report("activate-error",s?s->face:NULL,r);return r;}
float roundf(float v){static float(*real)(float);if(!real)real=dlsym(RTLD_NEXT,"roundf");float r=real(v);static unsigned n;if(n++<48){Dl_info info={0};void *caller=__builtin_return_address(0);dladdr(caller,&info);fprintf(stderr,"font-trace: round %g -> %g caller=%p base=%p\n",(double)v,(double)r,caller,info.dli_fbase);}return r;}
/* This opt-in probe observes the actual caller's metrics without changing
 * its instructions or data. Offsets are for the symbolized staged build. */
long lroundf(float v){
    static long(*real)(float);if(!real)real=dlsym(RTLD_NEXT,"lroundf");
    long r=real(v);Dl_info info={0};void *caller=__builtin_return_address(0);dladdr(caller,&info);
    uintptr_t off=(uintptr_t)caller-(uintptr_t)info.dli_fbase;
    static unsigned n;
    if(info.dli_fname && strstr(info.dli_fname,"libWPEWebKit") && off>=0x5e3a970 && off<0x5e3ad24 && n++<48){
        const unsigned char *base=info.dli_fbase;
        fprintf(stderr,"font-trace: round constants half=%a abs=%016lx limit=%a sign=%08x\n",*(const double *)(base+0x1a70fa0),*(const unsigned long *)(base+0x1a74450),*(const double *)(base+0x1a6fb50),*(const unsigned *)(base+0x1a717f0));
        const float *m=(const void *)((const char *)__builtin_frame_address(0)+16+0x20);
        fprintf(stderr,"font-trace: Font::platformInit call=%lx lround=%g -> %ld metrics ascent=%g descent=%g lead=%g avg=%g max=%g xheight=%g\n",(unsigned long)off,(double)v,r,(double)m[2],(double)m[3],(double)m[5],(double)m[6],(double)m[7],(double)m[11]);
    }return r;
}

FT_Error FT_Open_Face(FT_Library lib,const FT_Open_Args *args,FT_Long index,FT_Face *face){
    static FT_Error(*real)(FT_Library,const FT_Open_Args *,FT_Long,FT_Face *);if(!real)real=dlsym(RTLD_NEXT,"FT_Open_Face");
    int r=real(lib,args,index,face);static unsigned n;if(n++<48)fprintf(stderr,"font-trace: OpenFace flags=%u index=%ld result=%d path=%s family=%s\n",args->flags,index,r,args->flags&FT_OPEN_PATHNAME?args->pathname:"stream",!r&&*face?(*face)->family_name:"?");return r;
}
FT_Error FT_New_Memory_Face(FT_Library lib,const FT_Byte *bytes,FT_Long size,FT_Long index,FT_Face *face){
    static FT_Error(*real)(FT_Library,const FT_Byte *,FT_Long,FT_Long,FT_Face *);if(!real)real=dlsym(RTLD_NEXT,"FT_New_Memory_Face");
    int r=real(lib,bytes,size,index,face);static unsigned n;if(n++<48)fprintf(stderr,"font-trace: MemoryFace size=%ld index=%ld result=%d family=%s\n",size,index,r,!r&&*face?(*face)->family_name:"?");return r;
}

#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
typedef struct _FcConfig FcConfig;
typedef struct _FcPattern FcPattern;
typedef unsigned char FcChar8;
typedef int FcResult;
#define FC_FAMILY "family"
#define FC_FILE "file"
FILE *fopen(const char *path,const char *mode){
    static FILE *(*real)(const char *,const char *);if(!real)real=dlsym(RTLD_NEXT,"fopen");
    FILE *f=real(path,mode);int saved=errno;static unsigned n;
    if(strstr(path,".ttf")&&n++<48){struct stat st={0};int sr=f?fstat(fileno(f),&st):-1;fprintf(stderr,"font-trace: fopen path=%s result=%p errno=%d stat=%d size=%lld\n",path,f,saved,sr,(long long)st.st_size);}
    errno=saved;return f;
}
FcPattern *FcFontMatch(FcConfig *config,FcPattern *pattern,FcResult *result){
    static FcPattern *(*real)(FcConfig *,FcPattern *,FcResult *);if(!real)real=dlsym(RTLD_NEXT,"FcFontMatch");
    FcPattern *p=real(config,pattern,result);int saved=errno;static unsigned n;
    if(n++<32){FcResult(*get)(const FcPattern *,const char *,int,FcChar8 **)=dlsym(RTLD_NEXT,"FcPatternGetString");FcChar8 *family=NULL,*file=NULL;get(pattern,FC_FAMILY,0,&family);if(p)get(p,FC_FILE,0,&file);fprintf(stderr,"font-trace: FcFontMatch family=%s file=%s result=%d\n",family? (char *)family:"?",file?(char *)file:"?",*result);}
    errno=saved;return p;
}
__attribute__((constructor)) static void file_contract(void){
    const char *paths[]={"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf","/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"};
    for(unsigned i=0;i<2;i++){struct stat a={0},b={0};int st=stat(paths[i],&a);FILE *f=fopen(paths[i],"rb");if(!f){fprintf(stderr,"font-trace: contract stat=%d errno=%d\n",st,errno);continue;}
        int fd=fileno(f);int fst=fstat(fd,&b);unsigned char h[4]={0};size_t n=fread(h,1,4,f);int seek=fseek(f,0,SEEK_END);long size=ftell(f);
        void *m=mmap(NULL,b.st_size,PROT_READ,MAP_PRIVATE,fd,0);int err=errno;
        fprintf(stderr,"font-trace: contract path=%s stat=%d/%lld fstat=%d/%lld fread=%zu header=%02x%02x%02x%02x seek=%d ftell=%ld mmap=%p errno=%d\n",paths[i],st,(long long)a.st_size,fst,(long long)b.st_size,n,h[0],h[1],h[2],h[3],seek,size,m,err);
        if(m!=MAP_FAILED){unsigned char *p=m;fprintf(stderr,"font-trace: mapped header=%02x%02x%02x%02x\n",p[0],p[1],p[2],p[3]);munmap(m,b.st_size);}fclose(f);
    }
}
