param(
    [switch]$NoIso,
    [switch]$SkipWebKit,
    [ValidateSet('Desktop','Smoke','Vulkan','WebKit','GL','Stress','FlowSched','SMP','Native')]
    [string]$BootMode = 'Desktop',
    [switch]$CompatTrace,
    # Self-contained BIOS/USB boot: files remain reachable without a CD-ROM
    # provider. This does not claim support for arbitrary USB peripherals.
    [switch]$PortableBoot,
    # Output file in build/. The native tests use their own name so that the
    # desktop ISO run-virtualbox.ps1 boots is never replaced by a test image.
    [string]$IsoName = "vex-os.iso"
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BuildDir = Join-Path $Root "build"
$ObjDir = Join-Path $BuildDir "obj"
$IsoRoot = Join-Path $BuildDir "iso_root"
$IsoRootName = "iso_root"
$VendorDir = Join-Path $Root "vendor"
$KernelPath = Join-Path $BuildDir "vex-os"
$IsoPath = Join-Path $BuildDir $IsoName

$LimineVersion = "v11.3.1-binary"
$LimineProtocolCommit = "80ef54bed402b8c0b672a707c1df4c532f3428ad"
$FreestandingHeadersCommit = "097259a899d30f0a4b7a694de2de5fdda942e923"
$LvglVersion = "v9.5.0"

function Require-Command($Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command '$Name' was not found in PATH."
    }
}

function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [object[]]$Arguments = @()
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath $($Arguments -join ' ')"
    }
}

function Resolve-CommandPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [string[]]$CandidatePaths = @()
    )

    $Command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($Command) {
        return $Command.Source
    }

    foreach ($CandidatePath in $CandidatePaths) {
        if ($CandidatePath -and (Test-Path $CandidatePath)) {
            return $CandidatePath
        }
    }

    throw "Required command '$Name' was not found in PATH."
}

function Ensure-Repo($Path, $Url, $Ref) {
    if (-not (Test-Path $Path)) {
        Invoke-Native $Git @("clone", "--depth=1", $Url, $Path)
    }

    if (-not (Test-Path (Join-Path $Path ".git"))) {
        Write-Verbose "Using vendored snapshot at '$Path' because it is not a Git checkout."
        return
    }

    $ShallowLock = Join-Path $Path ".git/shallow.lock"
    if (Test-Path $ShallowLock) {
        Remove-Item -Force $ShallowLock
    }

    Invoke-Native $Git @("-C", $Path, "fetch", "--depth=1", "origin", $Ref)
    Invoke-Native $Git @("-C", $Path, "checkout", "--detach", "FETCH_HEAD")
}

function Copy-IsoPayload($RelativePath) {
    $Source = Join-Path $Root $RelativePath
    $Destination = Join-Path $IsoRoot $RelativePath

    if (-not (Test-Path $Source)) {
        Write-Warning "ISO payload '$RelativePath' was not found; skipping it."
        return
    }

    New-Item -ItemType Directory -Force -Path (Split-Path $Destination -Parent) | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Recurse -Force
}
function Build-DriverPackages {
    $BundledPython = Join-Path $env:LOCALAPPDATA 'hermes/hermes-agent/venv/Scripts/python.exe'
    if (Test-Path -LiteralPath $BundledPython) { $DriverPython = $BundledPython }
    else { $DriverPython = Resolve-CommandPath 'python' }
    Invoke-Native $DriverPython @((Join-Path $Root 'scripts/build-driver-packages.py'))
}


$Git = Resolve-CommandPath "git" @(
    "C:\Program Files\Git\cmd\git.exe",
    "C:\Program Files\Git\bin\git.exe"
)
$Clang = Resolve-CommandPath "clang" @(
    "C:\Program Files\LLVM\bin\clang.exe"
)
$LdLld = Resolve-CommandPath "ld.lld" @(
    "C:\Program Files\LLVM\bin\ld.lld.exe"
)

if (-not $NoIso) {
    $Xorriso = Resolve-CommandPath "xorriso" @(
        "C:\msys64\usr\bin\xorriso.exe"
    )
}

New-Item -ItemType Directory -Force -Path @($BuildDir, $ObjDir, $VendorDir) | Out-Null

