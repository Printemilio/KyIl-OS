/* iso9660.c — lightweight ISO 9660 reader for the boot CD-ROM.
 *
 * Provides on-demand access to files on the boot ISO so that shared libraries
 * do NOT need to be listed as Limine modules (which would exhaust physical
 * memory before the kernel starts for large sets like WebKit's ~300 MB of .so
 * files).
 *
 * Call order expected from main.c:
 *   disk_init()          — sets up ATAPI / ATA PIO CD-ROM driver
 *   fs_init()
 *   import_limine_modules()
 *   iso9660_init()       — reads PVD, locates root directory
 *
 * fs_read() / fs_exists() call back into iso9660_read() / iso9660_exists()
 * when a path is not found in the boot-module table or on the writable disk.
 */

#include <unos/iso9660.h>
#include <unos/disk.h>
#include <unos/fs.h>
#include <unos/pmm.h>
#include <unos/boot_requests.h>
#include <unos/debug_log.h>

#include <stddef.h>
#include <stdint.h>

/* ── constants ─────────────────────────────────────────────────────────────── */

#define ISO_SECTOR_SIZE  2048u
#define ISO_PVD_SECTOR   16u    /* Primary Volume Descriptor is always at LBA 16 */

/* ── module state ──────────────────────────────────────────────────────────── */

/* Scratch sector used during directory traversal.  The kernel is
 * single-threaded (cooperative scheduler) so one static buffer is enough. */
static uint8_t  g_sector_buf[ISO_SECTOR_SIZE];
static uint32_t g_root_lba;
static uint32_t g_root_size;
static int      g_iso_ready = 0;

/* ── helpers ────────────────────────────────────────────────────────────────── */

static void *iso_phys_to_virt(uint64_t phys)
{
    return (void *)(hhdm_request.response->offset + phys);
}

/* Read a 4-byte little-endian integer from an unaligned byte pointer. */
static uint32_t iso_le32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8u)
         | ((uint32_t)p[2] << 16u)
         | ((uint32_t)p[3] << 24u);
}

static int iso_toupper(int c)
{
    return (c >= 'a' && c <= 'z') ? (c - 32) : c;
}

static size_t iso_strlen(const char *s)
{
    size_t n = 0;
    while (s[n] != '\0') ++n;
    return n;
}

/* Case-insensitive comparison of an ISO 9660 file/directory identifier against
 * a VFS path component (e.g. "libfoo.so").
 *
 * ISO file names are uppercase, e.g. "LIBFOO.SO;1".  Directory names have no
 * version suffix.  We strip the ";N" version field and any trailing "." before
 * comparing so that both upper- and lower-case VFS paths match.
 */
static int iso_name_match(const uint8_t *id, uint8_t id_len, const char *query)
{
    /* Strip ";N" version suffix. */
    uint8_t cmp_len = id_len;
    for (uint8_t i = 0u; i < id_len; ++i) {
        if (id[i] == (uint8_t)';') { cmp_len = i; break; }
    }
    /* Strip trailing dot (rock-ridge / quirk guard). */
    if (cmp_len > 0u && id[cmp_len - 1u] == (uint8_t)'.') {
        --cmp_len;
    }

    size_t qlen = iso_strlen(query);
    if ((size_t)cmp_len != qlen) return 0;

    for (uint8_t i = 0u; i < cmp_len; ++i) {
        if (iso_toupper((int)id[i]) != iso_toupper((uint8_t)query[i])) return 0;
    }
    return 1;
}

/* ── directory traversal ────────────────────────────────────────────────────── */

/*
 * ISO 9660 directory record layout:
 *   [0]      record length
 *   [1]      extended attribute record length (usually 0)
 *   [2..5]   extent LBA (LE u32)
 *   [6..9]   extent LBA (BE u32)
 *   [10..13] data length (LE u32)
 *   [14..19] recording date
 *   [25]     file flags (bit 1 set = directory)
 *   [32]     file identifier length
 *   [33+]    file identifier
 */

/* Search one directory level for 'name'.
 * Returns: 1 = file found, 2 = directory found, 0 = not found, -1 = I/O error.
 * On success *out_lba and *out_size are filled with the entry's extent. */
static int iso_find_in_dir(uint32_t dir_lba, uint32_t dir_bytes,
                           const char *name,
                           uint32_t *out_lba, uint32_t *out_size)
{
    uint32_t sectors = (dir_bytes + ISO_SECTOR_SIZE - 1u) / ISO_SECTOR_SIZE;

    for (uint32_t s = 0u; s < sectors; ++s) {
        if (disk_cdrom_read(dir_lba + s, 1u, g_sector_buf) != 0) return -1;

        uint32_t off = 0u;
        while (off + 33u <= ISO_SECTOR_SIZE) {
            uint8_t rec_len = g_sector_buf[off + 0u];
            if (rec_len == 0u) break;                     /* sector padding */
            if (off + rec_len > ISO_SECTOR_SIZE) break;   /* corrupt/truncated */

            uint8_t  name_len = g_sector_buf[off + 32u];
            uint8_t  flags    = g_sector_buf[off + 25u];
            uint32_t e_lba    = iso_le32(g_sector_buf + off + 2u);
            uint32_t e_size   = iso_le32(g_sector_buf + off + 10u);

            /* Skip the special "." (id=0x00) and ".." (id=0x01) entries. */
            if (name_len == 1u &&
                (g_sector_buf[off + 33u] == 0x00u ||
                 g_sector_buf[off + 33u] == 0x01u)) {
                off += rec_len;
                continue;
            }

            if (iso_name_match(g_sector_buf + off + 33u, name_len, name)) {
                *out_lba  = e_lba;
                *out_size = e_size;
                return (flags & 0x02u) ? 2 : 1;  /* 2 = dir, 1 = file */
            }

            off += rec_len;
        }
    }

    return 0;  /* not found in this directory */
}

