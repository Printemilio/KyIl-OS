#ifndef UNOS_PE_H
#define UNOS_PE_H

#include <stdint.h>
#include <stddef.h>

int pe_load(const char *path);
int pe_spawn(const char *path);
int pe_spawn_pid(const char *path, uint32_t *pid);
int pe_spawn_pid_caps(const char *, uint32_t *, uint32_t);
int pe_load_args(const char *, const char *const *, size_t,
                 const char *const *, size_t, int *);
int pe_exec_args(const char *, const char *const *, size_t,
                 const char *const *, size_t);

#endif
