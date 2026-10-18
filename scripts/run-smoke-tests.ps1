param(
    [string]$VmName = "Vex-OS",
    [int]$TimeoutSeconds = 90,
    [switch]$SkipBuild,
    [switch]$WithWebKit
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BuildDir = Join-Path $Root "build"
$SerialLog = Join-Path $BuildDir "vex-serial.log"
$AutoSmokeMarker = Join-Path $Root "auto_smoke"
$VBoxManage = "VBoxManage"

if (-not (Get-Command $VBoxManage -ErrorAction SilentlyContinue)) {
    $DefaultVBoxManage = "C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
    if (Test-Path $DefaultVBoxManage) {
        $VBoxManage = $DefaultVBoxManage
    } else {
        throw "Required command 'VBoxManage' was not found in PATH."
    }
}

if ($SkipBuild) {
    throw "-SkipBuild cannot be used for smoke tests because the auto_smoke marker must be injected into a freshly built ISO."
}

function Stop-VmIfRunning {
    param([string]$Name)
    $Info = & $VBoxManage showvminfo $Name --machinereadable 2>$null
    if ($LASTEXITCODE -ne 0) {
        return
    }
    $StateLine = $Info | Select-String '^VMState=' | Select-Object -First 1
    $State = if ($StateLine) {
        $StateLine.Line -replace '^VMState="([^"]+)".*$', '$1'
    } else {
        $null
    }
    if ($State -eq 'running' -or $State -eq 'paused') {
        & $VBoxManage controlvm $Name poweroff | Out-Null
        for ($i = 0; $i -lt 50; $i++) {
            Start-Sleep -Milliseconds 200
            $Current = & $VBoxManage showvminfo $Name --machinereadable 2>$null |
                Select-String '^VMState=' | Select-Object -First 1
            if ($Current -and $Current.Line -match '^VMState="poweroff"') {
                break
            }
        }
    }
}

try {
    # Release the ISO before a build attempts to replace it. Previously this
    # happened only after the build, so a running/frozen VM could keep the ISO
    # open and make the global smoke test fail before booting anything.
    Stop-VmIfRunning -Name $VmName
    Set-Content -LiteralPath $AutoSmokeMarker -Value "auto_smoke" -NoNewline

    if (-not $SkipBuild) {
        if ($WithWebKit) {
            & (Join-Path $PSScriptRoot "build.ps1") -BootMode Smoke
        } else {
            & (Join-Path $PSScriptRoot "build-webkit-userspace.ps1") -Fast
            if ($LASTEXITCODE -ne 0) {
                throw "Smoke userspace build failed."
            }
            & (Join-Path $PSScriptRoot "build.ps1") -SkipWebKit -BootMode Smoke
        }
        if ($LASTEXITCODE -ne 0) {
            throw "Smoke build failed."
        }
    }

    Stop-VmIfRunning -Name $VmName
    & (Join-Path $PSScriptRoot "run-virtualbox.ps1") -VmName $VmName -SkipBuild

    $Deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $FoundGpu = $false
    $FoundVk = $false
    $FoundPe = $false
    $FoundLinuxElf = $false
    $FoundLinuxPie = $false
    $FoundLinuxCloneChild = $false
    $FoundLinuxClone = $false
    $FoundLinuxThreadContextChild = $false
    $FoundLinuxThreadContext = $false
    $FoundLinuxFutexWaiting = $false
    $FoundLinuxFutexWake = $false
    $FoundLinuxFutexAfterWake = $false
    $FoundLinuxFutex = $false
    $FoundLinuxFutexKill = $false
    $FoundLinuxFileMmap = $false
    $FoundLinuxWait4 = $false
    $FoundLinuxPipeExec = $false
    $FoundLinuxMemoryOwnership = $false
    $FoundLinuxUnsupported = $false
    $FoundLinuxSeqpacket = $false
    $FoundSecurityBoundary = $false
    $FoundNativeSyncFault = $false
    $FoundLinuxSyncFault = $false
    $FoundLinuxJitWx = $false
    $FoundUnhandledSyncFault = $false
    $FoundQueued = $false
    $FoundTtyRegionIdempotence = $false

    while ((Get-Date) -lt $Deadline) {
        Start-Sleep -Milliseconds 500
        if (-not (Test-Path $SerialLog)) {
            continue
        }

        $Log = Get-Content -LiteralPath $SerialLog -Raw
        if ($Log -match "smoke: autorun queued") {
            $FoundQueued = $true
        }
        if ($Log -match "smoke: repeated TTY region preserves cells/cursor ok") {
            $FoundTtyRegionIdempotence = $true
        }
        if ($Log -match "gpu_test: ok") {
            $FoundGpu = $true
        }
        if ($Log -match "vk_test: ok") {
            $FoundVk = $true
        }
        if ($Log -match "Vex PE probe: done") {
            $FoundPe = $true
        }
        if ($Log -match "smoke: Linux static ELF passed") {
            $FoundLinuxElf = $true
        }
        if ($Log -match "smoke: Linux static-PIE ELF passed") {
            $FoundLinuxPie = $true
        }
        if ($Log -match "Vex ELF clone smoke: child") {
            $FoundLinuxCloneChild = $true
        }
        if ($Log -match "Vex ELF clone smoke: done") {
            $FoundLinuxClone = $true
        }
        if ($Log -match "Vex ELF thread context smoke: child") {
            $FoundLinuxThreadContextChild = $true
        }
        if ($Log -match "Vex ELF thread context smoke: done") {
            $FoundLinuxThreadContext = $true
        }
        if ($Log -match "Vex ELF futex smoke: child waiting") {
            $FoundLinuxFutexWaiting = $true
        }
        if ($Log -match "Vex ELF futex smoke: parent wake") {
            $FoundLinuxFutexWake = $true
        }
        if ($Log -match "Vex ELF futex smoke: child after wake") {
            $FoundLinuxFutexAfterWake = $true
        }
        if ($Log -match "Vex ELF futex smoke: done") {
            $FoundLinuxFutex = $true
        }
        if ($Log -match "Vex ELF futex smoke: kill done") {
            $FoundLinuxFutexKill = $true
        }
        if ($Log -match "Vex ELF file-mmap smoke: done") {
            $FoundLinuxFileMmap = $true
        }
        if ($Log -match "Vex ELF wait4 smoke: exit=42") {
            $FoundLinuxWait4 = $true
        }
        if ($Log -match "Vex ELF pipe-exec smoke: payload\+exit ok") {
            $FoundLinuxPipeExec = $true
        }
        if ($Log -match "smoke: pipe/exec spawn contract ok") {
            $FoundLinuxPipeExec = $true
        }
        if ($Log -match "Vex ELF memory-ownership smoke: done") {
            $FoundLinuxMemoryOwnership = $true
        }
        if ($Log -match "Vex Linux virtual env smoke: /proc /dev statfs sysinfo ok") {
            $FoundLinuxUnsupported = $true
        }
        if ($Log -match "Vex ELF seqpacket smoke: done") {
            $FoundLinuxSeqpacket = $true
        }
        if ($Log -match "security_boundary: overall ok") {
            $FoundSecurityBoundary = $true
        }
        if ($Log -match "smoke: native synchronous fault-event ok") {
            $FoundNativeSyncFault = $true
        }
        if ($Log -match "smoke: Linux synchronous fault signals ok") {
            $FoundLinuxSyncFault = $true
        }
        if ($Log -match "Vex ELF JIT W_X smoke: mmap denied, RW to RX passed") {
            $FoundLinuxJitWx = $true
        }
        if ($Log -match "smoke: unhandled synchronous fault fatal cleanup ok") {
            $FoundUnhandledSyncFault = $true
        }
        if ($FoundQueued -and $FoundTtyRegionIdempotence -and $FoundGpu -and $FoundVk -and $FoundPe -and $FoundLinuxElf -and $FoundLinuxPie -and $FoundLinuxCloneChild -and $FoundLinuxClone -and $FoundLinuxThreadContextChild -and $FoundLinuxThreadContext -and $FoundLinuxFutexWaiting -and $FoundLinuxFutexWake -and $FoundLinuxFutexAfterWake -and $FoundLinuxFutex -and $FoundLinuxFutexKill -and $FoundLinuxFileMmap -and $FoundLinuxWait4 -and $FoundLinuxPipeExec -and $FoundLinuxMemoryOwnership -and $FoundLinuxUnsupported -and $FoundLinuxSeqpacket -and $FoundSecurityBoundary -and $FoundNativeSyncFault -and $FoundLinuxSyncFault -and $FoundLinuxJitWx -and $FoundUnhandledSyncFault) {
            break
        }
    }

    if (-not $FoundQueued) {
        throw "Smoke autorun did not start."
    }
    if (-not $FoundTtyRegionIdempotence) {
        throw "Repeated TTY region preservation smoke test did not pass."
    }
    if (-not $FoundGpu) {
        throw "GPU smoke test did not pass."
    }
    if (-not $FoundVk) {
        throw "Vulkan smoke test did not pass."
    }
    if (-not $FoundPe) {
        throw "PE smoke test did not pass."
    }
    if (-not $FoundLinuxElf) {
        throw "Linux ELF smoke test did not pass."
    }
    if (-not $FoundLinuxPie) {
        throw "Linux static-PIE ELF smoke test did not pass."
    }
    if (-not $FoundLinuxCloneChild) {
        throw "Linux clone child smoke marker was not observed."
    }
    if (-not $FoundLinuxClone) {
        throw "Linux clone smoke test did not pass."
    }
    if (-not $FoundLinuxThreadContextChild) {
        throw "Linux thread-context child marker was not observed."
    }
    if (-not $FoundLinuxThreadContext) {
        throw "Linux thread-context smoke test did not pass."
    }
    if (-not $FoundLinuxFutexWaiting) {
        throw "Linux futex child did not reach FUTEX_WAIT."
    }
    if (-not $FoundLinuxFutexWake) {
        throw "Linux futex parent wake marker was not observed."
    }
    if (-not $FoundLinuxFutexAfterWake) {
        throw "Linux futex child was not observed after wake."
    }
    if (-not $FoundLinuxFutex) {
        throw "Linux futex smoke test did not pass."
    }
    if (-not $FoundLinuxFutexKill) {
        throw "Linux futex kill smoke path did not pass."
    }
    if (-not $FoundLinuxFileMmap) {
        throw "Linux file-backed mmap smoke test did not pass."
    }
    if (-not $FoundLinuxWait4) {
        throw "Linux wait4 smoke test did not recover exit code 42."
    }
    if (-not $FoundLinuxPipeExec) {
        throw "Linux pipe/fork/dup2/execve smoke test did not pass."
    }
    if (-not $FoundLinuxMemoryOwnership) {
        throw "Linux memory-ownership smoke test did not pass."
    }
    if (-not $FoundLinuxUnsupported) {
        throw "Linux unsupported-syscall/virtual-env smoke test did not pass."
    }
    if (-not $FoundLinuxSeqpacket) {
        throw "Linux SEQPACKET smoke test did not pass."
    }
    if (-not $FoundSecurityBoundary) {
        throw "Security boundary test did not pass."
    }
    if (-not $FoundNativeSyncFault) {
        throw "Native synchronous fault-event smoke test did not pass."
    }
    if (-not $FoundLinuxSyncFault) {
        throw "Linux SIGSEGV/SIGBUS smoke test did not pass."
    }
    if (-not $FoundLinuxJitWx) {
        throw "Linux strict W^X JIT mmap/mprotect smoke test did not pass."
    }
    if (-not $FoundUnhandledSyncFault) {
        throw "Unhandled synchronous fault was not contained with the expected status."
    }

    Write-Host "Smoke tests passed."
}
finally {
    Stop-VmIfRunning -Name $VmName
    if (Test-Path $AutoSmokeMarker) {
        Remove-Item -Force $AutoSmokeMarker
    }
}
