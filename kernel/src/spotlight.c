#include <unos/spotlight.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/named_content.h>
#include <unos/kernel_guard.h>
#include <stdio.h>
#include <string.h>
#include <unos/fs.h>
#include <unos/tty.h>
#include <unos/users.h>

#define SPOTLIGHT_MAX_INDEXED 64u
#define SPOTLIGHT_MAX_RESULTS 7u
#define SPOTLIGHT_MAX_PATH 128u
#define SPOTLIGHT_MAX_TITLE 64u
#define SPOTLIGHT_MAX_DESCRIPTION 96u
#define SPOTLIGHT_MAX_TAGS 96u
static named_content_map_t preferences;
static kernel_gate_t preference_gate=KERNEL_GATE_INIT;

typedef struct {
    char path[SPOTLIGHT_MAX_PATH];
    size_t size;
} spotlight_indexed_path_t;

typedef struct {
    char path[SPOTLIGHT_MAX_PATH];
    char title[SPOTLIGHT_MAX_TITLE];
    char description[SPOTLIGHT_MAX_DESCRIPTION];
    char tags[SPOTLIGHT_MAX_TAGS];
    char category[24];
    uint32_t score;
    spotlight_result_kind_t kind;
    spotlight_action_t action;
    uint8_t program;
    uint8_t dangerous;
} spotlight_result_t;

typedef struct {
    const char *path;
    const char *title;
    const char *description;
    const char *tags;
    const char *category;
    spotlight_result_kind_t kind;
    spotlight_action_t action;
    uint8_t dangerous;
} spotlight_catalog_entry_t;

static spotlight_indexed_path_t indexed_paths[SPOTLIGHT_MAX_INDEXED];
static size_t indexed_count;
static spotlight_result_t results[SPOTLIGHT_MAX_RESULTS];
static spotlight_result_view_t result_views[SPOTLIGHT_MAX_RESULTS];
static size_t result_count;
static const char *active_query;
static uint32_t preference_score(const char *path);

static const spotlight_catalog_entry_t catalog[] = {
    { "app:terminal", "Terminal", "Open a shell session", "terminal shell console command", "Apps", SPOTLIGHT_RESULT_APP, SPOTLIGHT_ACTION_OPEN_TERMINAL, 0 },
    { "app:files", "Files", "Open the Suchu file manager", "gfm files file manager explorer folder", "Apps", SPOTLIGHT_RESULT_APP, SPOTLIGHT_ACTION_OPEN_FILES, 0 },
    { "app:editor", "Editor", "Open the KyIlOS editor", "editor text note", "Apps", SPOTLIGHT_RESULT_APP, SPOTLIGHT_ACTION_OPEN_EDITOR, 0 },
    { "app:browser", "Browser", "Open WebKit", "browser web webkit internet", "Apps", SPOTLIGHT_RESULT_APP, SPOTLIGHT_ACTION_OPEN_BROWSER, 0 },
    { "cmd:sysinfo", "sysinfo", "Show resolution, RAM, and processes", "system info memory ram kernel", "Commands", SPOTLIGHT_RESULT_COMMAND, SPOTLIGHT_ACTION_RUN_SYSINFO, 0 },
    { "cmd:ps", "ps", "List active processes", "process process list scheduler", "Commands", SPOTLIGHT_RESULT_COMMAND, SPOTLIGHT_ACTION_RUN_PS, 0 },
    { "cmd:log", "log", "Show recent system logs", "syslog debug journal error", "Commands", SPOTLIGHT_RESULT_COMMAND, SPOTLIGHT_ACTION_RUN_LOG, 0 }
};

static size_t string_length(const char *str) {
    size_t length = 0;

    while (str != NULL && str[length] != '\0') {
        ++length;
    }

    return length;
}

static int starts_with(const char *str, const char *prefix) {
    size_t i = 0;

    while (prefix[i] != '\0') {
        if (str[i] != prefix[i]) {
            return 0;
        }
        ++i;
    }

    return 1;
}

static int string_equals(const char *left, const char *right) {
    size_t i = 0;

    while (left != NULL && right != NULL && left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) {
            return 0;
        }
        ++i;
    }

    return left != NULL && right != NULL && left[i] == right[i];
}

static char lower_char(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }

    return c;
}

