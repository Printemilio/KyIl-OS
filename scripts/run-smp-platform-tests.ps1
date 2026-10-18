param(
    [int[]]$CpuCounts = @(1,2,3,4),
    [string]$VmName = 'Vex-OS',
    [int]$TimeoutSeconds = 180,
    [switch]$SkipBuild
)
# Platform/context/lifetime tests, including ordinary automatic placement.
# This is not exhaustive certification of every syscall or future device driver.
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$taskVBox='C:/Program Files/Oracle/VirtualBox/VBoxManage.exe'
$taskLog=Join-Path $taskRoot 'build/vex-serial.log'
$taskOutput=Join-Path $taskRoot 'build/smp'
New-Item -ItemType Directory -Force -Path $taskOutput | Out-Null
function Invoke-PlatformVBox {
    & $taskVBox @args
    if($LASTEXITCODE){throw "VBox failed: $($args -join ' ')"}
}
function Stop-PlatformVm {
    $info=& $taskVBox showvminfo $VmName --machinereadable
    if($LASTEXITCODE){throw 'VM not found'}
    $state=($info | Select-String '^VMState=').Line
    if($state -match '"(running|paused|gurumeditation)"') {
        Invoke-PlatformVBox controlvm $VmName poweroff | Out-Null
        for($i=0;$i -lt 100;++$i) {
            $info=& $taskVBox showvminfo $VmName --machinereadable
            if($info -match '^VMState="poweroff"$'){return}
            Start-Sleep -Milliseconds 100
        }
        throw 'VM still owns ISO'
    }
    if($state -match '"saved"'){Invoke-PlatformVBox discardstate $VmName}
}
$taskInfo=& $taskVBox showvminfo $VmName --machinereadable
if($LASTEXITCODE){throw 'VM not found'}
$taskOriginalCpu=[int](($taskInfo | Select-String '^cpus=').Line -replace '^cpus=','')
try {
    Stop-PlatformVm
    if(!$SkipBuild){& (Join-Path $PSScriptRoot 'build.ps1') -BootMode SMP;if($LASTEXITCODE){throw 'Build failed'}}
    foreach($taskCount in $CpuCounts) {
        if($taskCount -lt 1){throw 'CPU counts must be positive'}
        Stop-PlatformVm
        & (Join-Path $PSScriptRoot 'run-virtualbox.ps1') -VmName $VmName -SkipBuild -Headless -CpuCount $taskCount
        if($LASTEXITCODE){throw 'VM launch failed'}
        $taskDeadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        $taskPass=$false
        do {
            Start-Sleep -Milliseconds 250
            $taskText=if(Test-Path $taskLog){Get-Content -Raw -LiteralPath $taskLog}else{''}
            if($taskText -match 'smp-platform: FAILED|smp-dispatch: FAILED|smp-stop: FAILED|smp-fd-borrow: FAILED|smp-memory-borrow: FAILED|smp-shared-space: FAILED|smp-usercopy: FAILED|smp-fd-offset: FAILED|smp-wait: FAILED|smp-lease: FAILED|smp-counter: FAILED|smp-publication: FAILED|smp-spawn: FAILED|smp-irq-memory: FAILED|KERNEL PANIC'){
                Copy-Item -LiteralPath $taskLog -Destination (Join-Path $taskOutput "platform-$taskCount-failed.log") -Force
                throw "Platform failure with $taskCount CPUs"
            }
            if($taskText -match "smp-platform: CPUs=$taskCount .* result=0 " -and $taskText -match 'smp-platform: PASS' -and $taskText -match 'smp-dispatch: PASS' -and $taskText -match 'smp-stop: .* PASS' -and $taskText -match 'smp-fd-borrow: .* PASS' -and $taskText -match 'smp-memory-borrow: .* PASS' -and $taskText -match 'smp-shared-space: .* PASS' -and $taskText -match 'smp-usercopy: .* PASS' -and $taskText -match 'smp-fd-offset: .* PASS' -and $taskText -match 'smp-lease: .* PASS' -and $taskText -match 'smp-spawn: .* PASS' -and $taskText -match 'smp-publication: .* PASS' -and $taskText -match 'smp-counter: simultaneous .* PASS' -and $taskText -match 'smp-counter: blocked reader .* PASS' -and $taskText -match 'smp-irq-memory: .* PASS' -and $taskText -match "smp-automatic: $taskCount ordinary shared-CR3 threads executed across $taskCount CPUs PASS" -and ($taskCount -eq 1 -or $taskText -match 'smp-wait: 2000 .* PASS')) {$taskPass=$true;break}
        } while([DateTime]::UtcNow -lt $taskDeadline)
        Copy-Item -LiteralPath $taskLog -Destination (Join-Path $taskOutput "platform-$taskCount.log") -Force
        if(!$taskPass){throw "Platform timeout with $taskCount CPUs"}
        ($taskText -split "`n" | Select-String 'smp-platform: CPUs=').Line
    }
} finally {
    Stop-PlatformVm
    Invoke-PlatformVBox modifyvm $VmName --cpus $taskOriginalCpu
}
