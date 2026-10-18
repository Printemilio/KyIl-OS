#include <unos/users.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/heap.h>
#include <unos/security.h>
#include <unos/scheduler.h>

typedef struct user_node {
    user_t user;
    struct user_node *next;
} user_node_t;

static user_node_t *users;
static uint32_t next_uid;
static int users_ready;

static size_t string_length(const char *str) {
    size_t length = 0;
    while (str != NULL && str[length] != '\0') {
        ++length;
    }
    return length;
}

static int string_equals(const char *left, const char *right) {
    size_t i = 0;
    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) {
            return 0;
        }
        ++i;
    }
    return left[i] == right[i];
}

static void copy_username(char *dst, const char *src) {
    size_t i = 0;
    while (i + 1u < sizeof(((user_t *)0)->username) && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static user_node_t *find_user_by_name(const char *username) {
    for (user_node_t *node = users; node != NULL; node = node->next) {
        if (string_equals(node->user.username, username)) {
            return node;
        }
    }
    return NULL;
}

static user_node_t *find_user_by_uid(uint32_t uid) {
    for (user_node_t *node = users; node != NULL; node = node->next) {
        if (node->user.uid == uid) {
            return node;
        }
    }
    return NULL;
}

void users_init(void) {
    users = NULL;
    next_uid = 0;
    users_ready = 1;

    (void)user_create("root", 1);
    (void)user_login("root");
}

int user_create(const char *username, uint8_t is_admin) {
    if (!users_ready || username == NULL || username[0] == '\0' || string_length(username) >= sizeof(((user_t *)0)->username)) {
        return -1;
    }

    if (users != NULL && !user_has_capability(VEX_CAP_USER_ADMIN)) {
        security_audit_log_event("user-create", username, scheduler_current_pid(), scheduler_current_uid(),
                                 scheduler_current_capabilities(), 0u);
        return -1;
    }

    if (find_user_by_name(username) != NULL) {
        return -1;
    }

    user_node_t *node = kmalloc(sizeof(*node));
    if (node == NULL) {
        return -1;
    }

    node->user.uid = next_uid++;
    copy_username(node->user.username, username);
    node->user.is_admin = is_admin != 0;
    node->user.capabilities = security_default_caps_for_admin(is_admin);
    node->next = users;
    users = node;
    security_audit_log_event("user-create", username, scheduler_current_pid(), scheduler_current_uid(),
                             scheduler_current_capabilities(), 1u);
    return 0;
}

int user_login(const char *username) {
    if (!users_ready || username == NULL) {
        return -1;
    }

    user_node_t *node = find_user_by_name(username);
    if (node == NULL) {
        security_audit_log_event("login", username, scheduler_current_pid(), scheduler_current_uid(),
                                 scheduler_current_capabilities(), 0u);
        return -1;
    }

    scheduler_set_current_credentials(node->user.uid, node->user.capabilities);
    security_audit_log_event("login", username, scheduler_current_pid(), scheduler_current_uid(),
                             scheduler_current_capabilities(), 1u);
    return 0;
}

uint32_t user_current_uid(void) {
    return scheduler_current_uid();
}

uint32_t user_current_capabilities(void) {
    return scheduler_current_capabilities();
}

uint8_t user_has_capability(uint32_t capabilities) {
    return scheduler_current_has_capability(capabilities);
}

uint8_t user_is_admin(void) {
    return scheduler_current_is_admin();
}

const char *user_current_username(void) {
    user_node_t *node = find_user_by_uid(user_current_uid());
    return node == NULL ? "unknown" : node->user.username;
}

int user_lookup_uid(const char *username, uint32_t *uid_out) {
    user_node_t *node;

    if (username == NULL || uid_out == NULL) {
        return -1;
    }

    node = find_user_by_name(username);
    if (node == NULL) {
        return -1;
    }

    *uid_out = node->user.uid;
    return 0;
}

const char *user_name_for_uid(uint32_t uid) {
    user_node_t *node = find_user_by_uid(uid);
    return node == NULL ? "unknown" : node->user.username;
}
