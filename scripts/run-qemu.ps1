param(
    [string[]]$QemuArgs = @()
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$IsoPath = Join-Path $Root "build/vex-os.iso"

if (-not (Get-Command "qemu-system-x86_64" -ErrorAction SilentlyContinue)) {
    throw "Required command 'qemu-system-x86_64' was not found in PATH."
}

& (Join-Path $PSScriptRoot "build.ps1")

qemu-system-x86_64 `
    -M q35 `
    -m 256M `
    -cdrom $IsoPath `
    -boot d `
    @QemuArgs
