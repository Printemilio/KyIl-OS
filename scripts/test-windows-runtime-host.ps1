param([switch]$SkipWsl)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path "$PSScriptRoot/..").Path
$Out=Join-Path $Root 'build/windows'
$Clang='C:/Program Files/LLVM/bin/clang.exe'
$Link='C:/Program Files/LLVM/bin/ld.lld.exe'
function Run([string]$Exe,[string[]]$Arguments) {
    & $Exe @Arguments
    if($LASTEXITCODE){throw "Failed: $Exe (exit $LASTEXITCODE)"}
}
Push-Location $Root
try {
    New-Item -ItemType Directory -Force $Out | Out-Null
    Run $Clang @('-O2','-Wall','-Wextra','-Werror','tests/windows_handle_value_test.c','-o',"$Out/handle-value-test.exe")
    Run "$Out/handle-value-test.exe" @()
    Run $Clang @('-O2','-Wall','-Wextra','-Werror','tests/windows_command_reference_test.c','-lshell32','-o',"$Out/command-reference.exe")
    Run "$Out/command-reference.exe" @()
    Run $Clang @('-Wall','-Wextra','-Werror','tests/windows_pipe_policy_test.c','-o',"$Out/pipe_policy_test.exe")
    Run "$Out/pipe_policy_test.exe" @()
    Run $Clang @('-Wall','-Wextra','-Werror','tests/pe_image_test.c','userspace/windows/pe_image.c','-o',"$Out/pe_image_test.exe")
    Run "$Out/pe_image_test.exe" @()
    Run $Clang @('-Wall','-Wextra','-Werror','-Ikernel/include','tests/windows_nls_reference_test.c','userspace/windows/nls.c','-o',"$Out/nls_reference_test.exe")
    Run "$Out/nls_reference_test.exe" @()
    Run $Clang @('-O2','-Wall','-Wextra','-idirafter',"$Root/kernel/include",'tests/windows_encoding_reference_test.c','userspace/windows/encoding.c','-o',"$Out/encoding-reference.exe")
    Run "$Out/encoding-reference.exe" @()
    Run 'C:/Program Files/LLVM/bin/lld-link.exe' @('/dll','/noentry','/machine:x64',"/def:$Root/userspace/windows/tests/kernel32.def","/out:$Out/host-kernel32-import.dll","/implib:$Out/host-kernel32.lib")
    Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-DIOCP_HOST_REFERENCE','-c',"$Root/userspace/windows/tests/completion_smoke.c",'-o',"$Out/completion_host.obj")
    Run 'C:/Program Files/LLVM/bin/lld-link.exe' @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/completion_host.obj","$Out/host-kernel32.lib","/out:$Out/completion_host_reference.exe")
    Run "$Out/completion_host_reference.exe" @()
    Write-Host 'windows-iocp-reference: FIFO / wide packets / timeout activity / resumed quota / blocking consumers / close wake PASS (actual Windows; invalid-handle exceptions excluded)'
    Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-c',"$Root/userspace/windows/tests/overlapped_smoke.c",'-o',"$Out/overlapped_host.obj")
    Run 'C:/Program Files/LLVM/bin/lld-link.exe' @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/overlapped_host.obj","$Out/host-kernel32.lib","/out:$Out/overlapped_host.exe")
    Run "$Out/overlapped_host.exe" @()
    Write-Host 'windows-overlapped-reference: real 65536-byte positional read/write / IOCP / events / low-bit suppression / EOF / invalid buffer PASS (actual Windows)'
    Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-DTHREAD_HOST_REFERENCE','-c',"$Root/userspace/windows/tests/thread_control.c",'-o',"$Out/thread_control_host.obj")
    Run $Clang @('-target','x86_64-pc-windows-msvc','-c',"$Root/userspace/windows/tests/thread_control.S",'-o',"$Out/thread_control_host_asm.obj")
    Run 'C:/Program Files/LLVM/bin/lld-link.exe' @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/thread_control_host.obj","$Out/thread_control_host_asm.obj","$Out/host-kernel32.lib","/out:$Out/thread_control_host.exe")
    Run "$Out/thread_control_host.exe" @()
    Write-Host 'windows-thread-reference: 32 counted suspend/resume cycles, GPR/XMM, stopped progress, cycles PASS (actual Windows; invalid-handle exceptions excluded)'
    Run 'C:/Program Files/LLVM/bin/lld-link.exe' @('/dll','/noentry','/machine:x64',"/def:$Root/userspace/windows/tests/ntdll.def","/out:$Out/host-ntdll-import.dll","/implib:$Out/host-ntdll.lib")
    foreach($Fixture in @('chrome_contract','private_heap','inspection','multiple_wait','namespace_mutation','process_memory_counter','rng','virtual_memory','multimedia','registry_win32','security_data','startup_contract','shared_event')) {
        Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-c',"$Root/userspace/windows/tests/${Fixture}_smoke.c",'-o',"$Out/${Fixture}_host.obj")
        Run 'C:/Program Files/LLVM/bin/lld-link.exe' @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/${Fixture}_host.obj","$Out/host-kernel32.lib","$Out/host-ntdll.lib","/out:$Out/${Fixture}_host.exe")
        Run "$Out/${Fixture}_host.exe" @()
        Write-Host "windows-$Fixture-reference: actual Windows contract PASS (guest-only restrictions/residency checks excluded)"
    }
    Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-c',"$Root/userspace/windows/tests/loader_lock_probe.c",'-o',"$Out/loader_lock_probe_host.obj")
    Run 'C:/Program Files/LLVM/bin/lld-link.exe' @('/dll','/entry:DllMain','/nodefaultlib','/machine:x64',"$Out/loader_lock_probe_host.obj","$Out/host-kernel32.lib","/out:$Out/kyil-loader-lock-probe.dll")
    Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-c',"$Root/userspace/windows/tests/loader_lock_smoke.c",'-o',"$Out/loader_lock_host.obj")
    Run 'C:/Program Files/LLVM/bin/lld-link.exe' @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/loader_lock_host.obj","$Out/host-kernel32.lib","/out:$Out/loader_lock_host.exe")
    Run "$Out/loader_lock_host.exe" @()
    Write-Host 'windows-loader-lock-reference: real PEB lock / constructor and thread notification ownership / recursive lookup / blocked startup / wake PASS (actual Windows)'
    foreach($Mode in @('parent','child')) {
        $Defines=@();if($Mode -eq 'child'){$Defines+='-DCWD_CHILD'}
        Run $Clang (@('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-c',"$Root/userspace/windows/tests/cwd_smoke.c",'-o',"$Out/cwd_${Mode}_host.obj")+$Defines)
        Run 'C:/Program Files/LLVM/bin/lld-link.exe' @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/cwd_${Mode}_host.obj","$Out/host-kernel32.lib","/out:$Out/windows-cwd-$Mode.exe")
    }
    Run "$Out/windows-cwd-parent.exe" @()
    Write-Host 'windows-cwd-reference: canonical relative paths / PEB directory format / file IO / child directory environment PASS (actual Windows; native pre-start anchor excluded)'
    if(-not $SkipWsl) {
        Run $Clang @('-target','x86_64-unknown-none-elf','-c','tests/windows_guard_abi_test.S','-o',"$Out/guard_abi_test.o")
        Run $Clang @('-target','x86_64-unknown-none-elf','-c','userspace/windows/guard.S','-o',"$Out/guard_abi_current.o")
        Run $Link @('-static','-e','_start','-o',"$Out/guard_abi_current","$Out/guard_abi_test.o","$Out/guard_abi_current.o")
        Run 'wsl.exe' @('-d','Ubuntu-24.04','--','./build/windows/guard_abi_current')
    }
} finally {Pop-Location}
