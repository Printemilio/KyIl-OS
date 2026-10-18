#ifndef KYIL_PIPE_PROTOCOL_H
#define KYIL_PIPE_PROTOCOL_H
#include <stdint.h>
#define WP_MAGIC 0x55575031u
enum {WP_CREATE=1,WP_OPEN,WP_CONNECT,WP_DISCONNECT,WP_STATE,WP_WAIT,WP_PING};
typedef struct wp_packet {uint32_t magic,op,error,access,mode,limit,timeout,sd_size;char name[256];} wp_packet;
/* Payload is packet followed by an immutable self-relative SD snapshot.
 * Every RPC attaches a private reply endpoint. Service authority and sender
 * identity are never supplied by packet fields. */
#endif
