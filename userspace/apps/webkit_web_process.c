#include <stddef.h>
#include <stdint.h>

#include <unos/vex_web_compat.h>
#include <unos/vex_abi.h>

#include "vex_http_client.h"
#include "vex_page_cache.h"
#include "vex_syscall.h"

enum {
    kWidth = 1280u,
    kHeight = 720u,
    kStride = kWidth,
    kMaxCommand = 512u,
    kMaxUrl = 512u,
    kMaxTitle = 160u,
    kMaxStatus = 128u,
    kMaxText = 16384u,
    kMaxResponse = 262144u
};

static uint32_t g_pixels[kWidth * kHeight];
static char g_last_command[kMaxCommand];
static char g_current_url[kMaxUrl];
static char g_title[kMaxTitle];
static char g_status[kMaxStatus];
static char g_body[kMaxText];

static const char* find_case_insensitive(const char* text, const char* needle);

static int display_open(void)
{
    return (int)vex_syscall2(VEX_SYS_DISPLAY_OPEN, (uint64_t)kWidth, (uint64_t)kHeight);
}

static void display_present(void)
{
    vex_display_present_req_t req;
    req.pixels = g_pixels;
    req.width = kWidth;
    req.height = kHeight;
    req.stride = kStride;
    req.dirty = 0;
    (void)vex_syscall1(VEX_SYS_DISPLAY_PRESENT, (uint64_t)(uintptr_t)&req);
}

static int command_read(char* buffer, size_t size)
{
    vex_browser_command_req_t req;
    req.buffer = buffer;
    req.size = size;
    return (int)vex_syscall1(VEX_SYS_BROWSER_COMMAND_READ, (uint64_t)(uintptr_t)&req);
}

static size_t string_length(const char* text)
{
    size_t length = 0;
    while (text && text[length] != '\0') {
        ++length;
    }
    return length;
}

static char to_lower_ascii(char value)
{
    if (value >= 'A' && value <= 'Z') {
        return (char)(value - 'A' + 'a');
    }
    return value;
}

static int strings_equal(const char* left, const char* right)
{
    size_t index = 0;
    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return 0;
        }
        ++index;
    }
    return left[index] == right[index];
}

static int string_starts_with(const char* text, const char* prefix)
{
    size_t index = 0;
    while (prefix[index] != '\0') {
        if (text[index] != prefix[index]) {
            return 0;
        }
        ++index;
    }
    return 1;
}

static int string_starts_with_case_insensitive(const char* text, const char* prefix)
{
    size_t index = 0;
    while (prefix[index] != '\0') {
        if (to_lower_ascii(text[index]) != to_lower_ascii(prefix[index])) {
            return 0;
        }
        ++index;
    }
    return 1;
}

