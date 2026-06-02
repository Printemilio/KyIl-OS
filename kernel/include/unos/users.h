#ifndef UNOS_USERS_H
#define UNOS_USERS_H

#include <stdint.h>

typedef struct user {
    uint32_t uid;
    char username[32];
    uint8_t is_admin;
} user_t;

void users_init(void);
int user_create(const char *username, uint8_t is_admin);
int user_login(const char *username);
uint32_t user_current_uid(void);
uint8_t user_is_admin(void);
const char *user_current_username(void);

#endif
