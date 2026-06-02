#ifndef VEX_PAGE_CACHE_H
#define VEX_PAGE_CACHE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int vex_page_cache_lookup(const char* request_url,
    char* final_url_out,
    size_t final_url_capacity,
    char* html_out,
    size_t html_capacity,
    uint8_t* truncated_out);

void vex_page_cache_store(const char* request_url,
    const char* final_url,
    const char* html,
    uint8_t truncated);

#ifdef __cplusplus
}
#endif

#endif
