#ifndef UNOS_ELF_H
#define UNOS_ELF_H

#include <stddef.h>
#include <stdint.h>

int elf_load(const char *path);
/* Run a program with arguments and environment and wait for it; the wait
   status (as waitpid reports it) is stored in *status_out when not NULL. */
int elf_load_args(const char *path, const char *const *argv, size_t argc,
                  const char *const *envp, size_t envc, int *status_out);
int elf_spawn(const char *path);
int elf_spawn_pid_caps(const char *path, uint32_t *pid_out, uint32_t caps);
int elf_spawn_pid(const char *path, uint32_t *pid_out);
int elf_run_native_runtime(const char *, const char *const *, size_t,
                          const char *const *, size_t, uint32_t *, uint32_t,
                          int, int *, uint8_t);
int elf_exec_native_runtime(const char *, const char *const *, size_t,
                           const char *const *, size_t, uint8_t);
int elf_exec_current(const char *path);
int elf_spawn_native_snapshot(const void *,size_t,const char *const *,size_t,
    const char *const *,size_t,int (*)(uint32_t,void *),void *,uint32_t *);
int elf_exec_current_args(const char *path,
                          const char *const *argv, size_t argc,
                          const char *const *envp, size_t envc);

#endif
