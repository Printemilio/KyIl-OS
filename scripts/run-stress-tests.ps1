param(
    [string]$VmName = "Vex-OS-Stress",
    [int]$TimeoutSeconds = 180,
    [int]$MemoryMB = 6144,
    [switch]$WithWebKit
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BuildDir = Join-Path $Root "build"
$SerialLog = Join-Path $BuildDir "vex-serial.log"
$AutoStressMarker = Join-Path $Root "auto_stress"
$VBoxManage = "VBoxManage"

if (-not (Get-Command $VBoxManage -ErrorAction SilentlyContinue)) {
    $DefaultVBoxManage = "C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
    if (Test-Path $DefaultVBoxManage) {
        $VBoxManage = $DefaultVBoxManage
    } else {
        throw "Required command 'VBoxManage' was not found in PATH."
    }
}

function Stop-VmIfRunning {
    param([string]$Name)
    $Running = & $VBoxManage list runningvms | Select-String "`"$Name`""
    if ($Running) {
        & $VBoxManage controlvm $Name poweroff | Out-Null
        Start-Sleep -Seconds 2
    }
}

function Add-Marker {
    param(
        [hashtable]$Markers,
        [string]$Name,
        [object]$Found
    )

    if ([bool]$Found) {
        $Markers[$Name] = $true
    }
}

try {
    Set-Content -LiteralPath $AutoStressMarker -Value "auto_stress" -NoNewline

    if ($WithWebKit) {
        & (Join-Path $PSScriptRoot "build.ps1") -BootMode Stress
    } else {
        & (Join-Path $PSScriptRoot "build-webkit-userspace.ps1") -Fast
        if ($LASTEXITCODE -ne 0) {
            throw "Stress userspace build failed."
        }
        & (Join-Path $PSScriptRoot "build.ps1") -SkipWebKit -BootMode Stress
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Stress build failed."
    }

    Stop-VmIfRunning -Name $VmName
    & (Join-Path $PSScriptRoot "run-virtualbox.ps1") -VmName $VmName -SkipBuild -Headless -MemoryMB $MemoryMB
    if ($LASTEXITCODE -ne 0) {
        throw "VirtualBox stress VM failed to start."
    }

    $RequiredMarkers = @(
        "stress queued",
        "gpu",
        "vk",
        "pe",
        "linux elf",
        "linux pie",
        "linux clone child",
        "linux clone",
        "linux futex wait",
        "linux futex wake",
        "linux futex after wake",
        "linux futex",
        "linux file mmap",
        "linux wait4",
        "linux pipe exec",
        "mrac bench"
    )
    $Markers = @{}
    foreach ($Name in $RequiredMarkers) {
        $Markers[$Name] = $false
    }

    $Deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $Deadline) {
        Start-Sleep -Milliseconds 500
        if (-not (Test-Path $SerialLog)) {
            continue
        }

        $Log = [string](Get-Content -LiteralPath $SerialLog -Raw)
        Add-Marker $Markers "stress queued" ($Log -match "stress: autorun queued")
        Add-Marker $Markers "gpu" ($Log -match "gpu_test: ok")
        Add-Marker $Markers "vk" ($Log -match "vk_test: ok")
        Add-Marker $Markers "pe" ($Log -match "Vex PE probe: done")
        Add-Marker $Markers "linux elf" ($Log -match "Vex ELF smoke: done")
        Add-Marker $Markers "linux pie" ($Log -match "Vex ELF static-pie smoke: done")
        Add-Marker $Markers "linux clone child" ($Log -match "Vex ELF clone smoke: child")
        Add-Marker $Markers "linux clone" ($Log -match "Vex ELF clone smoke: done")
        Add-Marker $Markers "linux futex wait" ($Log -match "Vex ELF futex smoke: child waiting")
        Add-Marker $Markers "linux futex wake" ($Log -match "Vex ELF futex smoke: parent wake")
        Add-Marker $Markers "linux futex after wake" ($Log -match "Vex ELF futex smoke: child after wake")
        Add-Marker $Markers "linux futex" ($Log -match "Vex ELF futex smoke: done")
        Add-Marker $Markers "linux file mmap" ($Log -match "Vex ELF file-mmap smoke: done")
        Add-Marker $Markers "linux wait4" ($Log -match "Vex ELF wait4 smoke: exit=42")
        Add-Marker $Markers "linux pipe exec" ($Log -match "Vex ELF pipe-exec smoke: payload\+exit ok")
        Add-Marker $Markers "mrac bench" ($Log -match "mrac_bench: ok")

        $Missing = @($RequiredMarkers | Where-Object { -not $Markers[$_] })
        if ($Missing.Count -eq 0) {
            break
        }
    }

    $Missing = @($RequiredMarkers | Where-Object { -not $Markers[$_] })
    if ($Missing.Count -ne 0) {
        $Tail = ""
        if (Test-Path $SerialLog) {
            $Tail = (Get-Content -LiteralPath $SerialLog -Tail 80) -join "`n"
        }
        throw "Stress test timed out. Missing markers: $($Missing -join ', '). Serial tail:`n$Tail"
    }

    Write-Host "Stress tests passed with $MemoryMB MB RAM."
}
finally {
    Stop-VmIfRunning -Name $VmName
    if (Test-Path $AutoStressMarker) {
        Remove-Item -Force $AutoStressMarker
    }
}
