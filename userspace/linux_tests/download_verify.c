/*
 * Guest-side verifier for the browser download tests.
 *
 * Reads every file in the Downloads directory through the ordinary Linux file
 * API and checks it against the content that scripts/download-test-server.py
 * generates from the file name, so no state has to be shared with the host:
 *
 *   small.txt              the fixed 1 KiB text
 *   <seed>-<size>.<ext>    `size` bytes of the test pattern for `seed`
 *
 * A collision suffix " (n)" before the extension is ignored, so
 * "3-1048576 (1).bin" is checked as "3-1048576.bin". Leftover in-progress
 * files (".vexpart") are reported as failures.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef DOWNLOAD_DIRECTORY
#define DOWNLOAD_DIRECTORY "/home/root/Downloads"
#endif
#define SMALL_TEXT_SIZE 1024u
#define READ_CHUNK 65536u

static unsigned files_seen;
static unsigned files_ok;
static unsigned files_failed;
static unsigned files_partial;
static unsigned files_unchecked;

static uint8_t pattern_byte(uint64_t index, uint32_t seed) {
    return (uint8_t)((index * seed + (index >> 11)) & 0xffu);
}

static uint8_t small_text_byte(uint64_t index) {
    static const char line[] = "Vex-OS download test file\n";
    return (uint8_t)line[index % (sizeof(line) - 1u)];
}

/* "name (3).ext" -> "name.ext"; other names are copied unchanged. */
static void strip_collision_suffix(const char *name, char *out, size_t capacity) {
    const char *dot = strrchr(name, '.');
    size_t stem = dot != NULL ? (size_t)(dot - name) : strlen(name);
    size_t cut = stem;

    if (stem >= 4u && name[stem - 1u] == ')') {
        size_t i = stem - 1u;
        while (i > 0u && name[i - 1u] >= '0' && name[i - 1u] <= '9') {
            --i;
        }
        if (i > 0u && i < stem - 1u && name[i - 1u] == '(' && i >= 2u && name[i - 2u] == ' ') {
            cut = i - 2u;
        }
    }
    snprintf(out, capacity, "%.*s%s", (int)cut, name, dot != NULL ? dot : "");
}

static int verify_content(const char *path, uint64_t expected_size, int small, uint32_t seed,
                          char *reason, size_t reason_size) {
    uint8_t buffer[READ_CHUNK];
    uint64_t offset = 0u;
    struct stat info;
    int fd = open(path, O_RDONLY);

    if (fd < 0) {
        snprintf(reason, reason_size, "open: %s", strerror(errno));
        return -1;
    }
    if (fstat(fd, &info) != 0 || (uint64_t)info.st_size != expected_size) {
        snprintf(reason, reason_size, "size %lld, expected %llu",
                 (long long)info.st_size, (unsigned long long)expected_size);
        close(fd);
        return -1;
    }
    for (;;) {
        ssize_t count = read(fd, buffer, sizeof(buffer));
        ssize_t i;

        if (count < 0) {
            snprintf(reason, reason_size, "read: %s", strerror(errno));
            close(fd);
            return -1;
        }
        if (count == 0) {
            break;
        }
        for (i = 0; i < count; ++i) {
            uint8_t expected = small ? small_text_byte(offset + (uint64_t)i)
                                     : pattern_byte(offset + (uint64_t)i, seed);
            if (buffer[i] != expected) {
                snprintf(reason, reason_size, "byte %llu is %02x, expected %02x",
                         (unsigned long long)(offset + (uint64_t)i), buffer[i], expected);
                close(fd);
                return -1;
            }
        }
        offset += (uint64_t)count;
    }
    close(fd);
    if (offset != expected_size) {
        snprintf(reason, reason_size, "read %llu bytes, expected %llu",
                 (unsigned long long)offset, (unsigned long long)expected_size);
        return -1;
    }
    return 0;
}

static void check_file(const char *name) {
    char path[512];
    char base[256];
    char reason[160] = "";
    unsigned long long size = 0u;
    unsigned seed = 0u;
    struct stat info;

    ++files_seen;
    snprintf(path, sizeof(path), "%s/%s", DOWNLOAD_DIRECTORY, name);
    if (strstr(name, ".vexpart") != NULL) {
        ++files_partial;
        printf("download_verify: PARTIAL name=%s\n", name);
        return;
    }
    if (stat(path, &info) != 0 || !S_ISREG(info.st_mode)) {
        printf("download_verify: SKIP name=%s (not a regular file)\n", name);
        return;
    }

    strip_collision_suffix(name, base, sizeof(base));
    if (strcmp(base, "small.txt") == 0) {
        if (verify_content(path, SMALL_TEXT_SIZE, 1, 0u, reason, sizeof(reason)) == 0) {
            ++files_ok;
            printf("download_verify: OK name=%s size=%u\n", name, SMALL_TEXT_SIZE);
        } else {
            ++files_failed;
            printf("download_verify: FAIL name=%s %s\n", name, reason);
        }
        return;
    }
    if (sscanf(base, "%u-%llu.", &seed, &size) == 2) {
        if (verify_content(path, size, 0, seed, reason, sizeof(reason)) == 0) {
            ++files_ok;
            printf("download_verify: OK name=%s size=%llu\n", name, size);
        } else {
            ++files_failed;
            printf("download_verify: FAIL name=%s %s\n", name, reason);
        }
        return;
    }
    ++files_unchecked;
    printf("download_verify: UNCHECKED name=%s size=%lld\n", name, (long long)info.st_size);
}

int main(void) {
    DIR *directory = opendir(DOWNLOAD_DIRECTORY);
    struct dirent *entry;

    if (directory == NULL) {
        printf("download_verify: cannot open %s: %s\n", DOWNLOAD_DIRECTORY, strerror(errno));
        printf("download_verify: summary files=0 ok=0 failed=0 partial=0 unchecked=0 directory=missing\n");
        return 2;
    }
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
            strcmp(entry->d_name, ".ladybird_dir") == 0) {
            continue;
        }
        check_file(entry->d_name);
    }
    closedir(directory);
    printf("download_verify: summary files=%u ok=%u failed=%u partial=%u unchecked=%u\n",
           files_seen, files_ok, files_failed, files_partial, files_unchecked);
    return (files_failed == 0u && files_partial == 0u) ? 0 : 1;
}
