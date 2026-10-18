#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unos/browser_state.h>
#include <vex_download_names.h>

static void expect_name(const char *input, const char *expected) {
    char name[VEX_DOWNLOAD_NAME_SIZE];
    size_t length = vex_download_sanitize_name(input, name, sizeof(name));

    if (strcmp(name, expected) != 0) {
        fprintf(stderr, "sanitize(\"%s\") = \"%s\", expected \"%s\"\n",
                input ? input : "(null)", name, expected);
        assert(0);
    }
    assert(length == strlen(name));
    assert(length > 0u && length <= VEX_DOWNLOAD_FILE_NAME_MAX);
}

static void expect_uri_name(const char *uri, const char *expected) {
    char name[VEX_DOWNLOAD_NAME_SIZE];

    vex_download_name_from_uri(uri, name, sizeof(name));
    if (strcmp(name, expected) != 0) {
        fprintf(stderr, "name_from_uri(\"%s\") = \"%s\", expected \"%s\"\n",
                uri ? uri : "(null)", name, expected);
        assert(0);
    }
}

static void expect_candidate(const char *name, unsigned index, const char *expected) {
    char path[VEX_DOWNLOAD_PATH_SIZE];

    assert(vex_download_candidate_path(VEX_DOWNLOAD_DIRECTORY, name, index,
                                       path, sizeof(path)) == 0);
    if (strcmp(path, expected) != 0) {
        fprintf(stderr, "candidate(\"%s\", %u) = \"%s\", expected \"%s\"\n",
                name, index, path, expected);
        assert(0);
    }
}

int main(void) {
    char name[VEX_DOWNLOAD_NAME_SIZE];
    char path[VEX_DOWNLOAD_PATH_SIZE];
    char long_name[400];
    size_t i;

    /* Ordinary names are untouched. */
    expect_name("report.pdf", "report.pdf");
    expect_name("hello world.txt", "hello world.txt");
    expect_name("d\xc3\xa9j\xc3\xa0.txt", "d\xc3\xa9j\xc3\xa0.txt");

    /* Only the last component survives, whatever the separator. */
    expect_name("../../etc/passwd", "passwd");
    expect_name("C:\\Windows\\system32\\evil.exe", "evil.exe");
    expect_name("/absolute/path/file.bin", "file.bin");
    expect_name("dir/", VEX_DOWNLOAD_FALLBACK_NAME);

    /* No hidden files, dot-only or blank names, no control characters. */
    expect_name(".bashrc", "bashrc");
    expect_name("..", VEX_DOWNLOAD_FALLBACK_NAME);
    expect_name(".", VEX_DOWNLOAD_FALLBACK_NAME);
    expect_name("   ", VEX_DOWNLOAD_FALLBACK_NAME);
    expect_name("", VEX_DOWNLOAD_FALLBACK_NAME);
    expect_name(NULL, VEX_DOWNLOAD_FALLBACK_NAME);
    expect_name("a\nb\tc.txt", "a_b_c.txt");
    expect_name("trailing. . ", "trailing");

    /* The in-progress marker is reserved. */
    expect_name("movie.mp4.vexpart", "movie.mp4_vexpart");
    expect_name("movie.mp4.vexpart.wkdownload", "movie.mp4_vexpart.wkdownload");
    assert(vex_download_transient_offset("a.vexpart", 9u) == 1u);
    assert(vex_download_transient_offset("a.vexpart.wkdownload", 20u) == 1u);
    assert(vex_download_transient_offset("a.wkdownload", 12u) == 12u);
    assert(vex_download_transient_offset("avexpart", 8u) == 8u);

    /* Long names keep their extension and never split UTF-8. */
    memset(long_name, 'a', sizeof(long_name));
    memcpy(long_name + 300, ".tar.gz", 8);
    vex_download_sanitize_name(long_name, name, sizeof(name));
    assert(strlen(name) == VEX_DOWNLOAD_FILE_NAME_MAX);
    assert(strcmp(name + strlen(name) - 7u, ".tar.gz") == 0);

    for (i = 0u; i < 100u; ++i) {
        long_name[2u * i] = (char)0xc3;
        long_name[2u * i + 1u] = (char)0xa9;
    }
    long_name[200] = '\0';
    vex_download_sanitize_name(long_name, name, sizeof(name));
    assert(strlen(name) <= VEX_DOWNLOAD_FILE_NAME_MAX);
    assert(strlen(name) % 2u == 0u); /* only whole two-byte characters */

    /* A tiny output buffer is refused rather than overrun. */
    {
        char tiny[4];
        assert(vex_download_sanitize_name("abc.txt", tiny, sizeof(tiny)) == 0u);
    }

    /* URI fallbacks. */
    expect_uri_name("https://example.com/files/archive.tar.gz?token=1#top", "archive.tar.gz");
    expect_uri_name("https://example.com/a%20b.txt", "a b.txt");
    expect_uri_name("https://example.com/a%2Fc.txt", "c.txt"); /* decoded '/' is a separator */
    expect_uri_name("https://example.com/", VEX_DOWNLOAD_FALLBACK_NAME);
    expect_uri_name("https://example.com", VEX_DOWNLOAD_FALLBACK_NAME);
    expect_uri_name("https://example.com/path/", VEX_DOWNLOAD_FALLBACK_NAME);
    expect_uri_name("https://example.com/x%zz.bin", "x%zz.bin");
    expect_uri_name("https://example.com/x%4", "x%4");
    expect_uri_name(NULL, VEX_DOWNLOAD_FALLBACK_NAME);

    /* Candidate paths and collision numbering. */
    expect_candidate("notes.txt", 0, "/home/root/Downloads/notes.txt");
    expect_candidate("notes.txt", 1, "/home/root/Downloads/notes (1).txt");
    expect_candidate("notes.txt", 12, "/home/root/Downloads/notes (12).txt");
    expect_candidate("archive.tar.gz", 2, "/home/root/Downloads/archive (2).tar.gz");
    expect_candidate("noextension", 3, "/home/root/Downloads/noextension (3)");
    expect_candidate("bashrc", 1, "/home/root/Downloads/bashrc (1)");
    expect_candidate("trailingdot.", 1, "/home/root/Downloads/trailingdot. (1)");
    expect_candidate("a.verylongextensionname", 1, "/home/root/Downloads/a.verylongextensionname (1)");
    assert(vex_download_candidate_path(VEX_DOWNLOAD_DIRECTORY, "x",
        VEX_DOWNLOAD_MAX_COLLISION_INDEX + 1u, path, sizeof(path)) == -1);
    assert(vex_download_candidate_path(VEX_DOWNLOAD_DIRECTORY, "x", 0, path, 8) == -1);
    assert(path[0] == '\0');

    /* Worst case still fits the 128-byte VexFS path contract with the marker. */
    memset(long_name, 'b', 200);
    memcpy(long_name + 200, ".extension-16xx", 16);
    vex_download_sanitize_name(long_name, name, sizeof(name));
    assert(vex_download_candidate_path(VEX_DOWNLOAD_DIRECTORY, name,
        VEX_DOWNLOAD_MAX_COLLISION_INDEX, path, sizeof(path)) == 0);
    {
        char part[VEX_DOWNLOAD_PATH_SIZE];
        assert(vex_download_part_path(path, part, sizeof(part)) == 0);
        assert(strlen(part) < VEX_DOWNLOAD_PATH_SIZE);
        assert(strcmp(part + strlen(part) - 8u, VEX_DOWNLOAD_PART_SUFFIX) == 0);
        assert(vex_download_part_path(path, part, strlen(path) + 4u) == -1);
    }

    puts("download names: PASS");
    return 0;
}
