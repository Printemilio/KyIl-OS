/* Host unit test of GFM's file operations (userspace/apps/gfm/gfm_fs.c). */

#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../apps/gfm/gfm_fs.h"

static char root[256];

static void path_in(char *out, const char *relative) {
    snprintf(out, 512, "%s/%s", root, relative);
}

static void write_file(const char *relative, const void *data, size_t size) {
    char path[512];
    FILE *file;

    path_in(path, relative);
    file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite(data, 1, size, file) == size);
    fclose(file);
}

static int exists(const char *relative) {
    char path[512];
    struct stat info;

    path_in(path, relative);
    return stat(path, &info) == 0;
}

static void test_paths(void) {
    char out[GFM_PATH_MAX];

    assert(gfm_path_resolve("/home", "/a//b/./c/../d", out, sizeof(out)) == GFM_OK);
    assert(strcmp(out, "/a/b/d") == 0);
    assert(gfm_path_resolve("/home/root", "Downloads", out, sizeof(out)) == GFM_OK);
    assert(strcmp(out, "/home/root/Downloads") == 0);
    assert(gfm_path_resolve("/home/root", "../..", out, sizeof(out)) == GFM_OK);
    assert(strcmp(out, "/") == 0);
    assert(gfm_path_resolve("/", "/../../x", out, sizeof(out)) == GFM_OK);
    assert(strcmp(out, "/x") == 0);
    assert(gfm_path_resolve("/", "", out, sizeof(out)) == GFM_ERR_INVALID_PATH);
    assert(gfm_path_resolve("/", "/a\nb", out, sizeof(out)) == GFM_ERR_INVALID_PATH);
    {
        char long_path[300];
        memset(long_path, 'a', sizeof(long_path));
        long_path[0] = '/';
        long_path[299] = '\0';
        assert(gfm_path_resolve("/", long_path, out, sizeof(out)) == GFM_ERR_TOO_LONG);
    }

    assert(gfm_path_join("/", "x", out, sizeof(out)) == GFM_OK && strcmp(out, "/x") == 0);
    assert(gfm_path_join("/a/", "x", out, sizeof(out)) == GFM_OK && strcmp(out, "/a/x") == 0);
    gfm_path_parent("/a/b/c", out, sizeof(out));
    assert(strcmp(out, "/a/b") == 0);
    gfm_path_parent("/a", out, sizeof(out));
    assert(strcmp(out, "/") == 0);
    gfm_path_parent("/", out, sizeof(out));
    assert(strcmp(out, "/") == 0);
    assert(strcmp(gfm_path_basename("/a/b/c.txt"), "c.txt") == 0);
    assert(strcmp(gfm_path_basename("/a/b/"), "b/") == 0 || strcmp(gfm_path_basename("/a/b/"), "b") == 0);

    assert(gfm_path_is_within("/a/b", "/a/b"));
    assert(gfm_path_is_within("/a/b", "/a/b/c"));
    assert(!gfm_path_is_within("/a/b", "/a/bc"));
    assert(gfm_path_is_within("/", "/anything"));
}

static void test_names(void) {
    char name[GFM_SYS_NAME_MAX];

    snprintf(name, sizeof(name), "  report.txt  ");
    assert(gfm_name_prepare(name) == GFM_OK && strcmp(name, "report.txt") == 0);
    snprintf(name, sizeof(name), "a/b");
    assert(gfm_name_prepare(name) == GFM_ERR_INVALID_NAME);
    snprintf(name, sizeof(name), "..");
    assert(gfm_name_prepare(name) == GFM_ERR_INVALID_NAME);
    snprintf(name, sizeof(name), "   ");
    assert(gfm_name_prepare(name) == GFM_ERR_INVALID_NAME);
    snprintf(name, sizeof(name), "tab\there");
    assert(gfm_name_prepare(name) == GFM_ERR_INVALID_NAME);
    memset(name, 'x', 65);
    name[65] = '\0';
    assert(gfm_name_prepare(name) == GFM_ERR_INVALID_NAME);
    memset(name, 'x', 64);
    name[64] = '\0';
    assert(gfm_name_prepare(name) == GFM_OK);

    assert(gfm_is_transient_download("a.bin.vexpart"));
    assert(gfm_is_transient_download("a.bin.vexpart.wkdownload"));
    assert(!gfm_is_transient_download("a.bin"));
}

static void test_format(void) {
    char text[32];

    gfm_format_size(0u, text, sizeof(text));
    assert(strcmp(text, "0 B") == 0);
    gfm_format_size(1023u, text, sizeof(text));
    assert(strcmp(text, "1023 B") == 0);
    gfm_format_size(1536u, text, sizeof(text));
    assert(strcmp(text, "1.5 KB") == 0);
    gfm_format_size(8388608u, text, sizeof(text));
    assert(strcmp(text, "8.0 MB") == 0);
    gfm_format_mode(0755u, text, sizeof(text));
    assert(strcmp(text, "rwxr-xr-x") == 0);
    gfm_format_mode(0644u, text, sizeof(text));
    assert(strcmp(text, "rw-r--r--") == 0);
}

