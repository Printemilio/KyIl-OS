#ifndef UNOS_IPC_H
#define UNOS_IPC_H

#include <stddef.h>
#include <unos/ora.h>

void ipc_init(void);
int ipc_store(const char *name,const void *data,size_t size);
int ipc_acquire(const char *name,ora_view_t *view);
void ipc_remove(const char *name);

int pipe_create(void);
int pipe_write(int fd, const void *data, size_t size);
int pipe_read(int fd, void *buffer, size_t size);
size_t pipe_readable(int fd);
size_t pipe_writable(int fd);
int pipe_has_readers(int fd);
int pipe_has_writers(int fd);
void pipe_endpoint_close(int fd, int readable, int writable);
void pipe_close(int fd);
int pipe_wait(int fd,int writing);
void pipe_thread_stopped(uint32_t tid);

#endif
