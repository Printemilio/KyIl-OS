$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$taskInclude=Join-Path $taskRoot 'build/ora-test-include/unos'
New-Item -ItemType Directory -Force -Path $taskInclude | Out-Null
Copy-Item -LiteralPath (Join-Path $taskRoot 'kernel/include/unos/ora.h'),(Join-Path $taskRoot 'kernel/include/unos/kernel_allocator.h'),(Join-Path $taskRoot 'kernel/include/unos/versioned_store.h'),(Join-Path $taskRoot 'kernel/include/unos/named_content.h') -Destination $taskInclude -Force
$taskOutput=Join-Path $taskRoot 'build/ora-storage-test.exe'
& 'C:/Program Files/LLVM/bin/clang.exe' -O2 -Wall -Wextra -DORA_TESTING -I (Split-Path $taskInclude -Parent) (Join-Path $taskRoot 'tests/ora_storage_test.c') (Join-Path $taskRoot 'kernel/src/ora.c') (Join-Path $taskRoot 'kernel/src/versioned_store.c') (Join-Path $taskRoot 'kernel/src/named_content.c') -o $taskOutput
if($LASTEXITCODE){throw 'ORA hosted tests failed to compile'}
& $taskOutput
if($LASTEXITCODE){throw 'ORA hosted storage regression'}
