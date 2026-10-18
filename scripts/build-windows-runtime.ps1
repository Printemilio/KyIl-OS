$ErrorActionPreference='Stop'
$Root=(Resolve-Path "$PSScriptRoot/..").Path
$Out=Join-Path $Root 'build/windows'
$Stage=Join-Path $Root 'usr/pe-tests'
New-Item -ItemType Directory -Force $Out,$Stage,"$Root/usr/bin" | Out-Null
$Clang='C:/Program Files/LLVM/bin/clang.exe'
$Link='C:/Program Files/LLVM/bin/lld-link.exe'
$ElfLink='C:/Program Files/LLVM/bin/ld.lld.exe'
function Run([string]$Exe,[string[]]$Arguments) {
    & $Exe @Arguments
    if($LASTEXITCODE){throw "Failed: $Exe"}
}
$Objects=@()
Run 'python' @("$Root/scripts/generate-windows-export-census.py")
Run 'python' @("$Root/scripts/build-windows-provider-dlls.py")
foreach($Source in @('start.S','guard.S','fault_entry.S','capture_context.S','seh_entry.S','native.c','pe_image.c','loader.c','win32.c','rng.c','multimedia.c','advapi.c','security_data.c','memory_api.c','environment.c','environment_api.c','heap.c','load_config.c','synchronization.c','shell_api.c','slist.c','objects.c','registered_wait.c','completion.c','pipes.c','mapping.c','files.c','exceptions.c','unwind.c','seh.c','namespace.c','path_api.c','search.c','version.c','messages.c','console_api.c','console_control.c','profile_api.c','nt_core.c','registry.c','nls.c','encoding.c','system.c','calendar.c','process.c','job.c','wer.c')) {
    $Object="$Out/$Source.o";$Objects+=$Object
    Run $Clang @('-target','x86_64-unknown-none-elf','-march=x86-64','-mno-avx','-ffreestanding',
        '-fno-builtin','-fno-stack-protector','-fno-pie','-mno-red-zone','-O2','-g','-Wall','-Wextra',
        '-I',"$Root/kernel/include",'-I',"$Root/userspace/libvex/include",'-c',"$Root/userspace/windows/$Source",'-o',$Object)
}
Run $ElfLink (@('-static','-z','noexecstack','-e','_start','-Ttext=0x20000000','-o',"$Root/usr/bin/kyil-windows")+$Objects)
Run $Clang @('-target','x86_64-unknown-none-elf','-march=x86-64','-mno-avx','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-pie','-mno-red-zone','-O2','-g','-Wall','-Wextra','-I',"$Root/kernel/include",'-I',"$Root/userspace/libvex/include",'-c',"$Root/userspace/windows/pipe_service.c",'-o',"$Out/pipe-service.o")
Run $ElfLink @('-static','-z','noexecstack','-e','_start','-Ttext=0x20000000','-o',"$Root/usr/lib/kyil-windows/pipe-service","$Out/start.S.o","$Out/native.c.o","$Out/pipe-service.o")

Run $Clang @('-target','x86_64-unknown-none-elf','-march=x86-64','-mno-avx','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-pie','-mno-red-zone','-O2','-g','-Wall','-Wextra','-I',"$Root/kernel/include",'-I',"$Root/userspace/libvex/include",'-c',"$Root/userspace/windows/event_service.c",'-o',"$Out/event-service.o")
Run $ElfLink @('-static','-z','noexecstack','-e','_start','-Ttext=0x20000000','-o',"$Root/usr/lib/kyil-windows/event-service","$Out/start.S.o","$Out/native.c.o","$Out/event-service.o")
Run $Clang @('-target','x86_64-unknown-none-elf','-march=x86-64','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-pie','-O2',
    '-I',"$Root/kernel/include",'-I',"$Root/userspace/libvex/include",'-c',"$Root/userspace/windows/tests/runner.c",'-o',"$Out/runner.o")
