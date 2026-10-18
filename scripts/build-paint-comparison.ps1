$ErrorActionPreference='Stop'
$out='build/paint-comparison';New-Item -ItemType Directory -Force $out,usr/native-tests | Out-Null
$clang='C:/Program Files/LLVM/bin/clang.exe';$ld='C:/Program Files/LLVM/bin/ld.lld.exe'
$flags=@('-target','x86_64-unknown-none-elf','-O2','-ffreestanding','-fno-builtin','-ffunction-sections','-fdata-sections','-fno-stack-protector','-fno-pic','-m64','-mno-red-zone','-idirafter','kernel/include','-Iuserspace/libvex/include','-Ivendor/lvgl','-Ivendor/lvgl/src','-DLV_CONF_INCLUDE_SIMPLE')
$common=@('build/userspace-webkit/vexpaint/vp_crt0.o','build/userspace-webkit/vexpaint/vp_runtime.o','build/userspace-webkit/vexpaint/vp_vex_posix_compat.o')
foreach($s in @('userspace/libvex/src/vex_lvgl.c','userspace/libvex/src/vex_bench.c','userspace/apps/paint_bench_runner.c')){
    & $clang @flags -c $s -o "$out/$([IO.Path]::GetFileNameWithoutExtension($s)).o"
    if($LASTEXITCODE){throw "Compile failed $s"}
}
foreach($measured in @($false,$true)){
    $extra=@();if($measured){$extra=@('-DPAINT_BENCHMARK')}
    & $clang @flags @extra -c userspace/apps/vex_paint.c -o "$out/paint.o"
    if($LASTEXITCODE){throw 'Paint compile failed'}
    $dest=if($measured){'usr/native-tests/paint_lvgl_bench'}else{'vex_paint'}
    $profile=@();if($measured){$profile=@("$out/vex_bench.o")}
    & $ld -m elf_x86_64 -nostdlib -static --gc-sections -z max-page-size=0x1000 @common "$out/vex_lvgl.o" "$out/paint.o" @profile build/userspace-webkit/lvgl/liblvgl.a -o $dest
    if($LASTEXITCODE){throw 'Paint link failed'}
}
& $ld -m elf_x86_64 -nostdlib -static --gc-sections -z max-page-size=0x1000 @common "$out/paint_bench_runner.o" "$out/vex_bench.o" -o usr/native-tests/paint_runner
if($LASTEXITCODE){throw 'Runner link failed'}
Copy-Item vex_paint build/iso_root/vex_paint -Force
Copy-Item usr/native-tests/paint* build/iso_root/usr/native-tests -Force
