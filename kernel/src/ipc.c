#include <unos/ipc.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/named_content.h>
#include <unos/heap.h>
#include <unos/kernel_guard.h>
static kernel_gate_t pipe_gate=KERNEL_GATE_INIT;
#include <unos/events.h>
typedef struct pipe_waiter {
    struct pipe_waiter *next;
    uint32_t tid;
    uint8_t writing,woken;
} pipe_waiter_t;

static named_content_map_t blobs;
#define IPC_PIPE_COUNT 16u
#define IPC_PIPE_SIZE 4096u
#define FNV1A_OFFSET_BASIS 14695981039346656037ull
#define FNV1A_PRIME 1099511628211ull

typedef struct {
    int used;
    uint8_t *buffer;
    size_t read_pos;
    size_t write_pos;
    size_t size;
    uint32_t readers;
    uint32_t writers;
    uint32_t wait_refs;
    pipe_waiter_t *waiters;
    uint8_t closed;
} ipc_pipe_t;

static ipc_pipe_t pipes[IPC_PIPE_COUNT];
static int ipc_ready;
static void pipe_wake(ipc_pipe_t *pipe,int writing) {
    for(pipe_waiter_t *w=pipe->waiters;w;w=w->next)
        if(writing<0 || w->writing==(unsigned)writing) {
            __atomic_store_n(&w->woken,1u,__ATOMIC_RELEASE);
            scheduler_wake_flow(w->tid,scheduler_flow_current());
        }
}
static void pipe_free_closed(ipc_pipe_t *pipe) {
    if(pipe->closed && !pipe->wait_refs) {
        kfree(pipe->buffer);*pipe=(ipc_pipe_t){0};
    }
}
int pipe_wait(int fd,int writing) {
    pipe_waiter_t waiter={.tid=scheduler_current_tid(),.writing=writing!=0};
    ipc_pipe_t *pipe;
    {KERNEL_GUARD(pipe_gate);
     if(fd<0 || (unsigned)fd>=IPC_PIPE_COUNT || !pipes[fd].used)return -1;
     pipe=&pipes[fd];
     if(pipe->closed || (writing?(!pipe->readers || pipe->size<IPC_PIPE_SIZE):(!pipe->writers || pipe->size)))return 0;
     waiter.next=pipe->waiters;pipe->waiters=&waiter;++pipe->wait_refs;}
    while(!__atomic_load_n(&waiter.woken,__ATOMIC_ACQUIRE) && !vex_event_interrupt_pending())process_wait_event_interruptible(&waiter.woken);
    int result=vex_event_interrupt_pending()?-2:0;
    {KERNEL_GUARD(pipe_gate);
     pipe_waiter_t **link=&pipe->waiters;while(*link && *link!=&waiter)link=&(*link)->next;
     if(*link){*link=waiter.next;--pipe->wait_refs;}
     pipe_free_closed(pipe);}
    return result;
}
void pipe_thread_stopped(uint32_t tid) {
    KERNEL_GUARD(pipe_gate);
    for(unsigned i=0;i<IPC_PIPE_COUNT;++i) {
        ipc_pipe_t *pipe=&pipes[i];pipe_waiter_t **link=&pipe->waiters;
        while(*link){pipe_waiter_t *w=*link;
            if(w->tid!=tid){link=&w->next;continue;}
            *link=w->next;--pipe->wait_refs;}
        pipe_free_closed(pipe);
    }
}

void ipc_init(void) {
    for (size_t i = 0; i < IPC_PIPE_COUNT; ++i) {
        pipes[i].used = 0;
        pipes[i].buffer = NULL;
        pipes[i].read_pos = 0;
        pipes[i].write_pos = 0;
        pipes[i].size = 0;
        pipes[i].readers = 0u;
        pipes[i].writers = 0u;
    }

    ipc_ready = 1;
}

int ipc_store(const char *name,const void *data,size_t size) {
    if(!ipc_ready || !name || !*name || !data || !size)return -1;
    return named_content_put(&blobs,name,data,size,NAMED_UPSERT);
}
int ipc_acquire(const char *name,ora_view_t *view) {
    if(!ipc_ready || !name || !view)return -1;
    ora_ref_t content=named_content_acquire(&blobs,name);if(!content)return -1;
    ora_view_release(view);int result=ora_view_acquire(content,0,ora_size(content),view);ora_release(content);return result;
}
void ipc_remove(const char *name) {named_content_remove(&blobs,name);}