static void test_listing(void) {
    static gfm_listing_t listing;
    char path[512];

    path_in(path, "list");
    assert(mkdir(path, 0755) == 0);
    write_file("list/banana.txt", "bb", 2);
    write_file("list/Apple.txt", "a", 1);
    write_file("list/zebra", "", 0);
    path_in(path, "list/Docs");
    assert(mkdir(path, 0755) == 0);
    path_in(path, "list/archive");
    assert(mkdir(path, 0755) == 0);

    path_in(path, "list");
    assert(gfm_list(path, &listing) == GFM_OK);
    assert(listing.count == 5u && !listing.truncated);
    /* folders first, then files, each ignoring case */
    assert(strcmp(listing.entries[0].name, "archive") == 0);
    assert(strcmp(listing.entries[1].name, "Docs") == 0);
    assert(strcmp(listing.entries[2].name, "Apple.txt") == 0);
    assert(strcmp(listing.entries[3].name, "banana.txt") == 0);
    assert(strcmp(listing.entries[4].name, "zebra") == 0);
    assert(listing.entries[3].size == 2u && listing.entries[0].type == GFM_TYPE_DIRECTORY);
    {
        const uint32_t before = listing.fingerprint;
        write_file("list/new.txt", "x", 1);
        assert(gfm_list(path, &listing) == GFM_OK && listing.fingerprint != before);
    }

    path_in(path, "list/banana.txt");
    assert(gfm_list(path, &listing) == GFM_ERR_NOT_DIRECTORY);
    path_in(path, "list/missing");
    assert(gfm_list(path, &listing) == GFM_ERR_NOT_FOUND);

    assert(gfm_name_exists(root, "LIST"));
    assert(!gfm_name_exists(root, "nothing"));
}

static void test_make_and_rename(void) {
    char created[GFM_PATH_MAX];
    char renamed[GFM_PATH_MAX];
    char path[512];

    assert(gfm_make_directory(root, "work", created, sizeof(created)) == GFM_OK);
    assert(exists("work"));
    assert(gfm_make_directory(root, "work", created, sizeof(created)) == GFM_ERR_EXISTS);
    assert(gfm_make_directory(root, "WORK", created, sizeof(created)) == GFM_ERR_EXISTS);
    assert(gfm_make_directory(root, "a/b", created, sizeof(created)) == GFM_ERR_INVALID_NAME);
    assert(gfm_make_directory(root, "", created, sizeof(created)) == GFM_ERR_INVALID_NAME);

    write_file("work/one.txt", "hello", 5);
    write_file("work/two.txt", "world", 5);
    path_in(path, "work/one.txt");
    assert(gfm_rename(path, "uno.txt", renamed, sizeof(renamed)) == GFM_OK);
    assert(!exists("work/one.txt") && exists("work/uno.txt"));
    assert(strcmp(gfm_path_basename(renamed), "uno.txt") == 0);
    path_in(path, "work/uno.txt");
    assert(gfm_rename(path, "two.txt", renamed, sizeof(renamed)) == GFM_ERR_EXISTS);
    assert(gfm_rename(path, "TWO.TXT", renamed, sizeof(renamed)) == GFM_ERR_EXISTS);
    assert(gfm_rename(path, "uno.txt", renamed, sizeof(renamed)) == GFM_OK);   /* same name */
    assert(gfm_rename(path, "bad/name", renamed, sizeof(renamed)) == GFM_ERR_INVALID_NAME);
    assert(exists("work/uno.txt") && exists("work/two.txt"));

    /* a directory is renamed by copying its tree */
    path_in(path, "work");
    assert(gfm_rename(path, "job", renamed, sizeof(renamed)) == GFM_OK);
    assert(!exists("work") && exists("job/uno.txt") && exists("job/two.txt"));
    path_in(path, "nothing");
    assert(gfm_rename(path, "x", renamed, sizeof(renamed)) == GFM_ERR_NOT_FOUND);
}

