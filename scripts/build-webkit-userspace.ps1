param(
    [switch]$Fast,
    [string]$ExtraFlags = "",
    [switch]$SkipWebKitRuntime
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
$StageScript = Join-Path $Root "scripts\build_and_stage_webkit.sh"

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

function Quote-NativeArg {
    param([string]$Arg)
    return '"' + ($Arg -replace '\\', '\\' -replace '"', '\"') + '"'
}

function Invoke-NativeCapture {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FileName,
        [string[]]$Arguments = @()
    )

    $ProcessInfo = New-Object System.Diagnostics.ProcessStartInfo
    $ProcessInfo.FileName = $FileName
    $ProcessInfo.Arguments = (($Arguments | ForEach-Object { Quote-NativeArg $_ }) -join " ")
    $ProcessInfo.UseShellExecute = $false
    $ProcessInfo.RedirectStandardOutput = $true
    $ProcessInfo.RedirectStandardError = $true

    $Process = [System.Diagnostics.Process]::Start($ProcessInfo)
    $Stdout = $Process.StandardOutput.ReadToEnd()
    $Stderr = $Process.StandardError.ReadToEnd()
    $Process.WaitForExit()

    [PSCustomObject]@{
        ExitCode = $Process.ExitCode
        Stdout = $Stdout
        Stderr = $Stderr
    }
}

$Clang = Resolve-CommandPath "clang" @(
    "C:\Program Files\LLVM\bin\clang.exe"
)
$LdLld = Resolve-CommandPath "ld.lld" @(
    "C:\Program Files\LLVM\bin\ld.lld.exe"
)
$LlvmAr = Resolve-CommandPath "llvm-ar" @(
    "C:\Program Files\LLVM\bin\llvm-ar.exe"
)

if ((-not $SkipWebKitRuntime) -and (Test-Path $StageScript)) {
    $Wsl = Get-Command "wsl" -ErrorAction SilentlyContinue
    if ($Wsl) {
        $WslPath = Invoke-NativeCapture $Wsl.Source @("wslpath", "-u", $StageScript)
    } else {
        $WslPath = [PSCustomObject]@{ ExitCode = 1; Stdout = ""; Stderr = "wsl.exe was not found." }
    }

    # On hosts with a disabled/default WSL distribution, the unqualified
    # invocation can fail with E_ACCESSDENIED even though an installed distro
    # is usable.  Retry through an explicit distro when requested (or when the
    # standard Ubuntu image is present); this keeps the build deterministic
    # without changing the native VexOS runtime path.
    $WslUseExplicitDistro = $false
    $WslDistro = if ($env:VEX_WSL_DISTRO) { $env:VEX_WSL_DISTRO } else { "Ubuntu-24.04" }
    if (($WslPath.ExitCode -ne 0 -or -not $WslPath.Stdout.Trim()) -and $Wsl) {
        $ExplicitWslPath = Invoke-NativeCapture $Wsl.Source @(
            "-d", $WslDistro, "--", "wslpath", "-u", $StageScript
        )
        if ($ExplicitWslPath.ExitCode -eq 0 -and $ExplicitWslPath.Stdout.Trim()) {
            $WslPath = $ExplicitWslPath
            $WslUseExplicitDistro = $true
        }
    }

    if ($WslPath.ExitCode -eq 0 -and $WslPath.Stdout.Trim()) {
        Write-Host "  Attempting full WebKit runtime rebuild+stage via WSL..."
        $WslStageArgs = if ($WslUseExplicitDistro) {
            @("-d", $WslDistro, "--", "bash", $WslPath.Stdout.Trim())
        } else {
            @("bash", $WslPath.Stdout.Trim())
        }
        $WslStage = Invoke-NativeCapture $Wsl.Source $WslStageArgs
        if ($WslStage.Stdout) {
            Write-Host $WslStage.Stdout.TrimEnd()
        }
        if ($WslStage.ExitCode -ne 0) {
            Write-Warning "Full WPE/WebKit runtime was not staged. The native fallback renderer will still be packaged."
        } else {
            Write-Host "  Full WPE/WebKit runtime staged."
        }
    } else {
        Write-Warning "WSL is not available; skipping full WebKit runtime staging. The native fallback renderer will still be packaged."
    }
} elseif ($SkipWebKitRuntime) {
    Write-Warning "Skipping full WebKit runtime staging by request. Native userspace apps will still be rebuilt."
}
$BrowserAppSource = if (Test-Path (Join-Path $Root "webkit_linux_main")) {
    "$AppDir/webkit_launcher.c"
} else {
    "$AppDir/webkit_web_process.c"
}

