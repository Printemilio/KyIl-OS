#include <stddef.h>
#include <stdint.h>

#include <unos/vex_abi.h>
#include <unos/vex_legacy_compat.h>

#include "vex_syscall.h"

static size_t string_length(const char *text) {
    size_t length = 0u;
    while (text != NULL && text[length] != '\0') {
        ++length;
    }
    return length;
}

static void write_text(const char *text) {
    (void)write(STDOUT_FILENO, text, string_length(text));
}

static void write_status_line(const char *text) {
    static const char clear_tail[] = "                                        \n";
    (void)write(STDOUT_FILENO, text, string_length(text));
    (void)write(STDOUT_FILENO, clear_tail, sizeof(clear_tail) - 1u);
}

static void append_text(char *buffer, size_t capacity, size_t *length, const char *text) {
    size_t i = 0u;
    while (text != NULL && text[i] != '\0' && *length + 1u < capacity) {
        buffer[(*length)++] = text[i++];
    }
    buffer[*length] = '\0';
}

static const char *elevation_state_name(uint32_t state) {
    switch (state) {
    case VEX_ELEVATION_STATE_PENDING:
        return "pending";
    case VEX_ELEVATION_STATE_APPROVED:
        return "approved";
    case VEX_ELEVATION_STATE_DENIED:
        return "denied";
    case VEX_ELEVATION_STATE_EXPIRED:
        return "expired";
    case VEX_ELEVATION_STATE_CANCELLED:
        return "cancelled";
    default:
        return "unknown";
    }
}

static void print_admin_value(const char *label, uint8_t is_admin) {
    char line[96];
    size_t length = 0u;

    append_text(line, sizeof(line), &length, "elevate_test: ");
    append_text(line, sizeof(line), &length, label);
    append_text(line, sizeof(line), &length, " admin=");
    append_text(line, sizeof(line), &length, is_admin ? "yes" : "no");
    write_status_line(line);
}

static void print_admin_state(const char *label) {
    vex_bulk_result_t bulk;
    vex_bulk_req_t req;

    req.mask = VEX_BULK_UID;
    req.result = &bulk;
    if ((int64_t)vex_syscall1(VEX_SYS_BULK, (uint64_t)(uintptr_t)&req) != 0) {
        write_status_line("elevate_test: bulk failed");
        return;
    }

    print_admin_value(label, bulk.is_admin);
}

static void try_protected_access(const char *label) {
    char line[128];
    size_t length = 0u;
    int fd = open("/sys/elevate_probe", O_RDWR | O_CREAT, 0644);
    int result = fd < 0 ? -1 : close(fd);

    append_text(line, sizeof(line), &length, "elevate_test: ");
    append_text(line, sizeof(line), &length, label);
    append_text(line, sizeof(line), &length, " ");
    if (result != 0) {
        append_text(line, sizeof(line), &length, "protected access denied");
        write_status_line(line);
        return;
    }

    append_text(line, sizeof(line), &length, "protected access ok");
    write_status_line(line);
}

int main(void) {
    vex_elevation_request_req_t request;
    vex_elevation_status_req_t status;
    uint32_t previous_state = VEX_ELEVATION_STATE_NONE;

    write_status_line("elevate_test: begin");
    print_admin_state("before");
    try_protected_access("before");

    request.reason = "elevate_test wants admin access to /sys/elevate_probe";
    request.flags = VEX_ELEVATION_CAP_FS_ADMIN;
    request.request_id = 0u;
    if ((int64_t)vex_syscall1(VEX_SYS_ELEVATION_REQUEST, (uint64_t)(uintptr_t)&request) != 0) {
        write_status_line("elevate_test: request failed");
        return 1;
    }

    write_status_line("elevate_test: request submitted");
    for (uint32_t attempt = 0u; attempt < 200u; ++attempt) {
        char line[128];
        size_t length = 0u;

        status.request_id = request.request_id;
        status.state = VEX_ELEVATION_STATE_NONE;
        status.pid = 0u;
        status.flags = 0u;
        status.created_ticks = 0u;
        status.expires_ticks = 0u;
        status.is_admin = 0u;
        if ((int64_t)vex_syscall1(VEX_SYS_ELEVATION_STATUS, (uint64_t)(uintptr_t)&status) != 0) {
            write_status_line("elevate_test: status failed");
            return 1;
        }

        if (status.state != previous_state) {
            append_text(line, sizeof(line), &length, "elevate_test: state=");
            append_text(line, sizeof(line), &length, elevation_state_name(status.state));
            write_status_line(line);
            previous_state = status.state;
        }

        if (status.state != VEX_ELEVATION_STATE_PENDING) {
            break;
        }
        (void)usleep(100000u);
    }

    status.request_id = request.request_id;
    if ((int64_t)vex_syscall1(VEX_SYS_ELEVATION_STATUS, (uint64_t)(uintptr_t)&status) != 0) {
        write_status_line("elevate_test: final status failed");
        return 1;
    }

    if (status.state == VEX_ELEVATION_STATE_APPROVED) {
        print_admin_value("after", status.is_admin);
        try_protected_access("after");
    }

    write_status_line("elevate_test: end");
    return 0;
}
