#ifndef VEX_DOWNLOAD_NAMES_H
#define VEX_DOWNLOAD_NAMES_H

/*
 * File-name policy for browser downloads.
 *
 * Pure string helpers with no libc or GLib dependency, so the same code runs
 * in the browser runtime and in the hosted unit test. A download name comes
 * from the network and is untrusted: it is reduced to a single safe path
 * component, bounded so every derived path fits the VexFS path contract, and
 * made unique with a numeric suffix instead of overwriting an existing file.
 */

#include <stddef.h>
#include <stdint.h>

/* Default destination directory, created on demand through the normal FS API. */
#define VEX_DOWNLOAD_DIRECTORY "/home/root/Downloads"

/* A download is written beside its final name and renamed only on success, so
 * a truncated file never carries the name of a complete one. WebKit itself
 * streams into "<destination>.wkdownload" and moves it onto the destination
 * when the transfer completes; both suffixes mark data that is not a result. */
#define VEX_DOWNLOAD_PART_SUFFIX ".vexpart"
#define VEX_DOWNLOAD_WEBKIT_SUFFIX ".wkdownload"

/* Longest file name (bytes) before any collision suffix. With the directory,
 * " (999)" and the part suffix the longest path stays below 128 bytes. */
#define VEX_DOWNLOAD_FILE_NAME_MAX 64u
#define VEX_DOWNLOAD_MAX_COLLISION_INDEX 999u
#define VEX_DOWNLOAD_FALLBACK_NAME "download"

/* Untrusted input is examined up to this many bytes; the rest is ignored. */
#define VEX_DOWNLOAD_INPUT_MAX 1024u

/* A "real" extension is short; anything longer is considered part of the stem. */
#define VEX_DOWNLOAD_EXTENSION_MAX 16u

