param()

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$OutDir = Join-Path $Root "usr/linux-tests"
$BuildDir = Join-Path $Root "build/linux-tests"

function Resolve-CommandPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [string[]]$CandidatePaths = @()
    )

    $Command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($Command) {
        return $Command.Source
    }

    foreach ($CandidatePath in $CandidatePaths) {
        if ($CandidatePath -and (Test-Path $CandidatePath)) {
            return $CandidatePath
        }
    }

    throw "Required command '$Name' was not found in PATH."
}

function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [object[]]$Arguments = @()
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath $($Arguments -join ' ')"
    }
}

$Clang = Resolve-CommandPath "clang" @(
    "C:\Program Files\LLVM\bin\clang.exe"
)

New-Item -ItemType Directory -Force -Path @($OutDir, $BuildDir) | Out-Null

$SmokeSource = Join-Path $Root "userspace/linux_tests/linux_static_smoke.S"
$SmokeOut = Join-Path $OutDir "linux_static_smoke"
$AbortGroupSmokeSource = Join-Path $Root "userspace/linux_tests/linux_abort_group_smoke.S"
$AbortGroupSmokeOut = Join-Path $OutDir "linux_abort_group_smoke"
$PieSmokeSource = Join-Path $Root "userspace/linux_tests/linux_static_pie_smoke.S"
$PieSmokeOut = Join-Path $OutDir "linux_static_pie_smoke"
$CloneSmokeSource = Join-Path $Root "userspace/linux_tests/linux_clone_smoke.S"
$CloneSmokeOut = Join-Path $OutDir "linux_clone_smoke"
$ParentLifetimeSource = Join-Path $Root "userspace/linux_tests/linux_parent_lifetime_smoke.S"
$ParentLifetimeOut = Join-Path $OutDir "linux_parent_lifetime_smoke"
$ThreadContextSource = Join-Path $Root "userspace/linux_tests/linux_thread_context_smoke.S"
$ThreadContextOut = Join-Path $OutDir "linux_thread_context_smoke"
$FutexSmokeSource = Join-Path $Root "userspace/linux_tests/linux_futex_smoke.S"
$FutexSmokeOut = Join-Path $OutDir "linux_futex_smoke"
$FileMmapSmokeSource = Join-Path $Root "userspace/linux_tests/linux_file_mmap_smoke.S"
$FileMmapSmokeOut = Join-Path $OutDir "linux_file_mmap_smoke"
$Wait4SmokeSource = Join-Path $Root "userspace/linux_tests/linux_wait4_smoke.S"
$Wait4SmokeOut = Join-Path $OutDir "linux_wait4_smoke"
$ExecChildSource = Join-Path $Root "userspace/linux_tests/linux_exec_child.S"
$ExecChildOut = Join-Path $OutDir "linux_exec_child"
$PipeExecSmokeSource = Join-Path $Root "userspace/linux_tests/linux_pipe_exec_smoke.S"
$PipeExecSmokeOut = Join-Path $OutDir "linux_pipe_exec_smoke"
$MemoryOwnershipSource = Join-Path $Root "userspace/linux_tests/linux_memory_ownership_smoke.S"
$MemoryOwnershipOut = Join-Path $OutDir "linux_memory_ownership_smoke"
$FsIpcSource = Join-Path $Root "userspace/linux_tests/linux_fs_ipc_smoke.S"
$FsIpcOut = Join-Path $OutDir "linux_fs_ipc_smoke"
$ExecArgsSource = Join-Path $Root "userspace/linux_tests/linux_exec_args_smoke.S"
$ExecArgsOut = Join-Path $OutDir "linux_exec_args_smoke"
$UnsupportedSource = Join-Path $Root "userspace/linux_tests/linux_unsupported_smoke.S"
$UnsupportedOut = Join-Path $OutDir "linux_unsupported_smoke"
$SeqpacketSource = Join-Path $Root "userspace/linux_tests/linux_seqpacket_smoke.S"
$SeqpacketOut = Join-Path $OutDir "linux_seqpacket_smoke"
$SeqpacketFdPassSource = Join-Path $Root "userspace/linux_tests/linux_seqpacket_fdpass_smoke.S"
$SeqpacketFdPassOut = Join-Path $OutDir "linux_seqpacket_fdpass_smoke"
$Int3Source = Join-Path $Root "userspace/linux_tests/linux_int3_smoke.S"
$Int3Out = Join-Path $OutDir "linux_int3_smoke"
$FaultSignalSource = Join-Path $Root "userspace/linux_tests/linux_fault_signal_smoke.S"
$FaultSignalOut = Join-Path $OutDir "linux_fault_signal_smoke"
$JitWxSource = Join-Path $Root "userspace/linux_tests/linux_jit_wx_smoke.S"
$JitWxOut = Join-Path $OutDir "linux_jit_wx_smoke"
$FaultUnhandledSource = Join-Path $Root "userspace/linux_tests/linux_fault_unhandled_smoke.S"
$FaultUnhandledOut = Join-Path $OutDir "linux_fault_unhandled_smoke"
$DynamicLoaderSource = Join-Path $Root "userspace/linux_tests/vex_dynamic_loader.S"
$DynamicLoaderOut = Join-Path $OutDir "vex_dynamic_loader"
$DynamicSource = Join-Path $Root "userspace/linux_tests/linux_dynamic_smoke.S"
$DynamicObject = Join-Path $BuildDir "linux_dynamic_smoke.o"
$DynamicOut = Join-Path $OutDir "linux_dynamic_smoke"
$DynamicLibrarySource = Join-Path $Root "userspace/linux_tests/libvexdyn.S"
$DynamicLibraryObject = Join-Path $BuildDir "libvexdyn.o"
$DynamicLibraryOut = Join-Path $OutDir "libvexdyn.so"
$DynamicTlsSource = Join-Path $Root "userspace/linux_tests/linux_dynamic_tls_smoke.S"
$DynamicTlsObject = Join-Path $BuildDir "linux_dynamic_tls_smoke.o"
$DynamicTlsOut = Join-Path $OutDir "linux_dynamic_tls_smoke"
$DynamicTlsLibrarySource = Join-Path $Root "userspace/linux_tests/libvextls.S"
$DynamicTlsLibraryObject = Join-Path $BuildDir "libvextls.o"
$DynamicTlsLibraryOut = Join-Path $OutDir "libvextls.so"
$GlibcProbeSource = Join-Path $Root "userspace/linux_tests/glibc_probe.S"
$GlibcLoaderProbeSource = Join-Path $Root "userspace/linux_tests/glibc_loader_probe.S"
$GlibcProbeOut = Join-Path $OutDir "glibc_probe"
$GlibcLoaderProbeOut = Join-Path $OutDir "glibc_loader_probe"
$GlibcRuntime = Join-Path $Root "lib/libc.so.6"
$GlibcMathRuntime = Join-Path $Root "lib/libm.so.6"

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-static",
    "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $SmokeSource,
    "-o", $SmokeOut
)