static void copy_string(char* destination, size_t capacity, const char* source)
{
    size_t index = 0;
    if (capacity == 0) {
        return;
    }
    if (!source) {
        destination[0] = '\0';
        return;
    }
    while (index + 1u < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

static void append_buffer(char* destination, size_t capacity, size_t* length, const char* source)
{
    size_t index = 0;
    if (!destination || !length || !source) {
        return;
    }
    while (source[index] != '\0' && *length + 1u < capacity) {
        destination[(*length)++] = source[index++];
    }
    destination[*length] = '\0';
}

static void append_uint(char* destination, size_t capacity, size_t* length, uint32_t value)
{
    char digits[16];
    size_t used = 0;
    if (value == 0) {
        append_buffer(destination, capacity, length, "0");
        return;
    }
    while (value && used < sizeof(digits)) {
        digits[used++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (used > 0 && *length + 1u < capacity) {
        destination[(*length)++] = digits[--used];
    }
    destination[*length] = '\0';
}

static void url_encode_append(char* destination, size_t capacity, size_t* length, const char* source)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t index;
    for (index = 0; source[index] != '\0' && *length + 1u < capacity; ++index) {
        unsigned char c = (unsigned char)source[index];
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            destination[(*length)++] = (char)c;
        } else if (c == ' ') {
            destination[(*length)++] = '+';
        } else if (*length + 3u < capacity) {
            destination[(*length)++] = '%';
            destination[(*length)++] = hex[(c >> 4u) & 0x0fu];
            destination[(*length)++] = hex[c & 0x0fu];
        }
    }
    destination[*length] = '\0';
}

static void fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    uint32_t row;
    if (x >= kWidth || y >= kHeight) return;
    if (x + w > kWidth)  w = kWidth - x;
    if (y + h > kHeight) h = kHeight - y;
    for (row = 0; row < h; ++row) {
        uint32_t* pixels = g_pixels + (size_t)(y + row) * kStride + x;
        uint32_t col;
        for (col = 0; col < w; ++col) {
            pixels[col] = color;
        }
    }
}

static const uint8_t* glyph_bits(char c)
{
    static const uint8_t unk[7] = {0x0e,0x11,0x01,0x06,0x04,0x00,0x04};
    static const uint8_t gl[][7] = {
        {0x0e,0x11,0x13,0x15,0x19,0x11,0x0e},{0x04,0x0c,0x04,0x04,0x04,0x04,0x0e},
        {0x0e,0x11,0x01,0x02,0x04,0x08,0x1f},{0x1e,0x01,0x01,0x0e,0x01,0x01,0x1e},
        {0x02,0x06,0x0a,0x12,0x1f,0x02,0x02},{0x1f,0x10,0x10,0x1e,0x01,0x11,0x0e},
        {0x06,0x08,0x10,0x1e,0x11,0x11,0x0e},{0x1f,0x01,0x02,0x04,0x08,0x08,0x08},
        {0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e},{0x0e,0x11,0x11,0x0f,0x01,0x02,0x0c},
        {0x0e,0x11,0x11,0x1f,0x11,0x11,0x11},{0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e},
        {0x0e,0x11,0x10,0x10,0x10,0x11,0x0e},{0x1e,0x11,0x11,0x11,0x11,0x11,0x1e},
        {0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f},{0x1f,0x10,0x10,0x1e,0x10,0x10,0x10},
        {0x0e,0x11,0x10,0x17,0x11,0x11,0x0f},{0x11,0x11,0x11,0x1f,0x11,0x11,0x11},
        {0x0e,0x04,0x04,0x04,0x04,0x04,0x0e},{0x07,0x02,0x02,0x02,0x12,0x12,0x0c},
        {0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1f},
        {0x11,0x1b,0x15,0x15,0x11,0x11,0x11},{0x11,0x19,0x15,0x13,0x11,0x11,0x11},
        {0x0e,0x11,0x11,0x11,0x11,0x11,0x0e},{0x1e,0x11,0x11,0x1e,0x10,0x10,0x10},
        {0x0e,0x11,0x11,0x11,0x15,0x12,0x0d},{0x1e,0x11,0x11,0x1e,0x14,0x12,0x11},
        {0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e},{0x1f,0x04,0x04,0x04,0x04,0x04,0x04},
        {0x11,0x11,0x11,0x11,0x11,0x11,0x0e},{0x11,0x11,0x11,0x11,0x0a,0x0a,0x04},
        {0x11,0x11,0x11,0x15,0x15,0x1b,0x11},{0x11,0x11,0x0a,0x04,0x0a,0x11,0x11},
        {0x11,0x11,0x0a,0x04,0x04,0x04,0x04},{0x1f,0x01,0x02,0x04,0x08,0x10,0x1f}
    };
    static const uint8_t dash[7] = {0x00,0x00,0x00,0x1f,0x00,0x00,0x00};
    static const uint8_t dot[7] = {0x00,0x00,0x00,0x00,0x00,0x0c,0x0c};
    static const uint8_t slash[7] = {0x01,0x01,0x02,0x04,0x08,0x10,0x10};
    static const uint8_t colon[7] = {0x00,0x0c,0x0c,0x00,0x0c,0x0c,0x00};
    static const uint8_t excl[7] = {0x04,0x04,0x04,0x04,0x04,0x00,0x04};
    static const uint8_t ques[7] = {0x0e,0x11,0x01,0x06,0x04,0x00,0x04};

    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c >= '0' && c <= '9') return gl[c - '0'];
    if (c >= 'A' && c <= 'Z') return gl[10 + c - 'A'];
    switch (c) {
    case '-': return dash;
    case '.': return dot;
    case '/': return slash;
    case ':': return colon;
    case '!': return excl;
    case '?': return ques;
    default: return unk;
    }
}

