#ifndef KSW_RESOURCE_PROTOCOL_H
#define KSW_RESOURCE_PROTOCOL_H
/* Private userspace orchestration of native readable-file protection. No
 * Windows share/disposition/access constants cross the native boundary. */
#include <stdint.h>
#define KR_MAGIC 0x3152534bu
typedef struct {uint32_t magic,flags;int32_t result;uint32_t reserved;char path[512];} kr_open;
#endif
