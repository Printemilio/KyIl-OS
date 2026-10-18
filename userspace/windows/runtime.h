#ifndef KYIL_WINDOWS_RUNTIME_H
#define KYIL_WINDOWS_RUNTIME_H
#include "pe_image.h"
#include "handle_value.h"
/* PE32+ SizeOfImage is a 32-bit field; this is a format/address bound, not
 * a RAM limit inherited from the Dell. Native process_memory handles backing. */
#define WR_MAX_IMAGE ((uint64_t)UINT32_MAX & ~(uint64_t)(PE_PAGE-1))
#define WR_MAX_MODULES 64
#define WR_WIN __attribute__((ms_abi))
void *memcpy(void *,const void *,size_t);
void *memset(void *,int,size_t);
size_t wr_len(const char *);
int wr_equal(const char *,const char *,int);
void wr_log(const char *);
void wr_trace_contract(const char *,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
_Noreturn void wr_exit(unsigned);
_Noreturn void wr_finish(unsigned);
void *wr_map(void *,size_t,int);
void *wr_map_image(void *,size_t);
int wr_protect(void *,size_t,int);
int wr_unmap(void *,size_t);
int64_t wr_write(int,const void *,size_t);
uint64_t wr_pid(void);
uint64_t wr_ticks(void);
int wr_read_file(const char *,uint8_t **,size_t *);
int wr_read_fd(int,uint8_t **,size_t *);
void *wr_builtin(const char *,const char *);
typedef struct { uint32_t held; } wr_mutex;
void wr_lock(wr_mutex *);
void wr_unlock(wr_mutex *);
int wr_futex(uint32_t *,unsigned,unsigned,uint64_t);
uint32_t wr_tid(void);
int wr_set_gs(void *);
int wr_context(uint64_t *,uint64_t *,uint32_t *);
int wr_start_thread(void (*)(void *),void *,void *,uint32_t *,uint32_t *);
int wr_start_thread_flags(void (*)(void *),void *,void *,uint32_t *,uint32_t *,unsigned);
int wr_stop_thread(uint32_t,unsigned);
_Noreturn void wr_end_thread(void *,size_t,unsigned);
void wr_yield(void);
int wr_random(void *,size_t);
void *wr_native_alloc(size_t);
int wr_native_free(void *);
#endif
