/* Expose POSIX + GNU extensions before any include. */
#define _GNU_SOURCE

/*
 * Download manager for the Vex browser runtime. See webkit_downloads.h for the
 * responsibilities and the lifetime rules.
 *
 * WebKit objects used here are declared locally: the staging build compiles
 * against a deliberately small WPE header, like webkit_linux_main.c does.
 */

#include "webkit_downloads.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <vex_download_names.h>

typedef struct _WebKitDownload WebKitDownload;
typedef struct _WebKitURIRequest WebKitURIRequest;
typedef struct _WebKitURIResponse WebKitURIResponse;

extern WebKitNetworkSession *webkit_network_session_get_default(void);
extern void webkit_download_cancel(WebKitDownload *download);
extern void webkit_download_set_allow_overwrite(WebKitDownload *download, gboolean allowed);
extern void webkit_download_set_destination(WebKitDownload *download, const gchar *destination);
extern WebKitURIRequest *webkit_download_get_request(WebKitDownload *download);
extern WebKitURIResponse *webkit_download_get_response(WebKitDownload *download);
extern WebKitWebView *webkit_download_get_web_view(WebKitDownload *download);
extern guint64 webkit_download_get_received_data_length(WebKitDownload *download);
extern const gchar *webkit_uri_request_get_uri(WebKitURIRequest *request);
extern guint64 webkit_uri_response_get_content_length(WebKitURIResponse *response);
extern GQuark webkit_download_error_quark(void);
extern WebKitURIResponse *webkit_response_policy_decision_get_response(gpointer decision);
extern gboolean webkit_response_policy_decision_is_mime_type_supported(gpointer decision);
extern void webkit_policy_decision_download(gpointer decision);
extern gpointer webkit_uri_response_get_http_headers(WebKitURIResponse *response);
extern const char *soup_message_headers_get_one(gpointer headers, const char *name);

/* WebKitDownloadError codes (stable public API). */
#define VEX_WEBKIT_DOWNLOAD_ERROR_CANCELLED_BY_USER 400
#define VEX_WEBKIT_DOWNLOAD_ERROR_DESTINATION 401

/* Progress-only reports are coalesced so a fast transfer does not flood the
 * supervisor channel; the final state is never delayed. */
#define PROGRESS_REPORT_INTERVAL_US (100 * 1000)

/* How long shutdown waits for WebKit to acknowledge cancellations. */
#define SHUTDOWN_WAIT_US (1500 * 1000)

typedef struct download_slot {
    gboolean in_use;
    WebKitDownload *download;           /* strong reference while not final */
    vex_download_state_t state;
    char base_name[VEX_DOWNLOAD_NAME_SIZE];   /* sanitized, without " (n)" */
    char part_path[VEX_DOWNLOAD_PATH_SIZE];
    gboolean destination_chosen;
    gboolean cancel_requested;
    gboolean report_pending;
    gint64 last_report_us;
} download_slot;

static download_slot slots[VEX_BROWSER_MAX_DOWNLOADS];
static uint32_t next_download_id = 1u;
static vex_downloads_page_lookup page_lookup;

/* ── small helpers ───────────────────────────────────────────────────────── */

static void copy_text(char *destination, size_t capacity, const char *source) {
    g_strlcpy(destination, source != NULL ? source : "", capacity);
}

static download_slot *find_slot_by_id(uint32_t id) {
    unsigned i;

    for (i = 0u; i < VEX_BROWSER_MAX_DOWNLOADS; ++i) {
        if (slots[i].in_use && slots[i].state.id == id) {
            return &slots[i];
        }
    }
    return NULL;
}

static download_slot *allocate_slot(void) {
    unsigned i;

    for (i = 0u; i < VEX_BROWSER_MAX_DOWNLOADS; ++i) {
        if (!slots[i].in_use) {
            return &slots[i];
        }
    }
    return NULL;
}

static gboolean path_exists(const char *path) {
    struct stat info;

    return lstat(path, &info) == 0;
}

static void mark_report_pending(download_slot *slot) {
    slot->report_pending = TRUE;
}

static void log_state(const download_slot *slot, const char *event) {
    fprintf(stderr, "browser download: id=%u %s status=%u received=%llu total=%llu name=%s%s%s\n",
            slot->state.id, event, slot->state.status,
            (unsigned long long)slot->state.received_bytes,
            (unsigned long long)slot->state.total_bytes, slot->state.name,
            slot->state.error[0] != '\0' ? " error=" : "",
            slot->state.error);
}

