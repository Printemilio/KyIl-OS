#include <unos/text_editor_ui.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/fs.h>
#include <unos/gui.h>
#include <unos/heap.h>
#include <unos/ipc.h>
#include <unos/keyboard.h>
#include <unos/scheduler.h>
#include <unos/tty.h>
#include <unos/vex_legacy_compat.h>

#define EDITOR_MAGIC 0x5645584544495452ull
#define EDITOR_RECOVERY_VERSION 1ull
#define EDITOR_GAP_CAPACITY 256u
#define EDITOR_MARGIN_X 12ull
#define EDITOR_MARGIN_Y 10ull
#define EDITOR_LINE_HEIGHT 12ull
#define EDITOR_GUTTER_WIDTH 52ull
#define EDITOR_TOPBAR_HEIGHT 32ull
#define EDITOR_STATUS_HEIGHT 22ull
#define EDITOR_AUTOSAVE_LINES 500u
#define EDITOR_AUTOSAVE_CHARS 10000u
#define EDITOR_PROCESS_PRIORITY 1u

typedef enum editor_piece_source {
    EDITOR_SOURCE_ORIGINAL = 0,
    EDITOR_SOURCE_ADD = 1
} editor_piece_source_t;

typedef struct editor_piece {
    uint8_t source;
    size_t start;
    size_t length;
} editor_piece_t;

typedef struct editor_delta {
    size_t start;
    size_t delete_length;
    size_t add_start;
    size_t insert_length;
} editor_delta_t;

typedef struct editor_recovery_header {
    uint64_t magic;
    uint64_t version;
    uint64_t count;
} editor_recovery_header_t;

static char *original_buffer;
static size_t original_length;
static char *add_buffer;
static size_t add_length;
static size_t add_capacity;
static editor_piece_t *pieces;
static size_t piece_count;
static size_t piece_capacity;
static editor_delta_t *deltas;
static size_t delta_count;
static size_t delta_capacity;
static char *gap_buffer;
static size_t gap_capacity;
static size_t gap_start;
static size_t gap_end;
static size_t gap_anchor;
static size_t gap_delete_left;
static uint8_t gap_active;
static size_t cursor_offset;
static size_t preferred_column;
static size_t top_line;
static size_t dirty_chars;
static size_t dirty_lines;
static char current_path[128];
static char recovery_path[128];
static char title_text[64];
static char status_text[96];
static char save_prompt_path[128];
static uint8_t editor_open;
static uint8_t save_prompt_active;
static uint8_t autosave_flushing;
static uint32_t editor_pid_value;
static int editor_command_pipe = -1;
static uint8_t editor_active;
static uint8_t editor_dirty;
static uint64_t editor_origin_x;
static uint64_t editor_origin_y;
static uint64_t editor_width;
static uint64_t editor_height;

static void flush_gap_to_piece_table(void);

static size_t string_length(const char *str) {
    size_t length = 0;
    while (str != NULL && str[length] != '\0') {
        ++length;
    }
    return length;
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

static int string_equals(const char *left, const char *right) {
    size_t i = 0;
    while (left != NULL && right != NULL && left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) {
            return 0;
        }
        ++i;
    }
    return left != NULL && right != NULL && left[i] == '\0' && right[i] == '\0';
}

static int starts_with(const char *str, const char *prefix) {
    size_t i = 0;
    while (prefix != NULL && prefix[i] != '\0') {
        if (str == NULL || str[i] != prefix[i]) {
            return 0;
        }
        ++i;
    }
    return 1;
}

static const char *path_basename(const char *path) {
    const char *name = path;
    for (size_t i = 0; path != NULL && path[i] != '\0'; ++i) {
        if (path[i] == '/' && path[i + 1u] != '\0') {
            name = path + i + 1u;
        }
    }
    return name;
}

static void set_status(const char *prefix, const char *value) {
    copy_string(status_text, sizeof(status_text), prefix);
    append_string(status_text, sizeof(status_text), value);
}

static const char *display_path(void) {
    return current_path[0] == '\0' ? "nouveau fichier" : current_path;
}

static int ensure_piece_capacity(size_t needed) {
    if (piece_capacity >= needed) {
        return 1;
    }
    {
        size_t new_capacity = piece_capacity == 0u ? 16u : piece_capacity * 2u;
        editor_piece_t *new_pieces;
        while (new_capacity < needed) {
            new_capacity *= 2u;
        }
        new_pieces = krealloc(pieces, new_capacity * sizeof(editor_piece_t));
        if (new_pieces == NULL) {
            return 0;
        }
        pieces = new_pieces;
        piece_capacity = new_capacity;
    }
    return 1;
}

static int ensure_delta_capacity(size_t needed) {
    if (delta_capacity >= needed) {
        return 1;
    }
    {
        size_t new_capacity = delta_capacity == 0u ? 16u : delta_capacity * 2u;
        editor_delta_t *new_deltas;
        while (new_capacity < needed) {
            new_capacity *= 2u;
        }
        new_deltas = krealloc(deltas, new_capacity * sizeof(editor_delta_t));
        if (new_deltas == NULL) {
            return 0;
        }
        deltas = new_deltas;
        delta_capacity = new_capacity;
    }
    return 1;
}