# Note: the runtime still reuses some legacy implementation code, but the
# active WebKit path is now wired through vex_web_* compatibility names.

$CommonFlags = @(
    "-target", "x86_64-unknown-none-elf",
    "-O2", "-ffreestanding", "-fno-builtin",
    "-ffunction-sections", "-fdata-sections", "-fno-stack-protector", "-fno-pic",
    "-m64", "-march=x86-64", "-mabi=sysv", "-mno-red-zone",
    "-idirafter", (Join-Path $Root "kernel/include"),
    "-I$(Join-Path $Root 'vendor/vulkan-headers/include')",
    "-I$IncludeDir",
    "-I$(Join-Path $Root 'vendor/lvgl')",
    "-I$(Join-Path $Root 'vendor/lvgl/src')",
    "-DLV_CONF_INCLUDE_SIMPLE"
)

if ($ExtraFlags -ne "") {
    $CommonFlags += ($ExtraFlags -split '\s+' | Where-Object { $_ -ne "" })
}

# Build LVGL once and reuse the archive in every native Vex application.
# Object timestamps keep normal app-only rebuilds fast.
$LvglRoot = Join-Path $Root "vendor/lvgl/src"
$LvglOutDir = Join-Path $OutDir "lvgl"
$LvglLibrary = Join-Path $LvglOutDir "liblvgl.a"
$LvglConfig = Join-Path $IncludeDir "lv_conf.h"
$LvglObjects = @()
New-Item -ItemType Directory -Force -Path $LvglOutDir | Out-Null

foreach ($src in (Get-ChildItem -Path $LvglRoot -Recurse -File -Filter "*.c" | Sort-Object FullName)) {
    $relative = $src.FullName.Substring($LvglRoot.Length + 1)
    $obj = Join-Path $LvglOutDir ([System.IO.Path]::ChangeExtension($relative, ".o"))
    New-Item -ItemType Directory -Force -Path (Split-Path $obj -Parent) | Out-Null
    if ((-not (Test-Path $obj)) -or
        $src.LastWriteTimeUtc -gt (Get-Item $obj).LastWriteTimeUtc -or
        (Get-Item $LvglConfig).LastWriteTimeUtc -gt (Get-Item $obj).LastWriteTimeUtc) {
        & $Clang @CommonFlags -std=gnu11 -c $src.FullName -o $obj
        if ($LASTEXITCODE -ne 0) {
            throw "LVGL compilation failed: $($src.FullName)"
        }
    }
    $LvglObjects += $obj
}

$LvglArchiveResponse = Join-Path $LvglOutDir "objects.rsp"
[System.IO.File]::WriteAllLines($LvglArchiveResponse, ($LvglObjects | ForEach-Object { '"' + $_ + '"' }))
& $LlvmAr rcs $LvglLibrary "@$LvglArchiveResponse"
if ($LASTEXITCODE -ne 0) {
    throw "LVGL archive creation failed: $LvglLibrary"
}

$Sources = @(
    "$LibDir/crt0.c",
    "$LibDir/runtime.c",
    "$LibDir/vex_posix_compat.c",
    "$LibDir/vulkan.c",
    "$LibDir/vex_web_net.c",
    "$LibDir/vex_http_client.c",
    "$LibDir/vex_page_cache.c",
    $BrowserAppSource
)

$WebProcessSources = @(
    "$LibDir/crt0.c",
    "$LibDir/runtime.c",
    "$LibDir/vex_posix_compat.c",
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
    "$LibDir/vulkan.c",
    "$LibDir/vex_web_net.c",
    "$AppDir/webkit_network_process.c"
)

