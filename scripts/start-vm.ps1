# Run the published, verified image without rebuilding the development tree.
[CmdletBinding()]
param(
    [string]$VmName='KyIlOS',
    [string]$Iso,
    [ValidateRange(1,64)][int]$CpuCount=4,
    [ValidateRange(512,262144)][int]$MemoryMB=4096,
    [ValidateSet('bios','efi')][string]$Firmware='bios',
    [switch]$Headless,
    [switch]$Plan
)
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$published=-not $Iso
if($published){$Iso=Join-Path $root 'build/kyilos.iso'}
if(-not(Test-Path -LiteralPath $Iso -PathType Leaf)){throw 'Image absente : lancer scripts/download-vm.ps1 ou utiliser -Iso <chemin>.'}
$Iso=(Resolve-Path -LiteralPath $Iso).Path
if($published){
    $image=Get-Content -LiteralPath (Join-Path $root 'release/image.json') -Raw | ConvertFrom-Json
    if((Get-Item -LiteralPath $Iso).Length -ne $image.bytes -or (Get-FileHash -LiteralPath $Iso -Algorithm SHA256).Hash -ne $image.sha256){throw 'Image différente du snapshot publié : contrôle SHA-256 refusé.'}
}
$command=Get-Command VBoxManage -ErrorAction SilentlyContinue
$vbox=if($command){$command.Source}else{Join-Path $env:ProgramFiles 'Oracle/VirtualBox/VBoxManage.exe'}
if(-not(Test-Path -LiteralPath $vbox -PathType Leaf)){throw 'Installer VirtualBox, puis relancer ce script.'}
$build=Join-Path $root 'build'
$log=Join-Path $build ('kyilos-serial-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')+'.log')
if($Plan){[pscustomobject]@{vm=$VmName;iso=$Iso;cpus=$CpuCount;memory_mib=$MemoryMB;firmware=$Firmware;chipset='piix3';nic='82540EM/NAT';persistent_disk=$false;serial=$log}|ConvertTo-Json;return}
function VBox([string[]]$arguments){& $vbox @arguments;if($LASTEXITCODE){throw "VirtualBox a échoué : $($arguments[0])"}}
$machine=& $vbox list vms
if($LASTEXITCODE){throw 'Impossible de lire les VM VirtualBox.'}
$exists=$machine | Where-Object {$_ -match ('^"'+[regex]::Escape($VmName)+'" ')}
New-Item -ItemType Directory -Force $build | Out-Null
if($exists){
    $info=& $vbox showvminfo $VmName --machinereadable
    if($LASTEXITCODE){throw 'Impossible de lire la configuration de la VM.'}
    $state=$info | Select-String '^VMState=' | Select-Object -First 1
    if(-not $state -or $state.Line -ne 'VMState="poweroff"'){throw 'La VM doit être complètement arrêtée. Aucun arrêt forcé ni abandon de sauvegarde automatique.'}
    # Do not repurpose an unrelated existing machine or change its disks.
    $owner=& $vbox getextradata $VmName KyIlOS/ManagedBy
    if($LASTEXITCODE -or $owner -notcontains 'Value: KyIlOS-start-vm-v1'){throw 'Ce nom appartient à une autre VM. Choisir -VmName avec un nouveau nom.'}
}else{
    VBox @('createvm','--name',$VmName,'--ostype','Other_64','--basefolder',(Join-Path $build 'virtualbox'),'--register')
    VBox @('setextradata',$VmName,'KyIlOS/ManagedBy','KyIlOS-start-vm-v1')
    VBox @('storagectl',$VmName,'--name','IDE','--add','ide','--controller','PIIX4')
}
VBox @('modifyvm',$VmName,'--memory',"$MemoryMB",'--cpus',"$CpuCount",'--firmware',$Firmware,'--chipset','piix3','--ioapic','on','--paravirtprovider','none','--graphicscontroller','vboxvga','--vram','64','--rtcuseutc','on','--nic1','nat','--nictype1','82540EM','--cableconnected1','on','--boot1','dvd','--boot2','none','--boot3','none','--boot4','none','--uart1','0x3f8','4','--uartmode1','file',$log)
VBox @('storageattach',$VmName,'--storagectl','IDE','--port','1','--device','0','--type','dvddrive','--medium',$Iso)
$mode=if($Headless){'headless'}else{'gui'}
VBox @('startvm',$VmName,'--type',$mode)
Write-Host "KyIlOS démarré. Journal série : $log"
Write-Host 'Sans disque de données, les fichiers créés dans la VM sont volatils. Arrêt propre : menu du bureau ou commande shutdown.'