static int ensure_add_capacity(size_t extra) {
    if (add_length + extra <= add_capacity) {
        return 1;
    }
    {
        size_t new_capacity = add_capacity == 0u ? 256u : add_capacity * 2u;
        char *new_buffer;
        while (new_capacity < add_length + extra) {
            new_capacity *= 2u;
        }
        new_buffer = krealloc(add_buffer, new_capacity);
        if (new_buffer == NULL) {
            return 0;
        }
        add_buffer = new_buffer;
        add_capacity = new_capacity;
    }
    return 1;
}

static int append_add_text(const char *text, size_t length, size_t *start_out) {
    if (!ensure_add_capacity(length)) {
        return 0;
    }
    *start_out = add_length;
    for (size_t i = 0; i < length; ++i) {
        add_buffer[add_length++] = text[i];
    }
    return 1;
}

static size_t piece_table_length(void) {
    size_t total = 0;
    for (size_t i = 0; i < piece_count; ++i) {
        total += pieces[i].length;
    }
    return total;
}

static void shift_pieces_right(size_t index, size_t count) {
    if (count == 0u || index > piece_count) {
        return;
    }
    for (size_t i = piece_count; i > index; --i) {
        pieces[i + count - 1u] = pieces[i - 1u];
    }
}

static void shift_pieces_left(size_t index, size_t count) {
    if (count == 0u) {
        return;
    }
    for (size_t i = index; i + count < piece_count; ++i) {
        pieces[i] = pieces[i + count];
    }
}

static void merge_adjacent_pieces(void) {
    size_t i = 0;
    while (i + 1u < piece_count) {
        editor_piece_t *left = &pieces[i];
        editor_piece_t *right = &pieces[i + 1u];
        if (left->source == right->source && left->start + left->length == right->start) {
            left->length += right->length;
            shift_pieces_left(i + 1u, 1u);
            --piece_count;
            continue;
        }
        ++i;
    }
}

static void piece_table_insert(size_t offset, uint8_t source, size_t start, size_t length) {
    size_t position = 0;
    size_t i = 0;
    if (length == 0u || !ensure_piece_capacity(piece_count + 2u)) {
        return;
    }

    while (i < piece_count && position + pieces[i].length < offset) {
        position += pieces[i].length;
        ++i;
    }

    if (i == piece_count) {
        pieces[piece_count].source = source;
        pieces[piece_count].start = start;
        pieces[piece_count].length = length;
        ++piece_count;
        merge_adjacent_pieces();
        return;
    }

    if (offset == position) {
        shift_pieces_right(i, 1u);
        pieces[i].source = source;
        pieces[i].start = start;
        pieces[i].length = length;
        ++piece_count;
        merge_adjacent_pieces();
        return;
    }

    {
        const size_t inside = offset - position;
        editor_piece_t tail = pieces[i];
        pieces[i].length = inside;
        shift_pieces_right(i + 1u, 2u);
        pieces[i + 1u].source = source;
        pieces[i + 1u].start = start;
        pieces[i + 1u].length = length;
        pieces[i + 2u].source = tail.source;
        pieces[i + 2u].start = tail.start + inside;
        pieces[i + 2u].length = tail.length - inside;
        piece_count += 2u;
        merge_adjacent_pieces();
    }
}

static void piece_table_delete(size_t offset, size_t length) {
    size_t position = 0;
    size_t i = 0;

    while (length > 0u && i < piece_count) {
        const size_t piece_start = position;
        const size_t piece_end = position + pieces[i].length;

        if (offset >= piece_end) {
            position = piece_end;
            ++i;
            continue;
        }

        if (offset + length <= piece_start) {
            break;
        }

        {
            const size_t delete_start = offset > piece_start ? offset : piece_start;
            const size_t delete_end = (offset + length) < piece_end ? (offset + length) : piece_end;
            const size_t left_keep = delete_start - piece_start;
            const size_t right_keep = piece_end - delete_end;

            if (left_keep == 0u && right_keep == 0u) {
                shift_pieces_left(i, 1u);
                --piece_count;
                position = piece_start;
            } else if (left_keep == 0u) {
                pieces[i].start += delete_end - piece_start;
                pieces[i].length = right_keep;
                position = piece_start + pieces[i].length;
                ++i;
            } else if (right_keep == 0u) {
                pieces[i].length = left_keep;
                position = piece_start + pieces[i].length;
                ++i;
            } else if (ensure_piece_capacity(piece_count + 1u)) {
                editor_piece_t tail = pieces[i];
                pieces[i].length = left_keep;
                shift_pieces_right(i + 1u, 1u);
                pieces[i + 1u].source = tail.source;
                pieces[i + 1u].start = tail.start + (tail.length - right_keep);
                pieces[i + 1u].length = right_keep;
                ++piece_count;
                position = piece_start + pieces[i].length;
                i += 2u;
            } else {
                return;
            }

            length -= delete_end - delete_start;
            offset = delete_end;
        }
    }

    merge_adjacent_pieces();
}

