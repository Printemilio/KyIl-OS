param([string]$VmName='Vex-OS', [int]$TimeoutSeconds=180)
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$taskVBox='C:/Program Files/Oracle/VirtualBox/VBoxManage.exe'
$taskLog=Join-Path $taskRoot 'build/vex-serial.log'
function Stop-TestVm {
    $taskInfo=& $taskVBox showvminfo $VmName --machinereadable 2>$null
    if($LASTEXITCODE){throw "Unknown VM: $VmName"}
    if(-not ($taskInfo -match '^cpus=1$')) {throw 'FlowSched validation requires a one-vCPU VM.'}
    if($taskInfo -match '^VMState="(running|paused)"$') {
        & $taskVBox controlvm $VmName poweroff | Out-Null
        if($LASTEXITCODE){throw 'Cannot release test ISO'}
    }
    if($taskInfo -match '^VMState="saved"$') {
        & $taskVBox discardstate $VmName | Out-Null
        if($LASTEXITCODE){throw 'Cannot discard stale test state'}
    }
}
Stop-TestVm
& (Join-Path $PSScriptRoot 'test-flowsched.ps1')
& (Join-Path $PSScriptRoot 'build-flowsched-test.ps1')
& (Join-Path $PSScriptRoot 'build.ps1') -SkipWebKit -BootMode FlowSched
& (Join-Path $PSScriptRoot 'run-virtualbox.ps1') -VmName $VmName -SkipBuild -Headless
try {
    $taskDeadline=(Get-Date).AddSeconds($TimeoutSeconds)
    while((Get-Date) -lt $taskDeadline) {
        $taskText=if(Test-Path -LiteralPath $taskLog){Get-Content -LiteralPath $taskLog -Raw}else{''}
        if($taskText -match 'flowsched-runtime: (load FAILED|FAILED)' -or
           $taskText -match 'FlowSched PI .*FAILED' -or $taskText -match 'KERNEL PANIC') {
            throw "FlowSched VM regression: $taskLog"
        }
        if($taskText -match 'flowsched-runtime: PASS') {
            if($taskText -notmatch 'FlowSched PI mutex chain: PASS' -or
               $taskText -notmatch 'FlowSched PI unmap/owner-exit/kill cycles: PASS') {
                throw 'Missing mandatory PI lifecycle results'
            }
            $taskText -split "`n" | Where-Object {$_ -match '^FlowSched (runtime|PI)'}
            Write-Output 'FlowSched VM: PASS'
            return
        }
        Start-Sleep -Milliseconds 500
    }
    throw "FlowSched VM timeout: $taskLog"
} finally {
    Stop-TestVm
    if(Test-Path -LiteralPath $taskLog) {
        Copy-Item -LiteralPath $taskLog -Destination (Join-Path $taskRoot 'build/flowsched-runtime-vm.log') -Force
    }
}