Ensure-Repo (Join-Path $VendorDir "limine") "https://github.com/Limine-Bootloader/Limine.git" $LimineVersion
Ensure-Repo (Join-Path $VendorDir "limine-protocol") "https://github.com/Limine-Bootloader/limine-protocol.git" $LimineProtocolCommit
Ensure-Repo (Join-Path $VendorDir "freestnd-c-hdrs") "https://github.com/osdev0/freestnd-c-hdrs-0bsd.git" $FreestandingHeadersCommit
Ensure-Repo (Join-Path $VendorDir "lvgl") "https://github.com/lvgl/lvgl.git" $LvglVersion
$JitterPin = '2aa10e2423fc7d34d67ef666d8eb186d236272d8'
$JitterCheckout = Join-Path $VendorDir 'jitterentropy'
if (-not (Test-Path "$JitterCheckout/.git") -or (& $Git -C $JitterCheckout rev-parse HEAD) -ne $JitterPin) {
    Ensure-Repo $JitterCheckout 'https://github.com/smuellerDD/jitterentropy-library.git' $JitterPin
}
$JitterPatch = Join-Path $Root 'third_party/patches/jitterentropy/0001-freestanding-public-header.patch'
if ((Get-Content "$JitterCheckout/jitterentropy.h" -Raw) -notmatch '# if !defined\(JENT_BAREMETAL\)\r?\n# include <sys/stat.h>') {
    Invoke-Native $Git @('-C',$JitterCheckout,'apply',$JitterPatch)
}

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
    "-I", (Join-Path $VendorDir "lvgl"),
    "-I", (Join-Path $VendorDir "lvgl/src"),
    "-DLV_CONF_INCLUDE_SIMPLE",
    "-I", (Join-Path $VendorDir "limine-protocol/include"),
    "-isystem", (Join-Path $VendorDir "freestnd-c-hdrs/include")
)

$SourceRoot = Join-Path $Root "kernel/src"
if ($CompatTrace) { $CompilerFlags += '-DVEX_COMPAT_TRACE=1' }
$Sources = Get-ChildItem -Path $SourceRoot -Recurse -File |
    Where-Object { ($_.Extension -eq ".c" -or $_.Extension -eq ".S") -and $_.Name -ne 'compat_pe.c' } |
    Sort-Object FullName
$LvglSources = Get-ChildItem -Path (Join-Path $VendorDir "lvgl/src") -Recurse -File |
    Where-Object { $_.Extension -eq ".c" } |
    Sort-Object FullName
$Sources += $LvglSources
$JitterRoot = Join-Path $VendorDir 'jitterentropy'
$JitterSources = Get-ChildItem (Join-Path $JitterRoot 'src'),(Join-Path $JitterRoot 'arch') -Filter '*.c' |
    Where-Object { $_.Name -ne 'jitterentropy-arch-memory.c' }
$Sources += $JitterSources
$UacpiRoot=Join-Path $VendorDir 'uacpi'
if(-not(Test-Path "$UacpiRoot/.git") -or (& $Git -C $UacpiRoot rev-parse HEAD) -ne '2cb5e71ef58393839e1988ef9a8f1d8ba2845a8d') {
    Ensure-Repo $UacpiRoot 'https://github.com/uACPI/uACPI.git' '2cb5e71ef58393839e1988ef9a8f1d8ba2845a8d'
}
$Sources += Get-ChildItem (Join-Path $UacpiRoot 'source') -Filter '*.c'
$Objects = @()

foreach ($Source in $Sources) {
    if ($Source.FullName.StartsWith($SourceRoot)) {
        $RelativeSource = $Source.FullName.Substring($SourceRoot.Length + 1)
    } elseif ($Source.FullName.StartsWith($JitterRoot)) {
        $RelativeSource = Join-Path 'vendor/jitterentropy' $Source.FullName.Substring($JitterRoot.Length + 1)
    } elseif ($Source.FullName.StartsWith($UacpiRoot)) {
        $RelativeSource=Join-Path 'vendor/uacpi' $Source.Name
    } else {
        $RelativeSource = Join-Path "vendor/lvgl" $Source.FullName.Substring((Join-Path $VendorDir "lvgl/src").Length + 1)
    }
    $RelativeObject = [System.IO.Path]::ChangeExtension($RelativeSource, ".o")
    $Object = Join-Path $ObjDir $RelativeObject
    New-Item -ItemType Directory -Force (Split-Path $Object -Parent) | Out-Null

    $SourceFlags = $CompilerFlags
    if($Source.FullName.StartsWith($UacpiRoot) -or $Source.Name -in @('acpi_host.c','power.c')) {
        $SourceFlags += @('-I',(Join-Path $UacpiRoot 'include'),'-include',(Join-Path $Root 'kernel/include/unos/acpi_config.h'))
    }
    if ($Source.FullName.StartsWith($JitterRoot) -or $Source.Name -in @('random_provider.c','jitter_memory.c')) {
        $SourceFlags += @('-DJENT_BAREMETAL','-I',$JitterRoot,'-I',(Join-Path $JitterRoot 'src'),
            '-I',(Join-Path $Root 'kernel/ports/jitterentropy'))
    }
    if ($Source.FullName.StartsWith($JitterRoot)) { $SourceFlags += @('-O0','-fwrapv') }
    Invoke-Native $Clang ($SourceFlags + @("-c", $Source.FullName, "-o", $Object))
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

$LinkArguments = $LinkerFlags + $Objects + @("-o", $KernelPath)
$LinkResponsePath = Join-Path $BuildDir "kernel-link.rsp"
$LinkResponseLines = $LinkArguments | ForEach-Object {
    '"' + ([string]$_).Replace('\', '/') + '"'
}
[System.IO.File]::WriteAllLines($LinkResponsePath, $LinkResponseLines)
Invoke-Native $LdLld @("@$LinkResponsePath")

if ($NoIso) {
    Write-Host "Kernel built: $KernelPath"
    exit 0
}

if (-not $SkipWebKit) {
    Invoke-Native "powershell" @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "build-webkit-userspace.ps1"), "-Fast")
} else {
    Invoke-Native "powershell" @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "build-webkit-userspace.ps1"), "-Fast", "-SkipWebKitRuntime")
}

