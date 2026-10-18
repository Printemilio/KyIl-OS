param([switch]$Compile)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if($Compile){
    $WslRoot='/mnt/c/'+$Root.Substring(3).Replace('\','/')
    & wsl -d Ubuntu-24.04 -- /bin/sh "$WslRoot/scripts/build-vulkan-shaders.sh"
    if($LASTEXITCODE -ne 0){throw 'Khronos shader compilation/validation failed'}
}
$Output="/* Generated from GLSL by Khronos glslang; spirv-val --target-env vulkan1.0. */`n"
foreach($Source in Get-ChildItem -LiteralPath (Join-Path $Root 'userspace/apps/shaders') -File | Where-Object Extension -In '.comp','.frag' | Sort-Object BaseName){
    $File=Get-Item -LiteralPath (Join-Path $Root "build/compute-shaders/$($Source.BaseName).spv")
    $Bytes=[IO.File]::ReadAllBytes($File.FullName)
    $Output+="static const uint32_t $($File.BaseName)_spv[]={"
    for($Offset=0;$Offset -lt $Bytes.Length;$Offset+=4){
        if($Offset%32 -eq 0){$Output+="`n"}
        $Output+=('0x{0:x8}u,' -f [BitConverter]::ToUInt32($Bytes,$Offset))
    }
    $Output+="`n};`n"
}
[IO.File]::WriteAllText((Join-Path $Root 'userspace/apps/vk_compute_shaders.h'),$Output,[Text.UTF8Encoding]::new($false))
