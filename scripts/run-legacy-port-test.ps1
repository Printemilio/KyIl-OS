param([ValidateSet(1,2)][int]$CpuCount=2,[switch]$ModernCpu,[switch]$Stop)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path "$PSScriptRoot/..").Path
$VBox='C:/Program Files/Oracle/VirtualBox/VBoxManage.exe'
$Name='KyIlOS-Legacy-Test'
$Dir=Join-Path $Root 'build/d630-tests'
New-Item -ItemType Directory -Force $Dir | Out-Null
function V([string[]]$Arguments) {
    & $VBox @Arguments
    if($LASTEXITCODE){throw "VirtualBox operation failed: $($Arguments[0])"}
}
$Machines=& $VBox list vms
if(!($Machines | Where-Object {$_ -match '^"KyIlOS-Legacy-Test" '})) {
    V @('createvm','--name',$Name,'--ostype','Other_64','--basefolder',$Dir,'--register')
    V @('storagectl',$Name,'--name','IDE','--add','ide','--controller','PIIX4')
}
$Info=& $VBox showvminfo $Name --machinereadable
if($Info -contains 'VMState="running"' -or $Info -contains 'VMState="paused"') {
    V @('controlvm',$Name,'poweroff')
}
if($Stop){exit 0}
$Profile=if($ModernCpu){'host'}else{'Intel Core2 T7600 2.33GHz'}
$Label=if($ModernCpu){'modern'}else{'core2'}
$Log=Join-Path $Dir "$Label-cpu$CpuCount.log"
V @('modifyvm',$Name,'--memory','2048','--cpus',"$CpuCount",'--cpu-profile',$Profile,
    '--firmware','bios','--chipset','piix3','--ioapic','on','--paravirtprovider','none',
    '--graphicscontroller','vboxvga','--vram','64','--rtcuseutc','on',
    '--nic1','nat','--nictype1','82540EM','--cableconnected1','on',
    '--boot1','dvd','--boot2','none','--boot3','none','--boot4','none',
    '--uart1','0x3f8','4','--uartmode1','file',$Log,'--uart2','off','--usb','off','--usbehci','off')
V @('storageattach',$Name,'--storagectl','IDE','--port','1','--device','0','--type','dvddrive','--medium',"$Root/build/kyilos-legacy.iso")
if(Test-Path $Log){
    Copy-Item -LiteralPath $Log -Destination (Join-Path $Dir "$Label-cpu$CpuCount-$([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')).log")
    Remove-Item -LiteralPath $Log
}
V @('startvm',$Name,'--type','headless')
[pscustomobject]@{vm=$Name;cpu_count=$CpuCount;cpu_profile=$Profile;memory_mib=2048;firmware='BIOS';iso_sha256=(Get-FileHash "$Root/build/kyilos-legacy.iso").Hash;kernel_sha256=(Get-FileHash "$Root/build/vex-os").Hash;log=$Log} |
    ConvertTo-Json | Set-Content "$Dir/$Label-cpu$CpuCount.json"