static void remove_file(download_slot *slot, const char *path) {
    if (unlink(path) != 0 && errno != ENOENT) {
        fprintf(stderr, "browser download: id=%u cannot remove %s: %s\n",
                slot->state.id, path, strerror(errno));
    }
}

/* Delete everything an unfinished download may have left: the destination
 * placeholder WebKit creates and the temporary file it streams into. */
static void remove_partial_file(download_slot *slot) {
    char intermediate[VEX_DOWNLOAD_PATH_SIZE];

    if (slot->part_path[0] == '\0') {
        return;
    }
    if (g_snprintf(intermediate, sizeof(intermediate), "%s%s", slot->part_path,
                   VEX_DOWNLOAD_WEBKIT_SUFFIX) < (gint)sizeof(intermediate)) {
        remove_file(slot, intermediate);
    }
    remove_file(slot, slot->part_path);
    slot->part_path[0] = '\0';
}

/* ── destination policy ──────────────────────────────────────────────────── */

static gboolean ensure_download_directory(char *error, size_t error_size) {
    struct stat info;

    if (stat(VEX_DOWNLOAD_DIRECTORY, &info) == 0) {
        if (S_ISDIR(info.st_mode)) {
            return TRUE;
        }
        g_snprintf(error, error_size, "%s is not a directory", VEX_DOWNLOAD_DIRECTORY);
        return FALSE;
    }
    if (mkdir(VEX_DOWNLOAD_DIRECTORY, 0755) != 0 && errno != EEXIST) {
        g_snprintf(error, error_size, "Cannot create %s: %s",
                   VEX_DOWNLOAD_DIRECTORY, strerror(errno));
        return FALSE;
    }
    return TRUE;
}

/* True when another running download already owns this final name. A name is
 * reserved from the moment it is chosen until the download reaches a final
 * state, which keeps parallel downloads of the same file apart even though
 * neither file exists yet when the second name is chosen. */