$NetTestSources = @(
    "$LibDir/crt0.c",
    "$LibDir/runtime.c",
    "$LibDir/vex_posix_compat.c",
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
    "$LibDir/vulkan.c",
    "$AppDir/gpu_test.c"
)

$VkTestSources = @(
    "$LibDir/crt0.c",
    "$LibDir/runtime.c",
    "$LibDir/vex_posix_compat.c",
    "$LibDir/vulkan.c",
    "$AppDir/vk_test.c"
)

$MouseTestSources = @(
    "$LibDir/crt0.c",
    "$LibDir/runtime.c",
    "$LibDir/vex_posix_compat.c",
    "$AppDir/mouse_test.c"
)

$AllocStressSources = @(
    "$LibDir/crt0.c",
    "$LibDir/runtime.c",
    "$LibDir/vex_posix_compat.c",
    "$AppDir/alloc_stress.c"
)

$AppWindowDemoSources = @(
    "$LibDir/crt0.c",
    "$LibDir/runtime.c",
    "$LibDir/vex_posix_compat.c",
    "$AppDir/app_window_demo.c"
)

$GfmSources = @(
    "$LibDir/crt0.c",
    "$LibDir/runtime.c",
    "$LibDir/vex_posix_compat.c",
    "$LibDir/vex_lvgl.c",
    "$AppDir/gfm/gfm.c",
    "$AppDir/gfm/gfm_fs.c",
    "$AppDir/gfm/gfm_sys_vex.c",
    "$AppDir/gfm/gfm_view.c"
)

$VexEditorSources = @(
    "$LibDir/crt0.c",
    "$LibDir/runtime.c",
    "$LibDir/vex_posix_compat.c",
    "$LibDir/vex_lvgl.c",
    "$AppDir/vex_editor.c"
)

$VexPaintSources = @(
    "$LibDir/crt0.c",
    "$LibDir/runtime.c",
    "$LibDir/vex_posix_compat.c",
    "$LibDir/vex_lvgl.c",
    "$AppDir/vex_paint.c"
)

$VexLauncherSources = @(
    "$LibDir/crt0.c",
    "$LibDir/runtime.c",
    "$LibDir/vex_posix_compat.c",
    "$LibDir/vex_lvgl.c",
    "$AppDir/vex_launcher.c"
)

$VexTerminalSources = @(
    "$LibDir/crt0.c",
    "$LibDir/runtime.c",
    "$LibDir/vex_posix_compat.c",
    "$LibDir/vex_lvgl.c",
    "$AppDir/vex_terminal.c"
)

$ElevateTestSources = @(
    "$LibDir/crt0.c",
    "$LibDir/runtime.c",
    "$LibDir/vex_posix_compat.c",
    "$AppDir/elevate_test.c"
)

$AttackTestSources = @(
    "$LibDir/crt0.c",
    "$LibDir/runtime.c",
    "$LibDir/vex_posix_compat.c",
    "$AppDir/attack_test.c"
)

$SecurityBoundarySources = @(
    "$LibDir/crt0.c",
    "$LibDir/runtime.c",
    "$LibDir/vex_posix_compat.c",
    "$AppDir/security_boundary_test.c"
)

$VexEventFaultSources = @(
    "$LibDir/crt0.c",
    "$LibDir/runtime.c",
    "$LibDir/vex_posix_compat.c",
    "$AppDir/vex_event_fault_test.c"
)

$AttackKillTestSources = @(
    "$LibDir/crt0.c",
    "$LibDir/runtime.c",
    "$LibDir/vex_posix_compat.c",
    "$AppDir/attack_kill_test.c"
)

$AttackKillPayloadSources = @(
    "$LibDir/crt0.c",
    "$LibDir/runtime.c",
    "$LibDir/vex_posix_compat.c",
    "$AppDir/attack_kill_payload.c"
)

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
if (Test-Path $OutElf) {
    Remove-Item -Force $OutElf
}