Write-Host "Built Linux ELF smoke test: $SmokeOut"
Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu", "-nostdlib", "-static", "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000", "-Wl,--build-id=none",
    (Join-Path $Root 'userspace/linux_tests/linux_spawn_thread_smoke.S'),
    "-o", (Join-Path $OutDir 'linux_spawn_thread_smoke')
)
Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu", "-nostdlib", "-static", "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000", "-Wl,--build-id=none",
    (Join-Path $Root 'userspace/linux_tests/linux_spawn_borrow_smoke.S'),
    "-o", (Join-Path $OutDir 'linux_spawn_borrow_smoke')
)
Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu", "-nostdlib", "-static", "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000", "-Wl,--build-id=none",
    (Join-Path $Root 'userspace/linux_tests/linux_smp_shared_space.S'),
    "-o", (Join-Path $OutDir 'linux_smp_shared_space')
)
Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu", "-nostdlib", "-static", "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000", "-Wl,--build-id=none",
    (Join-Path $Root 'userspace/linux_tests/linux_smp_usercopy_smoke.S'),
    "-o", (Join-Path $OutDir 'linux_smp_usercopy_smoke')
)
Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu", "-nostdlib", "-static", "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000", "-Wl,--build-id=none",
    (Join-Path $Root "userspace/linux_tests/linux_native_domain_smoke.S"),
    "-o", (Join-Path $OutDir "linux_native_domain_smoke")
)

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-static",
    "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $AbortGroupSmokeSource,
    "-o", $AbortGroupSmokeOut
)