static void test_copy_move_delete(void) {
    char created[GFM_PATH_MAX];
    char source[512];
    char destination[512];
    char big[70000];
    size_t i;
    size_t count = 0u;
    FILE *file;

    for (i = 0u; i < sizeof(big); ++i) {
        big[i] = (char)(i * 7u + (i >> 8));
    }
    assert(mkdir((snprintf(destination, sizeof(destination), "%s/store", root), destination), 0755) == 0);
    write_file("job/big.bin", big, sizeof(big));

    /* copy a file: exact content, free name each time */
    path_in(source, "job/big.bin");
    assert(gfm_copy(source, destination, created, sizeof(created)) == GFM_OK);
    assert(strcmp(gfm_path_basename(created), "big.bin") == 0);
    {
        static char readback[70000];
        file = fopen(created, "rb");
        assert(file != NULL && fread(readback, 1, sizeof(readback), file) == sizeof(readback));
        fclose(file);
        assert(memcmp(readback, big, sizeof(big)) == 0);
    }
    assert(gfm_copy(source, destination, created, sizeof(created)) == GFM_OK);
    assert(strcmp(gfm_path_basename(created), "big copy.bin") == 0);
    assert(gfm_copy(source, destination, created, sizeof(created)) == GFM_OK);
    assert(strcmp(gfm_path_basename(created), "big copy 2.bin") == 0);

    /* copy next to itself */
    path_in(destination, "job");
    assert(gfm_copy(source, destination, created, sizeof(created)) == GFM_OK);
    assert(strcmp(gfm_path_basename(created), "big copy.bin") == 0);

    /* copy a folder tree */
    path_in(source, "job");
    path_in(destination, "store");
    assert(gfm_copy(source, destination, created, sizeof(created)) == GFM_OK);
    assert(exists("store/job/uno.txt") && exists("store/job/big.bin"));
    assert(gfm_count_descendants(created, &count) == GFM_OK && count == 4u);

    /* a folder cannot go into itself or below itself */
    path_in(destination, "job");
    assert(gfm_copy(source, destination, created, sizeof(created)) == GFM_ERR_INTO_ITSELF);
    {
        char below[512];
        assert(mkdir((snprintf(below, sizeof(below), "%s/job/sub", root), below), 0755) == 0);
        assert(gfm_move(source, below, created, sizeof(created)) == GFM_ERR_INTO_ITSELF);
        assert(gfm_copy(source, below, created, sizeof(created)) == GFM_ERR_INTO_ITSELF);
    }

    /* move a file; same folder and name conflicts are refused */
    path_in(source, "job/uno.txt");
    path_in(destination, "store");
    assert(gfm_move(source, destination, created, sizeof(created)) == GFM_OK);
    assert(!exists("job/uno.txt") && exists("store/uno.txt"));
    path_in(source, "store/uno.txt");
    assert(gfm_move(source, destination, created, sizeof(created)) == GFM_ERR_SAME_LOCATION);
    write_file("job/uno.txt", "again", 5);
    path_in(source, "job/uno.txt");
    assert(gfm_move(source, destination, created, sizeof(created)) == GFM_ERR_EXISTS);
    assert(exists("job/uno.txt"));

    /* move a folder: tree copied, original removed */
    path_in(source, "job");
    assert(mkdir((snprintf(destination, sizeof(destination), "%s/target", root), destination), 0755) == 0);
    assert(gfm_move(source, destination, created, sizeof(created)) == GFM_OK);
    assert(!exists("job") && exists("target/job/two.txt"));

    /* errors */
    path_in(source, "ghost");
    assert(gfm_copy(source, destination, created, sizeof(created)) == GFM_ERR_NOT_FOUND);
    path_in(source, "target/job/two.txt");
    path_in(destination, "target/job/two.txt");
    assert(gfm_copy(source, destination, created, sizeof(created)) == GFM_ERR_NOT_DIRECTORY);

    /* delete a file and a whole tree; the root is protected */
    assert(gfm_delete(source) == GFM_OK && !exists("target/job/two.txt"));
    path_in(source, "target");
    assert(gfm_delete(source) == GFM_OK && !exists("target"));
    assert(gfm_delete(source) == GFM_ERR_NOT_FOUND);
    assert(gfm_delete("/") == GFM_ERR_INVALID_PATH);
    assert(gfm_delete("relative") == GFM_ERR_INVALID_PATH);
}

static void test_executable_and_binary(void) {
    char path[512];
    gfm_entry_t entry;
    unsigned char binary[8] = { 0x7f, 'E', 'L', 'F', 0, 1, 2, 3 };

    write_file("tool", binary, sizeof(binary));
    write_file("note.txt", "plain text\n", 11);
    path_in(path, "tool");
    assert(gfm_looks_binary(path));
    assert(gfm_stat(path, &entry) == GFM_OK && (entry.mode & 0111u) == 0u);
    assert(gfm_set_executable(path, 1) == GFM_OK);
    assert(gfm_stat(path, &entry) == GFM_OK && (entry.mode & 0111u) == 0111u);
    assert(gfm_set_executable(path, 0) == GFM_OK);
    assert(gfm_stat(path, &entry) == GFM_OK && (entry.mode & 0111u) == 0u);
    path_in(path, "note.txt");
    assert(!gfm_looks_binary(path));
    path_in(path, "missing");
    assert(gfm_set_executable(path, 1) == GFM_ERR_NOT_FOUND);
}

int main(void) {
    char template[] = "/tmp/gfm-test-XXXXXX";

    assert(mkdtemp(template) != NULL);
    snprintf(root, sizeof(root), "%s", template);

    test_paths();
    test_names();
    test_format();
    test_listing();
    test_make_and_rename();
    test_copy_move_delete();
    test_executable_and_binary();

    {
        char command[300];
        snprintf(command, sizeof(command), "rm -rf '%s'", root);
        assert(system(command) == 0);
    }
    puts("gfm_fs: PASS");
    return 0;
}
