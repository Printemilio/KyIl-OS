param(
    [switch]$NoIso,
    [switch]$SkipWebKit
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BuildDir = Join-Path $Root "build"
$ObjDir = Join-Path $BuildDir "obj"
$IsoRoot = Join-Path $BuildDir "iso_root"
$IsoRootName = "iso_root"
$IsoName = "vex-os.iso"
$VendorDir = Join-Path $Root "vendor"
$KernelPath = Join-Path $BuildDir "vex-os"
$IsoPath = Join-Path $BuildDir $IsoName

$LimineVersion = "v11.3.1-binary"
$LimineProtocolCommit = "80ef54bed402b8c0b672a707c1df4c532f3428ad"
$FreestandingHeadersCommit = "097259a899d30f0a4b7a694de2de5fdda942e923"

function Require-Command($Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command '$Name' was not found in PATH."
    }
}

function Ensure-Repo($Path, $Url, $Ref) {
    if (-not (Test-Path $Path)) {
        git clone --depth=1 $Url $Path
    }

    $ShallowLock = Join-Path $Path ".git/shallow.lock"
    if (Test-Path $ShallowLock) {
        Remove-Item -Force $ShallowLock
    }

    git -C $Path fetch --depth=1 origin $Ref
    git -C $Path checkout --detach FETCH_HEAD
}


Require-Command "git"
Require-Command "clang"
Require-Command "ld.lld"

if (-not $NoIso) {
    Require-Command "xorriso"
}

New-Item -ItemType Directory -Force -Path @($BuildDir, $ObjDir, $VendorDir) | Out-Null

Ensure-Repo (Join-Path $VendorDir "limine") "https://github.com/Limine-Bootloader/Limine.git" $LimineVersion
Ensure-Repo (Join-Path $VendorDir "limine-protocol") "https://github.com/Limine-Bootloader/limine-protocol.git" $LimineProtocolCommit
Ensure-Repo (Join-Path $VendorDir "freestnd-c-hdrs") "https://github.com/osdev0/freestnd-c-hdrs-0bsd.git" $FreestandingHeadersCommit

$CompilerFlags = @(
    "-target", "x86_64-unknown-none-elf",
    "-Wall",
    "-Wextra",
    "-std=gnu11",
    "-g",
    "-O2",
    "-ffreestanding",
    "-nostdinc",
    "-fno-stack-protector",
    "-fno-stack-check",
    "-fno-lto",
    "-fno-PIC",
    "-ffunction-sections",
    "-fdata-sections",
    "-m64",
    "-march=x86-64",
    "-mabi=sysv",
    "-mno-80387",
    "-mno-mmx",
    "-mno-sse",
    "-mno-sse2",
    "-mno-red-zone",
    "-mcmodel=kernel",
    "-I", (Join-Path $Root "kernel/include"),
    "-I", (Join-Path $VendorDir "limine-protocol/include"),
    "-isystem", (Join-Path $VendorDir "freestnd-c-hdrs/include")
)

$SourceRoot = Join-Path $Root "kernel/src"
$Sources = Get-ChildItem -Path $SourceRoot -Recurse -File |
    Where-Object { $_.Extension -eq ".c" -or $_.Extension -eq ".S" } |
    Sort-Object FullName
$Objects = @()

foreach ($Source in $Sources) {
    $RelativeSource = $Source.FullName.Substring($SourceRoot.Length + 1)
    $RelativeObject = [System.IO.Path]::ChangeExtension($RelativeSource, ".o")
    $Object = Join-Path $ObjDir $RelativeObject
    New-Item -ItemType Directory -Force (Split-Path $Object -Parent) | Out-Null

    clang @CompilerFlags -c $Source.FullName -o $Object
    $Objects += $Object
}

$LinkerFlags = @(
    "-m", "elf_x86_64",
    "-nostdlib",
    "-static",
    "-z", "max-page-size=0x1000",
    "--gc-sections",
    "-T", (Join-Path $Root "kernel/arch/x86_64/linker.ld")
)

ld.lld @LinkerFlags @Objects -o $KernelPath

