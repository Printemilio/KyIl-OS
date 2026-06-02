#ifndef VEX_RUNTIME_H
#define VEX_RUNTIME_H

#include <stddef.h>
#include <stdint.h>
#include <unos/vex_abi.h>

#ifndef VEX_SSIZE_T_DEFINED
#define VEX_SSIZE_T_DEFINED
typedef long long ssize_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

void* malloc(size_t size);
void free(void* ptr);
void* calloc(size_t count, size_t size);
void* realloc(void* ptr, size_t size);
void abort(void);

int vex_mrac_put(uint64_t key_hash, const void* data, size_t size, uint32_t* version_out);
int vex_mrac_get(uint64_t key_hash, uint32_t version, void* buffer, size_t buffer_size, size_t* size_out, uint32_t* resolved_version);
int vex_mrac_remove(uint64_t key_hash, uint32_t version, uint32_t flags);
uint32_t vex_mrac_current_version(uint64_t key_hash);
size_t vex_mrac_version_count(uint64_t key_hash);
int vex_mrac_version_info(uint64_t key_hash, size_t index, uint32_t* version_out, size_t* size_out, uint8_t* is_current_out);
int vex_jit_arena_create(size_t size, uint32_t flags, uint64_t quota_bytes, void** rw_base_out, void** rx_base_out, uint32_t* arena_id_out, uint64_t* source_namespace_id_out);
int vex_jit_arena_destroy(void* addr, uint32_t arena_id, uint32_t flags);
ssize_t vex_debug_log_read(char *buf, size_t len, uint32_t *seq);
int vex_mouse_poll(vex_mouse_packet_t *packet);
int vex_gpu_query_info(vex_gpu_info_t *info);
int vex_gpu_buffer_create(vex_gpu_buffer_create_req_t *req);
int vex_gpu_buffer_destroy(uint32_t handle);
int vex_gpu_queue_submit(vex_gpu_submit_req_t *req);
int vex_gpu_present(vex_gpu_present_req_t *req);

#ifdef __cplusplus
}
#endif

#endif
