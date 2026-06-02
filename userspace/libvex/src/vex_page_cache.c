#include "vex_page_cache.h"

#include <stddef.h>
#include <stdint.h>

enum {
    VEX_PAGE_CACHE_SLOTS = 2u,
    VEX_PAGE_CACHE_URL_MAX = 512u,
    VEX_PAGE_CACHE_HTML_MAX = 262144u
};

typedef struct vex_page_cache_entry {
    uint8_t valid;
    uint8_t truncated;
    uint32_t age;
    char request_url[VEX_PAGE_CACHE_URL_MAX];
    char final_url[VEX_PAGE_CACHE_URL_MAX];
    char html[VEX_PAGE_CACHE_HTML_MAX];
} vex_page_cache_entry_t;

static vex_page_cache_entry_t g_page_cache[VEX_PAGE_CACHE_SLOTS];
static uint32_t g_page_cache_age;

static size_t string_length(const char* text)
{
    size_t length = 0;
    while (text != NULL && text[length] != '\0') {
        ++length;
    }
    return length;
}

static int strings_equal(const char* left, const char* right)
{
    size_t index = 0;
    if (left == NULL || right == NULL) {
        return 0;
    }
    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return 0;
        }
        ++index;
    }
    return left[index] == right[index];
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

int vex_page_cache_lookup(const char* request_url,
    char* final_url_out,
    size_t final_url_capacity,
    char* html_out,
    size_t html_capacity,
    uint8_t* truncated_out)
{
    size_t i;
    if (request_url == NULL || html_out == NULL || html_capacity == 0u) {
        return -1;
    }

    for (i = 0; i < VEX_PAGE_CACHE_SLOTS; ++i) {
        if (g_page_cache[i].valid != 0u && strings_equal(g_page_cache[i].request_url, request_url)) {
            copy_string(final_url_out, final_url_capacity, g_page_cache[i].final_url);
            copy_string(html_out, html_capacity, g_page_cache[i].html);
            if (truncated_out != NULL) {
                *truncated_out = g_page_cache[i].truncated;
            }
            g_page_cache[i].age = ++g_page_cache_age;
            return 0;
        }
    }

    return -1;
}

void vex_page_cache_store(const char* request_url,
    const char* final_url,
    const char* html,
    uint8_t truncated)
{
    size_t i;
    size_t slot = 0u;
    uint32_t oldest_age = 0xffffffffu;

    if (request_url == NULL || final_url == NULL || html == NULL) {
        return;
    }
    if (string_length(request_url) >= VEX_PAGE_CACHE_URL_MAX ||
        string_length(final_url) >= VEX_PAGE_CACHE_URL_MAX ||
        string_length(html) >= VEX_PAGE_CACHE_HTML_MAX) {
        return;
    }

    for (i = 0; i < VEX_PAGE_CACHE_SLOTS; ++i) {
        if (g_page_cache[i].valid != 0u && strings_equal(g_page_cache[i].request_url, request_url)) {
            slot = i;
            goto store;
        }
        if (g_page_cache[i].valid == 0u) {
            slot = i;
            goto store;
        }
        if (g_page_cache[i].age < oldest_age) {
            oldest_age = g_page_cache[i].age;
            slot = i;
        }
    }

store:
    g_page_cache[slot].valid = 1u;
    g_page_cache[slot].truncated = truncated;
    g_page_cache[slot].age = ++g_page_cache_age;
    copy_string(g_page_cache[slot].request_url, sizeof(g_page_cache[slot].request_url), request_url);
    copy_string(g_page_cache[slot].final_url, sizeof(g_page_cache[slot].final_url), final_url);
    copy_string(g_page_cache[slot].html, sizeof(g_page_cache[slot].html), html);
}