static char *materialize_piece_table(size_t *out_length) {
    size_t total = piece_table_length();
    char *buffer = kmalloc(total + 1u);
    size_t cursor = 0;

    if (buffer == NULL) {
        *out_length = 0;
        return NULL;
    }

    for (size_t i = 0; i < piece_count; ++i) {
        const char *source = pieces[i].source == EDITOR_SOURCE_ORIGINAL ? original_buffer : add_buffer;
        for (size_t j = 0; j < pieces[i].length; ++j) {
            buffer[cursor++] = source[pieces[i].start + j];
        }
    }

    buffer[cursor] = '\0';
    *out_length = cursor;
    return buffer;
}

static size_t gap_text_length(void) {
    return gap_capacity - (gap_end - gap_start);
}

static void gap_reset(void) {
    gap_active = 0;
    gap_anchor = cursor_offset;
    gap_delete_left = 0;
    gap_start = 0;
    gap_end = gap_capacity;
}

static int gap_init(void) {
    if (gap_buffer != NULL) {
        gap_reset();
        return 1;
    }
    gap_capacity = EDITOR_GAP_CAPACITY;
    gap_buffer = kmalloc(gap_capacity);
    if (gap_buffer == NULL) {
        return 0;
    }
    gap_reset();
    return 1;
}

static int gap_ensure_space(size_t need) {
    if (gap_end - gap_start >= need) {
        return 1;
    }
    {
        size_t text_length = gap_text_length();
        size_t new_capacity = gap_capacity * 2u;
        char *new_buffer;
        while (new_capacity - text_length < need) {
            new_capacity *= 2u;
        }
        new_buffer = kmalloc(new_capacity);
        if (new_buffer == NULL) {
            return 0;
        }
        for (size_t i = 0; i < gap_start; ++i) {
            new_buffer[i] = gap_buffer[i];
        }
        {
            const size_t tail_length = gap_capacity - gap_end;
            const size_t new_gap_end = new_capacity - tail_length;
            for (size_t i = 0; i < tail_length; ++i) {
                new_buffer[new_gap_end + i] = gap_buffer[gap_end + i];
            }
            kfree(gap_buffer);
            gap_buffer = new_buffer;
            gap_end = new_gap_end;
            gap_capacity = new_capacity;
        }
    }
    return 1;
}

static void gap_move_to(size_t logical_pos) {
    if (logical_pos > gap_text_length()) {
        logical_pos = gap_text_length();
    }
    while (gap_start > logical_pos) {
        gap_buffer[--gap_end] = gap_buffer[--gap_start];
    }
    while (gap_start < logical_pos) {
        gap_buffer[gap_start++] = gap_buffer[gap_end++];
    }
}

static int begin_gap_at_cursor(void) {
    if (!gap_init()) {
        return 0;
    }
    gap_active = 1;
    gap_anchor = cursor_offset;
    gap_delete_left = 0;
    gap_start = 0;
    gap_end = gap_capacity;
    return 1;
}

static size_t pending_start(void) {
    return gap_anchor >= gap_delete_left ? gap_anchor - gap_delete_left : 0u;
}

static int build_live_text(char **out_text, size_t *out_length) {
    size_t committed_length = 0;
    char *committed = materialize_piece_table(&committed_length);
    size_t live_length;
    char *live;
    size_t insert_length;
    size_t start;

    if (committed == NULL) {
        return 0;
    }

    if (!gap_active) {
        *out_text = committed;
        *out_length = committed_length;
        return 1;
    }

    insert_length = gap_text_length();
    start = pending_start();
    live_length = committed_length - gap_delete_left + insert_length;
    live = kmalloc(live_length + 1u);
    if (live == NULL) {
        kfree(committed);
        return 0;
    }

    {
        size_t cursor = 0;
        for (size_t i = 0; i < start; ++i) {
            live[cursor++] = committed[i];
        }
        for (size_t i = 0; i < gap_start; ++i) {
            live[cursor++] = gap_buffer[i];
        }
        for (size_t i = gap_end; i < gap_capacity; ++i) {
            live[cursor++] = gap_buffer[i];
        }
        for (size_t i = gap_anchor; i < committed_length; ++i) {
            live[cursor++] = committed[i];
        }
        live[cursor] = '\0';
    }

    kfree(committed);
    *out_text = live;
    *out_length = live_length;
    return 1;
}

static void append_delta(size_t start, size_t delete_length, size_t add_start, size_t insert_length) {
    if (!ensure_delta_capacity(delta_count + 1u)) {
        return;
    }
    deltas[delta_count].start = start;
    deltas[delta_count].delete_length = delete_length;
    deltas[delta_count].add_start = add_start;
    deltas[delta_count].insert_length = insert_length;
    ++delta_count;
}

