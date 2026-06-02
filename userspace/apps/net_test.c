#include <stddef.h>
#include <stdint.h>

#include <unos/vex_legacy_compat.h>
#include <unos/vex_web_compat.h>

#include "vex_http_client.h"

enum {
    kConnectIterations = 8u,
    kFetchIterations = 6u
};

static size_t string_length(const char* text)
{
    size_t length = 0;
    while (text != NULL && text[length] != '\0') {
        ++length;
    }
    return length;
}

static void write_text(const char* text)
{
    (void)write(STDOUT_FILENO, text, string_length(text));
}

static void append_text(char* buffer, size_t capacity, size_t* length, const char* text)
{
    size_t i = 0;
    while (text != NULL && text[i] != '\0' && *length + 1u < capacity) {
        buffer[(*length)++] = text[i++];
    }
    buffer[*length] = '\0';
}

static void append_u64(char* buffer, size_t capacity, size_t* length, uint64_t value)
{
    char digits[24];
    size_t count = 0;

    if (value == 0u) {
        append_text(buffer, capacity, length, "0");
        return;
    }

    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (count > 0u && *length + 1u < capacity) {
        buffer[(*length)++] = digits[--count];
    }
    buffer[*length] = '\0';
}

static void append_u32(char* buffer, size_t capacity, size_t* length, uint32_t value)
{
    append_u64(buffer, capacity, length, (uint64_t)value);
}

static uint64_t now_ms(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, 0) != 0) {
        return 0u;
    }
    return (uint64_t)tv.tv_sec * 1000u + (uint64_t)tv.tv_usec / 1000u;
}

static void print_duration(const char* label, uint64_t elapsed_ms)
{
    char line[128];
    size_t length = 0;
    append_text(line, sizeof(line), &length, "net_test: ");
    append_text(line, sizeof(line), &length, label);
    append_text(line, sizeof(line), &length, "=");
    append_u64(line, sizeof(line), &length, elapsed_ms);
    append_text(line, sizeof(line), &length, "ms\n");
    write_text(line);
}

static void print_bench(const char* label, uint64_t total_ms, uint32_t count)
{
    char line[160];
    size_t length = 0;
    uint64_t average_ms = count != 0u ? total_ms / count : 0u;

    append_text(line, sizeof(line), &length, "net_test: ");
    append_text(line, sizeof(line), &length, label);
    append_text(line, sizeof(line), &length, " total=");
    append_u64(line, sizeof(line), &length, total_ms);
    append_text(line, sizeof(line), &length, "ms avg=");
    append_u64(line, sizeof(line), &length, average_ms);
    append_text(line, sizeof(line), &length, "ms n=");
    append_u32(line, sizeof(line), &length, count);
    append_text(line, sizeof(line), &length, "\n");
    write_text(line);
}

static void append_query_counter(char* buffer, size_t capacity, size_t* length, uint32_t value)
{
    append_text(buffer, capacity, length, "?v=");
    append_u32(buffer, capacity, length, value);
}

static void run_connect_test(const char* host, uint16_t port, const char* label)
{
    uint64_t total_ms = 0u;
    uint32_t iteration;

    for (iteration = 0; iteration < kConnectIterations; ++iteration) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        uint64_t start_ms;
        uint64_t end_ms;

        if (sock < 0) {
            write_text("net_test: socket failed\n");
            return;
        }

        start_ms = now_ms();
        if (vex_web_connect_hostname(sock, host, port) != 0) {
            write_text("net_test: connect failed\n");
            (void)close(sock);
            return;
        }
        end_ms = now_ms();
        (void)close(sock);
        total_ms += end_ms - start_ms;
    }

    print_bench(label, total_ms, kConnectIterations);
}

static void run_fetch_test_uncached(const char* base_url, const char* label)
{
    static char html[65536];
    char url[640];
    char final_url[512];
    char status[128];
    vex_http_response_info_t info;
    uint64_t total_ms = 0u;
    uint32_t iteration;

    for (iteration = 0; iteration < kFetchIterations; ++iteration) {
        uint64_t start_ms;
        uint64_t end_ms;
        size_t length = 0u;

        url[0] = '\0';
        append_text(url, sizeof(url), &length, base_url);
        append_query_counter(url, sizeof(url), &length, iteration + 1u);

        start_ms = now_ms();
        if (vex_http_fetch_text(url, html, sizeof(html), final_url, sizeof(final_url), status, sizeof(status), &info) != 0) {
            write_text("net_test: fetch failed\n");
            return;
        }
        end_ms = now_ms();
        total_ms += end_ms - start_ms;
    }

    print_bench(label, total_ms, kFetchIterations);
}

static void run_fetch_test_cached(const char* url, const char* label)
{
    static char html[65536];
    char final_url[512];
    char status[128];
    vex_http_response_info_t info;
    uint64_t total_ms = 0u;
    uint32_t iteration;

    if (vex_http_fetch_text(url, html, sizeof(html), final_url, sizeof(final_url), status, sizeof(status), &info) != 0) {
        write_text("net_test: fetch failed\n");
        return;
    }

    for (iteration = 0; iteration < kFetchIterations; ++iteration) {
        uint64_t start_ms = now_ms();
        uint64_t end_ms;

        if (vex_http_fetch_text(url, html, sizeof(html), final_url, sizeof(final_url), status, sizeof(status), &info) != 0) {
            write_text("net_test: fetch failed\n");
            return;
        }

        end_ms = now_ms();
        total_ms += end_ms - start_ms;
    }

    print_bench(label, total_ms, kFetchIterations);
}

int main(void)
{
    write_text("net_test: begin\n");

    run_connect_test("example.com", 80u, "tcp connect http");
    run_connect_test("example.com", 443u, "tcp connect https");
    run_fetch_test_uncached("http://example.com/", "http fetch uncached");
    run_fetch_test_cached("http://example.com/", "http fetch cached");
    run_fetch_test_uncached("https://example.com/", "https fetch uncached");
    run_fetch_test_cached("https://example.com/", "https fetch cached");

    write_text("net_test: end\n");
    return 0;
}
