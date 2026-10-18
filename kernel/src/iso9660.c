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
#include <unos/kernel_guard.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ── constants ─────────────────────────────────────────────────────────────── */

#define ISO_SECTOR_SIZE  2048u
#define ISO_PVD_SECTOR   16u    /* Primary Volume Descriptor is always at LBA 16 */

/* ── module state ──────────────────────────────────────────────────────────── */

/* Boot-only PVD scratch. Runtime traversal uses a private sector below. */
static uint8_t  g_sector_buf[ISO_SECTOR_SIZE];
static uint32_t g_root_lba;
static uint32_t g_root_size;
static int      g_iso_ready = 0;

/* Chunk buffer for iso9660_pread — 64 sectors = 128 KB.  Statically allocated
 * so it is physically contiguous (kernel BSS) and safe for DMA/ATAPI reads. */
#define ISO_PREAD_CHUNK_SECTORS 64u
static uint8_t g_pread_chunk[ISO_PREAD_CHUNK_SECTORS * ISO_SECTOR_SIZE];

/* The mounted boot medium is read-only. Cache directory sectors, including
 * those used by repeated unsuccessful ld-linux path searches. No file data
 * is eagerly committed and failed reads are never cached. */
#define ISO_DIRECTORY_CACHE_SLOTS 64u
static struct {
    uint32_t lba;
    uint8_t valid;
    uint8_t bytes[ISO_SECTOR_SIZE];
} directory_cache[ISO_DIRECTORY_CACHE_SLOTS];
static uint32_t directory_cache_next;
static kernel_gate_t directory_cache_gate=KERNEL_GATE_INIT;
static kernel_gate_t pread_chunk_gate=KERNEL_GATE_INIT;

static int read_directory_sector(uint32_t lba, void *out) {
    {
    kernel_guard_t guard __attribute__((cleanup(kernel_guard_leave)))=kernel_guard_enter_irq_safe(&directory_cache_gate);
    for (uint32_t i = 0; i < ISO_DIRECTORY_CACHE_SLOTS; ++i) {
        if (directory_cache[i].valid && directory_cache[i].lba == lba) {
            memcpy(out, directory_cache[i].bytes, ISO_SECTOR_SIZE);
            return 0;
        }
    }
    }
    if (disk_cdrom_read(lba, 1u, out) != 0) return -1;
    kernel_guard_t guard __attribute__((cleanup(kernel_guard_leave)))=kernel_guard_enter_irq_safe(&directory_cache_gate);
    uint32_t slot = directory_cache_next++ % ISO_DIRECTORY_CACHE_SLOTS;
    directory_cache[slot].lba = lba;
    memcpy(directory_cache[slot].bytes, out, ISO_SECTOR_SIZE);
    directory_cache[slot].valid = 1u;
    return 0;
}

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
static int iso_ascii_name_equal(const char *name, size_t name_len,
                                const char *query)
{
    size_t qlen = iso_strlen(query);
    if (name_len != qlen) return 0;

    for (size_t i = 0u; i < name_len; ++i) {
        if (iso_toupper((unsigned char)name[i]) !=
            iso_toupper((unsigned char)query[i])) return 0;
    }
    return 1;
}

/* Match the user-visible name of one ISO directory record.  xorriso keeps
 * the original long/non-ISO name in the Rock Ridge NM system-use entry (for
 * example WPE_WEBKIT_2.0 has NM="wpe-webkit-2.0").  The old reader only
 * inspected the ISO9660 identifier, which silently changed '-' to '_' and
 * made valid nested runtime paths unreachable.  Prefer NM when present and
 * retain the identifier fallback for images without Rock Ridge metadata. */
static int iso_name_match(const uint8_t *record, uint8_t rec_len,
                          const char *query)
{
    if (record == NULL || query == NULL || rec_len < 33u) return 0;
    const uint8_t id_len = record[32u];
    if (33u + (size_t)id_len > (size_t)rec_len) return 0;
    const uint8_t *id = record + 33u;
    size_t system_use = 33u + (size_t)id_len + ((id_len & 1u) == 0u ? 1u : 0u);
    char rr_name[256];
    size_t rr_len = 0u;
    int rr_found = 0;

    while (system_use + 5u <= (size_t)rec_len) {
        const uint8_t *entry = record + system_use;
        const uint8_t entry_len = entry[2u];

        if (entry_len < 5u || system_use + (size_t)entry_len > (size_t)rec_len) {
            break;
        }
        if (entry[0] == (uint8_t)'N' && entry[1] == (uint8_t)'M') {
            const uint8_t flags = entry[4u];
            const size_t name_bytes = (size_t)entry_len - 5u;

            /* NM flags 2/4 denote the special '.'/'..' records.  Those are
             * skipped by iso_find_in_dir before this helper is called. */
            if ((flags & 0x06u) == 0u) {
                size_t copy = name_bytes;
                if (copy > sizeof(rr_name) - 1u - rr_len) {
                    copy = sizeof(rr_name) - 1u - rr_len;
                }
                for (size_t i = 0u; i < copy; ++i) {
                    rr_name[rr_len + i] = (char)entry[5u + i];
                }
                rr_len += copy;
                rr_found = 1;
            }
        }
        system_use += (size_t)entry_len;
    }

    if (rr_found && iso_ascii_name_equal(rr_name, rr_len, query)) {
        return 1;
    }

    /* Strip ";N" version suffix and trailing ISO padding dot. */
    uint8_t cmp_len = id_len;
    for (uint8_t i = 0u; i < id_len; ++i) {
        if (id[i] == (uint8_t)';') { cmp_len = i; break; }
    }
    if (cmp_len > 0u && id[cmp_len - 1u] == (uint8_t)'.') {
        --cmp_len;
    }
    return iso_ascii_name_equal((const char *)id, cmp_len, query);
}

