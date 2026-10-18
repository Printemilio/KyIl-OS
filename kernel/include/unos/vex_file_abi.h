#ifndef VEX_FILE_ABI_H
#define VEX_FILE_ABI_H

#include <stdint.h>

/* Native file metadata. VEX_SYS_STAT reports only the legacy subset (mode,
 * owner, size); VEX_SYS_STAT_EX reports everything VexFS knows about a file,
 * including its identity and timestamps. Returns 0 or a negated errno. */
#define VEX_SYS_STAT_EX 207ull

#define VEX_STAT_EX_PATH   0u /* follow a final symbolic link */
#define VEX_STAT_EX_LPATH  1u /* report the link itself */
#define VEX_STAT_EX_FD     2u
#define VEX_STAT_EX_OBJECT 3u /* FD metadata with stable backing identity; no path-hash hint */

#define VEX_STAT_EX_HAS_ATIME (1u << 0)
#define VEX_STAT_EX_HAS_MTIME (1u << 1)
#define VEX_STAT_EX_HAS_CTIME (1u << 2)

typedef struct vex_stat_ex {
    uint64_t dev;
    uint64_t ino;
    uint32_t mode;      /* S_IF* type bits | permission bits */
    uint32_t nlink;
    uint32_t uid;
    uint32_t gid;
    uint64_t rdev;
    int64_t size;
    int64_t blksize;
    int64_t blocks;     /* 512-byte units */
    int64_t atime_sec;
    int64_t mtime_sec;
    int64_t ctime_sec;
    uint32_t atime_nsec;
    uint32_t mtime_nsec;
    uint32_t ctime_nsec;
    uint32_t valid;     /* VEX_STAT_EX_HAS_* */
} vex_stat_ex_t;

/* Open `path` relative to `dirfd` (VEX_AT_FDCWD: the working directory).
 * With O_CREAT the new file is created immediately with `mode` (the caller
 * applies its umask); O_EXCL fails with -EEXIST when the name exists.
 * Returns the new descriptor or a negated errno. */
#define VEX_SYS_OPEN_EX 208ull
#define VEX_AT_FDCWD (-100)

typedef struct vex_open_ex_req {
    int32_t dirfd;
    uint32_t flags;
    uint32_t mode;
    uint32_t reserved;
    const char *path;
} vex_open_ex_req_t;

/* Resolve `path` against the directory open as `dirfd` (VEX_AT_FDCWD: the
 * working directory) into an absolute, normalized path written to `buffer`.
 * Returns the length (without NUL) or a negated errno. */
#define VEX_SYS_RESOLVE_AT 209ull

typedef struct vex_resolve_at_req {
    int32_t dirfd;
    uint32_t reserved;
    const char *path;
    char *buffer;
    uint64_t size;
} vex_resolve_at_req_t;

/* Read directory entries from a directory descriptor, continuing where the
 * previous call stopped. Fills `buffer` with vex_dirent_t records and
 * returns the number of bytes used, 0 at the end, or a negated errno. */
#define VEX_SYS_GETDENTS 210ull

typedef struct vex_dirent {
    uint64_t ino;
    int64_t next;       /* position after this entry */
    uint16_t reclen;    /* size of this record, 8-byte aligned */
    uint8_t type;       /* VEX_DT_* */
    char name[];        /* NUL-terminated */
} vex_dirent_t;

#define VEX_DT_UNKNOWN 0u
#define VEX_DT_DIR     4u
#define VEX_DT_REG     8u
#define VEX_DT_LNK     10u

typedef struct vex_getdents_req {
    int32_t fd;
    uint32_t reserved;
    void *buffer;
    uint64_t size;
} vex_getdents_req_t;

typedef struct vex_stat_ex_req {
    uint32_t kind;      /* VEX_STAT_EX_* */
    int32_t fd;
    const char *path;
    vex_stat_ex_t *out;
} vex_stat_ex_req_t;

/* Set the access and/or modification time of a file, named by an absolute
 * `path` or, when `path` is NULL, by the descriptor `fd`. Only the times
 * whose VEX_STAT_EX_HAS_ATIME/MTIME bit is set in `valid` change; the
 * change time becomes now. Only the owner (or an administrator) may do
 * this. Returns 0 or a negated errno. */
#define VEX_SYS_SET_TIMES 217ull
#define VEX_SET_TIMES_NOFOLLOW 1u

typedef struct vex_set_times_req {
    int32_t fd;
    uint32_t flags;     /* VEX_SET_TIMES_* */
    const char *path;
    int64_t seconds[2]; /* access, modification */
    uint32_t nanoseconds[2];
    uint32_t valid;
    uint32_t reserved;
} vex_set_times_req_t;

/* Change the permission bits of an open file. Returns 0 or a negated
 * errno. */
#define VEX_SYS_FCHMOD 218ull
#define VEX_SYS_USER_IDENTITY 224ull
/* Calling process identity from the native user database, not environment.
 * No foreign UID selection. Home is the existing native /home/<name> path. */
typedef struct vex_user_identity {
    uint32_t size,uid,capabilities,reserved;
    char name[32];
    char home[512];
} vex_user_identity_t;

typedef struct vex_fchmod_req {
    int32_t fd;
    uint32_t mode;
} vex_fchmod_req_t;

#endif