static int contains_case_insensitive(const char *text, const char *needle) {
    if (needle == NULL || needle[0] == '\0') {
        return 1;
    }

    for (size_t i = 0; text != NULL && text[i] != '\0'; ++i) {
        size_t j = 0;

        while (needle[j] != '\0' &&
               text[i + j] != '\0' &&
               lower_char(text[i + j]) == lower_char(needle[j])) {
            ++j;
        }

        if (needle[j] == '\0') {
            return 1;
        }
    }

    return 0;
}

static int equals_case_insensitive(const char *left, const char *right) {
    size_t i = 0;

    while (left != NULL && right != NULL && left[i] != '\0' && right[i] != '\0') {
        if (lower_char(left[i]) != lower_char(right[i])) {
            return 0;
        }
        ++i;
    }

    return left != NULL && right != NULL && left[i] == '\0' && right[i] == '\0';
}

static int starts_with_case_insensitive(const char *text, const char *prefix) {
    size_t i = 0;

    if (text == NULL || prefix == NULL) {
        return 0;
    }

    while (prefix[i] != '\0') {
        if (text[i] == '\0' || lower_char(text[i]) != lower_char(prefix[i])) {
            return 0;
        }
        ++i;
    }

    return 1;
}

static uint32_t relevance_score(const spotlight_result_t *result) {
    uint32_t score = preference_score(result->path) * 40u;

    if (active_query == NULL || active_query[0] == '\0') {
        return score;
    }

    if (equals_case_insensitive(result->title, active_query)) {
        score += 1000u;
    } else if (starts_with_case_insensitive(result->title, active_query)) {
        score += 760u;
    } else if (contains_case_insensitive(result->title, active_query)) {
        score += 520u;
    }

    if (contains_case_insensitive(result->tags, active_query)) {
        score += 260u;
    }
    if (contains_case_insensitive(result->description, active_query)) {
        score += 170u;
    }
    if (contains_case_insensitive(result->path, active_query)) {
        score += 110u;
    }

    if (result->kind == SPOTLIGHT_RESULT_APP) {
        score += 32u;
    } else if (result->kind == SPOTLIGHT_RESULT_COMMAND) {
        score += 24u;
    }

    return score;
}



static void copy_string(char *dst, size_t capacity, const char *src) {
    size_t i = 0;

    if (capacity == 0) {
        return;
    }

    while (src != NULL && src[i] != '\0' && i + 1u < capacity) {
        dst[i] = src[i];
        ++i;
    }

    dst[i] = '\0';
}

static void append_string(char *dst, size_t capacity, const char *src) {
    size_t length = string_length(dst);
    size_t i = 0;

    while (src != NULL && src[i] != '\0' && length + 1u < capacity) {
        dst[length++] = src[i++];
    }

    dst[length] = '\0';
}

static const char *basename(const char *path) {
    const char *name = path;

    if (path == NULL) {
        return "";
    }

    for (size_t i = 0; path[i] != '\0'; ++i) {
        if (path[i] == '/' && path[i + 1u] != '\0') {
            name = &path[i + 1u];
        }
    }

    return name;
}

static int has_suffix(const char *str, const char *suffix) {
    const size_t str_len = string_length(str);
    const size_t suffix_len = string_length(suffix);

    if (suffix_len > str_len) {
        return 0;
    }

    for (size_t i = 0; i < suffix_len; ++i) {
        if (str[str_len - suffix_len + i] != suffix[i]) {
            return 0;
        }
    }

    return 1;
}

static int current_user_bin_path(const char *path) {
    char prefix[64];

    copy_string(prefix, sizeof(prefix), "/home/");
    append_string(prefix, sizeof(prefix), user_current_username());
    append_string(prefix, sizeof(prefix), "/bin/");

    return starts_with(path, prefix);
}

static void collect_path(const char *path, size_t size) {
    spotlight_index_add(path, size);
}

void spotlight_index_add(const char *path, size_t size) {
    if (path == NULL) {
        return;
    }

    for (size_t i = 0; i < indexed_count; ++i) {
        if (string_equals(indexed_paths[i].path, path)) {
            indexed_paths[i].size = size;
            return;
        }
    }

    if (indexed_count >= SPOTLIGHT_MAX_INDEXED) {
        return;
    }

    copy_string(indexed_paths[indexed_count].path, sizeof(indexed_paths[indexed_count].path), path);
    indexed_paths[indexed_count].size = size;
    ++indexed_count;
}

