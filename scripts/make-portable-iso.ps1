param([string]$IsoName='kyilos-legacy.iso',
      [string]$UsbImageName='kyilos-legacy-usb.img',
      [switch]$IsoOnly)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path "$PSScriptRoot/..").Path
if([IO.Path]::GetFileName($IsoName) -ne $IsoName){throw 'IsoName must be a filename.'}
if($UsbImageName -notmatch '^[a-zA-Z0-9._-]+\.img$'){throw 'UsbImageName must be an .img filename.'}
$Stage=Join-Path $Root 'build/portable_iso_root'
New-Item -ItemType Directory -Force $Stage | Out-Null
# A separate staging tree preserves the ordinary desktop ISO and its config.
# robocopy /MIR is deliberately avoided: no recursive deletion is involved.
& robocopy "$Root/build/iso_root" $Stage /E /NFL /NDL /NJH /NJS /NP | Out-Null
if($LASTEXITCODE -gt 7){throw 'Staging copy failed'}
Copy-Item "$Root/build/vex-os" "$Stage/boot/vex-os" -Force
$Config=[Collections.Generic.List[string]]::new()
foreach($Line in Get-Content "$Root/boot/limine.conf") {
    if($Line -match '^\s*module_path:' -or $Line -match '^\s*resolution:'){continue}
    $Config.Add($Line)
}
& python "$Root/scripts/pack-boot-archive.py" $Stage "$Stage/system.boot"
if($LASTEXITCODE){throw 'Boot archive build failed'}
$Total=(Get-Item "$Stage/system.boot").Length;$Count=1
$Config.Add('    module_path: boot():/system.boot')
foreach($Path in @('limine.conf','boot/limine/limine.conf','boot/limine.conf','limine/limine.conf','EFI/BOOT/limine.conf')) {
    Set-Content -LiteralPath (Join-Path $Stage $Path) -Value $Config
}
$Output=Join-Path "$Root/build" $IsoName
Push-Location "$Root/build"
try {
    & 'C:/msys64/usr/bin/xorriso.exe' -as mkisofs -R -r -J -l -b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus -apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part --efi-boot-image --protective-msdos-label portable_iso_root -o $IsoName
    if($LASTEXITCODE){throw 'ISO creation failed'}
} finally {Pop-Location}
& "$Root/vendor/limine/limine.exe" bios-install $Output
if($LASTEXITCODE){throw 'BIOS installation failed'}
$UsbOutput=$null;$UsbHash=$null
if(-not $IsoOnly) {
    # Rufus does not know how to reinstall Limine in ISO extraction mode. Supply
    # an actual MBR/FAT32 disk image that it writes byte-for-byte instead.
    & wsl -d Ubuntu-24.04 -- bash scripts/make-usb-disk-image.sh $UsbImageName
    if($LASTEXITCODE){throw 'USB-HDD image creation failed (requires WSL mtools).'}
    $UsbOutput=Join-Path "$Root/build" $UsbImageName
    $UsbHash=(Get-FileHash $UsbOutput).Hash
}
[pscustomobject]@{iso=$Output;usb_image=$UsbOutput;usb_sha256=$UsbHash;module_count=$Count;module_mib=[math]::Round($Total/1MB,2);kernel_sha256=(Get-FileHash "$Stage/boot/vex-os").Hash;iso_sha256=(Get-FileHash $Output).Hash} |
    ConvertTo-Json | Tee-Object -FilePath "$Root/build/portable-iso-provenance.json"