static gboolean final_path_reserved(const download_slot *requester, const char *path) {
    unsigned i;

    for (i = 0u; i < VEX_BROWSER_MAX_DOWNLOADS; ++i) {
        const download_slot *other = &slots[i];

        if (other == requester || !other->in_use || !other->destination_chosen) {
            continue;
        }
        if (vex_download_status_is_final(other->state.status)) {
            continue;
        }
        if (strcmp(other->state.destination, path) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

/* Pick the first free "<name>", "<name> (1)", ... that is neither on disk, nor
 * reserved by another download. `avoid_partial` also rejects a name whose
 * in-progress file exists; publishing passes FALSE because that file is ours. */
static gboolean choose_final_path(download_slot *slot, const char *name, gboolean avoid_partial,
                                  char *path, size_t path_size, char *part, size_t part_size) {
    unsigned index;

    for (index = 0u; index <= VEX_DOWNLOAD_MAX_COLLISION_INDEX; ++index) {
        if (vex_download_candidate_path(VEX_DOWNLOAD_DIRECTORY, name, index, path, path_size) != 0 ||
            vex_download_part_path(path, part, part_size) != 0) {
            return FALSE;
        }
        if (final_path_reserved(slot, path) || path_exists(path)) {
            continue;
        }
        if (avoid_partial && path_exists(part)) {
            continue;
        }
        return TRUE;
    }
    return FALSE;
}

/* ── state transitions ───────────────────────────────────────────────────── */

static void release_download(download_slot *slot) {
    if (slot->download == NULL) {
        return;
    }
    g_signal_handlers_disconnect_by_data(slot->download, slot);
    g_object_unref(slot->download);
    slot->download = NULL;
}

/* The first outcome wins: a late "failed" must not overwrite a finished file,
 * and a cancellation that raced with completion must not hide the result. */
static void settle(download_slot *slot, uint32_t status, const char *error) {
    if (vex_download_status_is_final(slot->state.status)) {
        return;
    }
    slot->state.status = status;
    copy_text(slot->state.error, sizeof(slot->state.error), error);
    mark_report_pending(slot);
    log_state(slot, "settled");
}

static void update_progress(download_slot *slot) {
    WebKitURIResponse *response;
    guint64 total;

    slot->state.received_bytes = webkit_download_get_received_data_length(slot->download);
    response = webkit_download_get_response(slot->download);
    total = response != NULL ? webkit_uri_response_get_content_length(response) : 0u;
    /* A length smaller than what has arrived (encoded transfer) is not a total. */
    if (total == 0u || total == G_MAXUINT64 || total < slot->state.received_bytes) {
        total = VEX_DOWNLOAD_SIZE_UNKNOWN;
    }
    slot->state.total_bytes = total;
    mark_report_pending(slot);
}

/* Move the finished data to its final name. rename() replaces an existing file,
 * so the name is first claimed with an exclusive create: if another process took
 * it meanwhile, the claim fails and the next free name is used instead. */
static gboolean publish_download(download_slot *slot, char *error, size_t error_size) {
    struct stat info;
    char candidate[VEX_DOWNLOAD_PATH_SIZE];
    char part_unused[VEX_DOWNLOAD_PATH_SIZE];
    unsigned attempt;
    int claim;

    if (stat(slot->part_path, &info) != 0) {
        g_snprintf(error, error_size, "Downloaded data is missing: %s", strerror(errno));
        return FALSE;
    }
    for (attempt = 0u; attempt < 8u; ++attempt) {
        if (!choose_final_path(slot, slot->base_name, FALSE, candidate, sizeof(candidate),
                               part_unused, sizeof(part_unused))) {
            g_snprintf(error, error_size, "No free file name in %s", VEX_DOWNLOAD_DIRECTORY);
            return FALSE;
        }
        claim = open(candidate, O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (claim < 0) {
            if (errno == EEXIST) {
                continue;
            }
            g_snprintf(error, error_size, "Cannot create %s: %s", candidate, strerror(errno));
            return FALSE;
        }
        close(claim);
        if (rename(slot->part_path, candidate) == 0) {
            copy_text(slot->state.destination, sizeof(slot->state.destination), candidate);
            copy_text(slot->state.name, sizeof(slot->state.name), strrchr(candidate, '/') + 1);
            slot->part_path[0] = '\0';
            slot->state.received_bytes = (uint64_t)info.st_size;
            slot->state.total_bytes = (uint64_t)info.st_size;
            return TRUE;
        }
        g_snprintf(error, error_size, "Cannot move download into place: %s", strerror(errno));
        unlink(candidate);   /* give the claimed name back */
        return FALSE;
    }
    g_snprintf(error, error_size, "Cannot find a free file name in %s", VEX_DOWNLOAD_DIRECTORY);
    return FALSE;
}

/* ── WebKitDownload signal handlers ──────────────────────────────────────── */

static gboolean on_decide_destination(WebKitDownload *download, const gchar *suggested,
                                      gpointer data) {
    download_slot *slot = data;
    char name[VEX_DOWNLOAD_NAME_SIZE];
    char final_path[VEX_DOWNLOAD_PATH_SIZE];
    char part_path[VEX_DOWNLOAD_PATH_SIZE];
    char error[VEX_DOWNLOAD_ERROR_SIZE];

    if (slot->download != download || slot->destination_chosen) {
        return TRUE;
    }
    if (suggested != NULL && suggested[0] != '\0') {
        vex_download_sanitize_name(suggested, name, sizeof(name));
    } else {
        vex_download_name_from_uri(webkit_uri_request_get_uri(webkit_download_get_request(download)),
                                   name, sizeof(name));
    }

    if (!ensure_download_directory(error, sizeof(error))) {
        slot->cancel_requested = TRUE;
        settle(slot, VEX_DOWNLOAD_FAILED, error);
        webkit_download_cancel(download);
        return TRUE;
    }
    if (!choose_final_path(slot, name, TRUE, final_path, sizeof(final_path),
                           part_path, sizeof(part_path))) {
        slot->cancel_requested = TRUE;
        settle(slot, VEX_DOWNLOAD_FAILED, "No free file name in the Downloads directory");
        webkit_download_cancel(download);
        return TRUE;
    }

    copy_text(slot->base_name, sizeof(slot->base_name), name);
    copy_text(slot->state.name, sizeof(slot->state.name), strrchr(final_path, '/') + 1);
    copy_text(slot->state.destination, sizeof(slot->state.destination), final_path);
    copy_text(slot->part_path, sizeof(slot->part_path), part_path);
    slot->destination_chosen = TRUE;
    webkit_download_set_allow_overwrite(download, FALSE);
    webkit_download_set_destination(download, part_path);
    mark_report_pending(slot);
    log_state(slot, "destination");
    return TRUE;
}

static void on_received_data(WebKitDownload *download, guint64 length, gpointer data) {
    download_slot *slot = data;

    (void)length;
    if (slot->download == download && !vex_download_status_is_final(slot->state.status)) {
        update_progress(slot);
    }
}

static void on_failed(WebKitDownload *download, GError *error, gpointer data) {
    download_slot *slot = data;
    const gboolean cancelled =
        slot->cancel_requested ||
        g_error_matches(error, webkit_download_error_quark(),
                        VEX_WEBKIT_DOWNLOAD_ERROR_CANCELLED_BY_USER);

    if (slot->download != download) {
        return;
    }
    if (cancelled) {
        settle(slot, VEX_DOWNLOAD_CANCELLED, "");
    } else {
        settle(slot, VEX_DOWNLOAD_FAILED, error != NULL ? error->message : "Download failed");
    }
}

/* A server that closes the connection early can look like a normal end of
 * transfer. When it announced a length for the bytes we store (no content
 * encoding that could change their count) and fewer arrived, the file is
 * incomplete. */
static gboolean download_is_truncated(const download_slot *slot) {
    WebKitURIResponse *response = webkit_download_get_response(slot->download);
    gpointer headers = response != NULL ? webkit_uri_response_get_http_headers(response) : NULL;
    guint64 announced = response != NULL ? webkit_uri_response_get_content_length(response) : 0u;
    const char *encoding = headers != NULL ? soup_message_headers_get_one(headers, "Content-Encoding") : NULL;

    if (announced == 0u || announced == G_MAXUINT64) {
        return FALSE;
    }
    if (encoding != NULL && g_ascii_strcasecmp(encoding, "identity") != 0) {
        return FALSE;
    }
    return slot->state.received_bytes < announced;
}

/* WebKit emits "finished" last, after success or after "failed", once the
 * NetworkProcess has closed the file. That makes it the safe point to rename
 * or delete the data and to let go of the WebKitDownload. */
static void on_finished(WebKitDownload *download, gpointer data) {
    download_slot *slot = data;
    char error[VEX_DOWNLOAD_ERROR_SIZE];

    if (slot->download != download) {
        return;
    }
    if (!vex_download_status_is_final(slot->state.status)) {
        update_progress(slot);
        if (download_is_truncated(slot)) {
            g_snprintf(error, sizeof(error), "Connection closed after %llu of %llu bytes",
                       (unsigned long long)slot->state.received_bytes,
                       (unsigned long long)slot->state.total_bytes);
            settle(slot, VEX_DOWNLOAD_FAILED, error);
        } else if (publish_download(slot, error, sizeof(error))) {
            settle(slot, VEX_DOWNLOAD_FINISHED, "");
        } else {
            settle(slot, VEX_DOWNLOAD_FAILED, error);
        }
    }
    if (slot->state.status != VEX_DOWNLOAD_FINISHED) {
        remove_partial_file(slot);
    }
    release_download(slot);
}

static void on_download_started(WebKitNetworkSession *session, WebKitDownload *download,
                                gpointer data) {
    download_slot *slot = allocate_slot();
    WebKitWebView *view = webkit_download_get_web_view(download);
    const char *uri = webkit_uri_request_get_uri(webkit_download_get_request(download));

    (void)session;
    (void)data;
    if (slot == NULL) {
        fprintf(stderr, "browser download: too many downloads, refusing %s\n",
                uri != NULL ? uri : "(unknown)");
        webkit_download_cancel(download);
        return;
    }

    memset(slot, 0, sizeof(*slot));
    slot->in_use = TRUE;
    slot->download = g_object_ref(download);
    slot->state.id = next_download_id++;
    slot->state.tab_id = (page_lookup != NULL && view != NULL) ? page_lookup(view) : 0u;
    slot->state.status = VEX_DOWNLOAD_RUNNING;
    vex_download_name_from_uri(uri, slot->state.name, sizeof(slot->state.name));

    g_signal_connect(download, "decide-destination", G_CALLBACK(on_decide_destination), slot);
    g_signal_connect(download, "received-data", G_CALLBACK(on_received_data), slot);
    g_signal_connect(download, "failed", G_CALLBACK(on_failed), slot);
    g_signal_connect(download, "finished", G_CALLBACK(on_finished), slot);
    mark_report_pending(slot);
    fprintf(stderr, "browser download: id=%u started tab=%u uri=%s\n",
            slot->state.id, slot->state.tab_id, uri != NULL ? uri : "(unknown)");
}

/* ── public interface ────────────────────────────────────────────────────── */

static gboolean response_is_attachment(WebKitURIResponse *response) {
    gpointer headers = response != NULL ? webkit_uri_response_get_http_headers(response) : NULL;
    const char *disposition = headers != NULL
        ? soup_message_headers_get_one(headers, "Content-Disposition") : NULL;

    return disposition != NULL && g_ascii_strncasecmp(disposition, "attachment", 10) == 0;
}

gboolean vex_downloads_handle_response(gpointer decision) {
    WebKitURIResponse *response = webkit_response_policy_decision_get_response(decision);

    if (webkit_response_policy_decision_is_mime_type_supported(decision) &&
        !response_is_attachment(response)) {
        return FALSE;   /* displayable content: keep WebKit's own policy */
    }
    webkit_policy_decision_download(decision);
    return TRUE;
}

static void remove_stale_partial_files(void) {
    DIR *directory = opendir(VEX_DOWNLOAD_DIRECTORY);
    struct dirent *entry;
    char path[VEX_DOWNLOAD_PATH_SIZE];

    if (directory == NULL) {
        return;
    }
    while ((entry = readdir(directory)) != NULL) {
        size_t length = strlen(entry->d_name);

        if (vex_download_transient_offset(entry->d_name, length) == length) {
            continue;
        }
        if (g_snprintf(path, sizeof(path), "%s/%s", VEX_DOWNLOAD_DIRECTORY, entry->d_name) >=
            (gint)sizeof(path)) {
            continue;
        }
        if (unlink(path) == 0) {
            fprintf(stderr, "browser download: removed stale partial file %s\n", path);
        }
    }
    closedir(directory);
}

void vex_downloads_init(WebKitNetworkSession *session, vex_downloads_page_lookup lookup) {
    page_lookup = lookup;
    remove_stale_partial_files();
    g_signal_connect(session, "download-started", G_CALLBACK(on_download_started), NULL);
}

int vex_downloads_flush(vex_download_report_fn report, void *context) {
    gint64 now = g_get_monotonic_time();
    unsigned i;

    for (i = 0u; i < VEX_BROWSER_MAX_DOWNLOADS; ++i) {
        download_slot *slot = &slots[i];
        const gboolean final = vex_download_status_is_final(slot->state.status);

        if (!slot->in_use) {
            continue;
        }
        if (slot->report_pending) {
            if (!final && now - slot->last_report_us < PROGRESS_REPORT_INTERVAL_US) {
                continue;
            }
            switch (report(&slot->state, context)) {
            case VEX_DOWNLOAD_REPORT_DELIVERED:
                slot->report_pending = FALSE;
                slot->last_report_us = now;
                break;
            case VEX_DOWNLOAD_REPORT_WOULD_BLOCK:
                return 0;
            case VEX_DOWNLOAD_REPORT_FAILED:
                return -1;
            }
        }
        /* A final state has been delivered and WebKit has let go of the
         * download: the slot is free for the next one. */
        if (final && !slot->report_pending && slot->download == NULL) {
            memset(slot, 0, sizeof(*slot));
        }
    }
    return 0;
}

void vex_downloads_cancel(uint32_t id) {
    download_slot *slot = find_slot_by_id(id);

    if (slot == NULL || slot->download == NULL || vex_download_status_is_final(slot->state.status)) {
        return;
    }
    fprintf(stderr, "browser download: id=%u cancel requested\n", id);
    slot->cancel_requested = TRUE;
    webkit_download_cancel(slot->download);
}

static unsigned count_unreleased(void) {
    unsigned count = 0u;
    unsigned i;

    for (i = 0u; i < VEX_BROWSER_MAX_DOWNLOADS; ++i) {
        if (slots[i].in_use && slots[i].download != NULL) {
            ++count;
        }
    }
    return count;
}

void vex_downloads_shutdown(void) {
    gint64 deadline = g_get_monotonic_time() + SHUTDOWN_WAIT_US;
    unsigned i;

    for (i = 0u; i < VEX_BROWSER_MAX_DOWNLOADS; ++i) {
        if (slots[i].in_use && slots[i].download != NULL) {
            vex_downloads_cancel(slots[i].state.id);
        }
    }
    /* WebKit acknowledges asynchronously through the NetworkProcess. */
    while (count_unreleased() != 0u && g_get_monotonic_time() < deadline) {
        if (!g_main_context_iteration(NULL, FALSE)) {
            g_usleep(5000);
        }
    }
    /* Anything still pending did not answer in time: forget it and delete the
     * partial file; the NetworkProcess is torn down with the context. */
    for (i = 0u; i < VEX_BROWSER_MAX_DOWNLOADS; ++i) {
        download_slot *slot = &slots[i];

        if (!slot->in_use || slot->download == NULL) {
            continue;
        }
        fprintf(stderr, "browser download: id=%u did not acknowledge cancellation\n",
                slot->state.id);
        settle(slot, VEX_DOWNLOAD_CANCELLED, "");
        remove_partial_file(slot);
        release_download(slot);
    }
}
