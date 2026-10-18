#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Hosted adapter for subsystem gates: actual pthread exclusion, no privileged
 * CLI/STI. Concurrent native readers are tested separately inside KyIlOS. */
#include <pthread.h>
#define UNOS_KERNEL_GUARD_H
typedef pthread_mutex_t kernel_gate_t;
#define KERNEL_GATE_INIT PTHREAD_MUTEX_INITIALIZER
typedef struct {kernel_gate_t *gate;} kernel_guard_t;
static kernel_guard_t kernel_guard_enter_irq_safe(kernel_gate_t *gate){
    assert(!pthread_mutex_lock(gate));return (kernel_guard_t){gate};
}
static void kernel_guard_leave(kernel_guard_t *guard){assert(!pthread_mutex_unlock(guard->gate));}
#include "../../kernel/src/iso9660.c"

static unsigned calls, sectors, failures, directory_fixture;
int disk_cdrom_read(uint32_t lba, uint32_t count, void *out) {
    ++calls;
    sectors += count;
    if (failures) { --failures; return -1; }
    if (directory_fixture) {
        assert(lba == 10 && count == 1); /* never read the file extent */
        memset(out, 0, ISO_SECTOR_SIZE);
        unsigned char *record = out;
        record[0] = 44; record[2] = 100;
        record[10] = 0; record[11] = 0; record[12] = 0; record[13] = 1; /* 16 MiB */
        record[32] = 10; memcpy(record + 33, "library.so", 10);
        return 0;
    }
    for (unsigned i = 0; i < count * ISO_SECTOR_SIZE; ++i)
        ((unsigned char *)out)[i] = (unsigned char)((uint64_t)lba * ISO_SECTOR_SIZE + i);
    return 0;
}
int main(void) {
    unsigned char out[4096];
    g_iso_ready = 1;
    assert(iso9660_pread(100, 1000000, 0, out, sizeof(out)) == sizeof(out));
    assert(calls == 1 && sectors == 2);
    assert(iso9660_pread(100, 1000000, 17, out, sizeof(out)) == sizeof(out));
    assert(calls == 2 && sectors == 5 && out[0] == 17);
    assert(iso9660_pread(100, 4100, 4096, out, sizeof(out)) == 4);
    assert(sectors == 6);
    assert(iso9660_pread(100, 4100, 4100, out, sizeof(out)) == 0);
    unsigned before = calls;
    assert(!read_directory_sector(10, out));
    assert(!read_directory_sector(10, out));
    assert(calls == before + 1);
    failures = 1;
    assert(read_directory_sector(11, out) == -1);
    assert(!read_directory_sector(11, out));
    assert(calls == before + 3); /* failed read was not cached */
    memset(directory_cache, 0, sizeof(directory_cache));
    g_root_lba = 10; g_root_size = ISO_SECTOR_SIZE; directory_fixture = 1;
    uint32_t lba, size;
    before = calls;
    assert(iso_find_path("/library.so/.directory", &lba, &size) == -1);
    assert(iso_find_path("/library.so/", &lba, &size) == -1);
    assert(iso_find_path("/library.so", &lba, &size) == 1 && size == 16777216);
    assert(calls == before + 1);
    /* Real Rock Ridge dates, including UTC offsets, leap days and truncation. */
    uint8_t record[80]={0};record[32]=1;record[33]='A';
    uint8_t *tf=record+34;tf[0]='T';tf[1]='F';tf[2]=26;tf[3]=1;tf[4]=14;
    const uint8_t epoch[7]={70,1,1,1,0,0,4};
    for(unsigned i=0;i<3;i++)memcpy(tf+5+i*7,epoch,7);
    vex_file_times_t times;
    iso_record_times(record,60,&times);
    assert(times.valid==7 && times.seconds[0]==0 && times.seconds[1]==0 && times.seconds[2]==0);
    int64_t date;uint8_t leap[7]={124,2,29,0,0,0,0};
    assert(!iso_date_short(leap,&date) && date==1709164800);
    leap[0]=123;assert(iso_date_short(leap,&date)==-1);
    iso_record_times(record,59,&times);assert(times.valid==0);
    puts("ISO read budget/cache/EOF/error: PASS");
}