static void draw_char(uint32_t x, uint32_t y, char c, uint32_t color, uint32_t scale)
{
    uint32_t row;
    if (c == ' ') return;
    for (row = 0; row < 7u; ++row) {
        const uint8_t* g = glyph_bits(c);
        uint32_t col;
        for (col = 0; col < 5u; ++col) {
            if (!(g[row] & (uint8_t)(1u << (4u - col)))) continue;
            fill(x + col * scale, y + row * scale, scale, scale, color);
        }
    }
}

static uint32_t draw_text(uint32_t x, uint32_t y, uint32_t maxw, const char* text, uint32_t color, uint32_t scale)
{
    uint32_t cx = x;
    uint32_t cy = y;
    uint32_t step = 6u * scale;
    uint32_t line_height = 9u * scale;
    uint32_t glyph_width = 5u * scale;
    size_t i;
    if (!text) return y;
    for (i = 0; text[i] != '\0'; ++i) {
        char value = text[i];
        if (value == '\n') {
            cx = x;
            cy += line_height;
            continue;
        }
        if (cx + glyph_width > x + maxw && cx > x) {
            cx = x;
            cy += line_height;
        }
        draw_char(cx, cy, value, color, scale);
        cx += (value == ' ') ? 4u * scale : step;
    }
    return cy + line_height;
}

static int parse_fetch_url(const char* url, char* host, size_t host_capacity, char* path, size_t path_capacity, uint16_t* port)
{
    const char* cursor = url;
    size_t host_length = 0;
    if (string_starts_with_case_insensitive(cursor, "http://")) {
        cursor += 7;
        *port = 80;
    } else if (string_starts_with_case_insensitive(cursor, "https://")) {
        cursor += 8;
        *port = 443;
    } else {
        return -1;
    }

    while (cursor[host_length] != '\0' && cursor[host_length] != '/' && cursor[host_length] != ':' && host_length + 1u < host_capacity) {
        host[host_length] = to_lower_ascii(cursor[host_length]);
        ++host_length;
    }
    host[host_length] = '\0';
    if (host_length == 0) {
        return -1;
    }

    cursor += host_length;
    if (*cursor == ':') {
        uint16_t parsed_port = 0;
        ++cursor;
        while (*cursor >= '0' && *cursor <= '9') {
            parsed_port = (uint16_t)(parsed_port * 10u + (uint16_t)(*cursor - '0'));
            ++cursor;
        }
        if (parsed_port != 0) {
            *port = parsed_port;
        }
    }

    while (*cursor != '\0' && *cursor != '/') {
        ++cursor;
    }
    if (*cursor == '\0') {
        copy_string(path, path_capacity, "/");
    } else {
        copy_string(path, path_capacity, cursor);
    }
    return 0;
}

static const char* find_http_body(const char* response)
{
    size_t index;
    for (index = 0; response[index] != '\0'; ++index) {
        if (response[index] == '\r' && response[index + 1] == '\n' &&
            response[index + 2] == '\r' && response[index + 3] == '\n') {
            return response + index + 4;
        }
        if (response[index] == '\n' && response[index + 1] == '\n') {
            return response + index + 2;
        }
    }
    return response;
}

static int parse_http_status(const char* response)
{
    int status = 0;
    if (!response || !string_starts_with(response, "HTTP/")) {
        return 0;
    }
    while (*response != '\0' && *response != ' ') ++response;
    while (*response == ' ') ++response;
    while (*response >= '0' && *response <= '9') {
        status = status * 10 + (*response - '0');
        ++response;
    }
    return status;
}

