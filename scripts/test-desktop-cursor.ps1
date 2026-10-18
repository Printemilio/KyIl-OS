param([int]$Samples=48,[string]$Output='build/suchu-browser/cursor-samples',
      [int]$CloseAt=-1,[int]$CloseX=1140,[int]$CloseY=117,
      [string]$Python='C:/Users/emili/AppData/Local/hermes/hermes-agent/venv/Scripts/python.exe')
$ErrorActionPreference='Stop'
$vbox='C:/Program Files/Oracle/VirtualBox/VBoxManage.exe'
New-Item -ItemType Directory -Force $Output | Out-Null
$start=[Diagnostics.ProcessStartInfo]::new()
$start.FileName=$Python
$start.ArgumentList.Add((Join-Path $PSScriptRoot 'vm-cursor-position.py'))
$start.ArgumentList.Add('--stream')
$start.UseShellExecute=$false;$start.CreateNoWindow=$true
$start.RedirectStandardInput=$true;$start.RedirectStandardOutput=$true
$reader=[Diagnostics.Process]::Start($start)
$vb=New-Object -ComObject VirtualBox.VirtualBox
$session=New-Object -ComObject VirtualBox.Session
$vb.FindMachine('Vex-OS').LockMachine($session,1)
$missing=0;$results=@()
try {
    for($i=0;$i -lt $Samples;$i++) {
        if($i -eq $CloseAt) {
            if(!$result -or $result.error){throw 'Need an observed cursor before closing'}
            [int]$rx=$CloseX-[int]$result.x;[int]$ry=$CloseY-[int]$result.y
            $point=$result
            while($rx -or $ry) {
                $sx=[Math]::Clamp([int]$rx,[int]-64,[int]64);$sy=[Math]::Clamp([int]$ry,[int]-64,[int]64)
                $tx=[int]$point.x+$sx;$ty=[int]$point.y+$sy
                $session.Console.Mouse.PutMouseEvent($sx,$sy,0,0,0)
                $rx-=$sx;$ry-=$sy
                # VBox's emulated PS/2 packet slot can overwrite back-to-back
                # injections. Observe delivery of setup displacements first.
                $stepDeadline=[DateTime]::UtcNow.AddSeconds(2)
                do {
                    $check=Join-Path (Get-Location) "$Output/close-position.png"
                    & $vbox controlvm Vex-OS screenshotpng $check
                    $reader.StandardInput.WriteLine($check);$reader.StandardInput.Flush()
                    $point=$reader.StandardOutput.ReadLine()|ConvertFrom-Json
                }while(($point.error -or $point.x-ne $tx -or $point.y-ne $ty) -and [DateTime]::UtcNow-lt $stepDeadline)
                if($point.error -or $point.x-ne $tx -or $point.y-ne $ty){throw 'Setup displacement not delivered'}
            }
            # Confirm the actual position before the test click; these are
            # input settling observations, never replacements for sample data.
            $deadline=[DateTime]::UtcNow.AddSeconds(2)
            do {
                $check=Join-Path (Get-Location) "$Output/close-position.png"
                & $vbox controlvm Vex-OS screenshotpng $check
                $reader.StandardInput.WriteLine($check);$reader.StandardInput.Flush()
                $point=$reader.StandardOutput.ReadLine()|ConvertFrom-Json
            }while(($point.error -or $point.x-ne $CloseX -or $point.y-ne $CloseY) -and [DateTime]::UtcNow-lt $deadline)
            if($point.error -or $point.x-ne $CloseX -or $point.y-ne $CloseY){throw 'Close point not reached'}
            $session.Console.Mouse.PutMouseEvent(0,0,0,0,1)
            Start-Sleep -Milliseconds 25
            $session.Console.Mouse.PutMouseEvent(0,0,0,0,0)
        }
        # Continuous real PS/2 motion around the prepositioned cursor. No
        # retries: a screenshot without the cursor is a failed observation.
        $dx=if(($i%16)-lt 8){8}else{-8}
        $dy=if(($i%24)-lt 12){2}else{-2}
        $session.Console.Mouse.PutMouseEvent($dx,$dy,0,0,0)
        $path=Join-Path (Get-Location) "$Output/$i.png"
        & $vbox controlvm Vex-OS screenshotpng $path
        if($LASTEXITCODE){throw 'VM capture failed'}
        $reader.StandardInput.WriteLine($path);$reader.StandardInput.Flush()
        $result=$reader.StandardOutput.ReadLine()|ConvertFrom-Json
        if($result.error){$missing++}
        $results+=@{sample=$i;observation=$result}
    }
} finally {
    $session.UnlockMachine();$reader.StandardInput.Close()
    if(!$reader.WaitForExit(1000)){$reader.Kill()};$reader.Dispose()
}
$results|ConvertTo-Json -Depth 4|Set-Content "$Output/results.json"
Write-Output "Cursor: $Samples captures, $missing missing/partial (no retries)"
if($missing){throw 'Cursor regression observed'}
