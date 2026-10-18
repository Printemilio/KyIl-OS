#include <unos/browser_downloads.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/gui.h>
#include <unos/kernel_guard.h>

/* Leaf gate: nothing below calls out while it is held. */
static kernel_gate_t downloads_gate = KERNEL_GATE_INIT;

typedef struct download_entry {
    uint8_t used;
    uint32_t order;                 /* creation order, for eviction and listing */
    vex_download_state_t state;
} download_entry_t;

static download_entry_t downloads[VEX_BROWSER_MAX_DOWNLOADS];
static uint32_t downloads_next_order;

static int text_is_terminated(const char *text, size_t capacity) {
    for (size_t i = 0u; i < capacity; ++i) {
        if (text[i] == '\0') {
            return 1;
        }
    }
    return 0;
}

static int text_contains(const char *text, char wanted) {
    for (size_t i = 0u; text[i] != '\0'; ++i) {
        if (text[i] == wanted) {
            return 1;
        }
    }
    return 0;
}

static int report_is_valid(const vex_download_state_t *state) {
    if (state->id == 0u || state->reserved != 0u) {
        return 0;
    }
    if (state->status < VEX_DOWNLOAD_RUNNING || state->status > VEX_DOWNLOAD_CANCELLED) {
        return 0;
    }
    if (!text_is_terminated(state->name, sizeof(state->name)) ||
        !text_is_terminated(state->destination, sizeof(state->destination)) ||
        !text_is_terminated(state->error, sizeof(state->error))) {
        return 0;
    }
    /* A name is one path component; a destination, when known, is absolute. */
    if (text_contains(state->name, '/')) {
        return 0;
    }
    if (state->destination[0] != '\0' && state->destination[0] != '/') {
        return 0;
    }
    return 1;
}

static download_entry_t *find_entry(uint32_t id) {
    for (unsigned i = 0u; i < VEX_BROWSER_MAX_DOWNLOADS; ++i) {
        if (downloads[i].used && downloads[i].state.id == id) {
            return &downloads[i];
        }
    }
    return NULL;
}

/* A free slot, else the oldest finished one. Running downloads are never evicted. */
static download_entry_t *allocate_entry(void) {
    download_entry_t *oldest_final = NULL;

    for (unsigned i = 0u; i < VEX_BROWSER_MAX_DOWNLOADS; ++i) {
        if (!downloads[i].used) {
            return &downloads[i];
        }
        if (vex_download_status_is_final(downloads[i].state.status) &&
            (oldest_final == NULL || downloads[i].order < oldest_final->order)) {
            oldest_final = &downloads[i];
        }
    }
    return oldest_final;
}

void browser_downloads_session_started(void) {
    KERNEL_GUARD(downloads_gate);
    for (unsigned i = 0u; i < VEX_BROWSER_MAX_DOWNLOADS; ++i) {
        downloads[i] = (download_entry_t){0};
    }
    downloads_next_order = 0u;
}

void browser_downloads_session_ended(void) {
    KERNEL_GUARD(downloads_gate);
    for (unsigned i = 0u; i < VEX_BROWSER_MAX_DOWNLOADS; ++i) {
        if (downloads[i].used && !vex_download_status_is_final(downloads[i].state.status)) {
            downloads[i].state.status = VEX_DOWNLOAD_CANCELLED;
        }
    }
}

int browser_downloads_update(const vex_download_state_t *state) {
    download_entry_t *entry;

    if (state == NULL || !report_is_valid(state)) {
        return -22;
    }

    KERNEL_GUARD(downloads_gate);
    entry = find_entry(state->id);
    if (entry == NULL) {
        entry = allocate_entry();
        if (entry == NULL) {
            return -28;
        }
        *entry = (download_entry_t){0};
        entry->used = 1u;
        entry->order = downloads_next_order++;
    } else if (vex_download_status_is_final(entry->state.status)) {
        return 0;
    }
    entry->state = *state;
    return 0;
}

unsigned browser_downloads_snapshot(vex_download_state_t *out, unsigned capacity) {
    unsigned count = 0u;
    uint32_t previous_order = 0u;
    int have_previous = 0;

    if (out == NULL) {
        return 0u;
    }

    KERNEL_GUARD(downloads_gate);
    /* Selection by ascending order: the table is tiny and this stays simple. */
    while (count < capacity) {
        download_entry_t *next = NULL;

        for (unsigned i = 0u; i < VEX_BROWSER_MAX_DOWNLOADS; ++i) {
            if (!downloads[i].used) {
                continue;
            }
            if (have_previous && downloads[i].order <= previous_order) {
                continue;
            }
            if (next == NULL || downloads[i].order < next->order) {
                next = &downloads[i];
            }
        }
        if (next == NULL) {
            break;
        }
        out[count++] = next->state;
        previous_order = next->order;
        have_previous = 1;
    }
    return count;
}

int browser_downloads_request_cancel(uint32_t id) {
    {
        KERNEL_GUARD(downloads_gate);
        download_entry_t *entry = find_entry(id);

        if (entry == NULL || vex_download_status_is_final(entry->state.status)) {
            return -2;
        }
    }
    /* Queued to the supervisor like every other browser command. */
    return gui_browser_download_cancel(id);
}