Write-Host "Built Linux SIGABRT process-group smoke test: $AbortGroupSmokeOut"
Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu", "-nostdlib", "-static", "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000", "-Wl,--build-id=none",
    (Join-Path $Root "userspace/linux_tests/linux_kill_group_smoke.S"),
    "-o", (Join-Path $OutDir "linux_kill_group_smoke")
)

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-static-pie",
    "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $PieSmokeSource,
    "-o", $PieSmokeOut
)

Write-Host "Built Linux static-PIE ELF smoke test: $PieSmokeOut"

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-static",
    "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $CloneSmokeSource,
    "-o", $CloneSmokeOut
)

Write-Host "Built Linux clone ELF smoke test: $CloneSmokeOut"

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-static",
    "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $ParentLifetimeSource,
    "-o", $ParentLifetimeOut
)

Write-Host "Built Linux parent-lifetime ELF smoke test: $ParentLifetimeOut"

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-static",
    "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $ThreadContextSource,
    "-o", $ThreadContextOut
)

Write-Host "Built Linux thread-context ELF smoke test: $ThreadContextOut"

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-static",
    "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $FutexSmokeSource,
    "-o", $FutexSmokeOut
)

Write-Host "Built Linux futex ELF smoke test: $FutexSmokeOut"

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-static",
    "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $FileMmapSmokeSource,
    "-o", $FileMmapSmokeOut
)

Write-Host "Built Linux file-mmap ELF smoke test: $FileMmapSmokeOut"

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-static",
    "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $Wait4SmokeSource,
    "-o", $Wait4SmokeOut
)

Write-Host "Built Linux wait4 ELF smoke test: $Wait4SmokeOut"

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-static",
    "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $ExecChildSource,
    "-o", $ExecChildOut
)

Write-Host "Built Linux exec child ELF test: $ExecChildOut"

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-static",
    "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $PipeExecSmokeSource,
    "-o", $PipeExecSmokeOut
)

Write-Host "Built Linux pipe-exec ELF smoke test: $PipeExecSmokeOut"

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-static",
    "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $MemoryOwnershipSource,
    "-o", $MemoryOwnershipOut
)

Write-Host "Built Linux memory-ownership ELF smoke test: $MemoryOwnershipOut"

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-static",
    "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $FsIpcSource,
    "-o", $FsIpcOut
)

Write-Host "Built Linux filesystem/IPC ELF smoke test: $FsIpcOut"

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-static",
    "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $ExecArgsSource,
    "-o", $ExecArgsOut
)

Write-Host "Built Linux exec argv/env/auxv ELF smoke test: $ExecArgsOut"

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-static",
    "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $UnsupportedSource,
    "-o", $UnsupportedOut
)

Write-Host "Built Linux unsupported-syscall ELF smoke test: $UnsupportedOut"

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-static",
    "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $SeqpacketSource,
    "-o", $SeqpacketOut
)

Write-Host "Built Linux native SEQPACKET ELF smoke test: $SeqpacketOut"

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-static",
    "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $SeqpacketFdPassSource,
    "-o", $SeqpacketFdPassOut
)

Write-Host "Built Linux SEQPACKET capability-transfer smoke test: $SeqpacketFdPassOut"

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-static",
    "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $Int3Source,
    "-o", $Int3Out
)

Write-Host "Built ring-3 INT3 smoke test: $Int3Out"

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-static",
    "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $FaultSignalSource,
    "-o", $FaultSignalOut
)

