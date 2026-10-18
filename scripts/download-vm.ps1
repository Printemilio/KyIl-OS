[CmdletBinding()]
param([string]$ArchivePath,[switch]$Force)
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$manifest=Get-Content -LiteralPath (Join-Path $root 'release/vm.json') -Raw | ConvertFrom-Json
$image=Get-Content -LiteralPath (Join-Path $root 'release/image.json') -Raw | ConvertFrom-Json
$build=Join-Path $root 'build';New-Item -ItemType Directory -Force $build | Out-Null
$output=Join-Path $build 'kyilos.iso'
function Matches([string]$path,[long]$bytes,[string]$sha){return (Test-Path -LiteralPath $path -PathType Leaf) -and (Get-Item -LiteralPath $path).Length -eq $bytes -and (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -eq $sha}
if(Matches $output $image.bytes $image.sha256){Write-Host 'Image publiée déjà présente et vérifiée.';return}
if((Test-Path -LiteralPath $output) -and -not $Force){throw 'Une autre image existe dans build/kyilos.iso. La conserver ailleurs ou utiliser -Force.'}
if(-not $ArchivePath){
    $ArchivePath=Join-Path $build $manifest.asset
    if(-not(Matches $ArchivePath $manifest.bytes $manifest.sha256)){
        $temporary=$ArchivePath+'.download'
        [Net.ServicePointManager]::SecurityProtocol=[Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
        Write-Host "Téléchargement de $($manifest.tag) depuis GitHub Releases..."
        $oldProgress=$ProgressPreference;$ProgressPreference='SilentlyContinue'
        try{Invoke-WebRequest -Uri $manifest.url -OutFile $temporary -UseBasicParsing}finally{$ProgressPreference=$oldProgress}
        if(-not(Matches $temporary $manifest.bytes $manifest.sha256)){throw 'Archive téléchargée invalide : contrôle SHA-256 refusé.'}
        Move-Item -LiteralPath $temporary -Destination $ArchivePath -Force
    }
}
$ArchivePath=(Resolve-Path -LiteralPath $ArchivePath).Path
if(-not(Matches $ArchivePath $manifest.bytes $manifest.sha256)){throw 'Archive différente de la release attendue.'}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::OpenRead($ArchivePath)
$partial=$output+'.extracting'
try{
    $entry=$zip.GetEntry('build/kyilos.iso')
    if(-not $entry -or $entry.Length -ne $image.bytes){throw 'Entrée ISO absente ou de taille incorrecte.'}
    # Extract only this exact entry; archive paths never select a host target.
    [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$partial,$true)
}finally{$zip.Dispose()}
if(-not(Matches $partial $image.bytes $image.sha256)){throw 'ISO extraite invalide : contrôle SHA-256 refusé.'}
Move-Item -LiteralPath $partial -Destination $output -Force
Write-Host "Image vérifiée : $output"
Write-Host 'Lancement : .\scripts\start-vm.ps1'
