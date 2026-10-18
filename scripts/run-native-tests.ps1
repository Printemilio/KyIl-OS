param(
    [string]$VmName = "Vex-OS",
    [int]$TimeoutSeconds = 300,
    [ValidateRange(1,64)][int]$CpuCount = 4,
    [switch]$SkipUserspaceBuild,
    # The VexFS data disk: Keep reuses it across runs (persistence), Fresh
    # starts from an empty disk, V4 from a version 4 volume (migration),
    # None runs without a disk (everything in memory).
    [ValidateSet('Keep','Fresh','V4','None')][string]$Disk = 'Keep'
)

# Builds the Vex C library and its native test programs, boots an ISO in the
# auto_native_test mode and checks the results reported on the serial log.

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$SerialLog = Join-Path $Root "build/vex-serial.log"
$VBoxManage = "VBoxManage"
if (-not (Get-Command $VBoxManage -ErrorAction SilentlyContinue)) {
    $VBoxManage = "C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
}

function Stop-VmIfRunning([string]$Name) {
    $State = (& $VBoxManage showvminfo $Name --machinereadable 2>$null | Select-String '^VMState=' | Select-Object -First 1)
    if ($State -and ($State.Line -match '"(running|paused)"')) {
        & $VBoxManage controlvm $Name poweroff | Out-Null
        for ($i = 0; $i -lt 50; $i++) {
            Start-Sleep -Milliseconds 200
            $Now = & $VBoxManage showvminfo $Name --machinereadable 2>$null | Select-String '^VMState='
            if ($Now -and $Now.Line -match '"poweroff"') { break }
        }
    }
}

$DiskPath = Join-Path $Root "build/native-test-disk.vdi"

function Remove-TestDisk {
    if (Test-Path $DiskPath) {
        $Previous = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        & $VBoxManage closemedium disk $DiskPath --delete 2>&1 | Out-Null
        $ErrorActionPreference = $Previous
        if (Test-Path $DiskPath) { Remove-Item -LiteralPath $DiskPath -Force }
    }
}

function Detach-TestDisk {
    # Fails harmlessly when nothing is attached.
    $Previous = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $VBoxManage storageattach $VmName --storagectl "SATA" --port 1 --device 0 --medium none 2>&1 | Out-Null
    $ErrorActionPreference = $Previous
    $global:LASTEXITCODE = 0
}

function Invoke-Wsl([string]$Script) {
    $Unix = "/mnt/" + $Root.Substring(0,1).ToLower() + ($Root.Substring(2) -replace '\\','/')
    & wsl -d Ubuntu-24.04 -- bash -c "cd '$Unix' && $Script"
    if ($LASTEXITCODE -ne 0) { throw "WSL step failed: $Script" }
}

try {
    Stop-VmIfRunning $VmName
    if (-not $SkipUserspaceBuild) {
        Invoke-Wsl "bash userspace/libc/build.sh && bash userspace/libc/tests/build.sh >/dev/null"
        # OCaml programs, when the cross toolchain is installed (scripts/build-ocaml-toolchain.sh).
        Invoke-Wsl "if [ -x `$HOME/.local/vexos/ocaml-vexos/bin/ocamlopt.opt ]; then bash userspace/ocaml/tests/build.sh >/dev/null; fi"
    }
    & (Join-Path $PSScriptRoot "build.ps1") -SkipWebKit -BootMode Native -IsoName "vex-os-native.iso"
    if ($LASTEXITCODE -ne 0) { throw "ISO build failed." }

    Detach-TestDisk
    if ($Disk -eq 'Fresh' -or $Disk -eq 'V4') { Remove-TestDisk }
    if ($Disk -eq 'V4') {
        $Raw = Join-Path $Root "build/native-test-disk-v4.raw"
        $RawUnix = "build/native-test-disk-v4.raw"
        Invoke-Wsl "python3 scripts/make-vexfs-v4-image.py $RawUnix 256"
        & $VBoxManage convertfromraw $Raw $DiskPath --format VDI | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Could not convert the v4 image." }
        Remove-Item -LiteralPath $Raw -Force
    } elseif ($Disk -ne 'None' -and -not (Test-Path $DiskPath)) {
        & $VBoxManage createmedium disk --filename $DiskPath --size 256 --format VDI | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Could not create the test disk." }
    }
    $VmArgs = @{ VmName = $VmName; SkipBuild = $true; CpuCount = $CpuCount; IsoName = "vex-os-native.iso" }
    if ($Disk -ne 'None') { $VmArgs.DataDisk = $DiskPath }
    & (Join-Path $PSScriptRoot "run-virtualbox.ps1") @VmArgs

    $Deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $Done = $false
    while ((Get-Date) -lt $Deadline) {
        Start-Sleep -Milliseconds 500
        if (-not (Test-Path $SerialLog)) { continue }
        $Log = Get-Content -LiteralPath $SerialLog -Raw
        if ($Log -match "native-tests: runner exit=" -or $Log -match "native-tests: runner load FAILED" -or $Log -match "PANIC|panic:") {
            $Done = $true
            break
        }
    }
    $Lines = Get-Content -LiteralPath $SerialLog | Where-Object { $_ -match '^(native-tests|libc-|ocaml-|  )' -or $_ -match 'PANIC|panic:|Fatal error' }
    $Lines | ForEach-Object { Write-Host $_ }
    if (-not $Done) { throw "Native tests did not finish within $TimeoutSeconds s." }
    if (-not ($Log -match "native-tests: done, \d+ passed, 0 failed")) { throw "Native tests reported failures." }
    Write-Host "Native tests passed ($CpuCount CPUs)."
}
finally {
    Stop-VmIfRunning $VmName
    Detach-TestDisk
}
