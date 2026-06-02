param(
    [string]$VmName = "Vex-OS",
    [int]$TimeoutSeconds = 90,
    [switch]$SkipBuild
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

function Stop-VmIfRunning {
    param([string]$Name)
    $Running = & $VBoxManage list runningvms | Select-String "`"$Name`""
    if ($Running) {
        & $VBoxManage controlvm $Name poweroff | Out-Null
        Start-Sleep -Seconds 2
    }
}

try {
    Set-Content -LiteralPath $AutoSmokeMarker -Value "auto_smoke" -NoNewline

    if (-not $SkipBuild) {
        & (Join-Path $PSScriptRoot "build.ps1")
    }

    Stop-VmIfRunning -Name $VmName
    & (Join-Path $PSScriptRoot "run-virtualbox.ps1") -VmName $VmName -SkipBuild

    $Deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $FoundGpu = $false
    $FoundVk = $false
    $FoundQueued = $false

    while ((Get-Date) -lt $Deadline) {
        Start-Sleep -Milliseconds 500
        if (-not (Test-Path $SerialLog)) {
            continue
        }

        $Log = Get-Content -LiteralPath $SerialLog -Raw
        if ($Log -match "smoke: autorun queued") {
            $FoundQueued = $true
        }
        if ($Log -match "gpu_test: ok") {
            $FoundGpu = $true
        }
        if ($Log -match "vk_test: ok") {
            $FoundVk = $true
        }
        if ($FoundQueued -and $FoundGpu -and $FoundVk) {
            break
        }
    }

    if (-not $FoundQueued) {
        throw "Smoke autorun did not start."
    }
    if (-not $FoundGpu) {
        throw "GPU smoke test did not pass."
    }
    if (-not $FoundVk) {
        throw "Vulkan smoke test did not pass."
    }

    Write-Host "Smoke tests passed."
}
finally {
    Stop-VmIfRunning -Name $VmName
    if (Test-Path $AutoSmokeMarker) {
        Remove-Item -Force $AutoSmokeMarker
    }
}