void spotlight_index_remove(const char *path) {
    if (path == NULL) {
        return;
    }

    for (size_t i = 0; i < indexed_count; ++i) {
        if (string_equals(indexed_paths[i].path, path)) {
            for (size_t j = i + 1u; j < indexed_count; ++j) {
                indexed_paths[j - 1u] = indexed_paths[j];
            }
            --indexed_count;
            return;
        }
    }
}

void spotlight_index_build(void) {
    indexed_count = 0;
    fs_list(collect_path);
}

static int meta_key_matches(const char *line, size_t remaining, const char *key) {
    const size_t key_len = string_length(key);

    if (remaining < key_len) {
        return 0;
    }

    for (size_t i = 0; i < key_len; ++i) {
        if (line[i] != key[i]) {
            return 0;
        }
    }

    return 1;
}

static void copy_meta_value(char *dst, size_t capacity, const char *text, size_t size, size_t *offset) {
    size_t out = 0;

    while (*offset < size && text[*offset] != '\n' && out + 1u < capacity) {
        dst[out++] = text[*offset];
        ++(*offset);
    }

    dst[out] = '\0';
}

static void load_metadata(const char *path, char *title, size_t title_capacity, char *description, size_t description_capacity, char *tags, size_t tags_capacity) {
    ora_view_t fs_lease_0 __attribute__((cleanup(ora_view_release)))={0};
    char meta_path[SPOTLIGHT_MAX_PATH];
    void *data = NULL;
    size_t size = 0;

    copy_string(meta_path, sizeof(meta_path), path);
    append_string(meta_path, sizeof(meta_path), ".meta");

    if (fs_read_view(meta_path,&data,&size, &fs_lease_0) != 0 || data == NULL) {
        return;
    }

    const char *text = data;
    for (size_t offset = 0; offset < size;) {
        const size_t remaining = size - offset;

        if (meta_key_matches(&text[offset], remaining, "nom=")) {
            offset += 4u;
            copy_meta_value(title, title_capacity, text, size, &offset);
        } else if (meta_key_matches(&text[offset], remaining, "description=")) {
            offset += 12u;
            copy_meta_value(description, description_capacity, text, size, &offset);
        } else if (meta_key_matches(&text[offset], remaining, "tags=")) {
            offset += 5u;
            copy_meta_value(tags, tags_capacity, text, size, &offset);
        } else {
            while (offset < size && text[offset] != '\n') {
                ++offset;
            }
        }

        if (offset < size && text[offset] == '\n') {
            ++offset;
        }
    }
}

static int result_matches(const spotlight_result_t *result) {
    return contains_case_insensitive(result->path, active_query) ||
        contains_case_insensitive(result->title, active_query) ||
        contains_case_insensitive(result->description, active_query) ||
        contains_case_insensitive(result->tags, active_query);
}

static void preference_key(char *out,size_t capacity,const char *path) {
    const char *user=user_current_username();
    /* Length prefixes distinguish components even when queries contain ':'. */
    snprintf(out,capacity,"%u:%s%u:%s%u:%s",(unsigned)string_length(user),user,
        (unsigned)string_length(active_query),active_query,(unsigned)string_length(path),path);
}

static uint32_t preference_score(const char *path) {
    char key[384];preference_key(key,sizeof(key),path);
    ora_ref_t content=named_content_acquire(&preferences,key);uint32_t score=0;
    if(content && ora_size(content)==sizeof(score))memcpy(&score,ora_data(content),sizeof(score));
    ora_release(content);return score;
}

static void preference_increment(const char *path) {
    KERNEL_GUARD(preference_gate);
    char key[384];preference_key(key,sizeof(key),path);uint32_t score=preference_score(path);
    if(score!=UINT32_MAX)++score;
    (void)named_content_put(&preferences,key,&score,sizeof(score),NAMED_UPSERT);
}

static void insert_result(const spotlight_result_t *candidate) {
    size_t pos = result_count;

    if (pos < SPOTLIGHT_MAX_RESULTS) {
        ++result_count;
    } else if (candidate->score <= results[SPOTLIGHT_MAX_RESULTS - 1u].score) {
        return;
    } else {
        pos = SPOTLIGHT_MAX_RESULTS - 1u;
    }

    while (pos > 0 && candidate->score > results[pos - 1u].score) {
        results[pos] = results[pos - 1u];
        --pos;
    }

    results[pos] = *candidate;
}