static int save_recovery_log(void) {
    size_t total = sizeof(editor_recovery_header_t);
    uint8_t *buffer;
    editor_recovery_header_t *header;

    for (size_t i = 0; i < delta_count; ++i) {
        total += sizeof(uint64_t) * 3u + deltas[i].insert_length;
    }

    buffer = kmalloc(total);
    if (buffer == NULL) {
        return -1;
    }

    header = (editor_recovery_header_t *)buffer;
    header->magic = EDITOR_MAGIC;
    header->version = EDITOR_RECOVERY_VERSION;
    header->count = delta_count;

    {
        size_t cursor = sizeof(*header);
        for (size_t i = 0; i < delta_count; ++i) {
            const editor_delta_t *delta = &deltas[i];
            uint64_t *meta = (uint64_t *)(buffer + cursor);
            meta[0] = delta->start;
            meta[1] = delta->delete_length;
            meta[2] = delta->insert_length;
            cursor += sizeof(uint64_t) * 3u;
            for (size_t j = 0; j < delta->insert_length; ++j) {
                buffer[cursor + j] = (uint8_t)add_buffer[delta->add_start + j];
            }
            cursor += delta->insert_length;
        }
    }

    if (fs_write(recovery_path, buffer, total) != 0) {
        kfree(buffer);
        return -1;
    }

    kfree(buffer);
    return 0;
}

static void maybe_autosave_recovery(void) {
    if (autosave_flushing) {
        return;
    }
    if (dirty_chars < EDITOR_AUTOSAVE_CHARS && dirty_lines < EDITOR_AUTOSAVE_LINES) {
        return;
    }

    if (gap_active) {
        autosave_flushing = 1;
        flush_gap_to_piece_table();
        autosave_flushing = 0;
    }

    if (save_recovery_log() == 0) {
        set_status("Recovery autosave ", recovery_path);
        dirty_chars = 0;
        dirty_lines = 0;
    }
}

static void flush_gap_to_piece_table(void) {
    size_t insert_length;
    size_t add_start = 0;
    size_t start;

    if (!gap_active) {
        return;
    }

    insert_length = gap_text_length();
    start = pending_start();

    if (insert_length > 0u) {
        char *temp = kmalloc(insert_length);
        if (temp == NULL) {
            return;
        }
        for (size_t i = 0; i < gap_start; ++i) {
            temp[i] = gap_buffer[i];
        }
        for (size_t i = gap_end; i < gap_capacity; ++i) {
            temp[gap_start + (i - gap_end)] = gap_buffer[i];
        }
        if (!append_add_text(temp, insert_length, &add_start)) {
            kfree(temp);
            return;
        }
        kfree(temp);
    }

    if (gap_delete_left > 0u) {
        piece_table_delete(start, gap_delete_left);
    }
    if (insert_length > 0u) {
        piece_table_insert(start, EDITOR_SOURCE_ADD, add_start, insert_length);
    }
    if (gap_delete_left > 0u || insert_length > 0u) {
        append_delta(start, gap_delete_left, add_start, insert_length);
    }

    cursor_offset = start + insert_length;
    gap_reset();
    maybe_autosave_recovery();
}

static void reset_document(void) {
    kfree(original_buffer);
    kfree(add_buffer);
    kfree(pieces);
    kfree(deltas);
    kfree(gap_buffer);
    original_buffer = NULL;
    add_buffer = NULL;
    pieces = NULL;
    deltas = NULL;
    gap_buffer = NULL;
    original_length = 0;
    add_length = 0;
    add_capacity = 0;
    piece_count = 0;
    piece_capacity = 0;
    delta_count = 0;
    delta_capacity = 0;
    gap_capacity = 0;
    gap_start = 0;
    gap_end = 0;
    gap_anchor = 0;
    gap_delete_left = 0;
    gap_active = 0;
    cursor_offset = 0;
    preferred_column = 0;
    top_line = 0;
    dirty_chars = 0;
    dirty_lines = 0;
    current_path[0] = '\0';
    recovery_path[0] = '\0';
    title_text[0] = '\0';
    status_text[0] = '\0';
    save_prompt_path[0] = '\0';
    editor_open = 0;
    save_prompt_active = 0;
}

static void initialize_piece_table_from_original(void) {
    if (!ensure_piece_capacity(1u)) {
        return;
    }
    if (original_length > 0u) {
        pieces[0].source = EDITOR_SOURCE_ORIGINAL;
        pieces[0].start = 0;
        pieces[0].length = original_length;
        piece_count = 1u;
    }
}

static void build_recovery_path(const char *path) {
    const char *name = path_basename(path);
    size_t length;
    copy_string(recovery_path, sizeof(recovery_path), "/tmp/vex_recovery_");
    length = string_length(recovery_path);
    for (size_t i = 0; name[i] != '\0' && length + 1u < sizeof(recovery_path); ++i) {
        recovery_path[length++] = (name[i] == '/' || name[i] == ' ') ? '_' : name[i];
    }
    recovery_path[length] = '\0';
}

