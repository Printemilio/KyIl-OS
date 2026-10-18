#include "gfm_fs.h"

/* Self-contained on purpose: the native Vex runtime has no full libc. */

#define COPY_BUFFER_SIZE 16384u
#define BINARY_PROBE_SIZE 512u
#define TRANSIENT_PART ".vexpart"
#define TRANSIENT_WEBKIT ".wkdownload"

/* ── small text helpers ──────────────────────────────────────────────────── */

static size_t text_length(const char *text) {
    size_t length = 0u;

    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static void copy_text(char *destination, size_t capacity, const char *source) {
    size_t i = 0u;

    if (capacity == 0u) {
        return;
    }
    while (source[i] != '\0' && i + 1u < capacity) {
        destination[i] = source[i];
        ++i;
    }
    destination[i] = '\0';
}

static int text_equal(const char *a, const char *b) {
    size_t i = 0u;

    while (a[i] != '\0' && a[i] == b[i]) {
        ++i;
    }
    return a[i] == b[i];
}

static char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static int compare_ignoring_case(const char *a, const char *b) {
    size_t i = 0u;

    while (a[i] != '\0' && b[i] != '\0' && lower(a[i]) == lower(b[i])) {
        ++i;
    }
    return (int)(unsigned char)lower(a[i]) - (int)(unsigned char)lower(b[i]);
}

static int ends_with(const char *text, const char *suffix) {
    const size_t length = text_length(text);
    const size_t suffix_length = text_length(suffix);

    return length >= suffix_length && text_equal(text + length - suffix_length, suffix);
}

/* Appends to a bounded buffer; returns 0, or -1 when the text was cut. */
static int append_text(char *buffer, size_t capacity, const char *text) {
    size_t length = text_length(buffer);
    size_t i = 0u;

    while (text[i] != '\0') {
        if (length + 1u >= capacity) {
            buffer[length] = '\0';
            return -1;
        }
        buffer[length++] = text[i++];
    }
    buffer[length] = '\0';
    return 0;
}

static int append_decimal(char *buffer, size_t capacity, uint64_t value) {
    char digits[21];
    size_t count = 0u;
    char text[22];
    size_t i;

    if (value == 0u) {
        digits[count++] = '0';
    }
    while (value != 0u) {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    }
    for (i = 0u; i < count; ++i) {
        text[i] = digits[count - 1u - i];
    }
    text[count] = '\0';
    return append_text(buffer, capacity, text);
}

const char *gfm_status_text(gfm_status_t status) {
    switch (status) {
    case GFM_OK: return "Done";
    case GFM_ERR_INVALID_NAME: return "Invalid name (no slash, not empty, at most 64 characters)";
    case GFM_ERR_INVALID_PATH: return "Invalid path";
    case GFM_ERR_EXISTS: return "An item with that name already exists";
    case GFM_ERR_NOT_FOUND: return "Not found";
    case GFM_ERR_NOT_DIRECTORY: return "Not a folder";
    case GFM_ERR_TOO_LONG: return "Path too long for VexFS (127 characters)";
    case GFM_ERR_INTO_ITSELF: return "A folder cannot be placed inside itself";
    case GFM_ERR_SAME_LOCATION: return "The item is already in that folder";
    case GFM_ERR_TOO_DEEP: return "Folder tree is too deep";
    case GFM_ERR_IO: return "File system error (storage full or access denied)";
    }
    return "Unknown error";
}

/* ── paths and names ─────────────────────────────────────────────────────── */

gfm_status_t gfm_path_resolve(const char *cwd, const char *input, char *out, size_t capacity) {
    char combined[GFM_PATH_MAX * 2u];
    size_t used = 0u;
    size_t i = 0u;

    if (capacity == 0u || input == 0 || input[0] == '\0') {
        return GFM_ERR_INVALID_PATH;
    }
    combined[0] = '\0';
    if (input[0] != '/') {
        if (cwd == 0 || cwd[0] != '/' || append_text(combined, sizeof(combined), cwd) != 0 ||
            append_text(combined, sizeof(combined), "/") != 0) {
            return GFM_ERR_INVALID_PATH;
        }
    }
    if (append_text(combined, sizeof(combined), input) != 0) {
        return GFM_ERR_TOO_LONG;
    }

    out[0] = '\0';
    while (combined[i] != '\0') {
        size_t start;
        size_t length;

        while (combined[i] == '/') {
            ++i;
        }
        start = i;
        while (combined[i] != '\0' && combined[i] != '/') {
            if ((unsigned char)combined[i] < 0x20u || combined[i] == 0x7f) {
                return GFM_ERR_INVALID_PATH;
            }
            ++i;
        }
        length = i - start;
        if (length == 0u || (length == 1u && combined[start] == '.')) {
            continue;
        }
        if (length == 2u && combined[start] == '.' && combined[start + 1u] == '.') {
            while (used > 0u && out[used - 1u] != '/') {
                --used;
            }
            if (used > 0u) {
                --used;
            }
            out[used] = '\0';
            continue;
        }
        if (used + 1u + length + 1u > capacity || used + 1u + length >= GFM_PATH_MAX) {
            return GFM_ERR_TOO_LONG;
        }
        out[used++] = '/';
        while (length-- != 0u) {
            out[used++] = combined[start++];
        }
        out[used] = '\0';
    }
    if (used == 0u) {
        out[0] = '/';
        out[1] = '\0';
    }
    return GFM_OK;
}

gfm_status_t gfm_path_join(const char *directory, const char *name, char *out, size_t capacity) {
    size_t length;

    copy_text(out, capacity, directory);
    length = text_length(out);
    if (length > 1u && out[length - 1u] == '/') {
        out[--length] = '\0';
    }
    if (!(length == 1u && out[0] == '/') && append_text(out, capacity, "/") != 0) {
        return GFM_ERR_TOO_LONG;
    }
    if (append_text(out, capacity, name) != 0 || text_length(out) >= GFM_PATH_MAX) {
        return GFM_ERR_TOO_LONG;
    }
    return GFM_OK;
}

void gfm_path_parent(const char *path, char *out, size_t capacity) {
    size_t length;

    copy_text(out, capacity, path);
    length = text_length(out);
    while (length > 1u && out[length - 1u] == '/') {
        out[--length] = '\0';
    }
    while (length > 1u && out[length - 1u] != '/') {
        out[--length] = '\0';
    }
    if (length > 1u) {
        out[length - 1u] = '\0';
    }
}

const char *gfm_path_basename(const char *path) {
    size_t length = text_length(path);

    while (length > 1u && path[length - 1u] == '/') {
        --length;
    }
    while (length > 0u && path[length - 1u] != '/') {
        --length;
    }
    return path + length;
}

int gfm_path_is_within(const char *ancestor, const char *path) {
    size_t length = text_length(ancestor);
    size_t i;

    while (length > 1u && ancestor[length - 1u] == '/') {
        --length;
    }
    for (i = 0u; i < length; ++i) {
        if (path[i] != ancestor[i]) {
            return 0;
        }
    }
    return length == 1u || path[length] == '\0' || path[length] == '/';
}

gfm_status_t gfm_name_prepare(char *name) {
    size_t length = text_length(name);
    size_t start = 0u;
    size_t i;

    while (length > 0u && name[length - 1u] == ' ') {
        name[--length] = '\0';
    }
    while (name[start] == ' ') {
        ++start;
    }
    if (start > 0u) {
        for (i = 0u; i <= length - start; ++i) {
            name[i] = name[start + i];
        }
        length -= start;
    }
    if (length == 0u || length > GFM_NAME_LIMIT || text_equal(name, ".") || text_equal(name, "..")) {
        return GFM_ERR_INVALID_NAME;
    }
    for (i = 0u; i < length; ++i) {
        const unsigned char c = (unsigned char)name[i];
        if (c < 0x20u || c == 0x7fu || c == '/' || c == '\\') {
            return GFM_ERR_INVALID_NAME;
        }
    }
    return GFM_OK;
}

int gfm_is_transient_download(const char *name) {
    return ends_with(name, TRANSIENT_PART) || ends_with(name, TRANSIENT_PART TRANSIENT_WEBKIT);
}

/* ── reading ─────────────────────────────────────────────────────────────── */

static uint32_t hash_step(uint32_t hash, uint8_t byte) {
    return (hash ^ byte) * 16777619u;
}

static void sort_entries(gfm_entry_t *entries, size_t count) {
    size_t i;

    for (i = 1u; i < count; ++i) {
        gfm_entry_t value = entries[i];
        size_t position = i;

        while (position > 0u) {
            const gfm_entry_t *before = &entries[position - 1u];
            const int value_is_directory = value.type == GFM_TYPE_DIRECTORY;
            const int before_is_directory = before->type == GFM_TYPE_DIRECTORY;
            int order;

            if (before_is_directory != value_is_directory) {
                order = before_is_directory ? -1 : 1;
            } else {
                order = compare_ignoring_case(before->name, value.name);
                if (order == 0) {
                    order = text_equal(before->name, value.name) ? 0 : -1;
                }
            }
            if (order <= 0) {
                break;
            }
            entries[position] = *before;
            --position;
        }
        entries[position] = value;
    }
}

gfm_status_t gfm_list(const char *directory, gfm_listing_t *listing) {
    static gfm_sys_dirent_t raw[GFM_MAX_ENTRIES];
    size_t total = 0u;
    size_t i;
    uint32_t fingerprint = 2166136261u;

    listing->count = 0u;
    listing->truncated = 0;
    listing->fingerprint = 0u;
    if (gfm_sys_list(directory, raw, GFM_MAX_ENTRIES, &total) != 0) {
        gfm_sys_stat_t info;
        return gfm_sys_stat(directory, &info) == 0 ? GFM_ERR_NOT_DIRECTORY : GFM_ERR_NOT_FOUND;
    }
    if (total > GFM_MAX_ENTRIES) {
        listing->truncated = 1;
        total = GFM_MAX_ENTRIES;
    }
    for (i = 0u; i < total; ++i) {
        gfm_entry_t *entry = &listing->entries[listing->count];
        char path[GFM_PATH_MAX];
        gfm_sys_stat_t info;
        size_t j;

        if (raw[i].name[0] == '\0') {
            continue;
        }
        copy_text(entry->name, sizeof(entry->name), raw[i].name);
        entry->type = raw[i].type;
        entry->size = 0u;
        entry->mode = 0u;
        if (gfm_path_join(directory, entry->name, path, sizeof(path)) == GFM_OK &&
            gfm_sys_stat(path, &info) == 0) {
            if (entry->type == GFM_TYPE_UNKNOWN) {
                entry->type = info.type;
            }
            entry->size = info.type == GFM_TYPE_DIRECTORY ? 0u : info.size;
            entry->mode = info.mode;
        }
        for (j = 0u; entry->name[j] != '\0'; ++j) {
            fingerprint = hash_step(fingerprint, (uint8_t)entry->name[j]);
        }
        fingerprint = hash_step(fingerprint, entry->type);
        ++listing->count;
    }
    if (listing->count == GFM_MAX_ENTRIES && total == GFM_MAX_ENTRIES) {
        listing->truncated = 1;
    }
    sort_entries(listing->entries, listing->count);
    listing->fingerprint = fingerprint;
    return GFM_OK;
}

gfm_status_t gfm_stat(const char *path, gfm_entry_t *entry) {
    gfm_sys_stat_t info;

    if (gfm_sys_stat(path, &info) != 0) {
        return GFM_ERR_NOT_FOUND;
    }
    copy_text(entry->name, sizeof(entry->name), gfm_path_basename(path));
    entry->type = info.type;
    entry->size = info.type == GFM_TYPE_DIRECTORY ? 0u : info.size;
    entry->mode = info.mode;
    return GFM_OK;
}

int gfm_name_exists(const char *directory, const char *name) {
    static gfm_sys_dirent_t raw[GFM_MAX_ENTRIES];
    size_t total = 0u;
    size_t i;

    if (gfm_sys_list(directory, raw, GFM_MAX_ENTRIES, &total) != 0) {
        return 0;
    }
    if (total > GFM_MAX_ENTRIES) {
        total = GFM_MAX_ENTRIES;
    }
    for (i = 0u; i < total; ++i) {
        if (compare_ignoring_case(raw[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int path_exists(const char *path) {
    gfm_sys_stat_t info;

    return gfm_sys_stat(path, &info) == 0;
}

/* One listing buffer per recursion level keeps the stack small. */
static gfm_sys_dirent_t level_listing[GFM_COPY_MAX_DEPTH + 1u][GFM_MAX_ENTRIES];

static gfm_status_t count_below(const char *path, unsigned depth, size_t *count) {
    gfm_sys_dirent_t *raw = level_listing[depth];
    size_t total = 0u;
    size_t i;

    if (depth > GFM_COPY_MAX_DEPTH) {
        return GFM_ERR_TOO_DEEP;
    }
    if (gfm_sys_list(path, raw, GFM_MAX_ENTRIES, &total) != 0) {
        return GFM_ERR_NOT_DIRECTORY;
    }
    if (total > GFM_MAX_ENTRIES) {
        total = GFM_MAX_ENTRIES;
    }
    *count += total;
    for (i = 0u; i < total; ++i) {
        char child[GFM_PATH_MAX];

        if (raw[i].type == GFM_TYPE_DIRECTORY &&
            gfm_path_join(path, raw[i].name, child, sizeof(child)) == GFM_OK) {
            (void)count_below(child, depth + 1u, count);
        }
    }
    return GFM_OK;
}

gfm_status_t gfm_count_descendants(const char *path, size_t *count) {
    *count = 0u;
    return count_below(path, 0u, count);
}

int gfm_looks_binary(const char *path) {
    unsigned char probe[BINARY_PROBE_SIZE];
    long count;
    long i;
    int fd = gfm_sys_open_read(path);

    if (fd < 0) {
        return 0;
    }
    count = gfm_sys_read(fd, probe, sizeof(probe));
    gfm_sys_close(fd);
    for (i = 0; i < count; ++i) {
        if (probe[i] == 0u) {
            return 1;
        }
    }
    return 0;
}

/* ── changing ────────────────────────────────────────────────────────────── */

gfm_status_t gfm_make_directory(const char *directory, const char *name, char *created, size_t capacity) {
    char path[GFM_PATH_MAX];
    char clean[GFM_SYS_NAME_MAX];
    gfm_status_t status;

    copy_text(clean, sizeof(clean), name);
    status = gfm_name_prepare(clean);
    if (status != GFM_OK) {
        return status;
    }
    status = gfm_path_join(directory, clean, path, sizeof(path));
    if (status != GFM_OK) {
        return status;
    }
    if (gfm_name_exists(directory, clean)) {
        return GFM_ERR_EXISTS;
    }
    if (gfm_sys_mkdir(path, 0755u) != 0) {
        return GFM_ERR_IO;
    }
    copy_text(created, capacity, path);
    return GFM_OK;
}

static gfm_status_t remove_tree(const char *path) {
    gfm_sys_stat_t info;

    if (gfm_sys_stat(path, &info) != 0) {
        return GFM_ERR_NOT_FOUND;
    }
    if (info.type == GFM_TYPE_DIRECTORY) {
        return gfm_sys_remove_tree(path) == 0 && !path_exists(path) ? GFM_OK : GFM_ERR_IO;
    }
    return gfm_sys_unlink(path) == 0 ? GFM_OK : GFM_ERR_IO;
}

gfm_status_t gfm_delete(const char *path) {
    if (text_equal(path, "/") || path[0] != '/') {
        return GFM_ERR_INVALID_PATH;
    }
    return remove_tree(path);
}

static gfm_status_t copy_file(const char *from, const char *to, uint32_t mode) {
    static unsigned char buffer[COPY_BUFFER_SIZE];
    int source = gfm_sys_open_read(from);
    int target;
    gfm_status_t status = GFM_OK;

    if (source < 0) {
        return GFM_ERR_NOT_FOUND;
    }
    target = gfm_sys_open_create_exclusive(to, mode != 0u ? mode : 0644u);
    if (target < 0) {
        gfm_sys_close(source);
        return path_exists(to) ? GFM_ERR_EXISTS : GFM_ERR_IO;
    }
    for (;;) {
        long count = gfm_sys_read(source, buffer, sizeof(buffer));
        long written = 0;

        if (count < 0) {
            status = GFM_ERR_IO;
            break;
        }
        if (count == 0) {
            break;
        }
        while (written < count) {
            long chunk = gfm_sys_write(target, buffer + written, (size_t)(count - written));
            if (chunk <= 0) {
                status = GFM_ERR_IO;
                break;
            }
            written += chunk;
        }
        if (status != GFM_OK) {
            break;
        }
    }
    gfm_sys_close(source);
    if (gfm_sys_close(target) != 0 && status == GFM_OK) {
        status = GFM_ERR_IO;
    }
    if (status != GFM_OK) {
        (void)gfm_sys_unlink(to);   /* never leave a truncated copy behind */
    }
    return status;
}

static gfm_status_t copy_tree(const char *from, const char *to, unsigned depth) {
    gfm_sys_stat_t info;

    if (depth > GFM_COPY_MAX_DEPTH) {
        return GFM_ERR_TOO_DEEP;
    }
    if (gfm_sys_stat(from, &info) != 0) {
        return GFM_ERR_NOT_FOUND;
    }
    if (info.type != GFM_TYPE_DIRECTORY) {
        gfm_status_t status = copy_file(from, to, info.mode);

        if (status == GFM_OK) {
            (void)gfm_sys_chmod(to, info.mode);
        }
        return status;
    }
    if (gfm_sys_mkdir(to, info.mode != 0u ? info.mode : 0755u) != 0) {
        return path_exists(to) ? GFM_ERR_EXISTS : GFM_ERR_IO;
    }
    {
        gfm_sys_dirent_t *raw = level_listing[depth];
        size_t total = 0u;
        size_t i;

        if (gfm_sys_list(from, raw, GFM_MAX_ENTRIES, &total) != 0) {
            return GFM_ERR_IO;
        }
        if (total > GFM_MAX_ENTRIES) {
            total = GFM_MAX_ENTRIES;
        }
        for (i = 0u; i < total; ++i) {
            char child_from[GFM_PATH_MAX];
            char child_to[GFM_PATH_MAX];
            gfm_status_t status;

            if (gfm_path_join(from, raw[i].name, child_from, sizeof(child_from)) != GFM_OK ||
                gfm_path_join(to, raw[i].name, child_to, sizeof(child_to)) != GFM_OK) {
                return GFM_ERR_TOO_LONG;
            }
            status = copy_tree(child_from, child_to, depth + 1u);
            if (status != GFM_OK) {
                return status;
            }
        }
    }
    return GFM_OK;
}

/* Copy a tree; when it fails half way, remove what was already written. */
static gfm_status_t copy_tree_or_clean(const char *from, const char *to) {
    gfm_status_t status = copy_tree(from, to, 0u);

    if (status != GFM_OK && status != GFM_ERR_EXISTS && path_exists(to)) {
        (void)remove_tree(to);
    }
    return status;
}

/* "name" -> "name copy" -> "name copy 2"; the extension stays last. */
static gfm_status_t free_destination(const char *directory, const char *name, char *path,
                                     size_t capacity, int copy_suffix) {
    char candidate[GFM_SYS_NAME_MAX];
    size_t dot = text_length(name);
    size_t i;
    unsigned attempt;

    for (i = text_length(name); i > 1u; --i) {
        if (name[i - 1u] == '.') {
            dot = i - 1u;
            break;
        }
    }
    for (attempt = 0u; attempt < 1000u; ++attempt) {
        gfm_status_t status;

        candidate[0] = '\0';
        for (i = 0u; i < dot && i + 1u < sizeof(candidate); ++i) {
            candidate[i] = name[i];
            candidate[i + 1u] = '\0';
        }
        if (attempt > 0u || !copy_suffix) {
            if (attempt > 0u) {
                if (append_text(candidate, sizeof(candidate), " copy") != 0) {
                    return GFM_ERR_TOO_LONG;
                }
                if (attempt > 1u &&
                    (append_text(candidate, sizeof(candidate), " ") != 0 ||
                     append_decimal(candidate, sizeof(candidate), attempt) != 0)) {
                    return GFM_ERR_TOO_LONG;
                }
            }
        }
        if (append_text(candidate, sizeof(candidate), name + dot) != 0) {
            return GFM_ERR_TOO_LONG;
        }
        status = gfm_path_join(directory, candidate, path, capacity);
        if (status != GFM_OK) {
            return status;
        }
        if (!gfm_name_exists(directory, candidate)) {
            return GFM_OK;
        }
    }
    return GFM_ERR_EXISTS;
}

gfm_status_t gfm_copy(const char *path, const char *destination_directory, char *created, size_t capacity) {
    char target[GFM_PATH_MAX];
    gfm_sys_stat_t info;
    gfm_status_t status;

    if (gfm_sys_stat(path, &info) != 0) {
        return GFM_ERR_NOT_FOUND;
    }
    if (info.type == GFM_TYPE_DIRECTORY && gfm_path_is_within(path, destination_directory)) {
        return GFM_ERR_INTO_ITSELF;
    }
    if (gfm_sys_stat(destination_directory, &info) != 0 || info.type != GFM_TYPE_DIRECTORY) {
        return GFM_ERR_NOT_DIRECTORY;
    }
    status = free_destination(destination_directory, gfm_path_basename(path), target,
                              sizeof(target), 1);
    if (status != GFM_OK) {
        return status;
    }
    status = copy_tree_or_clean(path, target);
    if (status == GFM_OK) {
        copy_text(created, capacity, target);
    }
    return status;
}

gfm_status_t gfm_move(const char *path, const char *destination_directory, char *moved, size_t capacity) {
    char target[GFM_PATH_MAX];
    char parent[GFM_PATH_MAX];
    gfm_sys_stat_t source;
    gfm_sys_stat_t destination;
    gfm_status_t status;

    if (gfm_sys_stat(path, &source) != 0) {
        return GFM_ERR_NOT_FOUND;
    }
    if (gfm_sys_stat(destination_directory, &destination) != 0 ||
        destination.type != GFM_TYPE_DIRECTORY) {
        return GFM_ERR_NOT_DIRECTORY;
    }
    gfm_path_parent(path, parent, sizeof(parent));
    if (text_equal(parent, destination_directory)) {
        return GFM_ERR_SAME_LOCATION;
    }
    if (source.type == GFM_TYPE_DIRECTORY && gfm_path_is_within(path, destination_directory)) {
        return GFM_ERR_INTO_ITSELF;
    }
    status = gfm_path_join(destination_directory, gfm_path_basename(path), target, sizeof(target));
    if (status != GFM_OK) {
        return status;
    }
    if (gfm_name_exists(destination_directory, gfm_path_basename(path))) {
        return GFM_ERR_EXISTS;
    }
    if (source.type != GFM_TYPE_DIRECTORY) {
        if (gfm_sys_rename(path, target) == 0) {
            copy_text(moved, capacity, target);
            return GFM_OK;
        }
    }
    /* Directories (and files the file system refuses to rename) are copied,
     * then the original is removed; if the copy fails nothing is lost. */
    status = copy_tree_or_clean(path, target);
    if (status != GFM_OK) {
        return status;
    }
    status = remove_tree(path);
    if (status != GFM_OK) {
        return status;
    }
    copy_text(moved, capacity, target);
    return GFM_OK;
}

gfm_status_t gfm_rename(const char *path, const char *new_name, char *renamed, size_t capacity) {
    char clean[GFM_SYS_NAME_MAX];
    char parent[GFM_PATH_MAX];
    char target[GFM_PATH_MAX];
    gfm_sys_stat_t info;
    gfm_status_t status;

    copy_text(clean, sizeof(clean), new_name);
    status = gfm_name_prepare(clean);
    if (status != GFM_OK) {
        return status;
    }
    if (gfm_sys_stat(path, &info) != 0) {
        return GFM_ERR_NOT_FOUND;
    }
    gfm_path_parent(path, parent, sizeof(parent));
    status = gfm_path_join(parent, clean, target, sizeof(target));
    if (status != GFM_OK) {
        return status;
    }
    if (text_equal(target, path)) {
        copy_text(renamed, capacity, path);
        return GFM_OK;
    }
    /* A new name that only differs by case would collide with the item itself
     * on a case-insensitive view; treat any other match as a conflict. */
    if (gfm_name_exists(parent, clean) && compare_ignoring_case(clean, gfm_path_basename(path)) != 0) {
        return GFM_ERR_EXISTS;
    }
    if (info.type != GFM_TYPE_DIRECTORY) {
        if (gfm_sys_rename(path, target) != 0) {
            return GFM_ERR_IO;
        }
    } else {
        status = copy_tree_or_clean(path, target);
        if (status == GFM_OK) {
            status = remove_tree(path);
        }
        if (status != GFM_OK) {
            return status;
        }
    }
    copy_text(renamed, capacity, target);
    return GFM_OK;
}

gfm_status_t gfm_set_executable(const char *path, int executable) {
    gfm_sys_stat_t info;
    uint32_t mode;

    if (gfm_sys_stat(path, &info) != 0) {
        return GFM_ERR_NOT_FOUND;
    }
    mode = executable ? (info.mode | 0111u) : (info.mode & ~0111u);
    return gfm_sys_chmod(path, mode) == 0 ? GFM_OK : GFM_ERR_IO;
}

/* ── formatting ──────────────────────────────────────────────────────────── */

void gfm_format_size(uint64_t bytes, char *out, size_t capacity) {
    static const char *const units[] = { "B", "KB", "MB", "GB" };
    uint64_t whole = bytes;
    uint64_t tenths = 0u;
    unsigned unit = 0u;

    while (whole >= 1024u && unit < 3u) {
        tenths = (whole % 1024u) * 10u / 1024u;
        whole /= 1024u;
        ++unit;
    }
    out[0] = '\0';
    (void)append_decimal(out, capacity, whole);
    if (unit > 0u) {
        (void)append_text(out, capacity, ".");
        (void)append_decimal(out, capacity, tenths);
    }
    (void)append_text(out, capacity, " ");
    (void)append_text(out, capacity, units[unit]);
}

void gfm_format_mode(uint32_t mode, char *out, size_t capacity) {
    static const char letters[] = "rwxrwxrwx";
    size_t i;

    if (capacity < 10u) {
        if (capacity > 0u) {
            out[0] = '\0';
        }
        return;
    }
    for (i = 0u; i < 9u; ++i) {
        out[i] = (mode & (1u << (8u - i))) != 0u ? letters[i] : '-';
    }
    out[9] = '\0';
}