static void build_result_from_path(const char *path) {
    spotlight_result_t candidate;
    const char *name = basename(path);

    if (has_suffix(path, ".meta") || name[0] == '.') {
        return;
    }

    candidate.program = current_user_bin_path(path);
    copy_string(candidate.path, sizeof(candidate.path), path);
    copy_string(candidate.title, sizeof(candidate.title), name);
    candidate.description[0] = '\0';
    candidate.tags[0] = '\0';

    load_metadata(
        path,
        candidate.title,
        sizeof(candidate.title),
        candidate.description,
        sizeof(candidate.description),
        candidate.tags,
        sizeof(candidate.tags)
    );
    if (contains_case_insensitive(candidate.tags, "app") ||
        contains_case_insensitive(candidate.tags, "translated")) {
        candidate.program = 1u;
    }

    if (!result_matches(&candidate)) {
        return;
    }

    candidate.score = preference_score(path);
    candidate.kind = candidate.program != 0 ? SPOTLIGHT_RESULT_APP : SPOTLIGHT_RESULT_FILE;
    candidate.action = candidate.program != 0 ? SPOTLIGHT_ACTION_RUN_PROGRAM : SPOTLIGHT_ACTION_OPEN_PATH;
    candidate.dangerous = 0;
    copy_string(candidate.category, sizeof(candidate.category), candidate.program != 0 ? "Apps" : "Files");
    candidate.score = relevance_score(&candidate);
    insert_result(&candidate);
}

static void build_result_from_catalog(const spotlight_catalog_entry_t *entry) {
    spotlight_result_t candidate;

    copy_string(candidate.path, sizeof(candidate.path), entry->path);
    copy_string(candidate.title, sizeof(candidate.title), entry->title);
    copy_string(candidate.description, sizeof(candidate.description), entry->description);
    copy_string(candidate.tags, sizeof(candidate.tags), entry->tags);
    copy_string(candidate.category, sizeof(candidate.category), entry->category);
    candidate.kind = entry->kind;
    candidate.action = entry->action;
    candidate.program = entry->kind == SPOTLIGHT_RESULT_APP;
    candidate.dangerous = entry->dangerous;

    if (!result_matches(&candidate)) {
        return;
    }

    candidate.score = relevance_score(&candidate);
    insert_result(&candidate);
}

static void print_result(const spotlight_result_t *result) {
    tty_puts(result->title);

    if (result->description[0] != '\0') {
        tty_puts(" - ");
        tty_puts(result->description);
    }

    tty_puts(" (");
    tty_puts(result->path);
    tty_puts(")\n");
}

size_t spotlight_query(const char *query) {
    if (query == NULL || query[0] == '\0') {
        active_query = "";
        result_count = 0;
        return 0;
    }

    active_query = query;
    result_count = 0;

    for (size_t i = 0; i < sizeof(catalog) / sizeof(catalog[0]); ++i) {
        build_result_from_catalog(&catalog[i]);
    }

    for (size_t i = 0; i < indexed_count; ++i) {
        build_result_from_path(indexed_paths[i].path);
    }

    for (size_t i = 0; i < result_count; ++i) {
        result_views[i].path = results[i].path;
        result_views[i].title = results[i].title;
        result_views[i].description = results[i].description;
        result_views[i].category = results[i].category;
        result_views[i].kind = results[i].kind;
        result_views[i].action = results[i].action;
        result_views[i].program = results[i].program;
        result_views[i].dangerous = results[i].dangerous;
    }

    return result_count;
}

const spotlight_result_view_t *spotlight_result(size_t index) {
    if (index >= result_count) {
        return NULL;
    }

    return &result_views[index];
}

void spotlight_record_selection(size_t index) {
    if (index >= result_count) {
        return;
    }

    preference_increment(results[index].path);
}

void spotlight_search(const char *query) {
    if (query == NULL || query[0] == '\0') {
        tty_puts("usage: spotlight <query>\n");
        return;
    }

    spotlight_query(query);

    if (result_count == 0) {
        tty_puts("no results\n");
        return;
    }

    for (size_t i = 0; i < result_count; ++i) {
        print_result(&results[i]);
    }
}