static void configure_document_metadata(const char *path) {
    current_path[0] = '\0';
    title_text[0] = '\0';

    if (path != NULL && path[0] != '\0') {
        copy_string(current_path, sizeof(current_path), path);
        build_recovery_path(path);
        copy_string(title_text, sizeof(title_text), "Editeur ");
        append_string(title_text, sizeof(title_text), path_basename(path));
        set_status("Edition ", path);
        return;
    }

    build_recovery_path("nouveau.txt");
    copy_string(title_text, sizeof(title_text), "Editeur nouveau.txt");
    copy_string(status_text, sizeof(status_text), "Nouveau document");
}

static void apply_recovery_log(void) {
    void *data = NULL;
    size_t size = 0;

    if (fs_read(recovery_path, &data, &size) != 0 || data == NULL || size < sizeof(editor_recovery_header_t)) {
        return;
    }

    {
        const uint8_t *bytes = data;
        const editor_recovery_header_t *header = (const editor_recovery_header_t *)bytes;
        size_t cursor = sizeof(*header);

        if (header->magic != EDITOR_MAGIC || header->version != EDITOR_RECOVERY_VERSION) {
            return;
        }

        for (size_t i = 0; i < (size_t)header->count; ++i) {
            const uint64_t *meta;
            size_t start;
            size_t delete_length;
            size_t insert_length;
            size_t add_start = 0;

            if (cursor + sizeof(uint64_t) * 3u > size) {
                break;
            }

            meta = (const uint64_t *)(bytes + cursor);
            start = (size_t)meta[0];
            delete_length = (size_t)meta[1];
            insert_length = (size_t)meta[2];
            cursor += sizeof(uint64_t) * 3u;

            if (cursor + insert_length > size) {
                break;
            }

            if (delete_length > 0u) {
                piece_table_delete(start, delete_length);
            }
            if (insert_length > 0u) {
                if (!append_add_text((const char *)(bytes + cursor), insert_length, &add_start)) {
                    break;
                }
                piece_table_insert(start, EDITOR_SOURCE_ADD, add_start, insert_length);
            }
            cursor += insert_length;
        }

        set_status("Recovery chargee ", recovery_path);
    }
}

static int write_current_document(void) {
    size_t length = 0;
    char *buffer;

    if (current_path[0] == '\0') {
        copy_string(status_text, sizeof(status_text), "Save As requis");
        return -1;
    }

    flush_gap_to_piece_table();
    buffer = materialize_piece_table(&length);
    if (buffer == NULL) {
        set_status("Erreur memoire ", display_path());
        return -1;
    }

    if (length == 0u) {
        static char newline = '\n';
        if (fs_write(current_path, &newline, 1u) != 0) {
            kfree(buffer);
            set_status("Erreur save ", display_path());
            return -1;
        }
    } else if (fs_write(current_path, buffer, length) != 0) {
        kfree(buffer);
        set_status("Erreur save ", display_path());
        return -1;
    }

    kfree(buffer);
    dirty_chars = 0;
    dirty_lines = 0;
    delta_count = 0;
    if (fs_exists(recovery_path)) {
        (void)fs_delete(recovery_path);
    }
    set_status("Sauvegarde ", display_path());
    return 0;
}

static void open_document_internal(const char *path) {
    void *data = NULL;
    size_t size = 0;

    reset_document();
    gap_init();
    configure_document_metadata(path);

    if (path != NULL && path[0] != '\0' &&
            fs_read(path, &data, &size) == 0 && data != NULL && size > 0u) {
        original_buffer = kmalloc(size);
        if (original_buffer != NULL) {
            for (size_t i = 0; i < size; ++i) {
                original_buffer[i] = ((char *)data)[i];
            }
            original_length = size;
        }
    }

    initialize_piece_table_from_original();
    apply_recovery_log();
    cursor_offset = piece_table_length();
    preferred_column = 0;
    top_line = 0;
    editor_open = 1;
}

static void begin_save_prompt(void) {
    save_prompt_active = 1u;
    if (current_path[0] != '\0') {
        copy_string(save_prompt_path, sizeof(save_prompt_path), current_path);
    } else {
        (void)mkdir("/home/root/Desktop", 0u);
        copy_string(save_prompt_path, sizeof(save_prompt_path), "nouveau.txt");
    }
}

static int confirm_save_prompt(void) {
    char resolved_path[sizeof(current_path)];
    char previous_recovery[sizeof(recovery_path)];

    if (save_prompt_path[0] == '\0') {
        copy_string(status_text, sizeof(status_text), "Chemin vide");
        return 0;
    }

    copy_string(previous_recovery, sizeof(previous_recovery), recovery_path);
    if (save_prompt_path[0] == '/') {
        copy_string(resolved_path, sizeof(resolved_path), save_prompt_path);
    } else if (starts_with(save_prompt_path, "Desktop/")) {
        (void)mkdir("/home/root/Desktop", 0u);
        copy_string(resolved_path, sizeof(resolved_path), "/home/root/");
        append_string(resolved_path, sizeof(resolved_path), save_prompt_path);
    } else {
        (void)mkdir("/home/root/Desktop", 0u);
        copy_string(resolved_path, sizeof(resolved_path), "/home/root/Desktop/");
        append_string(resolved_path, sizeof(resolved_path), save_prompt_path);
    }

    configure_document_metadata(resolved_path);
    save_prompt_active = 0u;
    if (previous_recovery[0] != '\0' && !string_equals(previous_recovery, recovery_path) && fs_exists(previous_recovery)) {
        (void)fs_delete(previous_recovery);
    }
    return write_current_document() == 0;
}

