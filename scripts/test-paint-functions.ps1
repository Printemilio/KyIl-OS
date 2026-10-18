# Same real inputs and pixel checks on the two production apps, after the
# benchmark harness has created ten windows. Use FirstWindowId for other runs.
param([int]$FirstWindowId=11,[string]$Python='C:/Users/emili/AppData/Local/hermes/hermes-agent/venv/Scripts/python.exe')
$ErrorActionPreference='Stop';$out='build/paint-comparison/functions'
New-Item -ItemType Directory -Force $out | Out-Null
$inputScript="$PSScriptRoot/vm-browser-input.ps1"
function Click([int]$x,[int]$y){& $inputScript -Action click -X $x -Y $y -VerifyCursor}
function Snap([string]$name){$path="$out/$name.png";& $inputScript -Action screenshot -Screenshot $path;return $path}
function Pixel([string]$shot,[int]$x,[int]$y,[int[]]$rgb){
  $found=& $Python "$PSScriptRoot/paint-pixels.py" $shot --probe $x $y | ConvertFrom-Json
  if(($found.rgb -join ',')-ne($rgb -join ',')){throw "Pixel $x,$y expected $rgb, got $($found.rgb): $shot"}
}
foreach($kind in @('lvgl','suchu')) {
  Click 180 400
  $path=if($kind-eq 'lvgl'){'/vex_paint'}else{'/usr/bin/kyil-paint'}
  & $inputScript -Action keys -Text "run $path";& $inputScript -Action keys -ScanCodes @('1c','9c')
  Start-Sleep -Seconds 2
  $x=18+($FirstWindowId%4)*28;$y=72+($FirstWindowId%4)*22;++$FirstWindowId
  $cx=$x+8;$cy=$y+51
  Click ($cx+500) ($cy+100);$s=Snap "$kind-red";Pixel $s ($cx+496) ($cy+104) @(255,0,0)
  Click ($cx+22) ($cy+590);Click ($cx+600) ($cy+150)
  $s=Snap "$kind-black";Pixel $s ($cx+596) ($cy+154) @(0,0,0)
  Click ($cx+22) ($cy+629);Click ($cx+22) ($cy+659);Click ($cx+700) ($cy+200)
  $s=Snap "$kind-small";Pixel $s ($cx+696) ($cy+204) @(56,32,255);Pixel $s ($cx+725) ($cy+200) @(217,217,217)
  Click ($cx+22) ($cy+703);Click ($cx+800) ($cy+200)
  $s=Snap "$kind-large";Pixel $s ($cx+825) ($cy+200) @(56,32,255)
  Click ($cx+22) ($cy+681);Click ($cx+32) ($cy+518);Click ($cx+500) ($cy+100)
  $s=Snap "$kind-eraser";Pixel $s ($cx+496) ($cy+104) @(217,217,217)
  & $inputScript -Action move -X ($cx+32) -Y ($cy+518) -VerifyCursor
  $vb=New-Object -ComObject VirtualBox.VirtualBox;$session=New-Object -ComObject VirtualBox.Session
  $vb.FindMachine('Vex-OS').LockMachine($session,1)
  try{
    $session.Console.Mouse.PutMouseEvent(0,0,0,0,1);Start-Sleep -Milliseconds 550
    $s=Snap "$kind-clear-held";Pixel $s ($cx+596) ($cy+154) @(217,217,217)
  }finally{$session.Console.Mouse.PutMouseEvent(0,0,0,0,0);$session.UnlockMachine()}
  & $inputScript -Action drag -X ($cx+300) -Y ($cy+100) -ToX ($cx+900) -ToY ($cy+250) -VerifyCursor
  $null=Snap "$kind-drag"
  & $inputScript -Action drag -X ($x+1215) -Y ($y+776) -ToX ($x+495) -ToY ($y+336) -VerifyCursor
  Start-Sleep -Milliseconds 200;$s=Snap "$kind-compact"
  Pixel $s ($cx+116) ($cy+42) @(56,32,255)
  Click ($x+474) ($y+23);Start-Sleep -Milliseconds 300
}
Click 180 400;& $inputScript -Action keys -Text 'run /usr/native-tests/suchu_paint_test'
& $inputScript -Action keys -ScanCodes @('1c','9c');Start-Sleep -Seconds 2
$null=Snap 'model-tests';Copy-Item build/vex-serial.log "$out/serial.log" -Force
Write-Output 'PASS: production Paint colors/sizes/eraser/stationary clear/strokes/compact resize'