static int extract_location_header(const char* response, char* out, size_t capacity)
{
    size_t i;
    for (i = 0; response[i] != '\0'; ++i) {
        if ((i == 0 || response[i - 1] == '\n') &&
            to_lower_ascii(response[i]) == 'l' &&
            to_lower_ascii(response[i + 1]) == 'o' &&
            to_lower_ascii(response[i + 2]) == 'c' &&
            to_lower_ascii(response[i + 3]) == 'a' &&
            to_lower_ascii(response[i + 4]) == 't' &&
            to_lower_ascii(response[i + 5]) == 'i' &&
            to_lower_ascii(response[i + 6]) == 'o' &&
            to_lower_ascii(response[i + 7]) == 'n' &&
            response[i + 8] == ':') {
            size_t cursor = i + 9u;
            size_t out_len = 0;
            while (response[cursor] == ' ' || response[cursor] == '\t') ++cursor;
            while (response[cursor] != '\0' && response[cursor] != '\r' && response[cursor] != '\n' && out_len + 1u < capacity) {
                out[out_len++] = response[cursor++];
            }
            out[out_len] = '\0';
            return out_len == 0 ? -1 : 0;
        }
    }
    return -1;
}

static int is_chunked_response(const char* response)
{
    const char* marker = "transfer-encoding: chunked";
    size_t i;
    for (i = 0; response[i] != '\0'; ++i) {
        size_t j = 0;
        while (marker[j] != '\0' && to_lower_ascii(response[i + j]) == marker[j]) {
            ++j;
        }
        if (marker[j] == '\0') {
            return 1;
        }
    }
    return 0;
}

static size_t decode_chunked_body(char* body, size_t body_len)
{
    const char* in = body;
    const char* end = body + body_len;
    char* out = body;
    size_t out_len = 0;

    while (in < end) {
        uint32_t chunk_size = 0;
        int got_digit = 0;
        while (in < end && *in != '\r' && *in != '\n' && *in != ';') {
            char c = *in++;
            if (c >= '0' && c <= '9') { chunk_size = chunk_size * 16u + (uint32_t)(c - '0'); got_digit = 1; }
            else if (c >= 'a' && c <= 'f') { chunk_size = chunk_size * 16u + (uint32_t)(c - 'a' + 10u); got_digit = 1; }
            else if (c >= 'A' && c <= 'F') { chunk_size = chunk_size * 16u + (uint32_t)(c - 'A' + 10u); got_digit = 1; }
            else break;
        }
        while (in < end && *in != '\n') ++in;
        if (in < end) ++in;
        if (!got_digit || chunk_size == 0) break;
        while (chunk_size > 0 && in < end) {
            out[out_len++] = *in++;
            --chunk_size;
        }
        while (in < end && (*in == '\r' || *in == '\n')) ++in;
    }
    out[out_len] = '\0';
    return out_len;
}

static void decode_html_entity(const char** cursor, char* out)
{
    if (string_starts_with(*cursor, "&amp;")) { *out = '&'; *cursor += 5; return; }
    if (string_starts_with(*cursor, "&lt;")) { *out = '<'; *cursor += 4; return; }
    if (string_starts_with(*cursor, "&gt;")) { *out = '>'; *cursor += 4; return; }
    if (string_starts_with(*cursor, "&quot;")) { *out = '"'; *cursor += 6; return; }
    if (string_starts_with(*cursor, "&#39;")) { *out = '\''; *cursor += 5; return; }
    if (string_starts_with(*cursor, "&nbsp;")) { *out = ' '; *cursor += 6; return; }
    *out = **cursor;
    *cursor += 1;
}

static void extract_title_from_html(const char* html)
{
    const char* start;
    const char* end;
    size_t len = 0;
    g_title[0] = '\0';

    start = html;
    while (*start != '\0') {
        if (string_starts_with_case_insensitive(start, "<title")) {
            while (*start != '\0' && *start != '>') ++start;
            if (*start == '>') ++start;
            end = start;
            while (*end != '\0' && !string_starts_with_case_insensitive(end, "</title")) ++end;
            while (start < end && len + 1u < sizeof(g_title)) {
                char c;
                if (*start == '&') decode_html_entity(&start, &c);
                else c = *start++;
                if (c == '\r' || c == '\n' || c == '\t') c = ' ';
                g_title[len++] = c;
            }
            break;
        }
        ++start;
    }
    while (len > 0 && g_title[len - 1u] == ' ') --len;
    g_title[len] = '\0';
}