Write-Host "Built Linux synchronous fault-signal smoke test: $FaultSignalOut"

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-static",
    "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $JitWxSource,
    "-o", $JitWxOut
)

Write-Host "Built Linux strict-W^X JIT smoke test: $JitWxOut"

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-static",
    "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $FaultUnhandledSource,
    "-o", $FaultUnhandledOut
)

Write-Host "Built Linux unhandled-fault fatality smoke test: $FaultUnhandledOut"

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-static-pie",
    "-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $DynamicLoaderSource,
    "-o", $DynamicLoaderOut
)

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-c", $DynamicLibrarySource,
    "-o", $DynamicLibraryObject
)
Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-shared",
    "-fuse-ld=lld",
    "-Wl,-soname,libvexdyn.so",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $DynamicLibraryObject,
    "-o", $DynamicLibraryOut
)

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-c", $DynamicSource,
    "-o", $DynamicObject
)
Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-pie",
    "-fuse-ld=lld",
    "-Wl,-dynamic-linker,/usr/linux-tests/vex_dynamic_loader",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    "-L$OutDir",
    $DynamicObject,
    "-lvexdyn",
    "-o", $DynamicOut
)
Write-Host "Built dynamic ELF smoke test: $DynamicOut"

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-c", $DynamicTlsLibrarySource,
    "-o", $DynamicTlsLibraryObject
)
Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-shared",
    "-fuse-ld=lld",
    "-Wl,-soname,libvextls.so",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $DynamicTlsLibraryObject,
    "-o", $DynamicTlsLibraryOut
)
Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-c", $DynamicTlsSource,
    "-o", $DynamicTlsObject
)
Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-pie",
    "-fuse-ld=lld",
    "-Wl,-dynamic-linker,/usr/linux-tests/vex_dynamic_loader",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    "-L$OutDir",
    $DynamicTlsObject,
    "-lvextls",
    "-o", $DynamicTlsOut
)
Write-Host "Built dynamic TLS ELF smoke test: $DynamicTlsOut"

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-pie",
    "-fuse-ld=lld",
    "-Wl,--dynamic-linker=/lib64/ld-linux-x86-64.so.2",
    "-Wl,-z,now",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $GlibcProbeSource,
    $GlibcRuntime,
    $GlibcMathRuntime,
    "-o", $GlibcProbeOut
)
Write-Host "Built real glibc probe: $GlibcProbeOut"

Invoke-Native $Clang @(
    "-target", "x86_64-linux-gnu",
    "-nostdlib",
    "-pie",
    "-fuse-ld=lld",
    "-Wl,--dynamic-linker=/lib64/ld-linux-x86-64.so.2",
    "-Wl,-z,now",
    "-Wl,-z,max-page-size=0x1000",
    "-Wl,--build-id=none",
    $GlibcLoaderProbeSource,
    "-o", $GlibcLoaderProbeOut
)
Write-Host "Built glibc loader-only probe: $GlibcLoaderProbeOut"

$BrowserSystemSource = Join-Path $Root "userspace/linux_tests/linux_browser_system_smoke.S"
$BrowserSystemOut = Join-Path $OutDir "linux_browser_system_smoke"
Invoke-Native $Clang @("-target","x86_64-linux-gnu","-nostdlib","-static","-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000","-Wl,--build-id=none",$BrowserSystemSource,"-o",$BrowserSystemOut)
Write-Host "Built browser system ABI and capability regression: $BrowserSystemOut"

# This probe intentionally uses the actual staged glibc contracts (fork/spawn).
Invoke-Native "wsl" @("-d","Ubuntu-24.04","--cd",$Root,"--","gcc","-O2","-g",
    "-Wall","-Wextra","-Werror","-Wl,-z,max-page-size=0x1000",
    "userspace/linux_tests/linux_fd_inventory_smoke.c","-o","usr/linux-tests/linux_fd_inventory_smoke")
Write-Host "Built real glibc process FD inventory regression."

