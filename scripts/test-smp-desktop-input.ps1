param([ValidateRange(1,4)][int]$CpuCount=4,[switch]$RunJsc,[switch]$LegacyMotion)
# Real PS/2 input on the already running NORMAL Desktop ISO. Captures require
# visual review: this script does not infer pixel correctness from a callback.
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$taskVBox='C:/Program Files/Oracle/VirtualBox/VBoxManage.exe'
$taskLog=Join-Path $taskRoot 'build/vex-serial.log'
$taskInput=Join-Path $PSScriptRoot 'vm-browser-input.ps1'
$taskInfo=& $taskVBox showvminfo Vex-OS --machinereadable
if(!($taskInfo -match "^cpus=$CpuCount$")){throw 'Unexpected CPU configuration'}
$taskPrefix="build/smp/desktop-$CpuCount"
function Input { & $taskInput -VerifyCursor:(!$LegacyMotion) @args; if($LASTEXITCODE){throw 'Guest input failed'} }
function Wait-Log([string]$pattern,[int]$seconds=120) {
 $deadline=[DateTime]::UtcNow.AddSeconds($seconds)
 do {
  $text=Get-Content -LiteralPath $taskLog -Raw
  if($text -match 'KERNEL PANIC'){throw 'Kernel panic'}
  if($text -match $pattern){return}
  Start-Sleep -Milliseconds 250
 } while([DateTime]::UtcNow -lt $deadline)
 throw "Missing VM observation: $pattern"
}
Wait-Log 'gpu: driver software-framebuffer'
Start-Sleep -Seconds 2
Input -Action click -X 400 -Y 200 -Button 2
Input -Action click -X 480 -Y 227
Start-Sleep -Seconds 1
Input -Action click -X 400 -Y 350
Input -Action keys -Text "echo smp$CpuCount"
Input -Action keys -ScanCodes @('1c','9c')
Input -Action screenshot -Screenshot "$taskPrefix-terminal.png"
Input -Action drag -X 951 -Y 1066 -ToX 817 -ToY 850
Input -Action drag -X 310 -Y 88 -ToX 500 -ToY 210
Input -Action click -X 1500 -Y 200 -Button 2
Input -Action click -X 1560 -Y 340
Start-Sleep -Seconds 1
Input -Action drag -X 1260 -Y 869 -ToX 1098 -ToY 759
Input -Action drag -X 360 -Y 118 -ToX 760 -ToY 230
Input -Action drag -X 700 -Y 450 -ToX 950 -ToY 620
Input -Action screenshot -Screenshot "$taskPrefix-paint.png"
Input -Action click -X 1476 -Y 230
Input -Action click -X 1500 -Y 200 -Button 2
Input -Action click -X 1560 -Y 300
Wait-Log 'webkit_linux_main: entering main loop'
Input -Action click -X 1300 -Y 530
Input -Action keys -Text 'https://example.com/'
Input -Action keys -ScanCodes @('1c','9c')
Wait-Log 'browser navigation timing: tab=1 generation=1.*"protocol":"h2"'
Wait-Log 'target.frame_rendered'
Start-Sleep -Seconds 1
Input -Action screenshot -Screenshot "$taskPrefix-https.png"
Input -Action keys -ScanCodes @('1d','26','a6','9d')
Input -Action keys -Text 'file:///usr/linux-tests/webkit-smp.html'
Input -Action keys -ScanCodes @('1c','9c')
Start-Sleep -Seconds 5
Input -Action click -X 400 -Y 350
Input -Action keys -Text 'flowstats'
Input -Action keys -ScanCodes @('1c','9c')
Start-Sleep -Seconds 6
Input -Action keys -Text 'flowstats'
Input -Action keys -ScanCodes @('1c','9c')
$taskWorkerDeadline=[DateTime]::UtcNow.AddSeconds(90)
do {
 Input -Action screenshot -Screenshot "$taskPrefix-workers.png"
 $taskWorkerResult=& 'C:/Users/emili/AppData/Local/hermes/hermes-agent/venv/Scripts/python.exe' (Join-Path $PSScriptRoot 'vm-cursor-position.py') (Join-Path $taskRoot "$taskPrefix-workers.png") --worker-result
 if($LASTEXITCODE){throw 'Worker pixel observation failed'}
 $taskWorkerObservation=$taskWorkerResult | ConvertFrom-Json
 if($taskWorkerObservation.passed){break}
 Start-Sleep -Seconds 1
}while([DateTime]::UtcNow -lt $taskWorkerDeadline)
if(!$taskWorkerObservation.passed){throw 'Both JSC workers did not publish their real verified result'}
Input -Action click -X 1400 -Y 400
Input -Action keys -ScanCodes @('1d','26','a6','9d')
Input -Action keys -Text 'file:///usr/linux-tests/webkit-render.html'
Input -Action keys -ScanCodes @('1c','9c')
Start-Sleep -Seconds 4
Input -Action click -X 1080 -Y 330
Input -Action click -X 1260 -Y 330
Input -Action keys -Text "smp$CpuCount"
Input -Action wheel -X 1500 -Y 600 -Wheel 3
Input -Action drag -X 1906 -Y 1066 -ToX 1765 -ToY 950
Input -Action drag -X 1400 -Y 88 -ToX 1250 -ToY 200
Input -Action screenshot -Screenshot "$taskPrefix-render-input.png"
Input -Action click -X 1595 -Y 200
Start-Sleep -Seconds 3
Input -Action click -X 400 -Y 350
Input -Action keys -Text 'top'
Input -Action keys -ScanCodes @('1c','9c')
Start-Sleep -Seconds 2
Input -Action screenshot -Screenshot "$taskPrefix-closed-top.png"
if($RunJsc) {
 Input -Action keys -Text 'run /usr/linux-tests/linux_jsc_jit_test'
 Input -Action keys -ScanCodes @('1c','9c')
 Wait-Log 'JSC tier regression passed: mode=3' 180
}
Copy-Item -LiteralPath $taskLog -Destination (Join-Path $taskRoot "$taskPrefix.log") -Force
Write-Output "Native input sequence completed for $CpuCount CPU(s); review screenshots and log."