static void extract_text_from_html(const char* html)
{
    int inside_tag = 0;
    int previous_space = 1;
    size_t len = 0;
    const char* cursor;
    const char* body = find_case_insensitive(html, "<body");

    if (body != NULL) {
        while (*body != '\0' && *body != '>') ++body;
        if (*body == '>') ++body;
        cursor = body;
    } else {
        cursor = html;
    }

    g_body[0] = '\0';
    while (*cursor != '\0' && len + 1u < sizeof(g_body)) {
        if (string_starts_with_case_insensitive(cursor, "<!--")) {
            const char* end = find_case_insensitive(cursor, "-->");
            cursor = end != NULL ? end + 3 : cursor + 4;
            continue;
        }
        if (string_starts_with_case_insensitive(cursor, "<script") ||
            string_starts_with_case_insensitive(cursor, "<style") ||
            string_starts_with_case_insensitive(cursor, "<noscript") ||
            string_starts_with_case_insensitive(cursor, "<svg") ||
            string_starts_with_case_insensitive(cursor, "<template")) {
            const char* close_tag = NULL;
            if (string_starts_with_case_insensitive(cursor, "<script")) close_tag = "</script";
            else if (string_starts_with_case_insensitive(cursor, "<style")) close_tag = "</style";
            else if (string_starts_with_case_insensitive(cursor, "<noscript")) close_tag = "</noscript";
            else if (string_starts_with_case_insensitive(cursor, "<svg")) close_tag = "</svg";
            else close_tag = "</template";
            cursor = find_case_insensitive(cursor, close_tag);
            if (cursor == NULL) {
                break;
            }
            while (*cursor != '\0' && *cursor != '>') ++cursor;
            if (*cursor == '>') ++cursor;
            continue;
        }
        if (!inside_tag && *cursor == '<') {
            if (string_starts_with_case_insensitive(cursor, "<p") ||
                string_starts_with_case_insensitive(cursor, "<br") ||
                string_starts_with_case_insensitive(cursor, "<li") ||
                string_starts_with_case_insensitive(cursor, "<h1") ||
                string_starts_with_case_insensitive(cursor, "<h2") ||
                string_starts_with_case_insensitive(cursor, "<h3") ||
                string_starts_with_case_insensitive(cursor, "<div") ||
                string_starts_with_case_insensitive(cursor, "<section") ||
                string_starts_with_case_insensitive(cursor, "<article") ||
                string_starts_with_case_insensitive(cursor, "<main") ||
                string_starts_with_case_insensitive(cursor, "<tr")) {
                if (!previous_space && len + 2u < sizeof(g_body)) {
                    g_body[len++] = '\n';
                    previous_space = 1;
                }
            }
            inside_tag = 1;
            ++cursor;
            continue;
        }
        if (inside_tag) {
            if (*cursor == '>') {
                inside_tag = 0;
            }
            ++cursor;
            continue;
        }

        {
            char c;
            if (*cursor == '&') decode_html_entity(&cursor, &c);
            else c = *cursor++;

            if (c == '\r' || c == '\n' || c == '\t') c = ' ';
            if (c == ' ') {
                if (previous_space) continue;
                previous_space = 1;
            } else {
                previous_space = 0;
            }
            g_body[len++] = c;
        }
    }
    while (len > 0 && (g_body[len - 1u] == ' ' || g_body[len - 1u] == '\n')) --len;
    g_body[len] = '\0';
}

