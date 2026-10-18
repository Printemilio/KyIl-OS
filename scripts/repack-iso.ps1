param()
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Vbox='C:/Program Files/Oracle/VirtualBox/VBoxManage.exe'
$State=& $Vbox showvminfo Vex-OS --machinereadable
if($State -contains 'VMState="running"' -or $State -contains 'VMState="paused"'){throw 'Stop the VM before repacking its ISO.'}
# EFI and BIOS look up different configuration paths. Keep every boot copy
# identical to the staging root configuration, as build.ps1 does.
foreach($Relative in @('boot/limine/limine.conf','boot/limine.conf','limine/limine.conf','EFI/BOOT/limine.conf')){
    Copy-Item -LiteralPath (Join-Path $Root 'build/iso_root/limine.conf') -Destination (Join-Path $Root "build/iso_root/$Relative")
}
if(-not(Test-Path (Join-Path $Root 'build/iso_root/boot/vex-os')) -and -not(Test-Path (Join-Path $Root 'build/iso_root/boot/kernel'))){
    # Limine kernel name varies between build versions; require an existing config.
    if(-not(Test-Path (Join-Path $Root 'build/iso_root/limine.conf'))){throw 'Run build.ps1 first.'}
}
Push-Location (Join-Path $Root 'build')
try {
    & 'C:/msys64/usr/bin/xorriso.exe' -as mkisofs -R -r -J -l -b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus -apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part --efi-boot-image --protective-msdos-label iso_root -o vex-os.iso
    if($LASTEXITCODE -ne 0){throw 'ISO repack failed'}
} finally {Pop-Location}
& "$Root/vendor/limine/limine.exe" bios-install "$Root/build/vex-os.iso"
if($LASTEXITCODE -ne 0){throw 'limine bios-install failed'}