New-Item -ItemType Directory -Force "$Root/usr/native-tests" | Out-Null
Run $ElfLink @('-static','-z','noexecstack','-e','_start','-Ttext=0x20000000','-o',"$Root/usr/native-tests/windows_runtime_test", "$Out/start.S.o","$Out/native.c.o","$Out/runner.o")
Run $Clang @('-target','x86_64-unknown-none-elf','-march=x86-64','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-pie','-O2',
    '-I',"$Root/kernel/include",'-I',"$Root/userspace/libvex/include",'-c',"$Root/userspace/windows/tests/native_context.c",'-o',"$Out/native_context.o")
Run $ElfLink @('-static','-z','noexecstack','-e','_start','-Ttext=0x20000000','-o',"$Root/usr/native-tests/native_context_test", "$Out/start.S.o","$Out/native.c.o","$Out/native_context.o")
Run $Clang @('-target','x86_64-unknown-none-elf','-march=x86-64','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-pie','-O2',
    '-I',"$Root/kernel/include",'-I',"$Root/userspace/libvex/include",'-c',"$Root/userspace/windows/tests/native_fs_roots.c",'-o',"$Out/native_fs_roots.o")
Run $ElfLink @('-static','-z','noexecstack','-e','_start','-Ttext=0x20000000','-o',"$Root/usr/native-tests/native_fs_roots_test", "$Out/start.S.o","$Out/native.c.o","$Out/native_fs_roots.o")

foreach($NativeFixture in @('ultra_service','ultra_space')) {
    Run $Clang @('-target','x86_64-unknown-none-elf','-march=x86-64','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-pie','-mno-red-zone','-O2','-g','-Ikernel/include','-Iuserspace/libvex/include','-c',"$Root/userspace/windows/tests/$NativeFixture.c",'-o',"$Out/$NativeFixture.o")
    Run $ElfLink @('-static','-z','noexecstack','-e','_start','-Ttext=0x20000000','-o',"$Root/usr/native-tests/$NativeFixture", "$Out/start.S.o","$Out/native.c.o","$Out/$NativeFixture.o")
}
Run $Link @('/dll','/noentry','/machine:x64',"/def:$Root/userspace/windows/tests/kernel32.def", "/out:$Out/kernel32.dll", "/implib:$Out/kernel32.lib")
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/thread_control.c",'-o',"$Out/thread_control.obj")
Run $Clang @('-target','x86_64-pc-windows-msvc','-c',"$Root/userspace/windows/tests/thread_control.S",'-o',"$Out/thread_control_asm.obj")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/thread_control.obj","$Out/thread_control_asm.obj","$Out/kernel32.lib","/out:$Stage/windows-thread-control.exe")
Run $Clang @('-target','x86_64-unknown-none-elf','-march=x86-64','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-pie','-O2','-Ikernel/include','-Iuserspace/libvex/include','-c',"$Root/userspace/windows/tests/native_thread_control.c",'-o',"$Out/native_thread_control.o")
Run $ElfLink @('-static','-z','noexecstack','-e','_start','-Ttext=0x20000000','-o',"$Root/usr/native-tests/native_thread_control_test","$Out/start.S.o","$Out/native.c.o","$Out/native_thread_control.o")
foreach($Source in @('library','smoke')) {
    Run $Clang @('-target','x86_64-pc-windows-msvc','-march=x86-64','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/$Source.c",'-o',"$Out/$Source.obj")
}
Run $Link @('/dll','/entry:DllMain','/nodefaultlib','/machine:x64','/base:0x140000000','/dynamicbase',
    "$Out/library.obj","$Out/kernel32.lib","/out:$Stage/kyil-test.dll","/implib:$Out/kyil-test.lib")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64','/base:0x140000000','/dynamicbase',
    "$Out/smoke.obj","$Out/kernel32.lib","$Out/kyil-test.lib","/out:$Stage/windows-native-smoke.exe")