static void ensure_cursor_visible(const char *text, size_t length, uint64_t height) {
    size_t line = 0;
    size_t visible = (height - EDITOR_TOPBAR_HEIGHT - EDITOR_STATUS_HEIGHT - 2u * EDITOR_MARGIN_Y) / EDITOR_LINE_HEIGHT;

    for (size_t i = 0; i < cursor_offset && i < length; ++i) {
        if (text[i] == '\n') {
            ++line;
        }
    }

    if (line < top_line) {
        top_line = line;
    } else if (visible > 0u && line >= top_line + visible) {
        top_line = line - visible + 1u;
    }
}

static void compute_line_column(const char *text, size_t length, size_t offset, size_t *line_out, size_t *column_out) {
    size_t line = 0;
    size_t column = 0;

    for (size_t i = 0; i < offset && i < length; ++i) {
        if (text[i] == '\n') {
            ++line;
            column = 0;
        } else {
            ++column;
        }
    }

    *line_out = line;
    *column_out = column;
}

static size_t offset_from_line_column(const char *text, size_t length, size_t target_line, size_t target_column) {
    size_t line = 0;
    size_t column = 0;

    for (size_t i = 0; i < length; ++i) {
        if (line == target_line && column == target_column) {
            return i;
        }
        if (text[i] == '\n') {
            if (line == target_line) {
                return i;
            }
            ++line;
            column = 0;
        } else {
            ++column;
        }
    }

    return length;
}

static void cursor_move_horizontal(int delta) {
    if (delta < 0) {
        if (cursor_offset == 0u) {
            return;
        }
        if (gap_active) {
            size_t start = pending_start();
            size_t end = start + gap_text_length();
            if (cursor_offset > start && cursor_offset <= end) {
                --cursor_offset;
                gap_move_to(cursor_offset - start);
                return;
            }
            flush_gap_to_piece_table();
        }
        --cursor_offset;
        return;
    }

    {
        size_t live_length = 0;
        char *text;
        if (!build_live_text(&text, &live_length)) {
            return;
        }
        if (cursor_offset < live_length) {
            if (gap_active) {
                size_t start = pending_start();
                size_t end = start + gap_text_length();
                if (cursor_offset >= start && cursor_offset < end) {
                    ++cursor_offset;
                    gap_move_to(cursor_offset - start);
                    kfree(text);
                    return;
                }
                flush_gap_to_piece_table();
            }
            ++cursor_offset;
        }
        kfree(text);
    }
}

static void cursor_move_vertical(int delta) {
    size_t live_length = 0;
    char *text;
    size_t line;
    size_t column;

    flush_gap_to_piece_table();
    if (!build_live_text(&text, &live_length)) {
        return;
    }

    compute_line_column(text, live_length, cursor_offset, &line, &column);
    preferred_column = column;
    if (delta < 0 && line > 0u) {
        --line;
    } else if (delta > 0) {
        ++line;
    }
    cursor_offset = offset_from_line_column(text, live_length, line, preferred_column);
    kfree(text);
}

static void insert_char_into_gap(char c) {
    size_t pending_pos;

    if (!gap_active && !begin_gap_at_cursor()) {
        return;
    }

    pending_pos = cursor_offset - pending_start();
    gap_move_to(pending_pos);
    if (!gap_ensure_space(1u)) {
        return;
    }

    gap_buffer[gap_start++] = c;
    ++cursor_offset;
    ++dirty_chars;
    if (c == '\n') {
        ++dirty_lines;
    }
    set_status("Modifie ", display_path());
    maybe_autosave_recovery();
}

static void backspace_in_gap(void) {
    size_t start;
    size_t pending_pos;

    if (cursor_offset == 0u) {
        return;
    }

    if (!gap_active && !begin_gap_at_cursor()) {
        return;
    }

    start = pending_start();
    pending_pos = cursor_offset - start;
    gap_move_to(pending_pos);
    if (gap_start > 0u) {
        if (gap_buffer[gap_start - 1u] == '\n' && dirty_lines > 0u) {
            --dirty_lines;
        }
        --gap_start;
    } else {
        ++gap_delete_left;
    }
    --cursor_offset;
    ++dirty_chars;
    set_status("Modifie ", display_path());
    maybe_autosave_recovery();
}

