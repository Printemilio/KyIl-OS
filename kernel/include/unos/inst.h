#ifndef UNOS_INST_H
#define UNOS_INST_H

#include <stddef.h>
#include <stdint.h>

void inst_init(void);
int inst_execute(int argc, char **argv);
int inst_prepare_for_execution(const char *path, char *launch_path, size_t launch_path_capacity);
uint8_t inst_abi_for_path(const char *path);
int inst_abi_for_bytes(const void *,size_t);
void inst_cleanup_for_path(const char *path);

#endif