$Bytes=[IO.File]::ReadAllBytes("$Stage/windows-native-smoke.exe")
$Pe=[BitConverter]::ToUInt32($Bytes,60);$Opt=$Pe+24
$Section=$Opt+[BitConverter]::ToUInt16($Bytes,$Pe+20)
$Rwx=$Bytes.Clone();[BitConverter]::GetBytes([Convert]::ToUInt32('e0000020',16)).CopyTo($Rwx,$Section+36)
[IO.File]::WriteAllBytes("$Stage/windows-rwx-rejected.exe",$Rwx)
$Tls=$Bytes.Clone();[BitConverter]::GetBytes([uint32]4096).CopyTo($Tls,$Opt+112+9*8)
[BitConverter]::GetBytes([uint32]40).CopyTo($Tls,$Opt+116+9*8)
[IO.File]::WriteAllBytes("$Stage/windows-tls-rejected.exe",$Tls)
$Bad=$Bytes.Clone();[BitConverter]::GetBytes([Convert]::ToUInt32('fffffff0',16)).CopyTo($Bad,60)
[IO.File]::WriteAllBytes("$Stage/windows-bad-header.exe",$Bad)

foreach($TlsSource in @('tls_library','tls_smoke')) {
    Run $Clang @('-target','x86_64-pc-windows-msvc','-march=x86-64','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/$TlsSource.c",'-o',"$Out/$TlsSource.obj")
}
Run $Clang @('-target','x86_64-pc-windows-msvc','-march=x86-64','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g',
    '-DTLS_VALUE=201','-DTLS_SECOND=1','-c',"$Root/userspace/windows/tests/tls_library.c",'-o',"$Out/tls_second.obj")
Run $Link @('/dll','/entry:DllMain','/nodefaultlib','/machine:x64','/base:0x140000000','/dynamicbase','/include:_tls_used',
    "$Out/tls_library.obj","$Out/kernel32.lib","/out:$Stage/kyil-tls-one.dll","/implib:$Out/tls_one.lib")
Run $Link @('/dll','/entry:DllMain','/nodefaultlib','/machine:x64','/base:0x140000000','/dynamicbase','/include:_tls_used',
    "$Out/tls_second.obj","$Out/kernel32.lib","/out:$Stage/kyil-tls-two.dll","/implib:$Out/tls_two.lib")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64','/base:0x140000000','/dynamicbase','/include:_tls_used',
    "$Out/tls_smoke.obj","$Out/kernel32.lib","$Out/tls_one.lib","$Out/tls_two.lib","/out:$Stage/windows-tls-smoke.exe")
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/cfg_smoke.c",'-o',"$Out/cfg_smoke.obj")
foreach($CfgVariant in @('good','bad')) {
    $Defines=@();if($CfgVariant -eq 'bad'){$Defines=@('-DCFG_BAD=1')}
    Run $Clang (@('-target','x86_64-pc-windows-msvc','-c',"$Root/userspace/windows/tests/cfg_call.S",'-o',"$Out/cfg_$CfgVariant.obj")+$Defines)
    Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64','/include:_load_config_used',
        "$Out/cfg_smoke.obj","$Out/cfg_$CfgVariant.obj","$Out/kernel32.lib","/out:$Stage/windows-cfg-$CfgVariant.exe")
}
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/veh_smoke.c",'-o',"$Out/veh_smoke.obj")
Run $Clang @('-target','x86_64-pc-windows-msvc','-c',"$Root/userspace/windows/tests/veh_fault.S",'-o',"$Out/veh_fault.obj")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/veh_smoke.obj","$Out/veh_fault.obj","$Out/kernel32.lib","/out:$Stage/windows-veh-smoke.exe")
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/namespace_events.c",'-o',"$Out/namespace_events.obj")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/namespace_events.obj","$Out/kernel32.lib","/out:$Stage/windows-namespace-events.exe")
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/exit_race.c",'-o',"$Out/exit_race.obj")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/exit_race.obj","$Out/kernel32.lib","$Out/kyil-test.lib","/out:$Stage/windows-exit-race.exe")
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g',
    '-I',"$Root/kernel/include",'-I',"$Root/userspace/libvex/include",'-c',"$Root/userspace/windows/tests/file_smoke.c",'-o',"$Out/file_smoke.obj")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/file_smoke.obj","$Out/kernel32.lib","/out:$Stage/windows-file-smoke.exe")
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/completion_smoke.c",'-o',"$Out/completion_smoke.obj")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/completion_smoke.obj","$Out/kernel32.lib","/out:$Stage/windows-iocp-smoke.exe")
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-DIOCP_EXIT_RACE','-c',"$Root/userspace/windows/tests/completion_smoke.c",'-o',"$Out/completion_exit.obj")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/completion_exit.obj","$Out/kernel32.lib","/out:$Stage/windows-iocp-exit.exe")
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/mutex_critical.c",'-o',"$Out/mutex_critical.obj")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/mutex_critical.obj","$Out/kernel32.lib","/out:$Stage/windows-mutex-critical.exe")
Run $Clang @('-target','x86_64-unknown-none-elf','-march=x86-64','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-pie','-O2',
    '-I',"$Root/kernel/include",'-I',"$Root/userspace/libvex/include",'-c',"$Root/userspace/windows/tests/native_execution.c",'-o',"$Out/native_execution.o")
