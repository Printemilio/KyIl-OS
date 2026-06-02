#ifndef UNOS_IPC_H
#define UNOS_IPC_H

#include <stddef.h>

void ipc_init(void);
void *ipc_share(const char *key, void *data, size_t size);
void *ipc_get(const char *key);
void *ipc_update(const char *key, void *new_data, size_t size);
void ipc_release(const char *key);

int pipe_create(void);
int pipe_write(int fd, const void *data, size_t size);
int pipe_read(int fd, void *buffer, size_t size);
size_t pipe_readable(int fd);
size_t pipe_writable(int fd);
void pipe_close(int fd);

#endif