static int fetch_url_once(const char* url, char* body_out, size_t body_capacity, char* redirect, size_t redirect_capacity)
{
    char host[160];
    char path[512];
    char request[1024];
    static char response[kMaxResponse];
    size_t request_length = 0;
    size_t response_length = 0;
    uint16_t port = 0;
    int sock;

    if (parse_fetch_url(url, host, sizeof(host), path, sizeof(path), &port) != 0) {
        copy_string(g_status, sizeof(g_status), "Bad URL");
        return -1;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        copy_string(g_status, sizeof(g_status), "Socket failed");
        return -1;
    }
    if (vex_web_connect_hostname(sock, host, port) != 0) {
        (void)close(sock);
        copy_string(g_status, sizeof(g_status), port == 443 ? "HTTPS connect failed" : "HTTP connect failed");
        return -1;
    }

    request[0] = '\0';
    append_buffer(request, sizeof(request), &request_length, "GET ");
    append_buffer(request, sizeof(request), &request_length, path);
    append_buffer(request, sizeof(request), &request_length, " HTTP/1.1\r\nHost: ");
    append_buffer(request, sizeof(request), &request_length, host);
    append_buffer(request, sizeof(request), &request_length, "\r\nUser-Agent: VexBrowser/0.3\r\nAccept: text/html,application/xhtml+xml,*/*;q=0.9\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n");
    if (send(sock, request, request_length, 0) < 0) {
        (void)close(sock);
        copy_string(g_status, sizeof(g_status), "HTTP send failed");
        return -1;
    }

    response[0] = '\0';
    for (;;) {
        size_t space = sizeof(response) - response_length - 1u;
        ssize_t got;
        if (space == 0) break;
        got = recv(sock, response + response_length, space > 16384u ? 16384u : space, 0);
        if (got <= 0) break;
        response_length += (size_t)got;
    }
    response[response_length] = '\0';
    (void)close(sock);

    if (response_length == 0) {
        copy_string(g_status, sizeof(g_status), "HTTP returned no data");
        return -1;
    }

    {
        int status = parse_http_status(response);
        if (status >= 300 && status < 400 && extract_location_header(response, redirect, redirect_capacity) == 0) {
            copy_string(g_status, sizeof(g_status), "HTTP redirect");
            return 1;
        }
    }

    {
        char* body = (char*)find_http_body(response);
        if (is_chunked_response(response)) {
            size_t body_offset = (size_t)(body - response);
            size_t body_len = response_length > body_offset ? response_length - body_offset : 0;
            (void)decode_chunked_body(body, body_len);
        }
        copy_string(body_out, body_capacity, body);
    }

    copy_string(g_status, sizeof(g_status), port == 443 ? "HTTPS loaded" : "HTTP loaded");
    return 0;
}

static int fetch_url(const char* url, char* body_out, size_t body_capacity)
{
    char current[kMaxUrl];
    char redirect[kMaxUrl];
    int attempt;
    copy_string(current, sizeof(current), url);

    for (attempt = 0; attempt < 4; ++attempt) {
        int result;
        redirect[0] = '\0';
        result = fetch_url_once(current, body_out, body_capacity, redirect, sizeof(redirect));
        if (result <= 0) return result;
        copy_string(current, sizeof(current), redirect);
    }
    copy_string(g_status, sizeof(g_status), "Too many redirects");
    return -1;
}

static void build_target_url(const char* command, char* url_out, size_t capacity)
{
    const char* text = command;
    size_t length = 0;

    if (string_starts_with(command, "load ")) text = command + 5u;
    else if (string_starts_with(command, "navigate ")) text = command + 9u;

    if (string_starts_with_case_insensitive(text, "http://") || string_starts_with_case_insensitive(text, "https://")) {
        copy_string(url_out, capacity, text);
        return;
    }
    if (strings_equal(text, "vex://home") || strings_equal(text, "vexos://home") || text[0] == '\0') {
        copy_string(url_out, capacity, "vex://home");
        return;
    }

    url_out[0] = '\0';
    append_buffer(url_out, capacity, &length, "https://lite.duckduckgo.com/lite/?q=");
    url_encode_append(url_out, capacity, &length, text);
}

static void render_home_page(void)
{
    fill(0u, 0u, kWidth, kHeight, 0xff0f1420u);
    fill(0u, 0u, kWidth, 6u, 0xff28c4ffu);
    fill(44u, 34u, 1192u, 150u, 0xff131c2bu);
    fill(44u, 34u, 1192u, 4u, 0xff28c4ffu);
    draw_text(88u, 118u, 760u, "VEX BROWSER", 0xfff3f7ffu, 3u);
    draw_text(88u, 162u, 1040u, "Renderer ready. Enter a URL in the browser shell above.", 0xff8ea5c0u, 1u);
    draw_text(88u, 234u, 1040u, "This process now waits for real navigation commands and fetches the page body.", 0xffdbe7f6u, 1u);
}

