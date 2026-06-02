#ifndef VEX_HTTP_CLIENT_H
#define VEX_HTTP_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vex_http_response_info {
    int status_code;
    uint16_t port;
    uint8_t truncated;
    uint8_t secure;
    uint8_t redirected;
} vex_http_response_info_t;

int vex_http_fetch_text(const char* url,
    char* body_out,
    size_t body_capacity,
    char* final_url_out,
    size_t final_url_capacity,
    char* status_out,
    size_t status_capacity,
    vex_http_response_info_t* info_out);

#ifdef __cplusplus
}
#endif

#endif