static void draw_editor_frame(uint64_t x, uint64_t y, uint64_t w, uint64_t h) {
    const uint32_t shell = gui_color_rgb(10, 12, 20);
    const uint32_t pane  = gui_color_rgb(80, 38, 148);
    const uint32_t line  = gui_color_rgb(110, 70, 190);
    const uint32_t text  = gui_color_rgb(232, 228, 252);
    const uint32_t muted = gui_color_rgb(160, 148, 200);
    char status_line[96];

    gui_fill_rect(x, y, w, h, shell);
    gui_fill_rect(x, y, w, EDITOR_TOPBAR_HEIGHT, pane);
    gui_fill_rect(x, y + EDITOR_TOPBAR_HEIGHT - 1u, w, 1u, line);
    gui_fill_rect(x, y + h - EDITOR_STATUS_HEIGHT, w, EDITOR_STATUS_HEIGHT, pane);
    gui_fill_rect(x, y + h - EDITOR_STATUS_HEIGHT, w, 1u, line);
    tty_draw_text_clipped_at(x + 14u, y + 12u, w > 160u ? w - 160u : 8u, current_path[0] == '\0' ? "nouveau fichier" : current_path, text, pane);
    tty_draw_text_clipped_at(x + w - 132u, y + 12u, 104u, "CTRL+S SAVE AS", muted, pane);
    if (save_prompt_active) {
        copy_string(status_line, sizeof(status_line), "Save as (Enter): ");
        append_string(status_line, sizeof(status_line), save_prompt_path);
    } else {
        copy_string(status_line, sizeof(status_line), status_text);
    }
    tty_draw_text_clipped_at(x + 14u, y + h - 14u, w > 28u ? w - 28u : 8u, status_line, muted, pane);
}

void text_editor_ui_new_document(void) {
    open_document_internal(NULL);
}

void text_editor_ui_open(const char *path) {
    open_document_internal(path);
}

void text_editor_ui_render(uint64_t x, uint64_t y, uint64_t w, uint64_t h) {
    char *text;
    size_t length;
    uint64_t text_x;
    uint64_t text_y;
    uint64_t text_h;
    uint64_t visible_lines;
    size_t cursor_line = 0;
    size_t cursor_column = 0;

    if (!editor_open) {
        return;
    }

    if (!build_live_text(&text, &length)) {
        return;
    }

    ensure_cursor_visible(text, length, h);
    compute_line_column(text, length, cursor_offset, &cursor_line, &cursor_column);

    draw_editor_frame(x, y, w, h);
    text_x = x + EDITOR_GUTTER_WIDTH + EDITOR_MARGIN_X;
    text_y = y + EDITOR_TOPBAR_HEIGHT + EDITOR_MARGIN_Y;
    text_h = h - EDITOR_TOPBAR_HEIGHT - EDITOR_STATUS_HEIGHT - 2u * EDITOR_MARGIN_Y;
    visible_lines = text_h / EDITOR_LINE_HEIGHT;

    gui_fill_rect(x + 1u, y + EDITOR_TOPBAR_HEIGHT, EDITOR_GUTTER_WIDTH, h - EDITOR_TOPBAR_HEIGHT - EDITOR_STATUS_HEIGHT, gui_color_rgb(14, 16, 28));
    gui_fill_rect(x + EDITOR_GUTTER_WIDTH, y + EDITOR_TOPBAR_HEIGHT, 1u, h - EDITOR_TOPBAR_HEIGHT - EDITOR_STATUS_HEIGHT, gui_color_rgb(110, 70, 190));

    for (size_t index = 0, start = 0, rendered = 0; start <= length && rendered < visible_lines; ++index) {
        size_t end = start;
        char linebuf[128];
        size_t out = 0;

        while (end < length && text[end] != '\n') {
            if (out + 1u < sizeof(linebuf)) {
                linebuf[out++] = text[end];
            }
            ++end;
        }
        linebuf[out] = '\0';

        if (index >= top_line) {
            char number[16];
            size_t num = index + 1u;
            size_t count = 0;
            char digits[16];
            uint64_t row_y = text_y + rendered * EDITOR_LINE_HEIGHT;
            while (num > 0u && count < sizeof(digits)) {
                digits[count++] = (char)('0' + (num % 10u));
                num /= 10u;
            }
            if (count == 0u) {
                digits[count++] = '1';
            }
            for (size_t i = 0; i < count; ++i) {
                number[i] = digits[count - 1u - i];
            }
            number[count] = '\0';

            if (index == cursor_line) {
                gui_fill_rect(x + EDITOR_GUTTER_WIDTH + 1u, row_y - 2u, w - EDITOR_GUTTER_WIDTH - 2u, EDITOR_LINE_HEIGHT, gui_color_rgb(44, 36, 88));
            }
            tty_draw_text_clipped_at(x + 10u, row_y, EDITOR_GUTTER_WIDTH > 20u ? EDITOR_GUTTER_WIDTH - 20u : 8u, number, gui_color_rgb(140, 120, 178), gui_color_rgb(14, 16, 28));
            tty_draw_text_clipped_at(text_x, row_y, w > (text_x - x) + EDITOR_MARGIN_X ? w - (text_x - x) - EDITOR_MARGIN_X : 8u, linebuf, gui_color_rgb(232, 228, 252), index == cursor_line ? gui_color_rgb(44, 36, 88) : gui_color_rgb(10, 12, 20));
            ++rendered;
        }

        if (end == length) {
            break;
        }
        start = end + 1u;
    }

    if (cursor_line >= top_line && cursor_line < top_line + visible_lines) {
        const uint64_t caret_x = text_x + cursor_column * 8u;
        const uint64_t caret_y = text_y + (cursor_line - top_line) * EDITOR_LINE_HEIGHT;
        gui_fill_rect(caret_x, caret_y - 1u, 2u, 10u, gui_color_rgb(180, 96, 255));
    }

    kfree(text);
    gui_redraw_cursor_overlay();
}

