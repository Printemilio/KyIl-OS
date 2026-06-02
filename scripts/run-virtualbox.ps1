param(
[string]$VmName = "Vex-OS",
[switch]$SkipBuild,
[switch]$Headless
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$IsoPath = Join-Path $Root "build/vex-os.iso"
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

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build.ps1")
} elseif (-not (Test-Path $IsoPath)) {
    throw "ISO not found at '$IsoPath'. Run without -SkipBuild once to build it."
}

$ExistingVm = & $VBoxManage list vms | Select-String "`"$VmName`""
if (-not $ExistingVm) {
    & $VBoxManage createvm --name $VmName --ostype Other_64 --register | Out-Null
    & $VBoxManage modifyvm $VmName --memory 2048 --vram 128 --firmware efi --graphicscontroller vmsvga --audio-enabled on --audio-controller hda --audio-in on --audio-out on --nic1 nat --nictype1 82540EM --cableconnected1 on --boot1 dvd --boot2 none --boot3 none --boot4 none
    & $VBoxManage storagectl $VmName --name "IDE" --add ide --controller PIIX4
} else {
    & $VBoxManage modifyvm $VmName --memory 2048 --vram 128 --firmware efi --graphicscontroller vmsvga --audio-enabled on --audio-controller hda --audio-in on --audio-out on --nic1 nat --nictype1 82540EM --cableconnected1 on
}

if (Test-Path $SerialLog) {
    Remove-Item -LiteralPath $SerialLog -Force
}
& $VBoxManage modifyvm $VmName --uart1 0x3F8 4 --uartmode1 file $SerialLog

& $VBoxManage setextradata $VmName CustomVideoMode1 1920x1080x32
& $VBoxManage setextradata $VmName VBoxInternal2/EfiGraphicsResolution 1920x1080
& $VBoxManage setextradata global GUI/MaxGuestResolution any

& $VBoxManage storageattach $VmName `
    --storagectl "IDE" `
    --port 0 `
    --device 0 `
    --type dvddrive `
    --medium $IsoPath

if ($Headless) {
    & $VBoxManage startvm $VmName --type headless
} else {
    & $VBoxManage startvm $VmName
}
Start-Sleep -Seconds 1
& $VBoxManage controlvm $VmName setvideomodehint 1920 1080 32 0
