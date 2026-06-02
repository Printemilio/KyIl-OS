#include <unos/inst.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/debug_log.h>
#include <unos/dns.h>
#include <unos/fs.h>
#include <unos/heap.h>
#include <unos/compat.h>
#include <unos/tls.h>
#include <unos/tty.h>
#include <unos/users.h>

#define INST_MAX_FIELD 128u
#define INST_MAX_RESPONSE (96u * 1024u)
#define INST_MAX_PACKAGE (64u * 1024u)
#define INST_PATH_MAX 160u
#define INST_MANIFEST_MAX 1024u
#define INST_EXEC_READY 0
#define INST_EXEC_PREPARED_PENDING -2
#define FNV1A_OFFSET_BASIS 14695981039346656037ull
#define FNV1A_PRIME 1099511628211ull

#define INST_FMT_UNKNOWN 0u
#define INST_FMT_ELF64   1u
#define INST_FMT_PE      2u

#define ELF_MAGIC0 0x7fu
#define ELF_MAGIC1 'E'
#define ELF_MAGIC2 'L'
#define ELF_MAGIC3 'F'
#define ELF_CLASS_64 2u
#define ELF_DATA_LITTLE 1u
#define ELF_MACHINE_X86_64 0x3eu
#define PE_MACHINE_X86_64 0x8664u
#define PE_OPTIONAL_MAGIC_PE32 0x10bu
#define PE_OPTIONAL_MAGIC_PE32_PLUS 0x20bu

typedef struct {
    uint8_t format;
    uint16_t elf_type;
    uint16_t machine;
    uint16_t section_count;
    uint64_t entry;
    uint64_t image_base;
    uint32_t subsystem;
    uint32_t import_rva;
    uint32_t import_size;
    uint8_t has_interp;
    uint64_t hash;
} inst_binary_info_t;

static int inst_ready;

static int string_equals(const char *left, const char *right) {
    size_t i = 0;
    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) {
            return 0;
        }
        ++i;
    }
    return left[i] == right[i];
}

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

static const char *path_basename(const char *path) {
    const char *name = path;

    if (path == NULL || path[0] == '\0') {
        return "app";
    }

    for (const char *cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == '/' && cursor[1] != '\0') {
            name = cursor + 1;
        }
    }

    return name;
}

static void package_name_from_path(const char *path, char *out, size_t capacity) {
    const char *name = path_basename(path);
    size_t i = 0;

    if (capacity == 0u) {
        return;
    }

    while (name[i] != '\0' && i + 1u < capacity) {
        const char c = name[i];
        out[i] = (c == '/' || c == ' ' || c == '\\') ? '_' : c;
        ++i;
    }

    out[i] = '\0';
}

