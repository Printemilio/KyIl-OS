$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$PeBuildDir = Join-Path $Root "build\pe"
$PeTestsDir = Join-Path $Root "usr\pe-tests"
$ProbeDir = Join-Path $Root "userspace\pe_probe"

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

$Clang = Resolve-CommandPath "clang" @("C:\Program Files\LLVM\bin\clang.exe")
$DllTool = Resolve-CommandPath "llvm-dlltool" @("C:\Program Files\LLVM\bin\llvm-dlltool.exe")

New-Item -ItemType Directory -Force -Path @($PeBuildDir, $PeTestsDir) | Out-Null

$Kernel32ImportLib = Join-Path $PeBuildDir "libkernel32.a"
Invoke-Native $DllTool @(
    "-m", "i386:x86-64",
    "-d", (Join-Path $ProbeDir "kernel32.def"),
    "-l", $Kernel32ImportLib
)

Invoke-Native $Clang @(
    "-target", "x86_64-windows-gnu",
    "-nostdlib",
    "-Wl,--entry,mainCRTStartup",
    "-Wl,--subsystem,console",
    (Join-Path $ProbeDir "pe_probe.c"),
    $Kernel32ImportLib,
    "-o", (Join-Path $PeTestsDir "pe_probe.exe")
)

Write-Host "Built PE test: $(Join-Path $PeTestsDir "pe_probe.exe")"