Invoke-Native "wsl" @("-d","Ubuntu-24.04","--cd",$Root,"--","gcc","-O2","-g",
    "-Wall","-Wextra","-Werror","-pthread","-Iuserspace/libvex/include",
    "-idirafter","kernel/include","-Wl,-z,max-page-size=0x1000",
    "userspace/linux_tests/linux_jit_alias_test.c","-o","usr/linux-tests/linux_jit_alias_test")

Invoke-Native "wsl" @("-d","Ubuntu-24.04","--cd",$Root,"--","gcc","-O2","-g",
    "-Wall","-Wextra","-Werror","-Iuserspace/libvex/include","-idirafter","kernel/include",
    "-Wl,-z,max-page-size=0x1000",
    "userspace/linux_tests/linux_jsc_jit_test.c","-o","usr/linux-tests/linux_jsc_jit_test")
Copy-Item -LiteralPath (Join-Path $Root "userspace/linux_tests/jsc-jit-tiers.js") -Destination (Join-Path $OutDir "jsc-jit-tiers.js")
Copy-Item -LiteralPath (Join-Path $Root "userspace/linux_tests/webkit-smp.html") -Destination (Join-Path $OutDir "webkit-smp.html")

Invoke-Native "wsl" @("-d","Ubuntu-24.04","--cd",$Root,"--","gcc","-O2","-g",
    "-Wall","-Wextra","-Werror","-pthread","-Wl,-z,max-page-size=0x1000",
    "userspace/linux_tests/linux_sigreturn_concurrency_smoke.c","-o","usr/linux-tests/linux_sigreturn_concurrency_smoke")

Invoke-Native "wsl" @("-d","Ubuntu-24.04","--cd",$Root,"--","gcc","-O2","-g",
    "-Wall","-Wextra","-Werror","-Wl,-z,max-page-size=0x1000",
    "userspace/linux_tests/linux_poll_fd_smoke.c","-o","usr/linux-tests/linux_poll_fd_smoke")

# Guest-side checker for the browser download tests (see download-test-server.py).
Invoke-Native "wsl" @("-d","Ubuntu-24.04","--cd",$Root,"--","gcc","-O2","-g",
    "-Wall","-Wextra","-Werror","-Wl,-z,max-page-size=0x1000",
    "userspace/linux_tests/download_verify.c","-o","usr/linux-tests/download_verify")

Invoke-Native $Clang @("-target","x86_64-linux-gnu","-O2","-nostdlib","-static",
    "-fno-stack-protector","-fno-pic","-mno-sse","-mno-sse2","-fuse-ld=lld",
    "-Wl,-z,max-page-size=0x1000","-Wl,--build-id=none",
    "-Wl,-T,userspace/linux_tests/linux_elf_protect_edges.ld",
    "userspace/linux_tests/linux_elf_protect_edges.c","-o","usr/linux-tests/linux_elf_protect_edges")

# Include the actual WPE callback; unused backend/GL symbols are discarded.
Invoke-Native "wsl" @("-d","Ubuntu-24.04","--cd",$Root,"--","bash","-c",
    'gcc -O2 -Werror -std=c11 -DVEX_HOSTED_BUILD -ffunction-sections -fdata-sections -Iuserspace/wpe_stub -Iuserspace/libvex/include -idirafter kernel/include $(pkg-config --cflags glib-2.0 gio-unix-2.0) userspace/linux_tests/wpe_renderer_fd_test.c -Wl,--gc-sections -Wl,-z,max-page-size=0x1000 -o usr/linux-tests/wpe_renderer_fd_test')

# Native ABI regression alongside foreign ABI tests; its ELF note selects Vex.
& (Join-Path $PSScriptRoot 'build-ora-test.ps1')

Invoke-Native $Clang @("-target","x86_64-linux-gnu","-O2","-nostdlib","-static",
    "-fno-stack-protector","-fno-builtin","-fuse-ld=lld","-Wl,-e,_start",
    "userspace/linux_tests/linux_user_validation_test.c",
    "-o","usr/linux-tests/linux_user_validation_test")