static inline size_t vex_download_length(const char *text) {
    size_t length = 0u;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static inline int vex_download_ends_with(const char *text, size_t length,
                                         const char *suffix) {
    size_t suffix_length = vex_download_length(suffix);
    size_t i;

    if (length < suffix_length) {
        return 0;
    }
    for (i = 0u; i < suffix_length; ++i) {
        if (text[length - suffix_length + i] != suffix[i]) {
            return 0;
        }
    }
    return 1;
}

/* Offset of the extension (including its dot), or `length` when there is none.
 * A leading dot is not an extension; ".tar.gz" style pairs are kept together
 * so "a.tar.gz" collides as "a (1).tar.gz" rather than "a.tar (1).gz". */
static inline size_t vex_download_extension_offset(const char *name, size_t length) {
    size_t dot = length;
    size_t i;

    for (i = length; i > 1u; --i) {
        if (name[i - 1u] == '.') {
            dot = i - 1u;
            break;
        }
    }
    if (dot == length || length - dot > VEX_DOWNLOAD_EXTENSION_MAX || dot == length - 1u) {
        return length;
    }
    if (vex_download_ends_with(name, length, ".gz") ||
        vex_download_ends_with(name, length, ".bz2") ||
        vex_download_ends_with(name, length, ".xz") ||
        vex_download_ends_with(name, length, ".zst")) {
        if (dot > 4u && vex_download_ends_with(name, dot, ".tar")) {
            return dot - 4u;
        }
    }
    return dot;
}

/* Offset of the dot that starts a transient suffix, or `length` when the name
 * is an ordinary file name. */
static inline size_t vex_download_transient_offset(const char *name, size_t length) {
    const size_t part = sizeof(VEX_DOWNLOAD_PART_SUFFIX) - 1u;
    const size_t both = part + sizeof(VEX_DOWNLOAD_WEBKIT_SUFFIX) - 1u;

    if (vex_download_ends_with(name, length, VEX_DOWNLOAD_PART_SUFFIX VEX_DOWNLOAD_WEBKIT_SUFFIX)) {
        return length - both;
    }
    if (vex_download_ends_with(name, length, VEX_DOWNLOAD_PART_SUFFIX)) {
        return length - part;
    }
    return length;
}

static inline int vex_download_is_utf8_continuation(unsigned char byte) {
    return (byte & 0xc0u) == 0x80u;
}

/* Reduce an untrusted suggestion to one safe component. `output` is always a
 * non-empty NUL-terminated name of at most VEX_DOWNLOAD_FILE_NAME_MAX bytes
 * (and `capacity - 1`). Returns its length, or 0 when `capacity` is too small
 * to hold even the fallback name. */
static inline size_t vex_download_sanitize_name(const char *input, char *output,
                                                size_t capacity) {
    char work[VEX_DOWNLOAD_INPUT_MAX];
    size_t limit = VEX_DOWNLOAD_FILE_NAME_MAX;
    size_t length = 0u;
    size_t begin = 0u;
    size_t i;

    if (output == NULL || capacity <= sizeof(VEX_DOWNLOAD_FALLBACK_NAME)) {
        return 0u;
    }
    if (limit > capacity - 1u) {
        limit = capacity - 1u;
    }

    /* Keep only the last path component, whichever separator was used. */
    if (input != NULL) {
        size_t start = 0u;
        for (i = 0u; input[i] != '\0'; ++i) {
            if (input[i] == '/' || input[i] == '\\') {
                start = i + 1u;
            }
        }
        for (i = start; input[i] != '\0' && length < sizeof(work) - 1u; ++i) {
            unsigned char byte = (unsigned char)input[i];
            work[length++] = (byte < 0x20u || byte == 0x7fu) ? '_' : (char)byte;
        }
    }
    work[length] = '\0';

    /* No hidden files, "." or "..", and no trailing dots/spaces. */
    while (begin < length && (work[begin] == '.' || work[begin] == ' ')) {
        ++begin;
    }
    while (length > begin && (work[length - 1u] == '.' || work[length - 1u] == ' ')) {
        --length;
    }

    /* A server must not be able to name a file like our in-progress data. */
    {
        size_t transient = vex_download_transient_offset(work + begin, length - begin);
        if (transient != length - begin) {
            work[begin + transient] = '_';
        }
    }

    length -= begin;
    if (length == 0u) {
        for (i = 0u; i < sizeof(VEX_DOWNLOAD_FALLBACK_NAME); ++i) {
            output[i] = VEX_DOWNLOAD_FALLBACK_NAME[i];
        }
        return sizeof(VEX_DOWNLOAD_FALLBACK_NAME) - 1u;
    }

    if (length > limit) {
        /* Shorten the stem and keep the extension, never splitting a UTF-8 sequence. */
        size_t extension = vex_download_extension_offset(work + begin, length);
        size_t extension_length = length - extension;
        size_t stem_budget;
        size_t stem_end;

        if (extension_length >= limit) {
            extension_length = 0u;
            extension = length;
        }
        stem_budget = limit - extension_length;
        stem_end = stem_budget < extension ? stem_budget : extension;
        while (stem_end > 0u &&
               vex_download_is_utf8_continuation((unsigned char)work[begin + stem_end])) {
            --stem_end;
        }
        if (stem_end == 0u) {
            stem_end = stem_budget < extension ? stem_budget : extension;
        }
        for (i = 0u; i < stem_end; ++i) {
            output[i] = work[begin + i];
        }
        for (i = 0u; i < extension_length; ++i) {
            output[stem_end + i] = work[begin + extension + i];
        }
        length = stem_end + extension_length;
    } else {
        for (i = 0u; i < length; ++i) {
            output[i] = work[begin + i];
        }
    }
    output[length] = '\0';
    return length;
}

static inline int vex_download_hex_value(char digit) {
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    }
    if (digit >= 'a' && digit <= 'f') {
        return digit - 'a' + 10;
    }
    if (digit >= 'A' && digit <= 'F') {
        return digit - 'A' + 10;
    }
    return -1;
}

/* Fallback when the server proposes no name: the last path segment of the URI,
 * percent-decoded, then sanitized. Query and fragment are ignored. */
