#ifndef UNOS_USERS_H
#define UNOS_USERS_H

#include <stdint.h>

typedef struct user {
    uint32_t uid;
    char username[32];
    uint8_t is_admin;
    uint32_t capabilities;
} user_t;

void users_init(void);
int user_create(const char *username, uint8_t is_admin);
int user_login(const char *username);
uint32_t user_current_uid(void);
uint32_t user_current_capabilities(void);
uint8_t user_has_capability(uint32_t capabilities);
uint8_t user_is_admin(void);
const char *user_current_username(void);
int user_lookup_uid(const char *username, uint32_t *uid_out);
const char *user_name_for_uid(uint32_t uid);

#endif