/* Walk an absolute VFS path through the ISO file system.
 * Returns 0 on success (fills *out_lba, *out_size), -1 on failure. */
static int iso_find_path(const char *path, uint32_t *out_lba, uint32_t *out_size)
{
    if (!g_iso_ready || path == NULL || path[0] != '/') return -1;

    uint32_t cur_lba  = g_root_lba;
    uint32_t cur_size = g_root_size;
    const char *p = path + 1u;   /* skip leading '/' */

    if (*p == '\0') {
        /* Caller asked for the root directory itself. */
        *out_lba  = cur_lba;
        *out_size = cur_size;
        return 0;
    }

    while (*p != '\0') {
        /* Extract the next path component. */
        char comp[256];
        size_t ci = 0u;
        while (*p != '\0' && *p != '/' && ci < sizeof(comp) - 1u) {
            comp[ci++] = *p++;
        }
        comp[ci] = '\0';
        if (*p == '/') ++p;

        if (ci == 0u) continue;   /* double slash; skip */

        uint32_t e_lba = 0u, e_size = 0u;
        int r = iso_find_in_dir(cur_lba, cur_size, comp, &e_lba, &e_size);
        if (r <= 0) return -1;    /* not found or I/O error */

        cur_lba  = e_lba;
        cur_size = e_size;
    }

    *out_lba  = cur_lba;
    *out_size = cur_size;
    return 0;
}

/* ── Public API ─────────────────────────────────────────────────────────────── */

void iso9660_init(void)
{
    g_iso_ready = 0;

    if (!disk_cdrom_available()) {
        debug_log_puts("iso9660: no CD-ROM driver ready\n");
        return;
    }

    if (disk_cdrom_read(ISO_PVD_SECTOR, 1u, g_sector_buf) != 0) {
        debug_log_puts("iso9660: cannot read PVD (sector 16)\n");
        return;
    }

    /* The Primary Volume Descriptor starts with:
     *   [0]   descriptor type = 1
     *   [1..5] standard identifier = "CD001"
     */
    if (g_sector_buf[0] != 1u  ||
        g_sector_buf[1] != 'C' || g_sector_buf[2] != 'D' ||
        g_sector_buf[3] != '0' || g_sector_buf[4] != '0' || g_sector_buf[5] != '1') {
        debug_log_puts("iso9660: bad PVD signature\n");
        return;
    }

    /* Root Directory Record is embedded in the PVD at offset 156 (34 bytes).
     * Within that record:  +2 = extent LBA (LE u32),  +10 = data length (LE u32). */
    g_root_lba  = iso_le32(g_sector_buf + 156u + 2u);
    g_root_size = iso_le32(g_sector_buf + 156u + 10u);
    g_iso_ready = 1;
    debug_log_puts("iso9660: ready\n");
}

int iso9660_read(const char *path, void **data_out, size_t *size_out)
{
    uint32_t file_lba, file_size;

    if (!g_iso_ready || path == NULL || data_out == NULL || size_out == NULL) return -1;
    if (iso_find_path(path, &file_lba, &file_size) != 0) return -1;
    if (file_size == 0u) return -1;

    /* Determine how many 2048-byte sectors to read, then round up to full pages
     * so the allocation is at least as large as what we'll write into it. */
    uint32_t sectors   = (file_size + ISO_SECTOR_SIZE - 1u) / ISO_SECTOR_SIZE;
    uint64_t alloc_bytes = (uint64_t)sectors * ISO_SECTOR_SIZE;
    uint64_t pages     = (alloc_bytes + 4095ull) / 4096ull;

    void *phys = pmm_alloc_contiguous_pages(pages);
    if (phys == NULL) {
        debug_log_puts("iso9660: OOM for ");
        debug_log_puts(path);
        debug_log_puts("\n");
        return -1;
    }

    uint8_t *virt = (uint8_t *)iso_phys_to_virt((uint64_t)phys);

    /* disk_cdrom_read handles internal chunking for both AHCI and PIO paths. */
    if (disk_cdrom_read(file_lba, sectors, virt) != 0) {
        pmm_free_contiguous_pages(phys, pages);
        debug_log_puts("iso9660: read failed for ");
        debug_log_puts(path);
        debug_log_puts("\n");
        return -1;
    }

    /* Cache in the VFS boot-module table so subsequent fs_read() calls for this
     * path find the data without hitting the CD-ROM again.
     * fs_register_boot_module() copies the data into MRAC and then calls
     * pmm_reclaim_boot_module_range() on the source — that function only touches
     * BOOTLOADER_RECLAIMABLE pages, so our pmm_alloc'd pages are unaffected. */
    (void)fs_register_boot_module(path, virt, (size_t)file_size);

    *data_out = virt;
    *size_out = (size_t)file_size;
    return 0;
}

int iso9660_exists(const char *path)
{
    uint32_t lba, sz;
    if (!g_iso_ready) return 0;
    return iso_find_path(path, &lba, &sz) == 0 ? 1 : 0;
}