/* Decode the actual child name, rather than the lossy ISO uppercase alias. */
static int iso_record_name(const uint8_t *record, size_t length, char name[256])
{
    if (length < 33u || 33u + record[32] > length) return -1;
    size_t id_length = record[32], used = 0u;
    size_t offset = 33u + id_length + ((id_length & 1u) == 0u);
    int rock_ridge = 0;
    while (offset + 4u <= length) {
        const uint8_t *entry = record + offset;
        size_t bytes = entry[2];
        if (bytes < 4u || bytes > length - offset) return -1;
        if (entry[0] == 'N' && entry[1] == 'M') {
            if (bytes < 5u) return -1;
            if ((entry[4] & 6u) == 0u) {
                if (bytes - 5u > 255u - used) return -1;
                memcpy(name + used, entry + 5u, bytes - 5u);
                used += bytes - 5u;
                rock_ridge = 1;
            }
        }
        offset += bytes;
    }
    if (!rock_ridge) {
        for (size_t i = 0; i < id_length && record[33u + i] != ';'; ++i)
            name[used++] = (char)record[33u + i];
        if (used && name[used - 1u] == '.') --used;
    }
    name[used] = '\0';
    for (size_t i = 0; i < used; ++i)
        if (name[i] == '\0' || name[i] == '/') return -1;
    return used ? 0 : -1;
}

/* ── directory traversal ────────────────────────────────────────────────────── */

/* Decode Rock Ridge TF timestamps. Missing/invalid fields stay unknown. */
static int iso_date_short(const uint8_t *d, int64_t *seconds) {
    unsigned year=1900u+d[0],month=d[1],day=d[2];
    static const uint8_t days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    int zone=(int8_t)d[6];
    int leap=(year%4u==0u && (year%100u!=0u || year%400u==0u));
    if(month<1 || month>12 || !day || day>days[month-1]+(month==2 && leap) ||
       d[3]>23 || d[4]>59 || d[5]>59 || zone < -48 || zone > 52)return -1;
    int64_t total=0;
    for(unsigned y=1970;y<year;y++)total+=365+(y%4==0 && (y%100!=0 || y%400==0));
    for(unsigned y=year;y<1970;y++)total-=365+(y%4==0 && (y%100!=0 || y%400==0));
    for(unsigned m=1;m<month;m++)total+=days[m-1]+(m==2 && leap);
    *seconds=((total+day-1)*24+d[3])*3600+d[4]*60+d[5]-(int64_t)zone*900;
    return 0;
}
static void iso_record_times(const uint8_t *record,size_t size,vex_file_times_t *out) {
    if(!out)return;
    memset(out,0,sizeof(*out));
    if(size<33 || 33u+record[32]>size)return;
    size_t offset=33u+record[32]+((record[32]&1u)==0u);
    while(offset+5<=size){
        const uint8_t *entry=record+offset;size_t bytes=entry[2];
        if(bytes<5 || bytes>size-offset)break;
        if(entry[0]=='T' && entry[1]=='F' && entry[3]==1){
            unsigned flags=entry[4];size_t width=(flags&128)?17:7,at=5;
            for(unsigned bit=0;bit<7;bit++)if(flags&(1u<<bit)){
                if(at>bytes || width>bytes-at)return;
                int index=bit==0?3:bit==1?1:bit==2?0:bit==3?2:-1;
                if(index>=0 && width==7 && !iso_date_short(entry+at,&out->seconds[index]))
                    out->valid|=1u<<index;
                at+=width;
            }
        }
        offset+=bytes;
    }
}

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
                           uint32_t *out_lba, uint32_t *out_size, vex_file_times_t *times)
{
    uint8_t sector[ISO_SECTOR_SIZE];
    uint32_t sectors = (dir_bytes + ISO_SECTOR_SIZE - 1u) / ISO_SECTOR_SIZE;

    for (uint32_t s = 0u; s < sectors; ++s) {
        if (read_directory_sector(dir_lba + s, sector) != 0) return -1;

        uint32_t off = 0u;
        while (off + 33u <= ISO_SECTOR_SIZE) {
            uint8_t rec_len = sector[off + 0u];
            if (rec_len == 0u) break;                     /* sector padding */
            if (rec_len < 33u || off + rec_len > ISO_SECTOR_SIZE ||
                33u + sector[off + 32u] > rec_len) return -1;

            uint8_t  name_len = sector[off + 32u];
            uint8_t  flags    = sector[off + 25u];
            uint32_t e_lba    = iso_le32(sector + off + 2u);
            uint32_t e_size   = iso_le32(sector + off + 10u);

            /* Skip the special "." (id=0x00) and ".." (id=0x01) entries. */
            if (name_len == 1u &&
                (sector[off + 33u] == 0x00u ||
                 sector[off + 33u] == 0x01u)) {
                off += rec_len;
                continue;
            }

            if (iso_name_match(sector + off, rec_len, name)) {
                *out_lba  = e_lba;
                *out_size = e_size;
                iso_record_times(sector+off,rec_len,times);
                return (flags & 0x02u) ? 2 : 1;  /* 2 = dir, 1 = file */
            }

            off += rec_len;
        }
    }

    return 0;  /* not found in this directory */
}