$Objects = @()
foreach ($src in $Sources) {
    $obj = Join-Path $OutDir "$([System.IO.Path]::GetFileNameWithoutExtension($src)).o"
    & $Clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $Objects += $obj
}

& $LdLld `
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
$OutAllocStress = Join-Path $Root "alloc_stress"
$OutAppWindowDemo = Join-Path $Root "app_window_demo"
$OutGfm = Join-Path $Root "gfm"
$OutVexEditor = Join-Path $Root "vex_editor"
$OutVexPaint = Join-Path $Root "vex_paint"
$OutVexLauncher = Join-Path $Root "vex_launcher"
$OutVexTerminal = Join-Path $Root "vex_terminal"
$OutElevateTest = Join-Path $Root "elevate_test"
$OutAttackTest = Join-Path $Root "attack_test"
$OutSecurityBoundary = Join-Path $Root "security_boundary_test"
$OutVexEventFault = Join-Path $Root "vex_event_fault_test"
$OutAttackKillTest = Join-Path $Root "attack_kill_test"
$OutAttackKillPayload = Join-Path $Root "attack_kill_payload"

$WPObjects = @()
foreach ($src in $WebProcessSources) {
    $obj = Join-Path $WebProcessOutDir "wp_$([System.IO.Path]::GetFileNameWithoutExtension($src)).o"
    & $Clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $WPObjects += $obj
}

& $LdLld `
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
    & $Clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $NPObjects += $obj
}

& $LdLld `
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
    & $Clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $NTObjects += $obj
}

& $LdLld `
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
    & $Clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $GTObjects += $obj
}

& $LdLld `
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
    & $Clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $VKObjects += $obj
}

& $LdLld `
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
    & $Clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $MTObjects += $obj
}

& $LdLld `
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

$ASOutDir = Join-Path $OutDir "allocstress"
New-Item -ItemType Directory -Force -Path $ASOutDir | Out-Null

$ASObjects = @()
foreach ($src in $AllocStressSources) {
    $obj = Join-Path $ASOutDir "as_$([System.IO.Path]::GetFileNameWithoutExtension($src)).o"
    & $Clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $ASObjects += $obj
}

& $LdLld `
    -m elf_x86_64 `
    -nostdlib `
    -static `
    --gc-sections `
    -z max-page-size=0x1000 `
    @ASObjects `
    -o $OutAllocStress
if ($LASTEXITCODE -ne 0) {
    throw "Link failed: $OutAllocStress"
}

Write-Host "Built alloc stress: $OutAllocStress"

$AWDOutDir = Join-Path $OutDir "appwindowdemo"
New-Item -ItemType Directory -Force -Path $AWDOutDir | Out-Null

$AWDObjects = @()
foreach ($src in $AppWindowDemoSources) {
    $obj = Join-Path $AWDOutDir "awd_$([System.IO.Path]::GetFileNameWithoutExtension($src)).o"
    & $Clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $AWDObjects += $obj
}

