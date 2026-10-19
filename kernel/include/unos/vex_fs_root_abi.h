#ifndef VEX_FS_ROOT_ABI_H
#define VEX_FS_ROOT_ABI_H
#include <stdint.h>
#define VEX_SYS_FS_ROOT 225ull
#define VEX_FS_ROOT_CREATE 1u
#define VEX_FS_ROOT_QUERY 2u
#define VEX_FS_ROOT_DERIVE 3u
#define VEX_FS_ROOT_OPEN 4u
#define VEX_FS_ROOT_MKDIR 5u
#define VEX_FS_ROOT_STAT 6u
#define VEX_FS_ROOT_SEAL 7u
#define VEX_FS_ROOT_POLICY 8u
#define VEX_FS_ROOT_REMOVE 9u
#define VEX_FS_ROOT_MOVE 10u
#define VEX_FS_ROOT_REPLACE 11u
#define VEX_FS_ROOT_MODE 12u
/* MOVE/REPLACE: path is the source, out points to a copied input record of
 * relative destination/optional backup names in the same root. REPLACE
 * preserves destination ownership/mode/ACL. No outstanding file references
 * may exist at affected names. MODE applies flags as a permission-bit mask
 * and mode as its value, atomically; both are limited to 0777. */
typedef struct vex_fs_root_names { const char *destination,*backup; } vex_fs_root_names_t;
#define VEX_FS_REMOVE_DIRECTORY 1u
/* REMOVE is confined and atomic with pathname resolution. Regular files with
 * any outstanding file-object reference (including mappings/IPC) return EBUSY:
 * this operation does not promise delete-pending semantics. */
#define VEX_FS_READ 1u
#define VEX_FS_WRITE 2u
#define VEX_FS_CREATE 4u
#define VEX_FS_ENUM 8u
#define VEX_FS_REMOVE 16u
#define VEX_FS_RENAME 32u
#define VEX_FS_EXEC 64u
#define VEX_FS_TRANSFER 128u
#define VEX_FS_ALL 255u
#define VEX_FS_OPEN_REGULAR 0x10000000u /* reject directories atomically */
#define VEX_FS_OPEN_EXECUTABLE 0x20000000u /* readonly immutable executable authority */
#define VEX_FS_OPEN_PROTECTED 0x40000000u /* READONLY existing regular file; native name protection until last object/map/IPC ref. No read/write sharing lease. */
/* Native root capability = process-local FD to a typed, refcounted object.
 * CREATE: absolute native path + rights; returns new FD. DERIVE: parent FD,
 * relative subdirectory (empty = same anchor) + attenuated rights; returns FD.
 * OPEN: relative path, flags=O_*, mode=create permissions; returns file FD.
 * QUERY/STAT: writes info/stat_ex to out. MKDIR: relative path + mode.
 * SEAL: irreversible for this process group; unscoped paths are denied,
 * including after fork/exec. Existing resource capabilities remain valid.
 * POLICY: returns 0/1. Reserved/irrelevant fields must be zero.
 * Native numbering/wire tag is independent of legacy and foreign syscalls. */
typedef struct vex_fs_root_req {
    uint32_t size,operation;
    int32_t fd;
    uint32_t rights;
    const char *path;
    uint32_t flags,mode;
    void *out;
    uint64_t reserved;
} vex_fs_root_req_t;
typedef struct vex_fs_root_info {
    uint64_t identity;
    uint32_t rights,grant_uid;
    char path[512];
} vex_fs_root_info_t;
#endif
