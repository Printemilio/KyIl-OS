#ifndef UNOS_SCHEDULER_H
#define UNOS_SCHEDULER_H

#include <stdint.h>

typedef struct process {
    uint32_t pid;
    uint64_t *stack;
    uint64_t rsp;
    uint64_t cr3;
    enum { PROC_RUNNING, PROC_READY, PROC_WAITING, PROC_SLEEPING, PROC_ZOMBIE } state;
    uint8_t priority;
    uint32_t uid;
    uint8_t is_admin;
    uint8_t user_mode;
    uint8_t abi;
    uint64_t user_rsp;
    uint64_t user_alloc_base;
    uint64_t user_alloc_next;
    uint64_t sleep_until_tick;
    uint32_t parent_pid;
    int exit_status;
    uint8_t wait_collected;
    uint64_t syscall_frame_rsp;
    uint64_t resume_syscall_rsp;
    uint64_t pending_user_entry;
    uint64_t pending_old_cr3;
    uint64_t fs_base;
    uint8_t shares_address_space;
    uint64_t clear_child_tid;
    struct process *next;
} process_t;

typedef struct scheduler_process_info {
    uint32_t pid;
    uint32_t parent_pid;
    uint8_t state;
    uint8_t priority;
    uint8_t user_mode;
    uint8_t is_admin;
    uint32_t uid;
    uint64_t sleep_until_tick;
} scheduler_process_info_t;

void scheduler_init(void);
uint32_t scheduler_current_pid(void);
uint32_t scheduler_current_ppid(void);
uint8_t scheduler_process_accepts_input(uint32_t pid);
uint32_t process_create(void (*entry)(void), uint8_t priority);
uint32_t process_create_in_address_space(void (*entry)(void), uint8_t priority, uint64_t cr3);
uint32_t process_create_user(void (*entry)(void), uint8_t priority, uint64_t cr3, uint64_t user_rsp);
uint32_t process_create_user_abi(void (*entry)(void), uint8_t priority, uint64_t cr3, uint64_t user_rsp, uint8_t abi);
void scheduler_record_syscall_frame(uint64_t rsp);
uint32_t process_fork_current(void);
uint32_t process_clone_thread(uint64_t child_stack, uint64_t flags, uint64_t parent_tid, uint64_t child_tid, uint64_t tls);
void process_set_clear_child_tid(uint64_t addr);
void process_exit_group(int status);
int futex_wait(uint64_t uaddr, uint32_t expected);
int futex_wake(uint64_t uaddr, uint32_t count);
int process_exec_image(uint64_t new_cr3, uint64_t user_entry, uint64_t user_rsp, uint8_t abi, uint64_t user_alloc_next);
void process_yield(void);
void process_yield_to(uint32_t pid);
void process_sleep_ticks(uint64_t ticks);
void process_wait(void);
void process_wake(uint32_t pid);
int process_kill(uint32_t pid);
void process_exit(void);
void process_exit_status(int status);
int process_wait_pid(int32_t pid, int *status, uint32_t flags, uint32_t *reaped_pid);
void schedule(void);
void scheduler_tick(void);
uint64_t scheduler_ticks_now(void);
uint32_t scheduler_current_uid(void);
uint8_t scheduler_current_is_admin(void);
uint8_t scheduler_current_user_mode(void);
uint8_t scheduler_current_abi(void);
void scheduler_set_process_abi(uint32_t pid, uint8_t abi);
void scheduler_set_process_user_alloc_next(uint32_t pid, uint64_t user_alloc_next);
void scheduler_set_current_credentials(uint32_t uid, uint8_t is_admin);
void scheduler_set_current_fs_base(uint64_t fs_base);
uint64_t scheduler_current_fs_base(void);
process_t *scheduler_current_process(void);
void scheduler_map_kernel_page_all(uint64_t virt, uint64_t phys, uint64_t flags);
void scheduler_unmap_kernel_page_all(uint64_t virt);
const char *scheduler_state_name(uint32_t pid);
const char *scheduler_state_name_value(uint8_t state);
void scheduler_set_process_priority(uint32_t pid, uint8_t priority);
uint64_t scheduler_process_count(void);
int scheduler_process_info_at(uint64_t index, scheduler_process_info_t *out);

#endif