int text_editor_ui_handle_key(char key) {
    if (!editor_open) {
        return 0;
    }

    if (save_prompt_active) {
        if (key == 27) {
            save_prompt_active = 0u;
            set_status("Edition ", display_path());
            return 1;
        }
        if (key == '\n') {
            return confirm_save_prompt();
        }
        if (key == KEYBOARD_KEY_CTRL_S) {
            return 1;
        }
        if (key == '\b') {
            size_t length = string_length(save_prompt_path);
            if (length > 0u) {
                save_prompt_path[length - 1u] = '\0';
            }
            return 1;
        }
        if ((unsigned char)key >= 32u) {
            size_t length = string_length(save_prompt_path);
            if (length + 1u < sizeof(save_prompt_path)) {
                save_prompt_path[length] = key;
                save_prompt_path[length + 1u] = '\0';
            }
            return 1;
        }
        return 1;
    }

    if (key == KEYBOARD_KEY_CTRL_S) {
        begin_save_prompt();
        return 1;
    }
    if (key == KEYBOARD_KEY_LEFT) {
        cursor_move_horizontal(-1);
        return 1;
    }
    if (key == KEYBOARD_KEY_RIGHT) {
        cursor_move_horizontal(1);
        return 1;
    }
    if (key == KEYBOARD_KEY_UP) {
        cursor_move_vertical(-1);
        return 1;
    }
    if (key == KEYBOARD_KEY_DOWN) {
        cursor_move_vertical(1);
        return 1;
    }
    if (key == '\b') {
        backspace_in_gap();
        return 1;
    }
    if (key == '\n') {
        insert_char_into_gap('\n');
        return 1;
    }
    if (key == '\t') {
        insert_char_into_gap(' ');
        insert_char_into_gap(' ');
        insert_char_into_gap(' ');
        insert_char_into_gap(' ');
        return 1;
    }
    if ((unsigned char)key >= 32u) {
        insert_char_into_gap(key);
        return 1;
    }

    return 0;
}

int text_editor_ui_is_open(void) {
    return editor_open != 0;
}

const char *text_editor_ui_title(void) {
    return title_text[0] == '\0' ? "Editeur" : title_text;
}

static void text_editor_process_loop(void) {
    gui_app_command_t command;

    editor_active = 0u;
    editor_dirty = 0u;

    for (;;) {
        while (pipe_read(editor_command_pipe, &command, sizeof(command)) == (int)sizeof(command)) {
            if (command.type == GUI_APP_CMD_CONFIGURE) {
                editor_origin_x = command.x;
                editor_origin_y = command.y;
                editor_width = command.w;
                editor_height = command.h;
                editor_active = (uint8_t)(command.flags != 0u);
                editor_dirty = 1u;
                continue;
            }

            if (command.type == GUI_APP_CMD_OPEN_FILE) {
                if (command.path[0] == '\0') {
                    text_editor_ui_new_document();
                } else {
                    text_editor_ui_open(command.path);
                }
                editor_dirty = 1u;
                continue;
            }

            if (command.type == GUI_APP_CMD_REFRESH) {
                editor_dirty = 1u;
                continue;
            }

            if (!editor_active || !editor_open) {
                continue;
            }

            if (command.type == GUI_APP_CMD_KEY) {
                if (text_editor_ui_handle_key(command.key)) {
                    editor_dirty = 1u;
                }
            }
        }

        if (editor_active && editor_dirty && editor_open && gui_terminal_surface_visible() &&
            editor_width > 0u && editor_height > 0u) {
            text_editor_ui_render(editor_origin_x, editor_origin_y, editor_width, editor_height);
            editor_dirty = 0u;
        }

        process_yield();
    }
}

uint32_t text_editor_app_start(void) {
    if (editor_pid_value != 0u) {
        return editor_pid_value;
    }

    editor_command_pipe = pipe_create();
    if (editor_command_pipe < 0) {
        editor_command_pipe = -1;
        return 0;
    }

    editor_pid_value = process_create(text_editor_process_loop, EDITOR_PROCESS_PRIORITY);
    if (editor_pid_value == 0u) {
        pipe_close(editor_command_pipe);
        editor_command_pipe = -1;
    }
    return editor_pid_value;
}

uint32_t text_editor_app_pid(void) {
    return editor_pid_value;
}

void text_editor_app_send(const gui_app_command_t *command) {
    if (command == NULL || editor_command_pipe < 0) {
        return;
    }

    (void)pipe_write(editor_command_pipe, command, sizeof(*command));
}