/* Walk an absolute VFS path through the ISO file system.
 * Returns -1 on failure/not found, 1 if the final component is a file,
 * 2 if the final component is a directory. */
static int iso_find_path_metadata(const char *path, uint32_t *out_lba, uint32_t *out_size,vex_file_times_t *times)
{
    if (!g_iso_ready || path == NULL || path[0] != '/') return -1;

    uint32_t cur_lba  = g_root_lba;
    uint32_t cur_size = g_root_size;
    const char *p = path + 1u;   /* skip leading '/' */
    int last_type = 2;            /* root is a directory */

    if (*p == '\0') {
        /* Caller asked for the root directory itself. */
        *out_lba  = cur_lba;
        *out_size = cur_size;
        return 2;
    }

    while (*p != '\0') {
        /* Extract the next path component. */
        char comp[256];
        size_t ci = 0u;
        while (*p != '\0' && *p != '/' && ci < sizeof(comp) - 1u) {
            comp[ci++] = *p++;
        }
        comp[ci] = '\0';
        if (*p != '\0' && *p != '/') return -1; /* component too long */
        const int followed_by_slash = *p == '/';
        if (followed_by_slash) ++p;

        if (ci == 0u) continue;   /* double slash; skip */

        uint32_t e_lba = 0u, e_size = 0u;
        int r = iso_find_in_dir(cur_lba, cur_size, comp, &e_lba, &e_size,times);
        if (r <= 0) return -1;    /* not found or I/O error */
        /* Never interpret a regular file's extent as directory records.
         * Compatibility directory-marker probes append a child to files;
         * without this check a large ELF was scanned sector by sector. */
        if (r != 2 && followed_by_slash) return -1;

        last_type = r;
        cur_lba  = e_lba;
        cur_size = e_size;
    }

    *out_lba  = cur_lba;
    *out_size = cur_size;
    return last_type;  /* 1 = file, 2 = directory */
}

/* ── Public API ─────────────────────────────────────────────────────────────── */

static int iso_find_path(const char *path,uint32_t *lba,uint32_t *size) {
    return iso_find_path_metadata(path,lba,size,NULL);
}
int iso9660_get_times(const char *path,vex_file_times_t *times) {
    uint32_t lba,size;
    if(!times)return -1;
    memset(times,0,sizeof(*times));
    return iso_find_path_metadata(path,&lba,&size,times)<1?-1:0;
}

int iso9660_list_directory(const char *path, iso9660_directory_visitor_t visitor,
                           void *context)
{
    uint32_t lba, size;
    if (!visitor || iso_find_path(path, &lba, &size) != 2) return 0;
    /* A visitor can re-enter ISO lookup. Its traversal must not overwrite
     * the directory sector being enumerated. */
    uint8_t sector[ISO_SECTOR_SIZE];
    const uint64_t sectors = ((uint64_t)size + ISO_SECTOR_SIZE - 1u) / ISO_SECTOR_SIZE;
    for (uint64_t s = 0; s < sectors; ++s) {
        if (s > UINT32_MAX - lba || read_directory_sector(lba + (uint32_t)s, sector)) return -1;
        size_t valid = size - s * ISO_SECTOR_SIZE;
        if (valid > ISO_SECTOR_SIZE) valid = ISO_SECTOR_SIZE;
        for (size_t offset = 0; offset < valid;) {
            const uint8_t *record = sector + offset;
            size_t length = record[0];
            if (!length) break;
            if (length < 33u || length > valid - offset || 33u + record[32] > length) return -1;
            if (!(record[32] == 1u && record[33] <= 1u)) {
                char name[256];
                if (iso_record_name(record, length, name)) return -1;
                int result = visitor(name, iso_le32(record + 10), (record[25] & 2u) != 0u, context);
                if (result) return result;
            }
            offset += length;
        }
    }
    return 0;
}

