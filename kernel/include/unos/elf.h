#ifndef UNOS_ELF_H
#define UNOS_ELF_H

#include <stdint.h>

int elf_load(const char *path);
int elf_spawn(const char *path);
int elf_spawn_pid(const char *path, uint32_t *pid_out);
int elf_exec_current(const char *path);

#endif
