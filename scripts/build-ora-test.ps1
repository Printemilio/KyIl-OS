$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Push-Location $taskRoot
try {
    $taskDir='build/ora-native-test'
    New-Item -ItemType Directory -Force -Path $taskDir,'usr/linux-tests' | Out-Null
    $taskFlags=@('-target','x86_64-unknown-none-elf','-O2','-ffreestanding','-fno-builtin','-ffunction-sections','-fdata-sections','-fno-stack-protector','-fno-pic','-m64','-march=x86-64','-mabi=sysv','-mno-red-zone','-idirafter','kernel/include','-Iuserspace/libvex/include','-Ivendor/lvgl','-Ivendor/lvgl/src','-DLV_CONF_INCLUDE_SIMPLE')
    $taskSources=@('userspace/libvex/src/crt0.c','userspace/libvex/src/runtime.c','userspace/libvex/src/vex_posix_compat.c','userspace/apps/ora_native_abi_test.c')
    $taskObjects=@()
    foreach($taskSource in $taskSources) {
        $taskObject=Join-Path $taskDir (([IO.Path]::GetFileNameWithoutExtension($taskSource))+'.o')
        & 'C:/Program Files/LLVM/bin/clang.exe' @taskFlags -c $taskSource -o $taskObject
        if($LASTEXITCODE){throw "ORA native test compile failed: $taskSource"}
        $taskObjects+=$taskObject
    }
    & 'C:/Program Files/LLVM/bin/ld.lld.exe' -m elf_x86_64 -nostdlib -static --gc-sections -z max-page-size=0x1000 @taskObjects -o usr/linux-tests/ora_native_abi_test
    if($LASTEXITCODE){throw 'ORA native test link failed'}
    if(Test-Path build/iso_root/usr/linux-tests) {
        Copy-Item -LiteralPath usr/linux-tests/ora_native_abi_test -Destination build/iso_root/usr/linux-tests/ora_native_abi_test -Force
    }
} finally {Pop-Location}
