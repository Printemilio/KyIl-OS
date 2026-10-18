#ifndef KYIL_DRIVER_MANAGER_H
#define KYIL_DRIVER_MANAGER_H
#include <unos/driver_vm.h>
#include <stddef.h>
#include <stdint.h>
void driver_manager_init(void);
/* Native CLOCK interface returns NOT_BOUND only after hardware is released. */
int driver_clock_read(uint64_t date[8]);
/* Native character interface: byte streams, scheduler wait, no filesystem name. */
int driver_character_write(uint32_t id,uint8_t byte);
int driver_character_read(uint32_t id,uint8_t *byte,uint64_t timeout_ticks);
int driver_manager_load(const char *path,uint32_t *id);
int driver_manager_unload(uint32_t id);
int driver_manager_stop_all(void);
int driver_manager_install(const char *path,const uint8_t expected_sha[32]);
void driver_manager_command(const char *arguments);
int driver_manager_self_test(void);
#endif
