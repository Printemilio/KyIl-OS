#ifndef KYIL_FS_ROOT_H
#define KYIL_FS_ROOT_H
#include <stddef.h>
#include <stdint.h>
#include <unos/kernel_guard.h>
#define FS_ROOT_READ 1u
#define FS_ROOT_WRITE 2u
#define FS_ROOT_CREATE 4u
#define FS_ROOT_ENUM 8u
#define FS_ROOT_REMOVE 16u
#define FS_ROOT_RENAME 32u
#define FS_ROOT_EXEC 64u
#define FS_ROOT_TRANSFER 128u
#define FS_ROOT_ALL 255u
typedef struct fs_root fs_root_t;
typedef int (*fs_root_operation_t)(const char *,void *);
/* A live root is a namespace anchor. Its own/ancestor names cannot be removed
 * or rebound while referenced; descendants can still be modified normally.
 * No path-derived identifier is an authority and no disk format changes. */
fs_root_t *fs_root_create(const char *,unsigned,int *);
fs_root_t *fs_root_retain(fs_root_t *);
void fs_root_release(fs_root_t *);
uint64_t fs_root_identity(fs_root_t *);
int fs_root_path(fs_root_t *,char *,size_t);
int fs_root_invoke(fs_root_t *,unsigned,uint32_t,uint32_t,const char *,fs_root_operation_t,void *);
/* Only inside an active root callback; same namespace lock and authority. */
int fs_root_resolve_relative(fs_root_t *,const char *,char *,size_t,int);
uint32_t fs_root_credential_uid(uint32_t);
uint32_t fs_root_credential_caps(uint32_t);
int fs_root_active(fs_root_t *);
fs_root_t *fs_root_current(void);
unsigned fs_root_current_rights(void);
int fs_root_relative(fs_root_t *,const char *,char *,size_t);
int fs_root_path_pinned(const char *);
int fs_root_scope_allows(const char *,unsigned);
kernel_guard_t fs_namespace_begin(void);
void fs_namespace_end(kernel_guard_t *);
#endif