Run $ElfLink @('-static','-z','noexecstack','-e','_start','-Ttext=0x20000000','-o',"$Root/usr/native-tests/native_execution_test","$Out/start.S.o","$Out/native.c.o","$Out/native_execution.o")
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/overlapped_smoke.c",'-o',"$Out/overlapped_smoke.obj")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/overlapped_smoke.obj","$Out/kernel32.lib","/out:$Stage/windows-overlapped-smoke.exe")
Run $Clang @('-target','x86_64-unknown-none-elf','-march=x86-64','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-pie','-O2',
    '-I',"$Root/kernel/include",'-I',"$Root/userspace/libvex/include",'-c',"$Root/userspace/windows/tests/native_rendezvous.c",'-o',"$Out/native_rendezvous.o")
Run $ElfLink @('-static','-z','noexecstack','-e','_start','-Ttext=0x20000000','-o',"$Root/usr/native-tests/native_rendezvous_test","$Out/start.S.o","$Out/native.c.o","$Out/native_rendezvous.o")
foreach($Variant in @('server','client')) {
    $Defines=@();if($Variant -eq 'client'){$Defines=@('-DPIPE_CLIENT')}
    Run $Clang (@('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/named_pipe_smoke.c",'-o',"$Out/pipe_$Variant.obj")+$Defines)
    Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/pipe_$Variant.obj","$Out/kernel32.lib","/out:$Stage/windows-pipe-$Variant.exe")
}
foreach($Variant in @('parent','child')) {
    $Defines=@();if($Variant -eq 'child'){$Defines=@('-DPROCESS_CHILD')}
    Run $Clang (@('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/process_smoke.c",'-o',"$Out/process_$Variant.obj")+$Defines)
    Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/process_$Variant.obj","$Out/kernel32.lib","/out:$Stage/windows-process-$Variant.exe")
}
Run $Clang @('-target','x86_64-unknown-none-elf','-march=x86-64','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-pie','-O2',
    '-I',"$Root/kernel/include",'-I',"$Root/userspace/libvex/include",'-c',"$Root/userspace/windows/tests/native_process_resource.c",'-o',"$Out/native_process_resource.o")
Run $ElfLink @('-static','-z','noexecstack','-e','_start','-Ttext=0x20000000','-o',"$Root/usr/native-tests/native_process_resource_test","$Out/start.S.o","$Out/native.c.o","$Out/native_process_resource.o")
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/module_smoke.c",'-o',"$Out/module_smoke.obj")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/module_smoke.obj","$Out/kernel32.lib","/out:$Stage/windows-module-smoke.exe")
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/encoding_smoke.c",'-o',"$Out/encoding_smoke.obj")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/encoding_smoke.obj","$Out/kernel32.lib","/out:$Stage/windows-encoding-smoke.exe")
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/platform_smoke.c",'-o',"$Out/platform_smoke.obj")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/platform_smoke.obj","$Out/kernel32.lib","/out:$Stage/windows-platform-smoke.exe")
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/condition_once_smoke.c",'-o',"$Out/condition_once_smoke.obj")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/condition_once_smoke.obj","$Out/kernel32.lib","/out:$Stage/windows-condition-once-smoke.exe")
Run $Link @('/dll','/noentry','/machine:x64',"/def:$Root/userspace/windows/tests/version.def","/out:$Out/version.dll","/implib:$Out/version.lib")
Run 'C:/Program Files/LLVM/bin/llvm-rc.exe' @('/fo',"$Out/version_fixture.res","$Root/userspace/windows/tests/version_fixture.rc")
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/version_smoke.c",'-o',"$Out/version_smoke.obj")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/version_smoke.obj","$Out/version_fixture.res","$Out/kernel32.lib","$Out/version.lib","/out:$Stage/windows-version-smoke.exe")
Run $Link @('/dll','/noentry','/machine:x64',"/def:$Root/userspace/windows/tests/ntdll.def","/out:$Out/ntdll.dll","/implib:$Out/ntdll.lib")
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/nt_core_smoke.c",'-o',"$Out/nt_core_smoke.obj")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/nt_core_smoke.obj","$Out/kernel32.lib","$Out/ntdll.lib","/out:$Stage/windows-nt-core-smoke.exe")
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-DKYIL_NATIVE_TEST','-c',"$Root/userspace/windows/tests/inspection_smoke.c",'-o',"$Out/inspection_smoke.obj")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/inspection_smoke.obj","$Out/kernel32.lib","$Out/ntdll.lib","/out:$Stage/windows-inspection-smoke.exe")
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-Ikernel/include','-Iuserspace/libvex/include','-c',"$Root/userspace/windows/tests/memory_query_smoke.c",'-o',"$Out/memory_query_smoke.obj")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/memory_query_smoke.obj","$Out/kernel32.lib","/out:$Stage/windows-memory-query-smoke.exe")
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-funwind-tables','-c',"$Root/userspace/windows/tests/unwind_smoke.c",'-o',"$Out/unwind_smoke.obj")
Run $Clang @('-target','x86_64-pc-windows-msvc','-c',"$Root/userspace/windows/tests/unwind_frame.S",'-o',"$Out/unwind_frame.obj")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/unwind_smoke.obj","$Out/unwind_frame.obj","$Out/kernel32.lib","/out:$Stage/windows-unwind-smoke.exe")
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/heap_walk_smoke.c",'-o',"$Out/heap_walk_smoke.obj")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/heap_walk_smoke.obj","$Out/kernel32.lib","/out:$Stage/windows-heap-walk-smoke.exe")
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/fls_smoke.c",'-o',"$Out/fls_smoke.obj")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/fls_smoke.obj","$Out/kernel32.lib","/out:$Stage/windows-fls-smoke.exe")