Invoke-Native "powershell" @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "build-pe-tests.ps1"))
Invoke-Native "powershell" @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "build-windows-runtime.ps1"))
Invoke-Native "powershell" @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "build-linux-tests.ps1"))

if (Test-Path $IsoRoot) {
    Remove-Item -Recurse -Force $IsoRoot
}

New-Item -ItemType Directory -Force -Path @(
    (Join-Path $IsoRoot "boot"),
    (Join-Path $IsoRoot "boot/limine"),
    (Join-Path $IsoRoot "EFI/BOOT"),
    (Join-Path $IsoRoot "limine")
) | Out-Null

Copy-Item $KernelPath (Join-Path $IsoRoot "boot/vex-os")

$IsoPayloads = @(
    "webkit",
    "webkit_linux_main",
    "WebKitWebProcess",
    "WebKitNetworkProcess",
    "net_test",
    "gpu_test",
    "vk_test",
    "mouse_test",
    "alloc_stress",
    "app_window_demo",
    "gfm",
    "vex_editor",
    "vex_paint",
    "vex_launcher",
    "vex_terminal",
    "elevate_test",
    "attack_test",
    "security_boundary_test",
    "vex_event_fault_test",
    "attack_kill_test",
    "attack_kill_payload",
    "jsc",
    "WPEWebDriver",
    "ld-linux-x86-64.so.2",
    "lib",
    "lib64",
    "bin",
    "usr",
    "etc"
)

foreach ($Payload in $IsoPayloads) {
    Copy-IsoPayload $Payload
}

Build-DriverPackages
Copy-IsoPayload 'drivers'
$SourceLimineConfig = Join-Path $Root "boot/limine.conf"
$IsoLimineConfigs = @(
    (Join-Path $IsoRoot "boot/limine/limine.conf"),
    (Join-Path $IsoRoot "boot/limine.conf"),
    (Join-Path $IsoRoot "limine/limine.conf"),
    (Join-Path $IsoRoot "limine.conf"),
    (Join-Path $IsoRoot "EFI/BOOT/limine.conf")
)
$FilteredLimineConfig = [System.Collections.Generic.List[string]]::new()
$BootImportedBinaries = @(
    "webkit",
    "webkit_linux_main",
    "WebKitWebProcess",
    "WebKitNetworkProcess",
    "net_test",
    "gpu_test",
    "vk_test",
    "mouse_test",
    "alloc_stress",
    "app_window_demo",
    "gfm",
    "vex_editor",
    "vex_paint",
    "vex_launcher",
    "vex_terminal",
    "elevate_test",
    "attack_test",
    "security_boundary_test",
    "vex_event_fault_test",
    "attack_kill_test",
    "attack_kill_payload",
    "jsc",
    "WPEWebDriver"
)

function Add-LimineModulePath([string]$RelativePath) {
    $ModuleLine = "    module_path: boot():/$RelativePath"
    if (-not $FilteredLimineConfig.Contains($ModuleLine)) {
        $FilteredLimineConfig.Add($ModuleLine)
    }
}

