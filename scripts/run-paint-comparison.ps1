param([int]$CpuCount=1,[int]$Repetitions=3,[string]$Python='C:/Users/emili/AppData/Local/hermes/hermes-agent/venv/Scripts/python.exe')
$ErrorActionPreference='Stop';$dir="build/paint-comparison/raw/cpu$CpuCount"
New-Item -ItemType Directory -Force $dir | Out-Null
$vbox='C:/Program Files/Oracle/VirtualBox/VBoxManage.exe';$inputScript="$PSScriptRoot/vm-browser-input.ps1"
function Click([int]$x,[int]$y,[int]$b=1){& $inputScript -Action click -X $x -Y $y -Button $b -VerifyCursor}
function Capture([string]$name){$path="$dir/$name.png";& $inputScript -Action screenshot -Screenshot $path;return $path}
function Occurrences([string]$pattern){return @(Select-String $pattern build/vex-serial.log).Count}
function Await-Log([string]$pattern,[int]$before,[int]$seconds=90){
    $deadline=[DateTime]::UtcNow.AddSeconds($seconds)
    do{if((Occurrences $pattern)-gt $before){return};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow-lt $deadline)
    throw "Timeout $pattern"
}
$deadline=[DateTime]::UtcNow.AddSeconds(90)
do{$ready=Capture 'ready';$null=& $Python "$PSScriptRoot/vm-cursor-position.py" $ready 2>$null
    if(!$LASTEXITCODE){break};Start-Sleep -Milliseconds 500}while([DateTime]::UtcNow-lt $deadline)
Click 1250 200 2;Click 1310 227;Click 180 400
$windowId=1;$observations=@()
$startInfo=[Diagnostics.ProcessStartInfo]::new();$startInfo.FileName=$Python
$startInfo.ArgumentList.Add((Join-Path $PSScriptRoot 'paint-pixels.py'));$startInfo.ArgumentList.Add('--stream')
$startInfo.UseShellExecute=$false;$startInfo.CreateNoWindow=$true
$startInfo.RedirectStandardInput=$true;$startInfo.RedirectStandardOutput=$true
$pixelReader=[Diagnostics.Process]::Start($startInfo)
if($pixelReader.StandardOutput.ReadLine()-ne 'READY'){throw 'Pixel reader did not initialize'}
foreach($trial in 1..$Repetitions) {
  foreach($kind in $(if($trial%2){@('lvgl','suchu')}else{@('suchu','lvgl')})) {
    $readyCount=Occurrences "PAINTBENCH READY $kind ";$doneCount=Occurrences "PAINTBENCH DONE $kind";$exitCount=Occurrences "PAINTBENCH EXIT $kind "
    Click 180 400
    & $inputScript -Action keys -Text "run /usr/native-tests/paint_runner $kind"
    & $inputScript -Action keys -ScanCodes @('1c','9c')
    Await-Log "PAINTBENCH READY $kind " $readyCount
    $x=18+($windowId%4)*28;$y=72+($windowId%4)*22;++$windowId
    Click ($x+350) ($y+23)
    $before=Capture "$kind-$trial-before"
    & $inputScript -Action move -X ($x+608) -Y ($y+251) -VerifyCursor
    $vb=New-Object -ComObject VirtualBox.VirtualBox;$session=New-Object -ComObject VirtualBox.Session
    $vb.FindMachine('Vex-OS').LockMachine($session,1)
    try {
        $watch=[Diagnostics.Stopwatch]::StartNew();$session.Console.Mouse.PutMouseEvent(0,0,0,0,1)
        do {
            $shot=Capture "$kind-$trial-visible"
            $pixelReader.StandardInput.WriteLine((@{path=(Join-Path (Get-Location) $shot);x=$x+605;y=$y+256}|ConvertTo-Json -Compress))
            $pixel=$pixelReader.StandardOutput.ReadLine() | ConvertFrom-Json
            if($pixel.red){break}
        }while($watch.Elapsed.TotalSeconds-lt 10)
        $watch.Stop();$session.Console.Mouse.PutMouseEvent(0,0,0,0,0)
        if(!$pixel.red){throw "No real red stroke became visible: $kind"}
        $observations+=@{kind=$kind;trial=$trial;cpu_count=$CpuCount;visible_upper_ms=$watch.Elapsed.TotalMilliseconds;window_x=$x;window_y=$y}
        $observations | ConvertTo-Json -Depth 5 | Set-Content "$dir/visible.json"
    }finally{$session.Console.Mouse.PutMouseEvent(0,0,0,0,0);$session.UnlockMachine()}
    Click ($x+350) ($y+23)
    & $inputScript -Action keys -Text 'i'
    Start-Sleep -Seconds 3
    & $inputScript -Action keys -Text 'b'
    Await-Log "PAINTBENCH DONE $kind" $doneCount 180
    $after=Capture "$kind-$trial-drawn"
    Click ($x+1194) ($y+23)
    Await-Log "PAINTBENCH EXIT $kind " $exitCount
    Copy-Item build/vex-serial.log "$dir/$kind-$trial.log" -Force
    Start-Sleep -Seconds 1
  }
}
$observations | ConvertTo-Json -Depth 5 | Set-Content "$dir/visible.json"
# Separate two-instance RAM experiment; both normal event loops idle.
foreach($kind in @('lvgl','suchu')) {
    $readyCount=Occurrences "PAINTBENCH READY $kind ";$exitCount=Occurrences "PAINTBENCH EXIT $kind "
    Click 180 400;& $inputScript -Action keys -Text "run /usr/native-tests/paint_runner $kind 2"
    & $inputScript -Action keys -ScanCodes @('1c','9c')
    Await-Log "PAINTBENCH READY $kind " ($readyCount+1)
    $first=$windowId;$second=$windowId+1;$windowId+=2
    Start-Sleep -Seconds 3;$null=Capture "$kind-two-instances"
    foreach($id in @($second,$first)) {
        $x=18+($id%4)*28;$y=72+($id%4)*22;Click ($x+1194) ($y+23)
    }
    Await-Log "PAINTBENCH EXIT $kind " $exitCount
    Copy-Item build/vex-serial.log "$dir/$kind-two.log" -Force
}
Copy-Item build/vex-serial.log "$dir/all.log" -Force
$pixelReader.StandardInput.Close();$pixelReader.WaitForExit(1000)|Out-Null;$pixelReader.Dispose()
Write-Output "PASS: actual Paint comparisons on $CpuCount vCPU"