int pipe_create(void) {
    KERNEL_GUARD(pipe_gate);
    if (!ipc_ready) {
        return -1;
    }

    for (size_t i = 0; i < IPC_PIPE_COUNT; ++i) {
        if (pipes[i].used) {
            continue;
        }

        pipes[i].buffer = kmalloc(IPC_PIPE_SIZE);
        if (pipes[i].buffer == NULL) {
            return -1;
        }

        pipes[i].used = 1;
        pipes[i].read_pos = 0;
        pipes[i].write_pos = 0;
        pipes[i].size = 0;
        pipes[i].readers = 1u;
        pipes[i].writers = 1u;
        pipes[i].closed=0;pipes[i].wait_refs=0;pipes[i].waiters=NULL;
        return (int)i;
    }

    return -1;
}

int pipe_write(int fd, const void *data, size_t size) {
    KERNEL_GUARD(pipe_gate);
    if (fd < 0 || (size_t)fd >= IPC_PIPE_COUNT || !pipes[fd].used || data == NULL) {
        return -1;
    }

    ipc_pipe_t *pipe = &pipes[fd];
    const uint8_t *bytes = data;
    size_t written = 0;

    if (pipe->readers == 0u) {
        return -1;
    }

    while (written < size && pipe->size < IPC_PIPE_SIZE) {
        pipe->buffer[pipe->write_pos] = bytes[written];
        pipe->write_pos = (pipe->write_pos + 1u) % IPC_PIPE_SIZE;
        ++pipe->size;
        ++written;
    }

    if(written)pipe_wake(pipe,0);
    return (int)written;
}

int pipe_read(int fd, void *buffer, size_t size) {
    KERNEL_GUARD(pipe_gate);
    if (fd < 0 || (size_t)fd >= IPC_PIPE_COUNT || !pipes[fd].used || buffer == NULL) {
        return -1;
    }

    ipc_pipe_t *pipe = &pipes[fd];
    uint8_t *bytes = buffer;
    size_t read = 0;

    while (read < size && pipe->size > 0) {
        bytes[read] = pipe->buffer[pipe->read_pos];
        pipe->read_pos = (pipe->read_pos + 1u) % IPC_PIPE_SIZE;
        --pipe->size;
        ++read;
    }

    if(read)pipe_wake(pipe,1);
    return (int)read;
}

size_t pipe_readable(int fd) {
    KERNEL_GUARD(pipe_gate);
    if (fd < 0 || (size_t)fd >= IPC_PIPE_COUNT || !pipes[fd].used) {
        return 0u;
    }

    return pipes[fd].size;
}

size_t pipe_writable(int fd) {
    KERNEL_GUARD(pipe_gate);
    if (fd < 0 || (size_t)fd >= IPC_PIPE_COUNT || !pipes[fd].used) {
        return 0u;
    }

    return pipes[fd].readers == 0u ? 0u : IPC_PIPE_SIZE - pipes[fd].size;
}

int pipe_has_readers(int fd) {
    KERNEL_GUARD(pipe_gate);
    return fd >= 0 && (size_t)fd < IPC_PIPE_COUNT && pipes[fd].used && pipes[fd].readers != 0u;
}

int pipe_has_writers(int fd) {
    KERNEL_GUARD(pipe_gate);
    return fd >= 0 && (size_t)fd < IPC_PIPE_COUNT && pipes[fd].used && pipes[fd].writers != 0u;
}

void pipe_endpoint_close(int fd, int readable, int writable) {
    KERNEL_GUARD(pipe_gate);
    if (fd < 0 || (size_t)fd >= IPC_PIPE_COUNT || !pipes[fd].used) {
        return;
    }
    if (readable && pipes[fd].readers > 0u) {
        --pipes[fd].readers;
    }
    if (writable && pipes[fd].writers > 0u) {
        --pipes[fd].writers;
    }
    pipe_wake(&pipes[fd],-1);
    if (pipes[fd].readers == 0u && pipes[fd].writers == 0u) {
        pipe_close(fd);
    }
}

void pipe_close(int fd) {
    KERNEL_GUARD(pipe_gate);
    if (fd < 0 || (size_t)fd >= IPC_PIPE_COUNT || !pipes[fd].used) {
        return;
    }

    pipes[fd].closed=1;pipes[fd].readers=pipes[fd].writers=0;
    pipe_wake(&pipes[fd],-1);pipe_free_closed(&pipes[fd]);
}
