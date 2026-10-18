#ifndef KYIL_WINDOWS_EVENT_PROTOCOL_H
#define KYIL_WINDOWS_EVENT_PROTOCOL_H
#include <stdint.h>
enum {WE_CREATE=1,WE_SET,WE_RESET,WE_WAIT};
typedef struct {uint32_t operation,manual,initial,timeout,error,result;} we_packet;
#endif