foreach ($Line in Get-Content $SourceLimineConfig) {
    if ($Line -match '^\s*module_path\s*[:=]\s*boot(?:\(\):|://)/([^\s]+)') {
        $ModuleRelativePath = $Matches[1] -replace '/', [System.IO.Path]::DirectorySeparatorChar
        # Test markers left by interrupted runs must never select a boot mode.
        if ($ModuleRelativePath -in @('auto_smoke','auto_vk_test','auto_webkit','auto_gl_test','auto_stress','auto_flow_test','auto_smp_test','auto_native_test')) { continue }
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

if ($BootMode -ne 'Desktop') {
    $Marker = @{Smoke='auto_smoke';Vulkan='auto_vk_test';WebKit='auto_webkit';GL='auto_gl_test';Stress='auto_stress';FlowSched='auto_flow_test';SMP='auto_smp_test';Native='auto_native_test'}[$BootMode]
    Set-Content -LiteralPath (Join-Path $IsoRoot $Marker) -Value $Marker -NoNewline
    Add-LimineModulePath $Marker
}

foreach ($BootBinary in $BootImportedBinaries) {
    if (Test-Path (Join-Path $Root $BootBinary)) {
        Add-LimineModulePath $BootBinary
    }
}

foreach ($LibFile in Get-ChildItem -Path (Join-Path $Root "lib") -File -ErrorAction SilentlyContinue | Sort-Object Name) {
    $RelativeLibPath = "lib/$($LibFile.Name)"
    Add-LimineModulePath $RelativeLibPath
}

foreach ($Lib64File in Get-ChildItem -Path (Join-Path $Root "lib64") -File -ErrorAction SilentlyContinue | Sort-Object Name) {
    $RelativeLib64Path = "lib64/$($Lib64File.Name)"
    if ($RelativeLib64Path -ne "lib64/ld-linux-x86-64.so.2") {
        Add-LimineModulePath $RelativeLib64Path
    }
}

foreach ($LibexecFile in Get-ChildItem -Path (Join-Path $Root "usr/local/libexec/wpe-webkit-2.0") -File -ErrorAction SilentlyContinue | Sort-Object Name) {
    $RelativeLibexecPath = "usr/local/libexec/wpe-webkit-2.0/$($LibexecFile.Name)"
    Add-LimineModulePath $RelativeLibexecPath
}

foreach ($PeTestFile in Get-ChildItem -Path (Join-Path $Root "usr/pe-tests") -File -ErrorAction SilentlyContinue | Sort-Object Name) {
    $RelativePeTestPath = "usr/pe-tests/$($PeTestFile.Name)"
    Add-LimineModulePath $RelativePeTestPath
}

# Native Vex programs built with the Vex C library (toolchain/bin/x86_64-vexos-gcc).
foreach ($NativeTestFile in Get-ChildItem -Path (Join-Path $Root "usr/native-tests") -File -ErrorAction SilentlyContinue | Sort-Object Name) {
    Add-LimineModulePath "usr/native-tests/$($NativeTestFile.Name)"
}

foreach ($LinuxTestFile in Get-ChildItem -Path (Join-Path $Root "usr/linux-tests") -File -ErrorAction SilentlyContinue | Sort-Object Name) {
    $RelativeLinuxTestPath = "usr/linux-tests/$($LinuxTestFile.Name)"
    Add-LimineModulePath $RelativeLinuxTestPath
}

if ($PortableBoot) {
    # One boot module avoids Limine's bounded memory-map fragmentation.
    $PortableConfig = [System.Collections.Generic.List[string]]::new()
    foreach ($Line in $FilteredLimineConfig) {
        if ($Line -notmatch '^\s*module_path:') { $PortableConfig.Add($Line) }
    }
    Invoke-Native 'python' @((Join-Path $Root 'scripts/pack-boot-archive.py'),$IsoRoot,(Join-Path $IsoRoot 'system.boot'))
    $PortableConfig.Add('    module_path: boot():/system.boot')
    if ($BootMode -ne 'Desktop') { $PortableConfig.Add("    module_path: boot():/$Marker") }
    $FilteredLimineConfig = $PortableConfig
}

foreach ($IsoLimineConfig in $IsoLimineConfigs) {
    Set-Content -LiteralPath $IsoLimineConfig -Value $FilteredLimineConfig
}

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
    "-l",
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
    Invoke-Native $Xorriso $XorrisoArgs
} finally {
    Pop-Location
}

$LimineExe = Join-Path $LimineDir "limine.exe"
if (-not (Test-Path $LimineExe)) {
    $LimineExe = Join-Path $LimineDir "limine"
}

Invoke-Native $LimineExe @("bios-install", $IsoPath)

Write-Host "ISO built: $IsoPath"
