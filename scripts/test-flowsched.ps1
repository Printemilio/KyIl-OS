$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$taskInclude=Join-Path $taskRoot 'build/flowsched-test-include/unos'
New-Item -ItemType Directory -Force -Path $taskInclude | Out-Null
Copy-Item -LiteralPath (Join-Path $taskRoot 'kernel/include/unos/flowsched.h') -Destination $taskInclude -Force
$taskOutput=Join-Path $taskRoot 'build/flowsched-test.exe'
& 'C:/Program Files/LLVM/bin/clang.exe' -O2 -Wall -Wextra -I (Split-Path $taskInclude -Parent) (Join-Path $taskRoot 'tests/flowsched_test.c') (Join-Path $taskRoot 'kernel/src/flowsched.c') -o $taskOutput
if($LASTEXITCODE){throw 'FlowSched hosted tests compile failed'}
& $taskOutput
if($LASTEXITCODE){throw 'FlowSched hosted regression'}