& $LdLld `
    -m elf_x86_64 `
    -nostdlib `
    -static `
    --gc-sections `
    -z max-page-size=0x1000 `
    @AWDObjects `
    -o $OutAppWindowDemo
if ($LASTEXITCODE -ne 0) {
    throw "Link failed: $OutAppWindowDemo"
}

Write-Host "Built app window demo: $OutAppWindowDemo"

$GfmOutDir = Join-Path $OutDir "gfm"
New-Item -ItemType Directory -Force -Path $GfmOutDir | Out-Null

$GfmObjects = @()
foreach ($src in $GfmSources) {
    $obj = Join-Path $GfmOutDir "gfm_$([System.IO.Path]::GetFileNameWithoutExtension($src)).o"
    & $Clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $GfmObjects += $obj
}

& $LdLld `
    -m elf_x86_64 `
    -nostdlib `
    -static `
    --gc-sections `
    -z max-page-size=0x1000 `
    @GfmObjects `
    $LvglLibrary `
    -o $OutGfm
if ($LASTEXITCODE -ne 0) {
    throw "Link failed: $OutGfm"
}

Write-Host "Built GFM (Graphical File Manager): $OutGfm"

$VEOutDir = Join-Path $OutDir "vexeditor"
New-Item -ItemType Directory -Force -Path $VEOutDir | Out-Null

$VEObjects = @()
foreach ($src in $VexEditorSources) {
    $obj = Join-Path $VEOutDir "ve_$([System.IO.Path]::GetFileNameWithoutExtension($src)).o"
    & $Clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $VEObjects += $obj
}

& $LdLld `
    -m elf_x86_64 `
    -nostdlib `
    -static `
    --gc-sections `
    -z max-page-size=0x1000 `
    @VEObjects `
    $LvglLibrary `
    -o $OutVexEditor
if ($LASTEXITCODE -ne 0) {
    throw "Link failed: $OutVexEditor"
}

Write-Host "Built Vex Editor app: $OutVexEditor"

$VPOutDir = Join-Path $OutDir "vexpaint"
New-Item -ItemType Directory -Force -Path $VPOutDir | Out-Null

$VPObjects = @()
foreach ($src in $VexPaintSources) {
    $obj = Join-Path $VPOutDir "vp_$([System.IO.Path]::GetFileNameWithoutExtension($src)).o"
    & $Clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $VPObjects += $obj
}

& $LdLld `
    -m elf_x86_64 `
    -nostdlib `
    -static `
    --gc-sections `
    -z max-page-size=0x1000 `
    @VPObjects `
    $LvglLibrary `
    -o $OutVexPaint
if ($LASTEXITCODE -ne 0) {
    throw "Link failed: $OutVexPaint"
}

Write-Host "Built Vex Paint app: $OutVexPaint"

$VLOutDir = Join-Path $OutDir "vexlauncher"
New-Item -ItemType Directory -Force -Path $VLOutDir | Out-Null

$VLObjects = @()
foreach ($src in $VexLauncherSources) {
    $obj = Join-Path $VLOutDir "vl_$([System.IO.Path]::GetFileNameWithoutExtension($src)).o"
    & $Clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $VLObjects += $obj
}

& $LdLld `
    -m elf_x86_64 `
    -nostdlib `
    -static `
    --gc-sections `
    -z max-page-size=0x1000 `
    @VLObjects `
    $LvglLibrary `
    -o $OutVexLauncher
if ($LASTEXITCODE -ne 0) {
    throw "Link failed: $OutVexLauncher"
}

Write-Host "Built Vex Launcher app: $OutVexLauncher"

$VTOutDir = Join-Path $OutDir "vexterminal"
New-Item -ItemType Directory -Force -Path $VTOutDir | Out-Null

$VTObjects = @()
foreach ($src in $VexTerminalSources) {
    $obj = Join-Path $VTOutDir "vt_$([System.IO.Path]::GetFileNameWithoutExtension($src)).o"
    & $Clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $VTObjects += $obj
}

& $LdLld `
    -m elf_x86_64 `
    -nostdlib `
    -static `
    --gc-sections `
    -z max-page-size=0x1000 `
    @VTObjects `
    $LvglLibrary `
    -o $OutVexTerminal
if ($LASTEXITCODE -ne 0) {
    throw "Link failed: $OutVexTerminal"
}

Write-Host "Built Vex Terminal app: $OutVexTerminal"

$ETOutDir = Join-Path $OutDir "elevatetest"
New-Item -ItemType Directory -Force -Path $ETOutDir | Out-Null

$ETObjects = @()
foreach ($src in $ElevateTestSources) {
    $obj = Join-Path $ETOutDir "et_$([System.IO.Path]::GetFileNameWithoutExtension($src)).o"
    & $Clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $ETObjects += $obj
}

& $LdLld `
    -m elf_x86_64 `
    -nostdlib `
    -static `
    --gc-sections `
    -z max-page-size=0x1000 `
    @ETObjects `
    -o $OutElevateTest
if ($LASTEXITCODE -ne 0) {
    throw "Link failed: $OutElevateTest"
}

Write-Host "Built elevate test: $OutElevateTest"

