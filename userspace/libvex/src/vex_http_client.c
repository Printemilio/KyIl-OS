#include "vex_http_client.h"

#include <stddef.h>
#include <stdint.h>

#include <unos/vex_web_compat.h>

#include "vex_runtime.h"

enum {
    VEX_HTTP_MAX_URL = 512u,
    VEX_HTTP_MAX_HOST = 160u,
    VEX_HTTP_MAX_PATH = 512u,
    VEX_HTTP_MAX_REQUEST = 1024u,
    VEX_HTTP_MAX_RESPONSE = 524288u,   /* 512KB — was 262KB */
    VEX_HTTP_MAX_REDIRECTS = 4u
};

/* SO_RCVTIMEO/SO_SNDTIMEO value — kernel reads tv_sec + tv_usec (Linux timeval layout) */
typedef struct { long tv_sec; long tv_usec; } vex_timeval_t;

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
    if (capacity == 0u) {
        return;
    }
    if (source == NULL) {
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
    if (destination == NULL || length == NULL || source == NULL) {
        return;
    }
    while (source[index] != '\0' && *length + 1u < capacity) {
        destination[(*length)++] = source[index++];
    }
    destination[*length] = '\0';
}

static const char* find_case_insensitive(const char* text, const char* needle)
{
    size_t i;
    size_t needle_length = string_length(needle);
    if (!text || !needle || needle_length == 0u) {
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

static int parse_fetch_url(const char* url, char* host, size_t host_capacity, char* path, size_t path_capacity, uint16_t* port)
{
    const char* cursor = url;
    size_t host_length = 0;

    while (*cursor != '\0' && *cursor <= ' ') {
        ++cursor;
    }

    if (string_starts_with_case_insensitive(cursor, "http://")) {
        cursor += 7;
        *port = 80;
    } else if (string_starts_with_case_insensitive(cursor, "https://")) {
        cursor += 8;
        *port = 443;
    } else {
        return -1;
    }

    while (cursor[host_length] != '\0' &&
        cursor[host_length] != '/' &&
        cursor[host_length] != ':' &&
        cursor[host_length] > ' ' &&
        host_length + 1u < host_capacity) {
        host[host_length] = to_lower_ascii(cursor[host_length]);
        ++host_length;
    }
    host[host_length] = '\0';
    if (host_length == 0u) {
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
        if (parsed_port != 0u) {
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

static void absolutize_url(char* out, size_t capacity, const char* base_url, const char* href)
{
    size_t length = 0;
    const char* cursor = base_url;
    char scheme_host[VEX_HTTP_MAX_URL];

    if (capacity == 0u) {
        return;
    }
    out[0] = '\0';
    if (href == NULL || href[0] == '\0') {
        return;
    }
    if (string_starts_with_case_insensitive(href, "http://") ||
        string_starts_with_case_insensitive(href, "https://")) {
        copy_string(out, capacity, href);
        return;
    }
    if (string_starts_with(href, "//")) {
        append_buffer(out, capacity, &length, "https:");
        append_buffer(out, capacity, &length, href);
        return;
    }

    scheme_host[0] = '\0';
    if (string_starts_with_case_insensitive(cursor, "http://")) {
        append_buffer(scheme_host, sizeof(scheme_host), &length, "http://");
        cursor += 7;
    } else if (string_starts_with_case_insensitive(cursor, "https://")) {
        append_buffer(scheme_host, sizeof(scheme_host), &length, "https://");
        cursor += 8;
    } else {
        return;
    }
    while (*cursor != '\0' && *cursor != '/' && length + 1u < sizeof(scheme_host)) {
        scheme_host[length++] = *cursor++;
    }
    scheme_host[length] = '\0';

    if (href[0] == '/') {
        length = 0;
        append_buffer(out, capacity, &length, scheme_host);
        append_buffer(out, capacity, &length, href);
        return;
    }

    copy_string(out, capacity, href);
}

static int copy_url_after_marker(const char* marker, char* out, size_t capacity)
{
    size_t length = 0;
    char quote;
    if (marker == NULL || out == NULL || capacity == 0u) {
        return -1;
    }
    while (*marker != '\0' && *marker != '"' && *marker != '\'') {
        ++marker;
    }
    if (*marker == '\0') {
        return -1;
    }
    quote = *marker++;
    while (*marker != '\0' && *marker != quote && length + 1u < capacity) {
        out[length++] = *marker++;
    }
    out[length] = '\0';
    return length == 0u ? -1 : 0;
}

static int extract_client_redirect(const char* base_url, const char* body, char* redirect, size_t capacity)
{
    char raw[VEX_HTTP_MAX_URL];
    const char* marker;

    raw[0] = '\0';
    marker = find_case_insensitive(body, "url=");
    if (marker != NULL) {
        size_t length = 0;
        marker += 4;
        while (*marker == ' ' || *marker == '\'' || *marker == '"') {
            ++marker;
        }
        while (*marker != '\0' &&
            *marker != '"' &&
            *marker != '\'' &&
            *marker != ';' &&
            *marker != '<' &&
            length + 1u < sizeof(raw)) {
            raw[length++] = *marker++;
        }
        raw[length] = '\0';
    }
    if (raw[0] == '\0') {
        marker = find_case_insensitive(body, "location.href");
        if (marker == NULL) {
            marker = find_case_insensitive(body, "window.location");
        }
        if (marker == NULL) {
            marker = find_case_insensitive(body, "location.replace");
        }
        if (copy_url_after_marker(marker, raw, sizeof(raw)) != 0) {
            raw[0] = '\0';
        }
    }
    if (raw[0] == '\0' &&
        (find_case_insensitive(body, "non-javascript") != NULL ||
         find_case_insensitive(body, "redirected") != NULL)) {
        marker = find_case_insensitive(body, "href=");
        if (copy_url_after_marker(marker, raw, sizeof(raw)) != 0) {
            raw[0] = '\0';
        }
    }
    if (raw[0] == '\0') {
        return -1;
    }
    absolutize_url(redirect, capacity, base_url, raw);
    return redirect[0] == '\0' ? -1 : 0;
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
    while (*response != '\0' && *response != ' ') {
        ++response;
    }
    while (*response == ' ') {
        ++response;
    }
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
            while (response[cursor] == ' ' || response[cursor] == '\t') {
                ++cursor;
            }
            while (response[cursor] != '\0' &&
                response[cursor] != '\r' &&
                response[cursor] != '\n' &&
                out_len + 1u < capacity) {
                out[out_len++] = response[cursor++];
            }
            out[out_len] = '\0';
            return out_len == 0u ? -1 : 0;
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
            if (c >= '0' && c <= '9') {
                chunk_size = chunk_size * 16u + (uint32_t)(c - '0');
                got_digit = 1;
            } else if (c >= 'a' && c <= 'f') {
                chunk_size = chunk_size * 16u + (uint32_t)(c - 'a' + 10u);
                got_digit = 1;
            } else if (c >= 'A' && c <= 'F') {
                chunk_size = chunk_size * 16u + (uint32_t)(c - 'A' + 10u);
                got_digit = 1;
            } else {
                break;
            }
        }
        while (in < end && *in != '\n') {
            ++in;
        }
        if (in < end) {
            ++in;
        }
        if (!got_digit || chunk_size == 0u) {
            break;
        }
        while (chunk_size > 0u && in < end) {
            out[out_len++] = *in++;
            --chunk_size;
        }
        while (in < end && (*in == '\r' || *in == '\n')) {
            ++in;
        }
    }
    out[out_len] = '\0';
    return out_len;
}

static int parse_content_length(const char *response)
{
    size_t i;
    for (i = 0; response[i] != '\0'; ++i) {
        if (response[i] == '\r' && response[i+1] == '\n' &&
            response[i+2] == '\r' && response[i+3] == '\n') {
            return -1;   /* reached end of headers, not found */
        }
        if ((i == 0 || response[i-1] == '\n') &&
            to_lower_ascii(response[i+0]) == 'c' &&
            to_lower_ascii(response[i+1]) == 'o' &&
            to_lower_ascii(response[i+2]) == 'n' &&
            to_lower_ascii(response[i+3]) == 't' &&
            to_lower_ascii(response[i+4]) == 'e' &&
            to_lower_ascii(response[i+5]) == 'n' &&
            to_lower_ascii(response[i+6]) == 't' &&
            response[i+7] == '-' &&
            to_lower_ascii(response[i+8]) == 'l' &&
            to_lower_ascii(response[i+9]) == 'e' &&
            to_lower_ascii(response[i+10]) == 'n' &&
            to_lower_ascii(response[i+11]) == 'g' &&
            to_lower_ascii(response[i+12]) == 't' &&
            to_lower_ascii(response[i+13]) == 'h' &&
            response[i+14] == ':') {
            size_t cursor = i + 15u;
            int cl = 0;
            while (response[cursor] == ' ' || response[cursor] == '\t') ++cursor;
            while (response[cursor] >= '0' && response[cursor] <= '9') {
                cl = cl * 10 + (response[cursor] - '0');
                ++cursor;
            }
            return cl;
        }
    }
    return -1;
}

static int fetch_url_once(const char* url,
    char* body_out,
    size_t body_capacity,
    char* redirect,
    size_t redirect_capacity,
    char* status_out,
    size_t status_capacity,
    vex_http_response_info_t* info)
{
    char host[VEX_HTTP_MAX_HOST];
    char path[VEX_HTTP_MAX_PATH];
    char request[VEX_HTTP_MAX_REQUEST];
    static char response[VEX_HTTP_MAX_RESPONSE];
    size_t request_length = 0;
    size_t response_length = 0;
    uint16_t port = 0;
    int sock;

    if (parse_fetch_url(url, host, sizeof(host), path, sizeof(path), &port) != 0) {
        copy_string(status_out, status_capacity, "Bad URL");
        return -1;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        copy_string(status_out, status_capacity, "Socket failed");
        return -1;
    }
    {
        vex_timeval_t tv;
        tv.tv_sec  = 10;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, (uint32_t)sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, (uint32_t)sizeof(tv));
    }
    if (vex_web_connect_hostname(sock, host, port) != 0) {
        (void)close(sock);
        copy_string(status_out, status_capacity, port == 443u ? "HTTPS connect failed" : "HTTP connect failed");
        return -1;
    }

    request[0] = '\0';
    append_buffer(request, sizeof(request), &request_length, "GET ");
    append_buffer(request, sizeof(request), &request_length, path);
    append_buffer(request, sizeof(request), &request_length, " HTTP/1.1\r\nHost: ");
    append_buffer(request, sizeof(request), &request_length, host);
    append_buffer(request, sizeof(request), &request_length, "\r\nUser-Agent: Mozilla/5.0 VexBrowser/0.4\r\nAccept: text/html,application/xhtml+xml,*/*;q=0.9\r\nAccept-Language: en-US,en;q=0.8\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n");
    if (send(sock, request, request_length, 0) < 0) {
        (void)close(sock);
        copy_string(status_out, status_capacity, "HTTP send failed");
        return -1;
    }

    response[0] = '\0';
    {
        const char *headers_end = NULL;
        int content_length = -1;

        for (;;) {
            size_t space = sizeof(response) - response_length - 1u;
            ssize_t got;
            if (space == 0u) {
                if (info != NULL) info->truncated = 1u;
                break;
            }
            got = recv(sock, response + response_length, space, 0);
            if (got <= 0) break;
            response_length += (size_t)got;
            response[response_length] = '\0';

            /* Parse headers once the header terminator arrives */
            if (headers_end == NULL) {
                const char *he = find_http_body(response);
                if (he != response) {
                    headers_end = he;
                    content_length = parse_content_length(response);
                }
            }

            /* Early exit: stop as soon as body is complete */
            if (headers_end != NULL && content_length >= 0) {
                size_t hdr_size = (size_t)(headers_end - response);
                if (response_length >= hdr_size + (size_t)content_length) break;
            }
        }
    }
    (void)close(sock);

    if (info != NULL) {
        info->port = port;
        info->secure = (uint8_t)(port == 443u);
        info->status_code = parse_http_status(response);
    }

    if (response_length == 0u) {
        copy_string(status_out, status_capacity, "HTTP returned no data");
        return -1;
    }

    if (info != NULL &&
        info->status_code >= 300 &&
        info->status_code < 400 &&
        extract_location_header(response, redirect, redirect_capacity) == 0) {
        if (redirect[0] != 'h' && redirect[0] != 'H') {
            char absolute[VEX_HTTP_MAX_URL];
            absolutize_url(absolute, sizeof(absolute), url, redirect);
            if (absolute[0] != '\0') {
                copy_string(redirect, redirect_capacity, absolute);
            }
        }
        info->redirected = 1u;
        copy_string(status_out, status_capacity, "HTTP redirect");
        return 1;
    }

    {
        char* body = (char*)find_http_body(response);
        if (is_chunked_response(response)) {
            size_t body_offset = (size_t)(body - response);
            size_t body_len = response_length > body_offset ? response_length - body_offset : 0u;
            (void)decode_chunked_body(body, body_len);
        }
        if (extract_client_redirect(url, body, redirect, redirect_capacity) == 0) {
            if (info != NULL) {
                info->redirected = 1u;
            }
            copy_string(status_out, status_capacity, "Client redirect");
            return 1;
        }
        {
            size_t body_len = response_length - (size_t)((const char *)body - response);
            size_t copy_len = body_len < body_capacity - 1u ? body_len : body_capacity - 1u;
            __builtin_memcpy(body_out, body, copy_len);
            body_out[copy_len] = '\0';
        }
    }

    copy_string(status_out, status_capacity, port == 443u ? "HTTPS loaded" : "HTTP loaded");
    return 0;
}

int vex_http_fetch_text(const char* url,
    char* body_out,
    size_t body_capacity,
    char* final_url_out,
    size_t final_url_capacity,
    char* status_out,
    size_t status_capacity,
    vex_http_response_info_t* info_out)
{
    char current[VEX_HTTP_MAX_URL];
    char redirect[VEX_HTTP_MAX_URL];
    vex_http_response_info_t info;
    uint32_t attempt;

    if (url == NULL || body_out == NULL || body_capacity == 0u) {
        return -1;
    }

    body_out[0] = '\0';
    if (final_url_out != NULL && final_url_capacity != 0u) {
        final_url_out[0] = '\0';
    }
    if (status_out != NULL && status_capacity != 0u) {
        status_out[0] = '\0';
    }

    __builtin_memset(&info, 0, sizeof(info));
    copy_string(current, sizeof(current), url);
    while (current[0] != '\0' && current[0] <= ' ') {
        size_t i = 0u;
        do {
            current[i] = current[i + 1u];
            ++i;
        } while (current[i - 1u] != '\0');
    }

    for (attempt = 0; attempt < VEX_HTTP_MAX_REDIRECTS; ++attempt) {
        int result;
        redirect[0] = '\0';
        result = fetch_url_once(current, body_out, body_capacity, redirect, sizeof(redirect), status_out, status_capacity, &info);
        if (result <= 0) {
            if (result == 0 && final_url_out != NULL && final_url_capacity != 0u) {
                copy_string(final_url_out, final_url_capacity, current);
            }
            if (info_out != NULL) {
                *info_out = info;
            }
            return result;
        }
        copy_string(current, sizeof(current), redirect);
    }

    copy_string(status_out, status_capacity, "Too many redirects");
    if (info_out != NULL) {
        *info_out = info;
    }
    return -1;
}
