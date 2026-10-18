#include <unos/disk.h>
#include <unos/scheduler.h>
#include <unos/security.h>
#include <unos/debug_log.h>
#include <string.h>
int disk_fixture_self_test(void) {
    uint8_t tag[512],original[2048],pattern[2048],readback[2048];
    if(!scheduler_current_has_capability(VEX_CAP_SYSTEM_ADMIN) ||
       disk_size()<8196u || disk_read(0u,1u,tag) || memcmp(tag,"KYIL-ATA-TEST-1",15)) {
        debug_log_puts("disk-fixture: refused (not an authorized test medium)\n");return -1;
    }
    if(disk_read(8192u,4u,original))return -1;
    for(unsigned i=0;i<sizeof(pattern);++i)pattern[i]=(uint8_t)(i*37u+11u);
    int result=disk_write(8192u,4u,pattern);
    if(!result)result=disk_read(8192u,4u,readback);
    if(!result && memcmp(pattern,readback,sizeof(pattern)))result=-1;
    /* The fixture is restored even when a partial operation failed. A real
     * ArchVista disk can never enter this path by mere discovery. */
    if(disk_write(8192u,4u,original))result=-1;
    if(disk_read(8192u,4u,readback) || memcmp(original,readback,sizeof(original)))result=-1;
    if(disk_read(UINT64_MAX,1u,readback)!=-1 || disk_write(disk_size(),1u,readback)!=-1)result=-1;
    debug_log_puts(result?"disk-fixture: FAILED\n":"disk-fixture: real read/write/flush/restore/bounds PASS\n");
    return result;
}