static void render_remote_page(void)
{
    fill(0u, 0u, kWidth, kHeight, 0xff101722u);
    fill(0u, 0u, kWidth, 6u, 0xff28c4ffu);

    fill(36u, 28u, 1208u, 88u, 0xff131c2bu);
    fill(36u, 28u, 1208u, 4u, 0xff28c4ffu);
    draw_text(56u, 50u, 980u, g_title[0] ? g_title : "Untitled page", 0xfff3f7ffu, 2u);
    draw_text(56u, 82u, 1100u, g_current_url, 0xff87e6ffu, 1u);

    fill(36u, 136u, 1208u, 54u, 0xff162233u);
    fill(36u, 136u, 1208u, 2u, 0xff2d415cu);
    draw_text(56u, 156u, 980u, g_status, 0xffdbe7f6u, 1u);

    fill(36u, 214u, 1208u, 378u, 0xfff5f7fcu);
    fill(36u, 214u, 1208u, 4u, 0xff28c4ffu);
    draw_text(58u, 236u, 1160u, g_body[0] ? g_body : "No readable text extracted from the page.", 0xff1f2c3du, 1u);
}

static void update_page_from_command(const char* command)
{
    static char html[kMaxResponse];
    char target[kMaxUrl];
    vex_http_response_info_t response_info;
    uint8_t cache_truncated = 0u;

    build_target_url(command, target, sizeof(target));
    copy_string(g_current_url, sizeof(g_current_url), target);

    if (strings_equal(target, "vex://home")) {
        copy_string(g_title, sizeof(g_title), "Vex Browser Home");
        copy_string(g_status, sizeof(g_status), "Idle");
        g_body[0] = '\0';
        return;
    }

    copy_string(g_title, sizeof(g_title), "Loading");
    copy_string(g_status, sizeof(g_status), "Fetching page");
    g_body[0] = '\0';

    if (vex_page_cache_lookup(target,
        g_current_url,
        sizeof(g_current_url),
        html,
        sizeof(html),
        &cache_truncated) == 0) {
        copy_string(g_status, sizeof(g_status), cache_truncated != 0u ? "Cache hit (truncated)" : "Cache hit");
    } else {
        if (vex_http_fetch_text(target,
            html,
            sizeof(html),
            g_current_url,
            sizeof(g_current_url),
            g_status,
            sizeof(g_status),
            &response_info) != 0) {
            if (g_title[0] == '\0') copy_string(g_title, sizeof(g_title), "Load failed");
            if (g_body[0] == '\0') copy_string(g_body, sizeof(g_body), "The renderer could not fetch this page with the current network path.");
            return;
        }
        vex_page_cache_store(target, g_current_url, html, response_info.truncated);
        if (response_info.truncated != 0u) {
            copy_string(g_status, sizeof(g_status), "HTTP loaded (truncated)");
        }
    }

    extract_title_from_html(html);
    extract_text_from_html(html);

    if (g_title[0] == '\0') {
        copy_string(g_title, sizeof(g_title), g_current_url);
    }
    if (g_body[0] == '\0') {
        copy_string(g_body, sizeof(g_body), "The page loaded, but the renderer could not extract readable text.");
    }
}

int main(void)
{
    char command[kMaxCommand];

    if (display_open() != 0) {
        copy_string(g_status, sizeof(g_status), "Display open failed");
        return 1;
    }

    copy_string(g_last_command, sizeof(g_last_command), "vex://home");
    copy_string(g_current_url, sizeof(g_current_url), "vex://home");
    copy_string(g_title, sizeof(g_title), "Vex Browser Home");
    copy_string(g_status, sizeof(g_status), "Idle");
    g_body[0] = '\0';

    for (;;) {
        if (command_read(command, sizeof(command)) > 0 && !strings_equal(command, g_last_command)) {
            copy_string(g_last_command, sizeof(g_last_command), command);
            update_page_from_command(command);
        }

        if (strings_equal(g_current_url, "vex://home")) render_home_page();
        else render_remote_page();

        display_present();
        usleep(50000u);
    }
}
static const char* find_case_insensitive(const char* text, const char* needle)
{
    size_t i;
    size_t needle_length = string_length(needle);
    if (!text || !needle || needle_length == 0) {
        return text;
    }
    for (i = 0; text[i] != '\0'; ++i) {
        size_t j = 0;
        while (j < needle_length && text[i + j] != '\0' &&
            to_lower_ascii(text[i + j]) == to_lower_ascii(needle[j])) {
            ++j;
        }
        if (j == needle_length) {
            return text + i;
        }
    }
    return NULL;
}
