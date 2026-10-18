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
int vex_directory_list(const char *path, vex_directory_entry_t *entries,
                       size_t capacity, size_t *count_out);
int vex_open_path(const char *path, uint8_t type);
int vex_open_context(uint32_t application, char *buffer, size_t size);
int vex_jit_arena_create(size_t size, uint32_t flags, uint64_t quota_bytes, void** rw_base_out, void** rx_base_out, uint32_t* arena_id_out, uint64_t* source_namespace_id_out);
int vex_jit_arena_destroy(void* addr, uint32_t arena_id, uint32_t flags);
ssize_t vex_debug_log_read(char *buf, size_t len, uint32_t *seq);
int vex_mouse_poll(vex_mouse_packet_t *packet);
int vex_gpu_query_info(vex_gpu_info_t *info);
int vex_gpu_query_caps(vex_gpu_caps_t *caps);
int vex_gpu_buffer_create(vex_gpu_buffer_create_req_t *req);
int vex_gpu_buffer_destroy(uint32_t handle);
int vex_gpu_queue_submit(vex_gpu_submit_req_t *req);
uint64_t vex_gpu_queue_create(uint32_t family);
int vex_gpu_queue_destroy(uint64_t queue);
int vex_gpu_submit_begin(uint64_t queue, uint64_t *serial_out);
int vex_gpu_submit_complete(uint64_t queue, uint64_t serial, int32_t status);
int vex_gpu_fence_query(uint64_t queue, uint64_t serial);
int vex_gpu_fence_wait(uint64_t queue, uint64_t serial,
                       uint64_t timeout_ticks);
uint64_t vex_event_counter_create(uint64_t initial, uint32_t flags);
int vex_event_counter_signal(uint64_t capability, uint64_t value);
int vex_event_counter_read(uint64_t capability, uint64_t *value_out,
                           uint32_t flags, uint64_t timeout_ticks);
int vex_event_counter_release(uint64_t capability);
/* Native Vex process/thread API. THREAD_CREATE maps a private lazy stack in
   the owning Process; TLS_BASE is optional (zero inherits the caller's base).
   The entry runs through a return-safe userspace bootstrap. */
typedef void (*vex_thread_entry_t)(void *arg);
int vex_thread_create(vex_thread_entry_t entry, void *arg, uint64_t tls_base,
                      uint32_t *tid_out);
int vex_thread_join(uint32_t tid, int *status_out);
void vex_thread_exit(int status) __attribute__((noreturn));
int vex_gpu_present(vex_gpu_present_req_t *req);
int vex_gpu_surface_create(vex_gpu_surface_create_req_t *req);
int vex_gpu_surface_destroy(uint64_t surface_handle);
int vex_gpu_surface_present(vex_gpu_surface_present_req_t *req);
int vex_gpu_surface_wait(vex_gpu_surface_wait_req_t *req);
int vex_surface_retain(uint64_t surface_handle);
int vex_surface_query(vex_surface_query_req_t *req);
int vex_surface_map(vex_surface_map_req_t *req);
int vex_surface_unmap(vex_surface_unmap_req_t *req);
int vex_message_pair(vex_message_pair_req_t *req);
ssize_t vex_message_send_objects(vex_message_io_req_t *req);
ssize_t vex_message_receive_objects(vex_message_io_req_t *req);

#ifdef __cplusplus
}
#endif

#endif