void iso9660_init(void)
{
    g_iso_ready = 0;
    memset(directory_cache, 0, sizeof(directory_cache));
    directory_cache_next = 0;

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
    if (iso_find_path(path, &file_lba, &file_size) != 1) return -1;  /* 1 = file only */
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

    /* The temporary PMM buffer is never the returned cache pointer. Boot
     * registration reclaims its source and was returning freed ISO storage;
     * failed cache insertion also used to leave an owner with no file size. */
    const void *cached = NULL;
    int result = fs_cache_readonly_payload(path, virt, (size_t)file_size, &cached);
    pmm_free_contiguous_pages(phys, pages);
    if (result != 0) return -1; /* open/stat can use the real streaming backend */
    *data_out = (void *)cached;
    *size_out = (size_t)file_size;
    return 0;
}

int iso9660_exists(const char *path)
{
    uint32_t lba, sz;
    if (!g_iso_ready) return 0;
    return iso_find_path(path, &lba, &sz) >= 1 ? 1 : 0;
}

int iso9660_is_directory(const char *path)
{
    uint32_t lba, sz;
    if (!g_iso_ready || path == NULL) return 0;
    if (path[0] == '/' && path[1] == '\0') return 1;  /* root */
    return iso_find_path(path, &lba, &sz) == 2 ? 1 : 0;
}

int iso9660_get_location(const char *path, uint32_t *lba_out, uint32_t *size_out)
{
    int r;
    if (!g_iso_ready || path == NULL || lba_out == NULL || size_out == NULL) return -1;
    r = iso_find_path(path, lba_out, size_out);
    if (VEX_COMPAT_TRACE && r != 1) {
        debug_log_puts("iso9660: not found ");
        debug_log_puts(path);
        debug_log_puts("\n");
    }
    return r == 1 ? 0 : -1;
}

int iso9660_pread(uint32_t lba, uint32_t file_size,
                  size_t file_offset, void *dst, size_t count)
{
    uint8_t *out = (uint8_t *)dst;
    size_t remaining, done = 0u;

    if (!g_iso_ready || dst == NULL) return -1;
    if (file_size == 0u || (uint64_t)file_offset >= (uint64_t)file_size) return 0;

    remaining = (size_t)file_size - file_offset;
    if (count > remaining) count = remaining;
    if (count == 0u) return 0;

    while (done < count) {
        kernel_guard_t guard __attribute__((cleanup(kernel_guard_leave)))=kernel_guard_enter_irq_safe(&pread_chunk_gate);
        size_t cur_offset = file_offset + done;
        uint32_t sector_idx = (uint32_t)(cur_offset / ISO_SECTOR_SIZE);
        uint32_t sector_off = (uint32_t)(cur_offset % ISO_SECTOR_SIZE);
        uint32_t eof_sector = (file_size + ISO_SECTOR_SIZE - 1u) / ISO_SECTOR_SIZE;
        /* A page fault normally asks for 4 KiB. Do not re-read 128 KiB for
         * every fault: the previous constant amplified I/O by up to 64x. */
        uint64_t needed = ((uint64_t)sector_off + (count - done) + ISO_SECTOR_SIZE - 1u) / ISO_SECTOR_SIZE;
        uint32_t sectors_want = needed > ISO_PREAD_CHUNK_SECTORS
            ? ISO_PREAD_CHUNK_SECTORS : (uint32_t)needed;
        size_t bytes_avail, bytes_to_copy;

        if (sector_idx >= eof_sector) break;
        if (sector_idx + sectors_want > eof_sector)
            sectors_want = eof_sector - sector_idx;

        if (disk_cdrom_read(lba + sector_idx, sectors_want, g_pread_chunk) != 0) {
            /* Fallback: try a single sector */
            sectors_want = 1u;
            if (disk_cdrom_read(lba + sector_idx, 1u, g_pread_chunk) != 0)
                return done > 0u ? (int)done : -1;
        }

        bytes_avail = (size_t)sectors_want * ISO_SECTOR_SIZE - (size_t)sector_off;
        bytes_to_copy = (bytes_avail < (count - done)) ? bytes_avail : (count - done);
        memcpy(out + done, g_pread_chunk + sector_off, bytes_to_copy);
        done += bytes_to_copy;
    }

    return (int)done;
}
