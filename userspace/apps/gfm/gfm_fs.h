#ifndef GFM_FS_H
#define GFM_FS_H

/*
 * File model and operations of GFM (Graphical File Manager).
 *
 * No UI code lives here. Everything goes through the ordinary file API (see
 * gfm_sys.h); there is no private storage and no cached view of the file
 * system beyond one directory listing that the caller refreshes at will.
 *
 * Vex-specific limits the operations respect:
 *   - paths are at most 127 bytes (VexFS path contract);
 *   - a directory cannot be renamed in place, so renaming or moving one is a
 *     copy of the tree followed by removal of the original;
 *   - removing a directory removes its whole tree.
 */

#include <stddef.h>
#include <stdint.h>

#include "gfm_sys.h"

#define GFM_PATH_MAX 128u
#define GFM_NAME_LIMIT 64u        /* longest name GFM lets a user create */
#define GFM_MAX_ENTRIES 128u
#define GFM_COPY_MAX_DEPTH 8u

typedef enum gfm_status {
    GFM_OK = 0,
    GFM_ERR_INVALID_NAME,
    GFM_ERR_INVALID_PATH,
    GFM_ERR_EXISTS,
    GFM_ERR_NOT_FOUND,
    GFM_ERR_NOT_DIRECTORY,
    GFM_ERR_TOO_LONG,
    GFM_ERR_INTO_ITSELF,
    GFM_ERR_SAME_LOCATION,
    GFM_ERR_TOO_DEEP,
    GFM_ERR_IO
} gfm_status_t;

const char *gfm_status_text(gfm_status_t status);

typedef struct gfm_entry {
    char name[GFM_SYS_NAME_MAX];
    uint8_t type;             /* GFM_TYPE_* */
    uint64_t size;            /* bytes; 0 for directories */
    uint32_t mode;            /* permission bits */
} gfm_entry_t;

typedef struct gfm_listing {
    gfm_entry_t entries[GFM_MAX_ENTRIES];
    size_t count;
    int truncated;            /* the directory holds more than GFM_MAX_ENTRIES */
    uint32_t fingerprint;     /* names and types; changes when entries change */
} gfm_listing_t;

/* ── paths and names ─────────────────────────────────────────────────────── */

/* Resolve `input` (absolute, or relative to `cwd`) to a canonical absolute
 * path: no "//", ".", ".."; at most GFM_PATH_MAX - 1 bytes. */
gfm_status_t gfm_path_resolve(const char *cwd, const char *input, char *out, size_t capacity);
gfm_status_t gfm_path_join(const char *directory, const char *name, char *out, size_t capacity);
void gfm_path_parent(const char *path, char *out, size_t capacity);
const char *gfm_path_basename(const char *path);
/* True when `path` is `ancestor` itself or lies below it. */
int gfm_path_is_within(const char *ancestor, const char *path);

/* Trim surrounding spaces in place, then check the name is usable. */
gfm_status_t gfm_name_prepare(char *name);

/* ── reading ─────────────────────────────────────────────────────────────── */

/* Fill `listing` with the directory's children: folders first, then files,
 * each group sorted by name ignoring case. */
gfm_status_t gfm_list(const char *directory, gfm_listing_t *listing);
gfm_status_t gfm_stat(const char *path, gfm_entry_t *entry);
/* Number of entries below a directory (recursive). */
gfm_status_t gfm_count_descendants(const char *path, size_t *count);
/* True when the first bytes of a file are not text. */
int gfm_looks_binary(const char *path);
int gfm_name_exists(const char *directory, const char *name);

/* ── changing ────────────────────────────────────────────────────────────── */

gfm_status_t gfm_make_directory(const char *directory, const char *name, char *created, size_t capacity);
gfm_status_t gfm_rename(const char *path, const char *new_name, char *renamed, size_t capacity);
gfm_status_t gfm_delete(const char *path);
/* Copies into `destination_directory` under a free name ("x", "x copy",
 * "x copy 2", ...). Directories are copied recursively. */
gfm_status_t gfm_copy(const char *path, const char *destination_directory, char *created, size_t capacity);
/* Moves under the same name; refuses to replace an existing entry. */
gfm_status_t gfm_move(const char *path, const char *destination_directory, char *moved, size_t capacity);
gfm_status_t gfm_set_executable(const char *path, int executable);

/* Human text for a size: "512 B", "1.5 KB", "3.2 MB". */
void gfm_format_size(uint64_t bytes, char *out, size_t capacity);
/* "rwxr-xr-x" */
void gfm_format_mode(uint32_t mode, char *out, size_t capacity);
/* In-progress browser downloads carry a transient suffix. */
int gfm_is_transient_download(const char *name);

#endif
