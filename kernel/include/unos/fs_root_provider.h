#ifndef KYIL_FS_ROOT_PROVIDER_H
#define KYIL_FS_ROOT_PROVIDER_H
#include <unos/vex_fs_root_abi.h>
#include <unos/vex_file_abi.h>
int vex_fs_root_create_fd(const char *,unsigned);
int vex_fs_root_query(int,vex_fs_root_info_t *);
int vex_fs_root_derive(int,const char *,unsigned);
int vex_fs_root_open(int,const char *,unsigned,unsigned);
int vex_fs_root_mkdir(int,const char *,unsigned);
int vex_fs_root_stat(int,const char *,vex_stat_ex_t *);
int vex_fs_root_remove(int,const char *,unsigned);
int vex_fs_root_move(int,const char *,const char *,const char *,int);
int vex_fs_root_mode(int,const char *,unsigned,unsigned);
unsigned vex_fs_root_borrow_rights(void *);
int vex_fd_transfer_allowed(void *);
struct ora_view;
int vex_fd_executable_snapshot(void *,struct ora_view *);
int vex_fd_install_prepared(uint32_t,void *,int,uint8_t);
#endif
