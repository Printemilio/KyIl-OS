#include <unos/vex_abi.h>
#include "vex_syscall.h"
#include "../runtime.h"
static int run(const char *path,unsigned expected) {
    vex_process_spawn_req_t spawn={path,0,0};
    if(vex_syscall1(VEX_SYS_PROCESS_SPAWN,(uintptr_t)&spawn) || !spawn.pid) return 1;
    int status=-1;vex_process_wait_req_t wait={(int32_t)spawn.pid,&status,0};
    int64_t r=(int64_t)vex_syscall1(VEX_SYS_PROCESS_WAIT,(uintptr_t)&wait);
    /* Native wait returns the native raw exit code, not POSIX wait status. */
    if(r!=(int64_t)spawn.pid || status!=(int)expected) {
        wr_log("windows-runner: unexpected child result: ");wr_log(path);wr_log("\n");
        char value[]="status=0x00000000\n";for(unsigned i=0;i<8;i++)value[9+i]="0123456789abcdef"[((unsigned)status>>(28-i*4))&15];wr_log(value);return 1;
    }
    return 0;
}
static int pipes(void) {
    vex_process_spawn_req_t server={"/usr/pe-tests/windows-pipe-server.exe",0,0},client={"/usr/pe-tests/windows-pipe-client.exe",0,0};
    if(vex_syscall1(VEX_SYS_PROCESS_SPAWN,(uintptr_t)&server) || !server.pid ||
       vex_syscall1(VEX_SYS_PROCESS_SPAWN,(uintptr_t)&client) || !client.pid)return 1;
    int status=-1;vex_process_wait_req_t wait={(int32_t)client.pid,&status,0};
    if((int64_t)vex_syscall1(VEX_SYS_PROCESS_WAIT,(uintptr_t)&wait)!=client.pid || status)return 1;
    wait.pid=(int32_t)server.pid;status=-1;
    if((int64_t)vex_syscall1(VEX_SYS_PROCESS_WAIT,(uintptr_t)&wait)!=server.pid || status)return 1;
    wr_log("windows-runner: independent PE processes / native root rendezvous / actual named-pipe bytes PASS\n");return 0;
}
int wr_main(uint64_t argc,char **argv) {
    (void)argc;(void)argv;
    for(unsigned i=0;i<3;i++) if(run("/usr/pe-tests/windows-native-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-rwx-rejected.exe",126))goto fail;
    if(run("/usr/pe-tests/windows-tls-rejected.exe",126))goto fail;
    vex_process_spawn_req_t invalid={"/usr/pe-tests/windows-bad-header.exe",0,123};
    if(!vex_syscall1(VEX_SYS_PROCESS_SPAWN,(uintptr_t)&invalid) || invalid.pid)goto fail;
    if(run("/usr/pe-tests/pe_probe.exe",126))goto fail;
    wr_log("windows-runner: 3 PE/DLL cycles + 4 refusal paths PASS\n");
    if(run("/usr/pe-tests/windows-tls-smoke.exe",0) || run("/usr/pe-tests/windows-veh-smoke.exe",0) ||
       run("/usr/pe-tests/windows-cfg-good.exe",0) || run("/usr/pe-tests/windows-cfg-bad.exe",0xc0000409u) ||
       run("/usr/pe-tests/windows-namespace-events.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-file-smoke.exe",0) || run("/usr/pe-tests/windows-iocp-smoke.exe",0) || run("/usr/native-tests/native_execution_test",0))goto fail;
    if(run("/usr/pe-tests/windows-mutex-critical.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-startup_contract-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-shared_event-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-handle_value-smoke.exe",0))goto fail;
    for(unsigned cycle=0;cycle<6;cycle++)if(run("/usr/pe-tests/windows-affinity-exit.exe",17))goto fail;
    if(run("/usr/pe-tests/windows-multiple_wait-smoke.exe",0) || run("/usr/pe-tests/windows-multiple-wait-exit.exe",17))goto fail;
    if(run("/usr/pe-tests/windows-overlapped-smoke.exe",0) || run("/usr/native-tests/native_rendezvous_test",0))goto fail;
    if(pipes())goto fail;
    if(run("/usr/native-tests/native_process_resource_test",0) || run("/usr/pe-tests/windows-process-parent.exe",0) ||
       run("/usr/pe-tests/windows-module-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-loader_lock-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-encoding-smoke.exe",0) || run("/usr/pe-tests/windows-platform-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-contract_batch-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-async_contract-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-system_contract-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-file_lock-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-job-parent.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-wer-parent.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-condition-once-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-version-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-nt-core-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-inspection-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-rng-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-multimedia-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-cwd-parent.exe",0) || run("/usr/pe-tests/windows-process_memory_counter-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-times-parent.exe",0) || run("/usr/pe-tests/windows-memory_usage-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-namespace_mutation-smoke.exe",0) || run("/usr/pe-tests/windows-affinity-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-memory-query-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-virtual_memory-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-unwind-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-heap-walk-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-private_heap-smoke.exe",0) || run("/usr/pe-tests/windows-heap-corruption.exe",0xc0000374u) ||
       run("/usr/pe-tests/windows-chrome_contract-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-fls-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-file_batch-smoke.exe",0) || run("/usr/pe-tests/windows-profile-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-seh-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-slist-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-registry-parent.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-registry_win32-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-security_data-smoke.exe",0) || run("/usr/native-tests/ultra_space",0) || run("/usr/pe-tests/windows-protected_pipe-smoke.exe",0))goto fail;
    if(run("/usr/pe-tests/windows-thread-control.exe",0))goto fail;
    if(run("/usr/native-tests/native_thread_control_test",0))goto fail;
    for(unsigned i=0;i<3;i++)if(run("/usr/pe-tests/windows-iocp-exit.exe",23))goto fail;
    for(unsigned i=0;i<3;i++)if(run("/usr/pe-tests/windows-exit-race.exe",17))goto fail;
    wr_log("windows-runner: TLS / loader re-entry / VEH / CFG / prefix / events / section lifetime / concurrent exit PASS\n");
    if(run("/usr/native-tests/native_context_test",0) || run("/usr/native-tests/libc_thread_test",0) ||
       run("/usr/native-tests/native_console_test",0) || run("/usr/linux-tests/linux_thread_context_smoke",0) ||
       run("/usr/linux-tests/linux_jit_wx_smoke",0) || run("/usr/linux-tests/linux_fault_signal_smoke",0) ||
       run("/usr/linux-tests/linux_memory_ownership_smoke",0))goto fail;
    wr_log("windows-runner: native GS / libc pthread / console-rights / Linux TLS-FPU / W_X / faults / memory ownership PASS\n");return 0;
fail:
    wr_log("windows-runner: FAIL\n");return 1;
}
