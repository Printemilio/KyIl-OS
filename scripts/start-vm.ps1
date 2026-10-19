# Published image wrapper; Windows PowerShell 5.1 and PowerShell 7.
[CmdletBinding()]
param(
    [string]$VmName='KyIlOS',
    [string]$Iso,
    [ValidateRange(1,64)][int]$CpuCount=4,
    [ValidateRange(512,262144)][int]$MemoryMB=5120,
    [switch]$Headless,
    [switch]$ConfigureOnly,
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
    if((Get-Item -LiteralPath $Iso).Length -ne $image.bytes -or (Get-FileHash -LiteralPath $Iso -Algorithm SHA256).Hash -ne $image.sha256){throw 'Image différente de la version publiée : contrôle SHA-256 refusé.'}
}
$core=Join-Path $PSScriptRoot 'run-virtualbox.ps1'
$launch=@{VmName=$VmName;Iso=$Iso;CpuCount=$CpuCount;MemoryMB=$MemoryMB;Headless=$Headless;ConfigureOnly=$ConfigureOnly;Plan=$Plan;SerialLogPath=(Join-Path $root ('build/kyilos-serial-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')+'.log'))}
if($Plan){& $core @launch;return}
$command=Get-Command VBoxManage -ErrorAction SilentlyContinue
$vbox=if($command){$command.Source}else{Join-Path $env:ProgramFiles 'Oracle/VirtualBox/VBoxManage.exe'}
if(-not(Test-Path -LiteralPath $vbox -PathType Leaf)){throw 'Installer VirtualBox, puis relancer ce script.'}
function VBox([string[]]$arguments){& $vbox @arguments;if($LASTEXITCODE){throw "VirtualBox a échoué : $($arguments[0])"}}
$machines=& $vbox list vms
if($LASTEXITCODE){throw 'Impossible de lire les VM VirtualBox.'}
$exists=$machines | Where-Object {$_ -match ('^"'+[regex]::Escape($VmName)+'" ')}
if($exists){
    $owner=& $vbox getextradata $VmName KyIlOS/ManagedBy
    if($LASTEXITCODE -or $owner -notcontains 'Value: KyIlOS-start-vm-v1'){throw 'Ce nom appartient à une autre VM. Choisir -VmName avec un nouveau nom.'}
}else{
    VBox @('createvm','--name',$VmName,'--ostype','Other_64','--basefolder',(Join-Path $root 'build/virtualbox'),'--register')
    VBox @('setextradata',$VmName,'KyIlOS/ManagedBy','KyIlOS-start-vm-v1')
}
& $core @launch
if($LASTEXITCODE){throw 'Échec du lanceur VirtualBox.'}
Write-Host 'Arrêt propre : menu KyIlOS ou shutdown dans le Terminal. Sans disque de données, les fichiers de session sont volatils.'
