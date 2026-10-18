# Launch the latest built Vex-OS ISO in the existing VirtualBox VM (4 CPUs, normal GUI).
# This script never builds: run scripts/build.ps1 first. Use -Headless for automated tests.
param(
[string]$VmName = "Vex-OS",
# Kept so existing test scripts keep working; this launcher never builds anyway.
[switch]$SkipBuild,
[switch]$Headless,
[int]$MemoryMB = 4096,
[ValidateRange(1,64)][int]$CpuCount = 4,
# A hard disk image attached to SATA port 1, where VexFS keeps durable files.
[string]$DataDisk,
[string]$IsoName = "vex-os.iso"
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$IsoPath = Join-Path $Root (Join-Path "build" $IsoName)
$SerialLog = Join-Path $Root "build/vex-serial.log"
$VBoxManage = "VBoxManage"

if (-not (Get-Command $VBoxManage -ErrorAction SilentlyContinue)) {
    $DefaultVBoxManage = "C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
    if (Test-Path $DefaultVBoxManage) {
        $VBoxManage = $DefaultVBoxManage
    } else {
        throw "Required command 'VBoxManage' was not found in PATH."
    }
}

function Invoke-VBox {
    # VBoxManage reports progress ("0%...100%") on stderr; only its exit code matters.
    $PreviousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $VBoxManage @args 2>&1 | ForEach-Object { "$_" } | Write-Host
    $ExitCode = $LASTEXITCODE
    $ErrorActionPreference = $PreviousErrorActionPreference
    if ($ExitCode -ne 0) {
        throw "VBoxManage failed: $($args -join ' ')"
    }
}

function Get-VMState {
    $Info = & $VBoxManage showvminfo $VmName --machinereadable 2>$null
    if ($LASTEXITCODE -ne 0) {
        return $null
    }

    $StateLine = $Info | Select-String '^VMState=' | Select-Object -First 1
    if (-not $StateLine) {
        return $null
    }

    return ($StateLine.Line -replace '^VMState="([^"]+)".*$', '$1')
}

if (-not (Test-Path -LiteralPath $IsoPath -PathType Leaf)) {
    throw "ISO not found at '$IsoPath'. Build it first with scripts\build.ps1."
}
Write-Host "Using ISO: $IsoPath ($((Get-Item -LiteralPath $IsoPath).LastWriteTime))"

$ExistingVm = & $VBoxManage list vms | Select-String "`"$VmName`""
if (-not $ExistingVm) {
    Invoke-VBox createvm --name $VmName --ostype Other_64 --register | Out-Null
    Invoke-VBox modifyvm $VmName --rtcuseutc on --memory $MemoryMB --vram 128 --firmware efi --graphicscontroller vmsvga --audio-enabled on --audio-controller hda --audio-in on --audio-out on --cpus $CpuCount --nic1 nat --nictype1 82540EM --cableconnected1 on --boot1 dvd --boot2 none --boot3 none --boot4 none
} else {
    $VmState = Get-VMState
    if ($VmState -eq "saved") {
        Write-Host "Discarding saved state for VM '$VmName' so the latest ISO can be attached..."
        Invoke-VBox discardstate $VmName
    } elseif ($VmState -in @("running","paused","gurumeditation")) {
        # The VM must be stopped to change its CPU count and swap the ISO. A frozen
        # guest also looks "running" to VirtualBox, so restart it instead of failing.
        Write-Host "VM '$VmName' is already $VmState; powering it off to start the latest ISO..."
        Invoke-VBox controlvm $VmName poweroff
        for ($i = 0; $i -lt 50 -and (Get-VMState) -ne "poweroff"; $i++) {
            Start-Sleep -Milliseconds 200
        }
        if ((Get-VMState) -ne "poweroff") { throw "VM '$VmName' did not power off." }
        Start-Sleep -Seconds 2   # let VirtualBox release the VM session lock
    }

    Invoke-VBox modifyvm $VmName --memory $MemoryMB --vram 128 --firmware efi --graphicscontroller vmsvga --audio-enabled on --audio-controller hda --audio-in on --audio-out on --nic1 nat --nictype1 82540EM --cableconnected1 on
}

function Test-StorageController {
    param([string]$Name)
    $Info = & $VBoxManage showvminfo $VmName --machinereadable
    return [bool]($Info | Select-String "storagecontrollername.*=`"$Name`"")
}

if (-not (Test-StorageController -Name "SATA")) {
    Invoke-VBox storagectl $VmName --name "SATA" --add sata --controller IntelAhci --portcount 4 --bootable on
}

if (Test-StorageController -Name "IDE") {
    $PreviousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $VBoxManage storageattach $VmName `
        --storagectl "IDE" `
        --port 0 `
        --device 0 `
        --type dvddrive `
        --medium none 2>$null | Out-Null
    $ErrorActionPreference = $PreviousErrorActionPreference
    $global:LASTEXITCODE = 0
}

Invoke-VBox modifyvm $VmName --cpus $CpuCount
if (Test-Path $SerialLog) {
    Remove-Item -LiteralPath $SerialLog -Force
}
Invoke-VBox modifyvm $VmName --uart1 0x3F8 4 --uartmode1 file $SerialLog

Invoke-VBox setextradata $VmName CustomVideoMode1 1920x1080x32
Invoke-VBox setextradata $VmName VBoxInternal2/EfiGraphicsResolution 1920x1080
Invoke-VBox setextradata global GUI/MaxGuestResolution any

Invoke-VBox storageattach $VmName `
    --storagectl "SATA" `
    --port 0 `
    --device 0 `
    --type dvddrive `
    --medium $IsoPath

if ($DataDisk) {
    Invoke-VBox storageattach $VmName `
        --storagectl "SATA" `
        --port 1 `
        --device 0 `
        --type hdd `
        --medium $DataDisk
}

if ($Headless) {
    Invoke-VBox startvm $VmName --type headless
} else {
    Invoke-VBox startvm $VmName
}
Start-Sleep -Seconds 1
Invoke-VBox controlvm $VmName setvideomodehint 1920 1080 32 0
