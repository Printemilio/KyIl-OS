$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
& wsl.exe -d Ubuntu-24.04 --cd $taskRoot -- gcc -O2 -pthread -DVEX_HOSTED_BUILD -Iuserspace/libvex/include -idirafter kernel/include '-Wl,-z,max-page-size=0x1000' userspace/linux_tests/flowsched_runtime_test.c -o usr/linux-tests/flowsched_runtime_test
if($LASTEXITCODE){throw 'FlowSched runtime build failed'}
Copy-Item -LiteralPath (Join-Path $taskRoot 'usr/linux-tests/flowsched_runtime_test') -Destination (Join-Path $taskRoot 'build/iso_root/usr/linux-tests/flowsched_runtime_test') -Force
