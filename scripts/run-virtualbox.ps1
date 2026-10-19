# Desktop profile. Does not compile: build/update the desktop ISO separately.
# Dell/Core2 validation remains in run-legacy-port-test.ps1, on its own VM.
[CmdletBinding()]
param(
    [string]$VmName = 'Vex-OS',
    [switch]$SkipBuild, # compatibility with existing test callers; never builds
    [switch]$Headless,
    [ValidateRange(512,262144)][int]$MemoryMB = 5120,
    [ValidateRange(1,64)][int]$CpuCount = 4,
    [string]$DataDisk,
    [string]$IsoName = 'kyilos.iso',
    [string]$Iso,
    [string]$SerialLogPath,
    [switch]$Plan,
    [switch]$ConfigureOnly
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$IsoPath = if ($Iso) { [IO.Path]::GetFullPath($Iso) } else { Join-Path $Root (Join-Path 'build' $IsoName) }
if (-not (Test-Path -LiteralPath $IsoPath -PathType Leaf)) {
    throw "ISO absente : $IsoPath. Ce lanceur ne reconstruit pas l'image."
}
$IsoPath = (Resolve-Path -LiteralPath $IsoPath).Path
if ($DataDisk) { $DataDisk = (Resolve-Path -LiteralPath $DataDisk).Path }
$SerialLog = if ($SerialLogPath) {
    $logPath = if ([IO.Path]::IsPathRooted($SerialLogPath)) { $SerialLogPath } else { Join-Path $Root $SerialLogPath }
    [IO.Path]::GetFullPath($logPath)
} else { Join-Path $Root 'build/vex-serial.log' }
$profile = [ordered]@{
    vm=$VmName; iso=$IsoPath; cpus=$CpuCount; memory_mib=$MemoryMB
    cpu_profile='host'; firmware='efi'; chipset='ich9'
    graphics='vmsvga'; vram_mib=128; resolution='1920x1080'
    storage='AHCI/SATA'; nic='82540EM/NAT'; paravirtualization='none'
    renderer='KyIlOS software renderer; no hardware 3D driver claimed'
    serial=$SerialLog; data_disk=$DataDisk
}
if ($Plan) { $profile | ConvertTo-Json; return }
$command = Get-Command VBoxManage -ErrorAction SilentlyContinue
$VBoxManage = if ($command) { $command.Source } else { Join-Path $env:ProgramFiles 'Oracle/VirtualBox/VBoxManage.exe' }
if (-not (Test-Path -LiteralPath $VBoxManage -PathType Leaf)) { throw 'VirtualBox introuvable.' }
function VBox([string[]]$Arguments) {
    & $VBoxManage @Arguments
    if ($LASTEXITCODE) { throw "VBoxManage a échoué : $($Arguments -join ' ')" }
}
function Info {
    $lines = & $VBoxManage showvminfo $VmName --machinereadable
    if ($LASTEXITCODE) { throw "Configuration VirtualBox inaccessible : $VmName" }
    return $lines
}
function Show-Console {
    # Official VirtualBox API: ask the existing frontend to show its console.
    # Do not start another guest or just tell the user to find a hidden window.
    $virtualBox = New-Object -ComObject VirtualBox.VirtualBox
    $machine = $virtualBox.FindMachine($VmName)
    if ($machine.CanShowConsoleWindow()) {
        [void]$machine.ShowConsoleWindow()
    } else {
        Write-Warning 'Cette session ne dispose pas de console GUI. Utiliser Afficher dans le gestionnaire VirtualBox.'
    }
}
$machines = & $VBoxManage list vms
if ($LASTEXITCODE) { throw 'Liste des VM inaccessible.' }
$exists = $machines | Where-Object { $_ -match ('^"'+[regex]::Escape($VmName)+'" ') }
if ($exists) {
    $details = Info
    $state = $details | Select-String '^VMState=' | Select-Object -First 1
    if ($state -and $state.Line -in @('VMState="running"','VMState="paused"')) {
        Write-Host "La VM '$VmName' est déjà démarrée ou en pause."
        if (-not $Headless -and -not $ConfigureOnly) {
            Show-Console
            Write-Host 'Affichage de sa console demandé.'
        }
        Write-Host 'Aucune configuration modifiée. Pour changer le profil, arrêter KyIlOS avec shutdown puis relancer ce script.'
        return
    }
    if ($state -and $state.Line -eq 'VMState="aborted"') {
        Write-Warning "La session précédente de '$VmName' a été interrompue. La VM est à l'arrêt : relancement à froid, sans suppression d'état sauvegardé."
    } elseif (-not $state -or $state.Line -ne 'VMState="poweroff"') {
        throw "La VM '$VmName' doit être arrêtée proprement (shutdown dans KyIlOS). Aucun arrêt forcé ni suppression d'état sauvegardé automatique."
    }
} else {
    VBox @('createvm','--name',$VmName,'--ostype','Other_64','--register')
    $details = Info
}
$occupied = $details | Where-Object { $_.StartsWith('"SATA-0-0"=') -and $_ -ne '"SATA-0-0"="none"' -and $_ -ne '"SATA-0-0"="emptydrive"' }
$optical = $details | Where-Object { $_.StartsWith('"SATA-IsEjected-0-0"=') }
if ($occupied -and -not $optical) { throw 'SATA port 0 contient un disque, pas un lecteur optique. Il est conservé ; choisir une VM dédiée.' }
# Host CPU features remain detected by KyIlOS. Hyper-V/KVM guest enlightenments
# are not implemented by this kernel. VirtualBox may still use Hyper-V on Windows.
VBox @('modifyvm',$VmName,'--memory',"$MemoryMB",'--cpus',"$CpuCount",
    '--cpu-profile','host','--cpuexecutioncap','100','--hwvirtex','on','--nestedpaging','on',
    '--firmware','efi','--chipset','ich9','--acpi','on','--ioapic','on',
    '--paravirtprovider','none','--rtcuseutc','on',
    '--graphicscontroller','vmsvga','--vram','128','--accelerate3d','off',
    '--mouse','ps2','--keyboard','ps2',
    '--audio-enabled','on','--audio-controller','hda','--audio-in','off','--audio-out','on',
    '--nic1','nat','--nictype1','82540EM','--cableconnected1','on',
    '--boot1','dvd','--boot2','none','--boot3','none','--boot4','none')
if (-not ($details | Select-String '^storagecontrollername\d+="SATA"$')) {
    VBox @('storagectl',$VmName,'--name','SATA','--add','sata','--controller','IntelAhci','--portcount','4','--bootable','on')
}
# Only detach old optical media; preserve hard disks and unrelated controllers.
if ($details | Select-String '^storagecontrollername\d+="IDE"$') {
    foreach ($port in 0,1) { foreach ($device in 0,1) {
        $slot = '"IDE-'+$port+'-'+$device+'"='
        $medium = $details | Where-Object { $_.StartsWith($slot) }
        $dvd = $details | Where-Object { $_ -eq ('"IDE-IsEjected-'+$port+'-'+$device+'"="off"') }
        if ($medium -and $dvd) { VBox @('storageattach',$VmName,'--storagectl','IDE','--port',"$port",'--device',"$device",'--type','dvddrive','--medium','none') }
    }}
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $SerialLog) | Out-Null
if (Test-Path -LiteralPath $SerialLog) {
    $archive = Join-Path $Root 'build/virtualbox-logs'
    New-Item -ItemType Directory -Force -Path $archive | Out-Null
    Copy-Item -LiteralPath $SerialLog -Destination (Join-Path $archive ('serial-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')+'.log'))
}
VBox @('modifyvm',$VmName,'--uart1','0x3f8','4','--uartmode1','file',$SerialLog)
# Set a real firmware framebuffer mode, also requested by Limine. No Guest
# Additions/resize driver: do not send a misleading setvideomodehint after boot.
VBox @('setextradata',$VmName,'CustomVideoMode1','1920x1080x32')
VBox @('setextradata',$VmName,'VBoxInternal2/EfiGraphicsResolution','1920x1080')
if (-not $Headless) {
    # A monitor can have disappeared or moved. Never reuse an off-screen saved
    # X/Y (the observed failure was X=1920, with no screen to its right).
    Add-Type -AssemblyName System.Windows.Forms
    $area = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
    $hostWidth = [Math]::Min(1600,[Math]::Max(320,$area.Width-80))
    $hostHeight = [Math]::Min(900,[Math]::Max(240,$area.Height-120))
    $hostX = $area.X+[int](($area.Width-$hostWidth)/2)
    $hostY = $area.Y+[int](($area.Height-$hostHeight)/2)
    $position = "$hostX,$hostY,$hostWidth,$hostHeight"
    VBox @('setextradata',$VmName,'GUI/LastNormalWindowPosition',$position)
    VBox @('setextradata',$VmName,'GUI/LastScaleWindowPosition',$position)
    # Scale the host viewer to fit, not the guest framebuffer/resolution.
    VBox @('setextradata',$VmName,'GUI/Scale','true')
    $profile.host_window_position=$position
}
VBox @('setextradata',$VmName,'GUI/ScaleFactor','1')
VBox @('storageattach',$VmName,'--storagectl','SATA','--port','0','--device','0','--type','dvddrive','--medium',$IsoPath)
if ($DataDisk) { VBox @('storageattach',$VmName,'--storagectl','SATA','--port','1','--device','0','--type','hdd','--medium',$DataDisk) }
$profile | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Root 'build/virtualbox-desktop-profile.json')
Write-Host "ISO : $IsoPath ($((Get-Item -LiteralPath $IsoPath).LastWriteTime))"
Write-Host "Desktop : $CpuCount vCPU / $MemoryMB MiB / CPU host / UEFI / ICH9 / 1920x1080"
if (-not $ConfigureOnly) {
    if ($Headless) {
        VBox @('startvm',$VmName,'--type','headless')
    } else {
        $idLine = $details | Select-String '^UUID=' | Select-Object -First 1
        if (-not $idLine) { throw 'UUID de la VM inaccessible.' }
        $machineId = [guid]($idLine.Line -replace '^UUID="([^"]+)"$','$1')
        $frontend = Join-Path (Split-Path -Parent $VBoxManage) 'VirtualBoxVM.exe'
        if (-not (Test-Path -LiteralPath $frontend -PathType Leaf)) { throw 'Frontend GUI VirtualBox introuvable.' }
        # Explicitly visible frontend, even when the invoking shell is hidden.
        $frontendProcess = Start-Process -FilePath $frontend -ArgumentList @('--startvm',$machineId.ToString()) -WindowStyle Normal -PassThru
        $startDeadline = [DateTime]::UtcNow.AddSeconds(30)
        do {
            $liveState = (Info | Select-String '^VMState=' | Select-Object -First 1).Line
            if ($liveState -in @('VMState="running"','VMState="paused"')) { break }
            Start-Sleep -Milliseconds 200
        } while ([DateTime]::UtcNow -lt $startDeadline)
        if ($liveState -notin @('VMState="running"','VMState="paused"')) {
            throw "Le frontend n'a pas démarré la VM (état : $liveState). Consulter ses journaux VirtualBox."
        }
        Write-Host "Console GUI lancée (processus $($frontendProcess.Id))."
    }
    Write-Host "Journal série : $SerialLog"
}