foreach($Mode in @('parent','worker','leaf')) {
    $Defines=@('-DKYIL_NATIVE_TEST');if($Mode -eq 'worker'){$Defines+='-DJOB_WORKER'};if($Mode -eq 'leaf'){$Defines+='-DJOB_LEAF'}
    Run $Clang (@('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/job_smoke.c",'-o',"$Out/job_$Mode.obj")+$Defines)
    Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/job_$Mode.obj","$Out/kernel32.lib","/out:$Stage/windows-job-$Mode.exe")
}
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-Ikernel/include','-Iuserspace/libvex/include','-c',"$Root/userspace/windows/tests/wer_provider.c",'-o',"$Out/wer_provider.obj")
Run $Link @('/dll','/entry:DllMain','/nodefaultlib','/machine:x64',"$Out/wer_provider.obj","$Out/kernel32.lib","/out:$Stage/kyil-wer-provider.dll")
foreach($Mode in @('parent','child')) {
    $Defines=@();if($Mode -eq 'child'){$Defines+='-DWER_CHILD'}
    Run $Clang (@('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/wer_smoke.c",'-o',"$Out/wer_$Mode.obj")+$Defines)
    Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/wer_$Mode.obj","$Out/kernel32.lib","$Out/ntdll.lib","/out:$Stage/windows-wer-$Mode.exe")
}
foreach($Mode in @('parent','child')) {
    $Defines=@('-DKYIL_NATIVE_TEST');if($Mode -eq 'child'){$Defines+='-DCWD_CHILD'}
    Run $Clang (@('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/cwd_smoke.c",'-o',"$Out/cwd_$Mode.obj")+$Defines)
    Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/cwd_$Mode.obj","$Out/kernel32.lib","/out:$Stage/windows-cwd-$Mode.exe")
}
foreach($Fixture in @('file_batch','profile','contract_batch','async_contract','system_contract','file_lock','chrome_contract','private_heap','multiple_wait','namespace_mutation','affinity','memory_usage','process_memory_counter','rng','virtual_memory','multimedia','registry_win32','loader_lock','security_data','protected_pipe','startup_contract','handle_value','shared_event')) {
    Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-DKYIL_NATIVE_TEST','-Ikernel/include','-Iuserspace/libvex/include','-c',"$Root/userspace/windows/tests/${Fixture}_smoke.c",'-o',"$Out/${Fixture}_smoke.obj")
    Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/${Fixture}_smoke.obj","$Out/kernel32.lib","/out:$Stage/windows-$Fixture-smoke.exe")
}
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-DHEAP_CORRUPT','-c',"$Root/userspace/windows/tests/private_heap_smoke.c",'-o',"$Out/heap_corruption.obj")
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-DWAIT_EXIT','-c',"$Root/userspace/windows/tests/multiple_wait_smoke.c",'-o',"$Out/multiple_wait_exit.obj")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/multiple_wait_exit.obj","$Out/kernel32.lib","/out:$Stage/windows-multiple-wait-exit.exe")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/heap_corruption.obj","$Out/kernel32.lib","/out:$Stage/windows-heap-corruption.exe")
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-funwind-tables','-fexceptions','-fasync-exceptions','-c',"$Root/userspace/windows/tests/seh_smoke.c",'-o',"$Out/seh_smoke.obj")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/seh_smoke.obj","$Out/kernel32.lib","$Out/ntdll.lib","/out:$Stage/windows-seh-smoke.exe")
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/slist_smoke.c",'-o',"$Out/slist_smoke.obj")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/slist_smoke.obj","$Out/kernel32.lib","/out:$Stage/windows-slist-smoke.exe")
foreach($Mode in @('parent','child')) {
    $Defines=@();if($Mode -eq 'child'){$Defines=@('-DREGISTRY_CHILD')}
    Run $Clang (@('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/registry_smoke.c",'-o',"$Out/registry_$Mode.obj")+$Defines)
    Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/registry_$Mode.obj","$Out/kernel32.lib","$Out/ntdll.lib","/out:$Stage/windows-registry-$Mode.exe")
}
Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-DREGISTRY_READBACK','-c',"$Root/userspace/windows/tests/registry_smoke.c",'-o',"$Out/registry_readback.obj")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/registry_readback.obj","$Out/kernel32.lib","$Out/ntdll.lib","/out:$Stage/windows-registry-readback.exe")

foreach($Mode in @('parent','child')) {
    $Defines=@();if($Mode -eq 'child'){$Defines+='-DTIMES_CHILD'}
    Run $Clang (@('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/process_times_smoke.c",'-o',"$Out/times_$Mode.obj")+$Defines)
    Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/times_$Mode.obj","$Out/kernel32.lib","/out:$Stage/windows-times-$Mode.exe")
}

foreach($Mode in @('parent','low','high','middle')) {
    $Defines=@();if($Mode -eq 'low'){$Defines+='-DORDER_LEVEL=0x200'};if($Mode -eq 'high'){$Defines+='-DORDER_LEVEL=0x300'};if($Mode -eq 'middle'){$Defines+='-DORDER_LEVEL=0x280'}
    Run $Clang (@('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/shutdown_order_smoke.c",'-o',"$Out/order_$Mode.obj")+$Defines)
    Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/order_$Mode.obj","$Out/kernel32.lib","/out:$Stage/windows-order-$Mode.exe")
}



Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-DAFFINITY_EXIT','-c',"$Root/userspace/windows/tests/affinity_smoke.c",'-o',"$Out/affinity_exit.obj")
Run $Link @('/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x64',"$Out/affinity_exit.obj","$Out/kernel32.lib","/out:$Stage/windows-affinity-exit.exe")

Run $Clang @('-target','x86_64-pc-windows-msvc','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-c',"$Root/userspace/windows/tests/loader_lock_probe.c",'-o',"$Out/loader_lock_probe.obj")
Run $Link @('/dll','/entry:DllMain','/nodefaultlib','/machine:x64',"$Out/loader_lock_probe.obj","$Out/kernel32.lib","/out:$Stage/kyil-loader-lock-probe.dll")
Write-Host 'Built native userspace runtime and direct PE/DLL smoke (no conversion).'