$ATOutDir = Join-Path $OutDir "attacktest"
New-Item -ItemType Directory -Force -Path $ATOutDir | Out-Null

$ATObjects = @()
foreach ($src in $AttackTestSources) {
    $obj = Join-Path $ATOutDir "at_$([System.IO.Path]::GetFileNameWithoutExtension($src)).o"
    & $Clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $ATObjects += $obj
}

& $LdLld `
    -m elf_x86_64 `
    -nostdlib `
    -static `
    --gc-sections `
    -z max-page-size=0x1000 `
    @ATObjects `
    -o $OutAttackTest
if ($LASTEXITCODE -ne 0) {
    throw "Link failed: $OutAttackTest"
}

Write-Host "Built attack test: $OutAttackTest"

$SBOutDir = Join-Path $OutDir "securityboundarytest"
New-Item -ItemType Directory -Force -Path $SBOutDir | Out-Null

$SBObjects = @()
foreach ($src in $SecurityBoundarySources) {
    $obj = Join-Path $SBOutDir "sb_$([System.IO.Path]::GetFileNameWithoutExtension($src)).o"
    & $Clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $SBObjects += $obj
}

& $LdLld `
    -m elf_x86_64 `
    -nostdlib `
    -static `
    --gc-sections `
    -z max-page-size=0x1000 `
    @SBObjects `
    -o $OutSecurityBoundary
if ($LASTEXITCODE -ne 0) {
    throw "Link failed: $OutSecurityBoundary"
}

Write-Host "Built security boundary test: $OutSecurityBoundary"

$VEFOutDir = Join-Path $OutDir "vexeventfaulttest"
New-Item -ItemType Directory -Force -Path $VEFOutDir | Out-Null
$VEFObjects = @()
foreach ($src in $VexEventFaultSources) {
    $obj = Join-Path $VEFOutDir "vef_$([System.IO.Path]::GetFileNameWithoutExtension($src)).o"
    & $Clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $VEFObjects += $obj
}
& $LdLld -m elf_x86_64 -nostdlib -static --gc-sections -z max-page-size=0x1000 @VEFObjects -o $OutVexEventFault
if ($LASTEXITCODE -ne 0) {
    throw "Link failed: $OutVexEventFault"
}
Write-Host "Built native synchronous fault-event test: $OutVexEventFault"

$AKOutDir = Join-Path $OutDir "attackkilltest"
New-Item -ItemType Directory -Force -Path $AKOutDir | Out-Null

$AKObjects = @()
foreach ($src in $AttackKillTestSources) {
    $obj = Join-Path $AKOutDir "ak_$([System.IO.Path]::GetFileNameWithoutExtension($src)).o"
    & $Clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $AKObjects += $obj
}

& $LdLld `
    -m elf_x86_64 `
    -nostdlib `
    -static `
    --gc-sections `
    -z max-page-size=0x1000 `
    @AKObjects `
    -o $OutAttackKillTest
if ($LASTEXITCODE -ne 0) {
    throw "Link failed: $OutAttackKillTest"
}

Write-Host "Built attack kill test: $OutAttackKillTest"

$AKPOutDir = Join-Path $OutDir "attackkillpayload"
New-Item -ItemType Directory -Force -Path $AKPOutDir | Out-Null

$AKPObjects = @()
foreach ($src in $AttackKillPayloadSources) {
    $obj = Join-Path $AKPOutDir "akp_$([System.IO.Path]::GetFileNameWithoutExtension($src)).o"
    & $Clang @CommonFlags -std=gnu11 -c $src -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $src"
    }
    $AKPObjects += $obj
}

& $LdLld `
    -m elf_x86_64 `
    -nostdlib `
    -static `
    --gc-sections `
    -z max-page-size=0x1000 `
    @AKPObjects `
    -o $OutAttackKillPayload
if ($LASTEXITCODE -ne 0) {
    throw "Link failed: $OutAttackKillPayload"
}

Write-Host "Built attack kill payload: $OutAttackKillPayload"

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
Write-Host "WebKit userspace build complete."
exit 0
