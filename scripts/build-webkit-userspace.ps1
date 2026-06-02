param(
    [switch]$Fast,
    [string]$ExtraFlags = ""
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$OutDir = Join-Path $Root "build/userspace-webkit"
$IncludeDir = Join-Path $Root "userspace/libvex/include"
$LibDir = Join-Path $Root "userspace/libvex/src"
$AppDir = Join-Path $Root "userspace/apps"
$OutElf = Join-Path $Root "webkit"
$DefaultWebKitBuildDir = if ($env:OS -eq "Windows_NT") {
    Join-Path ([System.IO.Path]::GetTempPath()) "webkit-vexos-webcore"
} else {
    "/tmp/webkit-vexos-webcore"
}
$WebKitBuildDir = if ($env:VEX_WEBKIT_BUILD_DIR) { $env:VEX_WEBKIT_BUILD_DIR } else { $DefaultWebKitBuildDir }

# Note: the runtime still reuses some legacy implementation code, but the
# active WebKit path is now wired through vex_web_* compatibility names.

$CommonFlags = @(
    "-target", "x86_64-unknown-none-elf",
    "-O2", "-ffreestanding", "-fno-builtin",
    "-ffunction-sections", "-fdata-sections", "-fno-stack-protector", "-fno-pic",
    "-m64", "-march=x86-64", "-mabi=sysv", "-mno-red-zone",
    "-idirafter", (Join-Path $Root "kernel/include"),
    "-I$IncludeDir"
)

if ($ExtraFlags -ne "") {
    $CommonFlags += ($ExtraFlags -split '\s+' | Where-Object { $_ -ne "" })
}

$Sources = @(
    "$LibDir/crt0.c",
    "$LibDir/runtime.c",
    "$LibDir/vex_posix_compat.c",
    "$LibDir/vex_vk.c",
    "$LibDir/vulkan.c",
    "$LibDir/vex_web_net.c",
    "$LibDir/vex_http_client.c",
    "$LibDir/vex_page_cache.c",
    "$AppDir/webkit_web_process.c"
)

$WebProcessSources = @(
    "$LibDir/crt0.c",
    "$LibDir/runtime.c",
    "$LibDir/vex_posix_compat.c",
    "$LibDir/vex_vk.c",
    "$LibDir/vulkan.c",
    "$LibDir/vex_web_net.c",
    "$LibDir/vex_http_client.c",
    "$LibDir/vex_page_cache.c",
    "$AppDir/webkit_web_process.c"
)

$NetworkProcessSources = @(
    "$LibDir/crt0.c",
    "$LibDir/runtime.c",
    "$LibDir/vex_posix_compat.c",
    "$LibDir/vex_vk.c",
    "$LibDir/vulkan.c",
    "$LibDir/vex_web_net.c",
    "$AppDir/webkit_network_process.c"
)

$NetTestSources = @(
    "$LibDir/crt0.c",
    "$LibDir/runtime.c",
    "$LibDir/vex_posix_compat.c",
    "$LibDir/vex_vk.c",
    "$LibDir/vulkan.c",
    "$LibDir/vex_web_net.c",
    "$LibDir/vex_http_client.c",
    "$LibDir/vex_page_cache.c",
    "$AppDir/net_test.c"
)

$GpuTestSources = @(
    "$LibDir/crt0.c",
    "$LibDir/runtime.c",
    "$LibDir/vex_posix_compat.c",
    "$LibDir/vex_vk.c",
    "$LibDir/vulkan.c",
    "$AppDir/gpu_test.c"
)

$VkTestSources = @(
    "$LibDir/crt0.c",
    "$LibDir/runtime.c",
    "$LibDir/vex_posix_compat.c",
    "$LibDir/vex_vk.c",
    "$LibDir/vulkan.c",
    "$AppDir/vk_test.c"
)

$MouseTestSources = @(
    "$LibDir/crt0.c",
    "$LibDir/runtime.c",
    "$LibDir/vex_posix_compat.c",
    "$AppDir/mouse_test.c"
)

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
if (Test-Path $OutElf) {
    Remove-Item -Force $OutElf
}

$Objects = @()
foreach ($src in $Sources) {
    $obj = Join-Path $OutDir "$([System.IO.Path]::GetFileNameWithoutExtension($src)).o"
    clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $Objects += $obj
}

ld.lld `
    -m elf_x86_64 `
    -nostdlib `
    -static `
    --gc-sections `
    -z max-page-size=0x1000 `
    @Objects `
    -o $OutElf
if ($LASTEXITCODE -ne 0) {
    throw "Link failed: $OutElf"
}

if (-not (Test-Path $OutElf)) {
    throw "Linker did not produce output: $OutElf"
}

$WebProcessOutDir = Join-Path $OutDir "webprocess"
New-Item -ItemType Directory -Force -Path $WebProcessOutDir | Out-Null
$OutWebProcess = Join-Path $Root "WebKitWebProcess"
$OutNetworkProcess = Join-Path $Root "WebKitNetworkProcess"
$OutNetTest = Join-Path $Root "net_test"
$OutGpuTest = Join-Path $Root "gpu_test"
$OutVkTest = Join-Path $Root "vk_test"
$OutMouseTest = Join-Path $Root "mouse_test"

$WPObjects = @()
foreach ($src in $WebProcessSources) {
    $obj = Join-Path $WebProcessOutDir "wp_$([System.IO.Path]::GetFileNameWithoutExtension($src)).o"
    clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $WPObjects += $obj
}

ld.lld `
    -m elf_x86_64 `
    -nostdlib `
    -static `
    --gc-sections `
    -z max-page-size=0x1000 `
    @WPObjects `
    -o $OutWebProcess
if ($LASTEXITCODE -ne 0) {
    throw "Link failed: $OutWebProcess"
}

Write-Host "Built WebKitWebProcess stub: $OutWebProcess"

$NPOutDir = Join-Path $OutDir "networkprocess"
New-Item -ItemType Directory -Force -Path $NPOutDir | Out-Null

$NPObjects = @()
foreach ($src in $NetworkProcessSources) {
    $obj = Join-Path $NPOutDir "np_$([System.IO.Path]::GetFileNameWithoutExtension($src)).o"
    clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $NPObjects += $obj
}

ld.lld `
    -m elf_x86_64 `
    -nostdlib `
    -static `
    --gc-sections `
    -z max-page-size=0x1000 `
    @NPObjects `
    -o $OutNetworkProcess
if ($LASTEXITCODE -ne 0) {
    throw "Link failed: $OutNetworkProcess"
}

Write-Host "Built WebKitNetworkProcess stub: $OutNetworkProcess"

$NTOutDir = Join-Path $OutDir "nettest"
New-Item -ItemType Directory -Force -Path $NTOutDir | Out-Null

$NTObjects = @()
foreach ($src in $NetTestSources) {
    $obj = Join-Path $NTOutDir "nt_$([System.IO.Path]::GetFileNameWithoutExtension($src)).o"
    clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $NTObjects += $obj
}

ld.lld `
    -m elf_x86_64 `
    -nostdlib `
    -static `
    --gc-sections `
    -z max-page-size=0x1000 `
    @NTObjects `
    -o $OutNetTest
if ($LASTEXITCODE -ne 0) {
    throw "Link failed: $OutNetTest"
}

Write-Host "Built net test: $OutNetTest"

$GTOutDir = Join-Path $OutDir "gputest"
New-Item -ItemType Directory -Force -Path $GTOutDir | Out-Null

$GTObjects = @()
foreach ($src in $GpuTestSources) {
    $obj = Join-Path $GTOutDir "gt_$([System.IO.Path]::GetFileNameWithoutExtension($src)).o"
    clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $GTObjects += $obj
}

ld.lld `
    -m elf_x86_64 `
    -nostdlib `
    -static `
    --gc-sections `
    -z max-page-size=0x1000 `
    @GTObjects `
    -o $OutGpuTest
if ($LASTEXITCODE -ne 0) {
    throw "Link failed: $OutGpuTest"
}

Write-Host "Built gpu test: $OutGpuTest"

$VKOutDir = Join-Path $OutDir "vktest"
New-Item -ItemType Directory -Force -Path $VKOutDir | Out-Null

$VKObjects = @()
foreach ($src in $VkTestSources) {
    $obj = Join-Path $VKOutDir "vk_$([System.IO.Path]::GetFileNameWithoutExtension($src)).o"
    clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $VKObjects += $obj
}

ld.lld `
    -m elf_x86_64 `
    -nostdlib `
    -static `
    --gc-sections `
    -z max-page-size=0x1000 `
    @VKObjects `
    -o $OutVkTest
if ($LASTEXITCODE -ne 0) {
    throw "Link failed: $OutVkTest"
}

Write-Host "Built vk test: $OutVkTest"

$MTOutDir = Join-Path $OutDir "mousetest"
New-Item -ItemType Directory -Force -Path $MTOutDir | Out-Null

$MTObjects = @()
foreach ($src in $MouseTestSources) {
    $obj = Join-Path $MTOutDir "mt_$([System.IO.Path]::GetFileNameWithoutExtension($src)).o"
    clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $MTObjects += $obj
}

ld.lld `
    -m elf_x86_64 `
    -nostdlib `
    -static `
    --gc-sections `
    -z max-page-size=0x1000 `
    @MTObjects `
    -o $OutMouseTest
if ($LASTEXITCODE -ne 0) {
    throw "Link failed: $OutMouseTest"
}

Write-Host "Built mouse test: $OutMouseTest"

$Artifacts = @(
    @{
        Source = "/usr/lib64/ld-linux-x86-64.so.2"
        Destination = (Join-Path $Root "lib64/ld-linux-x86-64.so.2")
    },
    @{
        Source = "/usr/lib64/ld-linux-x86-64.so.2"
        Destination = (Join-Path $Root "ld-linux-x86-64.so.2")
    }
)

foreach ($artifact in $Artifacts) {
    if (-not (Test-Path $artifact.Source)) {
        Write-Warning "Optional WebKit runtime artifact not found: $($artifact.Source); skipping it."
        continue
    }

    New-Item -ItemType Directory -Force -Path (Split-Path $artifact.Destination -Parent) | Out-Null
    Copy-Item $artifact.Source $artifact.Destination -Force
}

Write-Host "Built userspace launcher: $OutElf"
Write-Host "Staged WebKit runtime artifacts."
# ── WSL: prefer full build+stage of the Linux-side WPE runtime ───────────────
$_stageScript = Join-Path $Root "scripts\build_and_stage_webkit.sh"
if (Test-Path $_stageScript) {
    $wslPathResult = wsl wslpath -u $_stageScript 2>$null
    if ($LASTEXITCODE -eq 0 -and $wslPathResult) {
        Write-Host "  Attempting full WebKit runtime rebuild+stage via WSL..."
        try { $null = wsl bash $wslPathResult.Trim() 2>$null } catch { }
        Write-Host "  Done (check staged runtime artifacts for result)."
    }
}
Write-Host "WebKit userspace build complete."
exit 0