static void copy_string(char *dst, const char *src, size_t capacity) {
    size_t i = 0;
    if (capacity == 0) {
        return;
    }

    while (i + 1u < capacity && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static void append_string(char *dst, size_t capacity, size_t *length, const char *src) {
    while (*src != '\0' && *length + 1u < capacity) {
        dst[*length] = *src;
        ++(*length);
        ++src;
    }
    dst[*length] = '\0';
}

static uint64_t fnv1a64(const void *data, size_t size) {
    const uint8_t *bytes = data;
    uint64_t hash = FNV1A_OFFSET_BASIS;

    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= FNV1A_PRIME;
    }

    return hash;
}

static void print_hex64(uint64_t value) {
    for (int shift = 60; shift >= 0; shift -= 4) {
        const uint8_t nibble = (uint8_t)((value >> (uint8_t)shift) & 0x0fu);
        tty_putchar((char)(nibble < 10u ? '0' + nibble : 'a' + (nibble - 10u)));
    }
}

static uint16_t read_u16(const uint8_t *data, size_t size, size_t offset) {
    if (offset + 2u > size) {
        return 0;
    }
    return (uint16_t)data[offset] | ((uint16_t)data[offset + 1u] << 8u);
}

static uint32_t read_u32(const uint8_t *data, size_t size, size_t offset) {
    if (offset + 4u > size) {
        return 0;
    }
    return (uint32_t)data[offset] |
        ((uint32_t)data[offset + 1u] << 8u) |
        ((uint32_t)data[offset + 2u] << 16u) |
        ((uint32_t)data[offset + 3u] << 24u);
}

static uint64_t read_u64(const uint8_t *data, size_t size, size_t offset) {
    const uint64_t lo = read_u32(data, size, offset);
    const uint64_t hi = read_u32(data, size, offset + 4u);
    return lo | (hi << 32u);
}

static void print_size(size_t value) {
    char digits[20];
    size_t count = 0;

    if (value == 0) {
        tty_putchar('0');
        return;
    }

    while (value > 0 && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (count > 0) {
        tty_putchar(digits[--count]);
    }
}

static void append_u64_dec(char *dst, size_t capacity, size_t *length, uint64_t value) {
    char digits[21];
    size_t count = 0;

    if (value == 0u) {
        append_string(dst, capacity, length, "0");
        return;
    }

    while (value > 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (count > 0u) {
        char one[2];
        one[0] = digits[--count];
        one[1] = '\0';
        append_string(dst, capacity, length, one);
    }
}

static void append_hex64(char *dst, size_t capacity, size_t *length, uint64_t value) {
    append_string(dst, capacity, length, "0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        const uint8_t nibble = (uint8_t)((value >> (uint8_t)shift) & 0x0fu);
        char one[2];
        one[0] = (char)(nibble < 10u ? '0' + nibble : 'a' + (nibble - 10u));
        one[1] = '\0';
        append_string(dst, capacity, length, one);
    }
}

static void progress_bar(size_t value, size_t max) {
    const size_t width = 20u;
    size_t filled = max == 0 ? 0 : (value * width) / max;
    if (filled > width) {
        filled = width;
    }

    tty_puts("inst: [");
    for (size_t i = 0; i < width; ++i) {
        tty_putchar(i < filled ? '#' : '.');
    }
    tty_puts("] ");
    print_size(value);
    tty_puts(" bytes\n");
}

static int analyze_elf64(const uint8_t *data, size_t size, inst_binary_info_t *out) {
    const uint64_t phoff = read_u64(data, size, 32u);
    const uint16_t phentsize = read_u16(data, size, 54u);
    const uint16_t phnum = read_u16(data, size, 56u);

    if (size < 64u ||
        data[0] != ELF_MAGIC0 ||
        data[1] != ELF_MAGIC1 ||
        data[2] != ELF_MAGIC2 ||
        data[3] != ELF_MAGIC3 ||
        data[4] != ELF_CLASS_64 ||
        data[5] != ELF_DATA_LITTLE) {
        return -1;
    }

    out->format = INST_FMT_ELF64;
    out->elf_type = read_u16(data, size, 16u);
    out->machine = read_u16(data, size, 18u);
    out->entry = read_u64(data, size, 24u);
    out->section_count = read_u16(data, size, 60u);
    out->image_base = 0u;
    out->subsystem = 0u;
    out->import_rva = 0u;
    out->import_size = 0u;
    out->has_interp = 0u;

    if (phoff != 0u && phentsize >= 56u && phnum != 0u && phoff < size) {
        for (uint16_t index = 0; index < phnum; ++index) {
            const uint64_t entry_offset = phoff + ((uint64_t)index * phentsize);
            if (entry_offset + 4u > size) {
                break;
            }

            if (read_u32(data, size, (size_t)entry_offset) == 3u) {
                out->has_interp = 1u;
                break;
            }
        }
    }
    return 0;
}

static int analyze_pe(const uint8_t *data, size_t size, inst_binary_info_t *out) {
    if (size < 0x100u || data[0] != 'M' || data[1] != 'Z') {
        return -1;
    }

    const uint32_t pe_offset = read_u32(data, size, 0x3cu);
    if (pe_offset + 24u > size ||
        data[pe_offset] != 'P' ||
        data[pe_offset + 1u] != 'E' ||
        data[pe_offset + 2u] != 0 ||
        data[pe_offset + 3u] != 0) {
        return -1;
    }

    const size_t coff = pe_offset + 4u;
    const uint16_t optional_size = read_u16(data, size, coff + 16u);
    const size_t optional = coff + 20u;
    if (optional + optional_size > size || optional_size < 96u) {
        return -1;
    }

    const uint16_t magic = read_u16(data, size, optional);
    const size_t data_directory = magic == PE_OPTIONAL_MAGIC_PE32_PLUS ? optional + 112u : optional + 96u;
    if (magic != PE_OPTIONAL_MAGIC_PE32 && magic != PE_OPTIONAL_MAGIC_PE32_PLUS) {
        return -1;
    }

    out->format = INST_FMT_PE;
    out->machine = read_u16(data, size, coff);
    out->section_count = read_u16(data, size, coff + 2u);
    out->entry = read_u32(data, size, optional + 16u);
    out->image_base = magic == PE_OPTIONAL_MAGIC_PE32_PLUS
        ? read_u64(data, size, optional + 24u)
        : read_u32(data, size, optional + 28u);
    out->subsystem = read_u16(data, size, optional + 68u);
    out->import_rva = data_directory + 16u <= size ? read_u32(data, size, data_directory + 8u) : 0u;
    out->import_size = data_directory + 16u <= size ? read_u32(data, size, data_directory + 12u) : 0u;
    return 0;
}

static int analyze_binary(const uint8_t *data, size_t size, inst_binary_info_t *out) {
    if (out == NULL) {
        return -1;
    }

    out->format = INST_FMT_UNKNOWN;
    out->elf_type = 0u;
    out->machine = 0u;
    out->section_count = 0u;
    out->entry = 0u;
    out->image_base = 0u;
    out->subsystem = 0u;
    out->import_rva = 0u;
    out->import_size = 0u;
    out->has_interp = 0u;
    out->hash = fnv1a64(data, size);

    if (data == NULL || size == 0u) {
        return -1;
    }

    if (analyze_elf64(data, size, out) == 0) {
        out->hash = fnv1a64(data, size);
        return 0;
    }

    if (analyze_pe(data, size, out) == 0) {
        out->hash = fnv1a64(data, size);
        return 0;
    }

    return -1;
}

static const char *format_name(uint8_t format) {
    if (format == INST_FMT_ELF64) {
        return "ELF64";
    }
    if (format == INST_FMT_PE) {
        return "PE";
    }
    return "unknown";
}

static void print_binary_info(const inst_binary_info_t *info) {
    tty_puts("format: ");
    tty_puts(format_name(info->format));
    tty_putchar('\n');
    tty_puts("machine: 0x");
    print_hex64(info->machine);
    tty_putchar('\n');
    tty_puts("entry: ");
    print_hex64(info->entry);
    tty_putchar('\n');
    if (info->format == INST_FMT_ELF64) {
        tty_puts("elf type: ");
        print_size(info->elf_type);
        tty_putchar('\n');
        tty_puts("interp: ");
        tty_puts(info->has_interp != 0u ? "yes" : "no");
        tty_putchar('\n');
    }
    tty_puts("sections: ");
    print_size(info->section_count);
    tty_putchar('\n');
    if (info->format == INST_FMT_PE) {
        tty_puts("image base: ");
        print_hex64(info->image_base);
        tty_putchar('\n');
        tty_puts("subsystem: ");
        print_size(info->subsystem);
        tty_putchar('\n');
        tty_puts("import table: rva ");
        print_hex64(info->import_rva);
        tty_puts(" size ");
        print_size(info->import_size);
        tty_putchar('\n');
    }
    tty_puts("hash: ");
    print_hex64(info->hash);
    tty_putchar('\n');
}

static int parse_https_url(const char *url, char *host, size_t host_capacity, char *path, size_t path_capacity) {
    if (!starts_with(url, "https://")) {
        return -1;
    }

    const char *cursor = url + 8u;
    size_t host_len = 0;
    while (*cursor != '\0' && *cursor != '/') {
        if (host_len + 1u < host_capacity) {
            host[host_len++] = *cursor;
        }
        ++cursor;
    }
    host[host_len] = '\0';

    copy_string(path, *cursor == '\0' ? "/" : cursor, path_capacity);
    return host[0] == '\0' ? -1 : 0;
}

static int package_name_from_url(const char *url, char *name, size_t capacity) {
    const char *end = url;
    const char *start = url;
    char host[INST_MAX_FIELD];
    char path[INST_MAX_FIELD];

    while (*end != '\0' && *end != '?' && *end != '#') {
        if (*end == '/') {
            start = end + 1;
        }
        ++end;
    }

    if (start == end) {
        if (parse_https_url(url, host, sizeof(host), path, sizeof(path)) != 0) {
            return -1;
        }
        copy_string(name, host, capacity);
        return name[0] == '\0' ? -1 : 0;
    }

    size_t i = 0;
    while (start < end && i + 1u < capacity) {
        name[i++] = *start++;
    }
    name[i] = '\0';
    return name[0] == '\0' ? -1 : 0;
}

static uint8_t *http_body(uint8_t *response, size_t size, size_t *body_size) {
    for (size_t i = 0; i + 3u < size; ++i) {
        if (response[i] == '\r' && response[i + 1u] == '\n' &&
            response[i + 2u] == '\r' && response[i + 3u] == '\n') {
            *body_size = size - i - 4u;
            return response + i + 4u;
        }
    }

    *body_size = size;
    return response;
}

static int https_download(const char *url, uint8_t **data, size_t *size) {
    char host[INST_MAX_FIELD];
    char path[INST_MAX_FIELD];
    uint32_t ip = 0;

    if (parse_https_url(url, host, sizeof(host), path, sizeof(path)) != 0) {
        tty_puts("inst: only https URLs are supported\n");
        return -1;
    }

    tty_puts("inst: resolving ");
    tty_puts(host);
    tty_putchar('\n');
    if (dns_resolve(host, &ip) != 0) {
        tty_puts("inst: dns failed\n");
        return -1;
    }

    tty_puts("inst: connecting tls\n");
    int sock = tls_connect(ip, 443, host);
    if (sock < 0) {
        tty_puts("inst: tls connect failed\n");
        return -1;
    }

    char request[384];
    size_t request_len = 0;
    append_string(request, sizeof(request), &request_len, "GET ");
    append_string(request, sizeof(request), &request_len, path);
    append_string(request, sizeof(request), &request_len, " HTTP/1.1\r\nHost: ");
    append_string(request, sizeof(request), &request_len, host);
    append_string(request, sizeof(request), &request_len, "\r\nConnection: close\r\n\r\n");

    if (tls_send(sock, request, request_len) != 0) {
        tls_close(sock);
        tty_puts("inst: download request failed\n");
        return -1;
    }

    tty_puts("inst: downloading\n");
    uint8_t *response = kmalloc(INST_MAX_RESPONSE);
    if (response == NULL) {
        tls_close(sock);
        return -1;
    }

    size_t total = 0;
    for (;;) {
        size_t chunk = INST_MAX_RESPONSE - total;
        if (chunk == 0) {
            break;
        }

        if (tls_recv(sock, response + total, &chunk) != 0 || chunk == 0) {
            break;
        }
        total += chunk;
        progress_bar(total, INST_MAX_RESPONSE);
    }

    tls_close(sock);

    if (total == 0) {
        kfree(response);
        tty_puts("inst: empty download\n");
        return -1;
    }

    size_t payload_size = 0;
    uint8_t *payload = http_body(response, total, &payload_size);
    if (payload_size == 0 || payload_size > INST_MAX_PACKAGE) {
        kfree(response);
        tty_puts("inst: invalid package size\n");
        return -1;
    }

    uint8_t *package = kmalloc(payload_size);
    if (package == NULL) {
        kfree(response);
        return -1;
    }

    for (size_t i = 0; i < payload_size; ++i) {
        package[i] = payload[i];
    }

    kfree(response);
    *data = package;
    *size = payload_size;
    tty_puts("inst: downloaded ");
    print_size(payload_size);
    tty_puts(" bytes\n");
    return 0;
}

static void install_path(char *out, size_t capacity, const char *name) {
    size_t length = 0;
    append_string(out, capacity, &length, "/home/");
    append_string(out, capacity, &length, user_current_username());
    append_string(out, capacity, &length, "/bin/");
    append_string(out, capacity, &length, name);
}

static int install_url(const char *url) {
    char name[INST_MAX_FIELD];
    char path[INST_MAX_FIELD];
    uint8_t *package_data = NULL;
    size_t package_size = 0;

    if (package_name_from_url(url, name, sizeof(name)) != 0) {
        tty_puts("inst: invalid package URL\n");
        return -1;
    }

    tty_puts("inst: package ");
    tty_puts(name);
    tty_putchar('\n');

    if (https_download(url, &package_data, &package_size) != 0) {
        return -1;
    }

    tty_puts("inst: warning: no signature found (fnv1a64 ");
    print_hex64(fnv1a64(package_data, package_size));
    tty_puts(")\n");

    install_path(path, sizeof(path), name);
    tty_puts("inst: installing to ");
    tty_puts(path);
    tty_putchar('\n');
    if (fs_write(path, package_data, package_size) != 0) {
        kfree(package_data);
        tty_puts("inst: install failed\n");
        return -1;
    }

    kfree(package_data);
    tty_puts("inst: installed ");
    tty_puts(name);
    tty_puts(" to ");
    tty_puts(path);
    tty_puts(" (");
    print_size(package_size);
    tty_puts(" bytes)");
    tty_putchar('\n');
    return 0;
}

static int remove_name(const char *name) {
    char path[INST_MAX_FIELD];

    if (name == NULL || name[0] == '\0') {
        tty_puts("usage: inst remove <name>\n");
        return -1;
    }

    install_path(path, sizeof(path), name);
    if (fs_delete(path) != 0) {
        tty_puts("inst: remove failed\n");
        return -1;
    }
    inst_cleanup_for_path(path);

    tty_puts("inst: removed ");
    tty_puts(name);
    tty_puts(" from ");
    tty_puts(path);
    tty_putchar('\n');
    return 0;
}

static void package_path(char *out, size_t capacity, const char *name, const char *suffix) {
    size_t length = 0;
    append_string(out, capacity, &length, "/home/");
    append_string(out, capacity, &length, user_current_username());
    append_string(out, capacity, &length, "/packages/");
    append_string(out, capacity, &length, name);
    append_string(out, capacity, &length, suffix);
}

static void hidden_package_path(char *out, size_t capacity, const char *name, const char *suffix) {
    size_t length = 0;
    append_string(out, capacity, &length, "/home/");
    append_string(out, capacity, &length, user_current_username());
    append_string(out, capacity, &length, "/packages/.");
    append_string(out, capacity, &length, name);
    append_string(out, capacity, &length, suffix);
}

static void hidden_source_meta_path(char *out, size_t capacity, const char *source_path, const char *suffix) {
    size_t length = 0;
    const char *name = path_basename(source_path);
    const char *end = name;

    while (source_path != NULL && source_path < name && length + 1u < capacity) {
        out[length++] = *source_path++;
    }
    out[length] = '\0';

    append_string(out, capacity, &length, ".");
    while (*end != '\0' && length + 1u < capacity) {
        out[length++] = *end++;
    }
    out[length] = '\0';
    append_string(out, capacity, &length, suffix);
}

static void build_manifest(char *out, size_t capacity, const char *name, const inst_binary_info_t *info, const char *source_path) {
    size_t length = 0;

    append_string(out, capacity, &length, "vex.package=");
    append_string(out, capacity, &length, name);
    append_string(out, capacity, &length, "\nsource=");
    append_string(out, capacity, &length, source_path);
    append_string(out, capacity, &length, "\nformat=");
    append_string(out, capacity, &length, format_name(info->format));
    append_string(out, capacity, &length, "\nmachine=");
    append_hex64(out, capacity, &length, info->machine);
    append_string(out, capacity, &length, "\nentry=");
    append_hex64(out, capacity, &length, info->entry);
    append_string(out, capacity, &length, "\nsections=");
    append_u64_dec(out, capacity, &length, info->section_count);
    append_string(out, capacity, &length, "\nhash=");
    append_hex64(out, capacity, &length, info->hash);
    append_string(out, capacity, &length, "\nabi=");
    append_string(out, capacity, &length, info->format == INST_FMT_ELF64 ? "foreign-elf64" : "windows-pe");
    append_string(out, capacity, &length, "\ntranslation=");
    append_string(out, capacity, &length, info->format == INST_FMT_ELF64 ? "linux-syscall-map-pending" : "pe-import-map");
    append_string(out, capacity, &length, "\nstatus=");
    append_string(out, capacity, &length, "prepared");
    append_string(out, capacity, &length, "\n");
}

static void build_meta(char *out, size_t capacity, const char *name, const inst_binary_info_t *info) {
    size_t length = 0;

    append_string(out, capacity, &length, "nom=");
    append_string(out, capacity, &length, name);
    append_string(out, capacity, &length, "\ndescription=");
    append_string(out, capacity, &length, info->format == INST_FMT_PE ? "Windows PE package" : "Foreign ELF package");
    append_string(out, capacity, &length, "\ntags=app package translated ");
    append_string(out, capacity, &length, format_name(info->format));
    append_string(out, capacity, &length, "\nhidden=true\n");
}

static int prepare_package_for_path(const char *source_path, const char *name, const void *data, size_t size, const inst_binary_info_t *info, int hidden) {
    char payload_target[INST_PATH_MAX];
    char manifest_target[INST_PATH_MAX];
    char meta_target[INST_PATH_MAX];
    char manifest[INST_MANIFEST_MAX];
    char meta[INST_MANIFEST_MAX];
    const char *payload_suffix = info->format == INST_FMT_PE ? ".pe" : ".elf";

    if (hidden) {
        hidden_package_path(payload_target, sizeof(payload_target), name, payload_suffix);
        hidden_package_path(manifest_target, sizeof(manifest_target), name, ".vexabi");
        hidden_source_meta_path(meta_target, sizeof(meta_target), source_path, ".meta");
    } else {
        package_path(payload_target, sizeof(payload_target), name, payload_suffix);
        package_path(manifest_target, sizeof(manifest_target), name, ".vexabi");
        package_path(meta_target, sizeof(meta_target), name, ".meta");
    }

    build_manifest(manifest, sizeof(manifest), name, info, source_path);
    build_meta(meta, sizeof(meta), name, info);

    if (fs_write(payload_target, (void *)data, size) != 0 ||
        fs_write(manifest_target, manifest, string_length(manifest)) != 0 ||
        fs_write(meta_target, meta, string_length(meta)) != 0) {
        return -1;
    }

    return 0;
}

static int info_path(const char *path) {
    void *data = NULL;
    size_t size = 0;
    inst_binary_info_t info;

    if (path == NULL || fs_read(path, &data, &size) != 0 || data == NULL) {
        tty_puts("inst: file not found\n");
        return -1;
    }

    if (analyze_binary(data, size, &info) != 0) {
        tty_puts("inst: unsupported binary\n");
        return -1;
    }

    tty_puts("path: ");
    tty_puts(path);
    tty_putchar('\n');
    tty_puts("size: ");
    print_size(size);
    tty_putchar('\n');
    print_binary_info(&info);
    if (info.format == INST_FMT_PE) {
        tty_puts("translation: PE parsed; imports must map to Vex ABI before execution\n");
    } else if (info.format == INST_FMT_ELF64) {
        tty_puts("translation: ELF parsed; foreign syscalls must map to Vex ABI before execution\n");
    }
    return 0;
}

static int translate_path(const char *source_path, const char *name) {
    void *data = NULL;
    size_t size = 0;
    inst_binary_info_t info;

    if (source_path == NULL || name == NULL || name[0] == '\0') {
        tty_puts("usage: inst translate <path> <name>\n");
        return -1;
    }

    if (fs_read(source_path, &data, &size) != 0 || data == NULL) {
        tty_puts("inst: file not found\n");
        return -1;
    }

    if (analyze_binary(data, size, &info) != 0) {
        tty_puts("inst: unsupported binary\n");
        return -1;
    }

    if (info.format == INST_FMT_ELF64) {
        if (info.machine != ELF_MACHINE_X86_64) {
            tty_puts("inst: ELF machine unsupported\n");
            return -1;
        }

        tty_puts("inst: translating ELF -> VexOS ABI package\n");
        if (prepare_package_for_path(source_path, name, data, size, &info, 0) != 0) {
            tty_puts("inst: write failed\n");
            return -1;
        }

        tty_puts("inst: ELF package prepared; execution waits for syscall thunk runtime\n");
        return 0;
    }

    if (info.format == INST_FMT_PE) {
        if (info.machine != PE_MACHINE_X86_64) {
            tty_puts("inst: only PE x86_64 is prepared currently\n");
            return -1;
        }

        tty_puts("inst: translating PE -> VexOS ABI package\n");
        if (prepare_package_for_path(source_path, name, data, size, &info, 0) != 0) {
            tty_puts("inst: write failed\n");
            return -1;
        }

        tty_puts("inst: PE package prepared; execution waits for PE import thunk runtime\n");
        return 0;
    }

    tty_puts("inst: unsupported binary\n");
    return -1;
}

int inst_prepare_for_execution(const char *path, char *launch_path, size_t launch_path_capacity) {
    void *data = NULL;
    size_t size = 0;
    inst_binary_info_t info;
    char name[INST_MAX_FIELD];

    if (path == NULL || launch_path == NULL || launch_path_capacity == 0u) {
        return -1;
    }

    copy_string(launch_path, path, launch_path_capacity);

    if (fs_read(path, &data, &size) != 0 || data == NULL) {
        debug_log_puts("inst: fs_read failed ");
        debug_log_puts(path);
        debug_log_puts("\n");
        return -1;
    }

    if (analyze_binary(data, size, &info) != 0) {
        debug_log_puts("inst: analyze failed ");
        debug_log_puts(path);
        debug_log_puts("\n");
        return -1;
    }

    if (info.format == INST_FMT_ELF64) {
        return INST_EXEC_READY;
    }

    package_name_from_path(path, name, sizeof(name));
    if (prepare_package_for_path(path, name, data, size, &info, 1) != 0) {
        tty_puts("inst: automatic translation failed\n");
        return -1;
    }

    tty_puts("inst: prepared ");
    tty_puts(format_name(info.format));
    tty_puts(" package for ");
    tty_puts(path);
    tty_putchar('\n');

    if (info.format == INST_FMT_ELF64) {
        return INST_EXEC_READY;
    }

    if (info.format == INST_FMT_PE) {
        tty_puts("inst: PE execution waits for import thunk runtime\n");
    } else {
        tty_puts("inst: ELF execution waits for syscall thunk runtime\n");
    }

    return INST_EXEC_PREPARED_PENDING;
}

uint8_t inst_abi_for_path(const char *path) {
    void *data = NULL;
    size_t size = 0;
    inst_binary_info_t info;

    if (path == NULL || fs_read(path, &data, &size) != 0 || data == NULL || analyze_binary(data, size, &info) != 0) {
        return VEX_ABI_NATIVE;
    }

    if (info.format == INST_FMT_PE) {
        return VEX_ABI_PE;
    }

    if (info.format == INST_FMT_ELF64 &&
        (!starts_with(path, "/home/root/bin/") || info.elf_type != 2u || info.has_interp != 0u)) {
        return VEX_ABI_LINUX;
    }

    return VEX_ABI_NATIVE;
}

void inst_cleanup_for_path(const char *path) {
    char name[INST_MAX_FIELD];
    char sidecar[INST_PATH_MAX];

    if (path == NULL || path[0] == '\0') {
        return;
    }

    package_name_from_path(path, name, sizeof(name));

    hidden_package_path(sidecar, sizeof(sidecar), name, ".pe");
    (void)fs_delete(sidecar);
    hidden_package_path(sidecar, sizeof(sidecar), name, ".elf");
    (void)fs_delete(sidecar);
    hidden_package_path(sidecar, sizeof(sidecar), name, ".vexabi");
    (void)fs_delete(sidecar);
    hidden_source_meta_path(sidecar, sizeof(sidecar), path, ".meta");
    (void)fs_delete(sidecar);
}

void inst_init(void) {
    inst_ready = 1;
}

int inst_execute(int argc, char **argv) {
    if (!inst_ready || argc < 2 || argv == NULL || argv[0] == NULL) {
        tty_puts("usage: inst install <url> | inst info <path> | inst translate <path> <name> | inst remove <name>\n");
        return -1;
    }

    if (string_equals(argv[0], "install") && argc == 2) {
        return install_url(argv[1]);
    }

    if (string_equals(argv[0], "info") && argc == 2) {
        return info_path(argv[1]);
    }

    if (string_equals(argv[0], "translate") && argc == 3) {
        return translate_path(argv[1], argv[2]);
    }

    if (string_equals(argv[0], "remove") && argc == 2) {
        return remove_name(argv[1]);
    }

    tty_puts("usage: inst install <url> | inst info <path> | inst translate <path> <name> | inst remove <name>\n");
    return -1;
}
