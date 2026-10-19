#ifndef KYIL_USER_PROTOCOL_H
#define KYIL_USER_PROTOCOL_H
#include <stdint.h>
#define WU_MAGIC 0x31525557u
#define WU_MESSAGE 0xfffffffdu
enum {WU_SESSION=1,WU_REGISTER,WU_UNREGISTER,WU_CREATE,WU_DESTROY,WU_FIND,WU_QUERY,WU_TITLE,WU_THREAD_EXIT};
/* Bounded UTF-16 protocol, no process pointers or client-asserted identity. */
typedef struct {uint32_t magic,op,error,id,atom,after,parent,pid,tid,has_class,has_title,pad;
    uint64_t instance;uint16_t name[256],title[512];} wu_packet;
#endif
