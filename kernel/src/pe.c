/* PE policy/implementation lives in the native userspace runtime. This is
 * only executable-format dispatch: it never parses or rewrites a PE image. */
#include <unos/pe.h>
#include <unos/elf.h>
#include <unos/compat.h>
#include <stddef.h>
#define WINDOWS_RUNTIME "/usr/bin/kyil-windows"
static int arguments(const char *path,const char *const *argv,size_t argc,
                     const char **out) {
    if(!path || argc>126)return -1;
    out[0]=WINDOWS_RUNTIME;
    out[1]=path;
    for(size_t i=1;i<argc;i++)out[i+1]=argv[i];
    out[argc>0?argc+1:2]=NULL;
    return (int)(argc>0?argc+1:2);
}
int pe_load_args(const char *path,const char *const *argv,size_t argc,
                 const char *const *envp,size_t envc,int *status) {
    const char *args[128];int n=arguments(path,argv,argc,args);
    return n<0?-1:elf_run_native_runtime(WINDOWS_RUNTIME,args,(size_t)n,envp,envc,NULL,0,1,status,VEX_ABI_PE);
}
int pe_exec_args(const char *path,const char *const *argv,size_t argc,
                 const char *const *envp,size_t envc) {
    const char *args[128];int n=arguments(path,argv,argc,args);
    return n<0?-1:elf_exec_native_runtime(WINDOWS_RUNTIME,args,(size_t)n,envp,envc,VEX_ABI_PE);
}
int pe_load(const char *path) {return pe_load_args(path,NULL,0,NULL,0,NULL);}
int pe_spawn_pid_caps(const char *path,uint32_t *pid,uint32_t caps) {
    const char *args[]={WINDOWS_RUNTIME,path,NULL};
    return elf_run_native_runtime(WINDOWS_RUNTIME,args,2,NULL,0,pid,caps,0,NULL,VEX_ABI_PE);
}
int pe_spawn_pid(const char *path,uint32_t *pid) {return pe_spawn_pid_caps(path,pid,0);}
int pe_spawn(const char *path) {return pe_spawn_pid(path,NULL);}