static inline size_t vex_download_name_from_uri(const char *uri, char *output,
                                                size_t capacity) {
    char segment[VEX_DOWNLOAD_INPUT_MAX];
    size_t segment_length = 0u;
    size_t position = 0u;
    size_t last_separator;
    size_t path_end;
    size_t i;

    if (uri == NULL) {
        return vex_download_sanitize_name(NULL, output, capacity);
    }
    for (i = 0u; uri[i] != '\0'; ++i) {
        if (uri[i] == ':' && uri[i + 1u] == '/' && uri[i + 2u] == '/') {
            position = i + 3u;
            while (uri[position] != '\0' && uri[position] != '/' &&
                   uri[position] != '?' && uri[position] != '#') {
                ++position;
            }
            break;
        }
    }
    path_end = position;
    last_separator = position;
    while (uri[path_end] != '\0' && uri[path_end] != '?' && uri[path_end] != '#') {
        if (uri[path_end] == '/') {
            last_separator = path_end + 1u;
        }
        ++path_end;
    }
    for (i = last_separator; i < path_end && segment_length < sizeof(segment) - 1u; ++i) {
        if (uri[i] == '%' && i + 2u < path_end) {
            int high = vex_download_hex_value(uri[i + 1u]);
            int low = vex_download_hex_value(uri[i + 2u]);
            if (high >= 0 && low >= 0) {
                segment[segment_length++] = (char)(high * 16 + low);
                i += 2u;
                continue;
            }
        }
        segment[segment_length++] = uri[i];
    }
    segment[segment_length] = '\0';
    return vex_download_sanitize_name(segment, output, capacity);
}

/* directory + "/" + name, or for index > 0 "stem (index).ext". Returns 0, or -1
 * when the result does not fit `capacity` or the index is out of range. */
static inline int vex_download_candidate_path(const char *directory, const char *name,
                                              unsigned index, char *output,
                                              size_t capacity) {
    size_t name_length = vex_download_length(name);
    size_t extension = vex_download_extension_offset(name, name_length);
    size_t used = 0u;
    size_t i;
    char digits[8];
    size_t digit_count = 0u;

    if (output == NULL || capacity == 0u || index > VEX_DOWNLOAD_MAX_COLLISION_INDEX) {
        return -1;
    }
#define VEX_DOWNLOAD_APPEND(character)                  \
    do {                                                \
        if (used + 1u >= capacity) {                    \
            output[0] = '\0';                           \
            return -1;                                  \
        }                                               \
        output[used++] = (character);                   \
    } while (0)

    for (i = 0u; directory[i] != '\0'; ++i) {
        VEX_DOWNLOAD_APPEND(directory[i]);
    }
    VEX_DOWNLOAD_APPEND('/');
    for (i = 0u; i < extension; ++i) {
        VEX_DOWNLOAD_APPEND(name[i]);
    }
    if (index != 0u) {
        unsigned value = index;
        VEX_DOWNLOAD_APPEND(' ');
        VEX_DOWNLOAD_APPEND('(');
        while (value != 0u) {
            digits[digit_count++] = (char)('0' + value % 10u);
            value /= 10u;
        }
        while (digit_count != 0u) {
            VEX_DOWNLOAD_APPEND(digits[--digit_count]);
        }
        VEX_DOWNLOAD_APPEND(')');
    }
    for (i = extension; i < name_length; ++i) {
        VEX_DOWNLOAD_APPEND(name[i]);
    }
#undef VEX_DOWNLOAD_APPEND
    output[used] = '\0';
    return 0;
}

/* final path + VEX_DOWNLOAD_PART_SUFFIX. Returns 0 or -1 when it does not fit. */
static inline int vex_download_part_path(const char *final_path, char *output,
                                         size_t capacity) {
    size_t length = vex_download_length(final_path);
    size_t suffix_length = vex_download_length(VEX_DOWNLOAD_PART_SUFFIX);
    size_t i;

    if (output == NULL || length + suffix_length + 1u > capacity) {
        return -1;
    }
    for (i = 0u; i < length; ++i) {
        output[i] = final_path[i];
    }
    for (i = 0u; i <= suffix_length; ++i) {
        output[length + i] = VEX_DOWNLOAD_PART_SUFFIX[i];
    }
    return 0;
}

#endif
