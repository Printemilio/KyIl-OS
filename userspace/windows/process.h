#ifndef KYIL_WINDOWS_PROCESS_H
#define KYIL_WINDOWS_PROCESS_H
#include "windows.h"
typedef struct {
    uint32_t size,pad;uint16_t *reserved,*desktop,*title;
    uint32_t x,y,xsize,ysize,xchars,ychars,fill,flags;
    uint16_t show,reserved_bytes;uint32_t pad2;void *reserved2,*input,*output,*error;
} wr_startup;
_Static_assert(sizeof(wr_startup)==104,"x64 STARTUPINFOW");
typedef struct {void *process,*thread;uint32_t pid,tid;} wr_process_info;
typedef struct {uint64_t handle;int32_t fd;uint32_t access,flags,overlapped;} wr_inherited_file;
#define WR_BOOT_MAX_FILES 64u
typedef struct wr_bootstrap {
    uint32_t magic,size,command_length,environment_length,cwd_length,image_length,file_count,reserved;
    uint32_t job_member,job_reserved;
    wr_startup startup;
    wr_inherited_file files[WR_BOOT_MAX_FILES];
    uint16_t image[512],cwd[512],command[32768],environment[32768];
} wr_bootstrap;
extern wr_bootstrap *wr_boot;
int wr_process_init(void);
int wr_process_capability(void *);
int wr_process_self_access(void *,unsigned);
unsigned wr_process_handle_kind(void *);
int wr_child_bootstrap(int);
void *wr_process_exports(const char *);
int wr_process_close(void *);
int wr_process_wait(void *,unsigned,unsigned *);
int wr_primary_resume(void *,unsigned *);
int wr_primary_exit_code(void *,unsigned *);
int wr_primary_id(void *,unsigned *);
void wr_process_quiesce(int);
int wr_file_export(void *,wr_inherited_file *,int);
int wr_files_inherit(wr_inherited_file *,unsigned *,int);
int wr_pipe_adopt(const wr_inherited_file *);
int wr_object_adopt(const wr_inherited_file *);
int wr_object_export(void *,wr_inherited_file *,int);
int wr_objects_inherit(wr_inherited_file *,unsigned *);
int wr_pipe_export(void *,wr_inherited_file *,unsigned *);
int wr_pipes_inherit(wr_inherited_file *,unsigned *);
int wr_pipe_flags(void *,unsigned,unsigned,unsigned *);
void *wr_pipe_duplicate(void *,int);
int wr_file_adopt(const wr_inherited_file *);
void wr_files_boot_stdio(void *,void *,void *);
int wr_file_flags(void *,unsigned,unsigned,unsigned *);
int wr_namespace_adopt(int,int,const uint16_t *,const uint16_t *);
int wr_application_root(void);
int wr_system_root(void);
int wr_read_fd(int,uint8_t **,size_t *);
int wr_namespace_open_native(const char *,unsigned);
int wr_namespace_set_cwd(const uint16_t *);
int wr_cwd_resource(const uint16_t *,uint16_t *,size_t);
int wr_namespace_image(const uint16_t *,uint16_t *,size_t,char *,size_t);
int wr_profile_folder(unsigned,uint16_t *,size_t);
const uint16_t *wr_environment_block(void);
int wr_environment_snapshot(uint16_t *,size_t);
void wr_environment_lock(int);
int wr_environment_replace(const uint16_t *,size_t);
void *wr_environment_exports(const char *);
void wr_environment_quiesce(int);
int wr_process_adopt(const wr_inherited_file *);
void *wr_file_duplicate(void *,int);
int wr_utf8(const uint16_t *,char *,size_t);
#endif