if ($NoIso) {
    Write-Host "Kernel built: $KernelPath"
    exit 0
}

if (-not $SkipWebKit) {
    & (Join-Path $PSScriptRoot "build-webkit-userspace.ps1") -Fast
    if ($LASTEXITCODE -ne 0) {
        throw "WebKit userspace build failed."
    }
}

if (Test-Path $IsoRoot) {
    Remove-Item -Recurse -Force $IsoRoot
}

New-Item -ItemType Directory -Force -Path @(
    (Join-Path $IsoRoot "boot"),
    (Join-Path $IsoRoot "boot/limine"),
    (Join-Path $IsoRoot "EFI/BOOT")
) | Out-Null

Copy-Item $KernelPath (Join-Path $IsoRoot "boot/vex-os")
$SourceLimineConfig = Join-Path $Root "boot/limine.conf"
$IsoLimineConfig = Join-Path $IsoRoot "boot/limine/limine.conf"
$FilteredLimineConfig = [System.Collections.Generic.List[string]]::new()

foreach ($Line in Get-Content $SourceLimineConfig) {
    if ($Line -match '^\s*module_path\s*[:=]\s*boot(?:\(\):|://)/([^\s]+)') {
        $ModuleRelativePath = $Matches[1] -replace '/', [System.IO.Path]::DirectorySeparatorChar
        $ModuleSource = Join-Path $Root $ModuleRelativePath
        $ModuleDestination = Join-Path $IsoRoot $ModuleRelativePath

        if (-not (Test-Path $ModuleSource)) {
            Write-Warning "Limine module '$ModuleRelativePath' was declared but '$ModuleSource' does not exist; skipping it."
            continue
        }

        New-Item -ItemType Directory -Force -Path (Split-Path $ModuleDestination -Parent) | Out-Null
        Copy-Item $ModuleSource $ModuleDestination
    }

    $FilteredLimineConfig.Add($Line)
}

$AutoSmokeMarker = Join-Path $Root "auto_smoke"
if (Test-Path $AutoSmokeMarker) {
    Copy-Item $AutoSmokeMarker (Join-Path $IsoRoot "auto_smoke")
    $FilteredLimineConfig.Add("    module_path: boot():/auto_smoke")
}

Set-Content -LiteralPath $IsoLimineConfig -Value $FilteredLimineConfig

$LimineDir = Join-Path $VendorDir "limine"
Copy-Item (Join-Path $LimineDir "limine-bios.sys") (Join-Path $IsoRoot "boot/limine/")
Copy-Item (Join-Path $LimineDir "limine-bios-cd.bin") (Join-Path $IsoRoot "boot/limine/")
Copy-Item (Join-Path $LimineDir "limine-uefi-cd.bin") (Join-Path $IsoRoot "boot/limine/")
Copy-Item (Join-Path $LimineDir "BOOTX64.EFI") (Join-Path $IsoRoot "EFI/BOOT/")
Copy-Item (Join-Path $LimineDir "BOOTIA32.EFI") (Join-Path $IsoRoot "EFI/BOOT/")

if (Test-Path $IsoPath) {
    Remove-Item -Force $IsoPath
}

$XorrisoArgs = @(
    "-as", "mkisofs",
    "-R",
    "-r",
    "-J",
    "-b", "boot/limine/limine-bios-cd.bin",
    "-no-emul-boot",
    "-boot-load-size", "4",
    "-boot-info-table",
    "-hfsplus",
    "-apm-block-size", "2048",
    "--efi-boot", "boot/limine/limine-uefi-cd.bin",
    "-efi-boot-part",
    "--efi-boot-image",
    "--protective-msdos-label",
    $IsoRootName,
    "-o", $IsoName
)

Push-Location $BuildDir
try {
    xorriso @XorrisoArgs
} finally {
    Pop-Location
}

$LimineExe = Join-Path $LimineDir "limine.exe"
if (-not (Test-Path $LimineExe)) {
    $LimineExe = Join-Path $LimineDir "limine"
}

& $LimineExe bios-install $IsoPath

Write-Host "ISO built: $IsoPath"
