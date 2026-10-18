#include "windows.h"
#include "process.h"
#include <unos/vex_abi.h>
#include "command_parse.h"
/* A Shell32 userspace contract, not a native shell or process parser. */
static uint16_t **WR_WIN CommandLineToArgvW(const uint16_t *source,int *argc){
    if(!source || !argc){wr_current_teb()->last_error=87;return NULL;}
    uint16_t *snapshot=wr_native_alloc(65536);if(!snapshot){wr_current_teb()->last_error=8;return NULL;}
    size_t bytes=0;unsigned error=0,found=0;
    while(bytes<65536 && !found){uintptr_t at=(uintptr_t)source;
        if(at>UINTPTR_MAX-bytes){error=998;break;}at+=bytes;
        size_t chunk=4096-(at&4095);if(chunk>65536-bytes)chunk=65536-bytes;
        if(wr_memory_copy((uint8_t *)snapshot+bytes,(const void *)at,chunk)){error=998;break;}
        size_t end=bytes+chunk;for(size_t i=bytes/2;i<end/2;i++)if(!snapshot[i]){found=1;break;}bytes=end;
    }
    if(!error && !found)error=206;
    if(!error && !snapshot[0]){unsigned (WR_WIN *name)(void *,uint16_t *,unsigned)=wr_module_exports("GetModuleFileNameW");
        unsigned n=name(NULL,snapshot,32768);if(!n || n>=32768)error=206;}
    uint16_t **argv=NULL;size_t count=0,units=0;
    void *(WR_WIN *alloc)(unsigned,size_t)=wr_heap_exports("LocalAlloc");
    void *(WR_WIN *free_block)(void *)=wr_heap_exports("LocalFree");
    if(!error){wr_command_parse(snapshot,NULL,NULL,&count,&units);
        if(count>INT32_MAX || count>SIZE_MAX/sizeof(*argv)-1 || units>(SIZE_MAX-(count+1)*sizeof(*argv))/2)error=8;
        else {argv=alloc(0,(count+1)*sizeof(*argv)+units*2);if(!argv)error=8;}}
    if(!error){wr_command_parse(snapshot,argv,(uint16_t *)(argv+count+1),&count,&units);int value=(int)count;
        if(wr_memory_copy(argc,&value,sizeof(value)))error=998;}
    wr_native_free(snapshot);if(error){if(argv)free_block(argv);wr_current_teb()->last_error=error;return NULL;}return argv;
}
static uint32_t WR_WIN SHGetFolderPathW(void *owner,int folder,void *token,unsigned kind,uint16_t *out){
    (void)owner;uint16_t path[512];unsigned error=0;
    if(!out || kind>1 || ((unsigned)folder&~0xd0ffu))error=87;
    else if(token)error=50;
    else error=(unsigned)wr_profile_folder((unsigned)folder&255,path,512);
    size_t n=0;if(!error){while(path[n])n++;if(n>=260)error=206;}
    if(!error && !(folder&0x4000)){
        vex_stat_ex_t st;int r=wr_namespace_call(VEX_FS_ROOT_STAT,path,0,0,&st,0);
        if(r && (folder&0x8000)){
            for(size_t i=3;i<=n;i++)if(path[i]=='\\' || !path[i]){uint16_t saved=path[i];path[i]=0;
                r=wr_namespace_call(VEX_FS_ROOT_STAT,path,0,0,&st,0);
                if(r==-2)r=wr_namespace_call(VEX_FS_ROOT_MKDIR,path,0,0700,NULL,1);
                path[i]=saved;if(r)break;}
            if(!r)r=wr_namespace_call(VEX_FS_ROOT_STAT,path,0,0,&st,0);
        }
        if(r)error=r==-2?3:wr_file_error(-r);else if((st.mode&0170000)!=0040000)error=267;
    }
    if(!error && wr_memory_copy(out,path,(n+1)*2))error=998;
    if(error){uint16_t zero=0;(void)wr_memory_copy(out,&zero,2);return 0x80070000u|error;}return 0;
}
void *wr_shell_exports(const char *name){
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(CommandLineToArgvW);
    E(SHGetFolderPathW);
#undef E
    return NULL;
}
