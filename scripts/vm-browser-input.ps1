param(
    [string]$VmName='Vex-OS',
    [ValidateSet('click','doubleclick','move','wheel','drag','keys','screenshot')][string]$Action='move',
    [int]$X=1070,[int]$Y=326,[int]$ToX=1070,[int]$ToY=326,
    [int]$Wheel=0,[int]$Button=1,[string[]]$ScanCodes,[string]$Text,
    [string]$Screenshot='build/webkit-native-input.png',
    [switch]$VerifyCursor,
    [string]$Python='C:/Users/emili/AppData/Local/hermes/hermes-agent/venv/Scripts/python.exe'
)
$ErrorActionPreference='Stop'
$taskVBoxPath='C:/Program Files/Oracle/VirtualBox/VBoxManage.exe'
if($Action -eq 'keys') {
    if($Text) {
        if($ScanCodes){throw 'Choose Text or ScanCodes.'}
        # ASCII portion of Vex's PS/2 AZERTY map. These are actual guest keys.
        $taskMap=@{};$taskLetters='azertyuiopqsdfghjklmwxcvbn'
        $taskCodes=@(0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1e,0x1f,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x2c,0x2d,0x2e,0x2f,0x30,0x31)
        for($i=0;$i -lt $taskLetters.Length;$i++){$taskMap[[string]$taskLetters[$i]]=$taskCodes[$i]}
        $taskMap['-']=7;$taskMap['_']=9;$taskMap[':']=0x34;$taskMap[' ']=0x39
        foreach($taskChar in $Text.ToCharArray()) {
            $taskKey=[string]$taskChar;$taskLower=$taskKey.ToLowerInvariant();$taskShift=$taskKey -cne $taskLower
            if($taskKey -eq '/'){$taskCode=0x34;$taskShift=$true}
            elseif($taskKey -eq '.'){$taskCode=0x33;$taskShift=$true}
            elseif($taskKey -eq '?'){$taskCode=0x32;$taskShift=$true}
            elseif($taskKey -eq '='){$taskCode=0x0d;$taskShift=$false}
            elseif($taskKey -eq '&'){$taskCode=0x02;$taskShift=$false}
            elseif($taskKey -eq '+'){$taskCode=0x0d;$taskShift=$true}
            elseif($taskKey -match '^[0-9]$'){$taskCode=if($taskKey -eq '0'){0x0b}else{1+[int]::Parse($taskKey)};$taskShift=$true}
            elseif($taskMap.ContainsKey($taskLower)){$taskCode=$taskMap[$taskLower]}
            else {throw "No test PS/2 mapping for $taskKey"}
            $taskSequence=@();if($taskShift){$taskSequence+='2a'}
            $taskSequence+=$taskCode.ToString('x2');$taskSequence+=($taskCode+128).ToString('x2')
            if($taskShift){$taskSequence+='aa'}
            & $taskVBoxPath controlvm $VmName keyboardputscancode @taskSequence
            if($LASTEXITCODE -ne 0){throw 'Keyboard injection failed.'}
            Start-Sleep -Milliseconds 100
        }
        exit 0
    }
    & $taskVBoxPath controlvm $VmName keyboardputscancode @ScanCodes;exit $LASTEXITCODE
}
if($Action -eq 'screenshot') { & $taskVBoxPath controlvm $VmName screenshotpng (Join-Path (Get-Location) $Screenshot);exit $LASTEXITCODE }
# Real guest PS/2 input through VirtualBox's documented IMouse interface.
# No JS injection, fabricated DOM effects, or browser automation shortcut.
$taskVBox=New-Object -ComObject VirtualBox.VirtualBox
$taskSession=New-Object -ComObject VirtualBox.Session
$taskVBox.FindMachine($VmName).LockMachine($taskSession,1)
try {
    $taskMouse=$taskSession.Console.Mouse
    $taskCursorReader=$null
    if($VerifyCursor) {
        $taskCursorStart=[Diagnostics.ProcessStartInfo]::new()
        $taskCursorStart.FileName=$Python
        $taskCursorStart.ArgumentList.Add((Join-Path $PSScriptRoot 'vm-cursor-position.py'))
        $taskCursorStart.ArgumentList.Add('--stream')
        $taskCursorStart.UseShellExecute=$false;$taskCursorStart.CreateNoWindow=$true
        $taskCursorStart.RedirectStandardInput=$true;$taskCursorStart.RedirectStandardOutput=$true
        $taskCursorReader=[Diagnostics.Process]::Start($taskCursorStart)
    }
    function Read-GuestCursor {
        $taskCapture=Join-Path (Get-Location) 'build/smp/input-cursor.png'
        # A capture can intersect a real compositor update and see only part
        # of the cursor. Retry that observation; never invent its coordinates.
        for($taskCaptureAttempt=0;$taskCaptureAttempt -lt 20;$taskCaptureAttempt++) {
            & $taskVBoxPath controlvm $VmName screenshotpng $taskCapture
            if($LASTEXITCODE){throw 'Cursor capture failed'}
            $taskCursorReader.StandardInput.WriteLine($taskCapture)
            $taskPosition=$taskCursorReader.StandardOutput.ReadLine() | ConvertFrom-Json
            if($taskPosition -and !$taskPosition.error){return $taskPosition}
            if(!$taskPosition -or $taskPosition.error -notmatch 'found 0$'){break}
            Start-Sleep -Milliseconds 25
        }
        throw "Actual guest cursor could not be located: $($taskPosition.error)"
    }
    if($VerifyCursor){$taskStart=Read-GuestCursor}
    else {for($i=0;$i -lt 21;$i++) { $taskMouse.PutMouseEvent(-100,-100,0,0,0);Start-Sleep -Milliseconds 25 }}
    function Move-GuestCursor([int]$dx,[int]$dy,[int]$buttons) {
        if($VerifyCursor){$position=Read-GuestCursor}
        while($dx -ne 0 -or $dy -ne 0) {
            $sx=[Math]::Max(-100,[Math]::Min(100,$dx));$sy=[Math]::Max(-100,[Math]::Min(100,$dy))
            $taskMouse.PutMouseEvent($sx,$sy,0,0,$buttons);$dx-=$sx;$dy-=$sy
            if($VerifyCursor) {
                # Confirm actual delivery of each bounded PS/2 displacement.
                $position=Await-GuestCursor ($position.x+$sx) ($position.y+$sy)
            }else{Start-Sleep -Milliseconds 25}
        }
    }
    function Await-GuestCursor([int]$px,[int]$py) {
        $deadline=[DateTime]::UtcNow.AddSeconds(15)
        do {
            $position=Read-GuestCursor
            if($position.x -eq [Math]::Min($px,$position.width-17) -and
               $position.y -eq [Math]::Min($py,$position.height-17)){return $position}
            Start-Sleep -Milliseconds 50
        }while([DateTime]::UtcNow -lt $deadline)
        throw "Guest cursor stalled/diverged: expected $px,$py observed $($position.x),$($position.y)"
    }
    if($VerifyCursor){Move-GuestCursor ($X-$taskStart.x) ($Y-$taskStart.y) 0;$taskOrigin=Await-GuestCursor $X $Y}
    else {Move-GuestCursor $X $Y 0}
    switch($Action) {
        'click' {$taskMouse.PutMouseEvent(0,0,0,0,$Button);Start-Sleep -Milliseconds 150;$taskMouse.PutMouseEvent(0,0,0,0,0)}
        'doubleclick' {for($i=0;$i -lt 2;$i++){$taskMouse.PutMouseEvent(0,0,0,0,$Button);Start-Sleep -Milliseconds 70;$taskMouse.PutMouseEvent(0,0,0,0,0);Start-Sleep -Milliseconds 70}}
        'wheel' {$taskMouse.PutMouseEvent(0,0,$Wheel,0,0)}
        'drag' {$taskMouse.PutMouseEvent(0,0,0,0,1);Start-Sleep -Milliseconds 150;if($VerifyCursor){Move-GuestCursor ($ToX-$taskOrigin.x) ($ToY-$taskOrigin.y) 1;$null=Await-GuestCursor $ToX $ToY}else{Move-GuestCursor ($ToX-$X) ($ToY-$Y) 1};Start-Sleep -Milliseconds 150;$taskMouse.PutMouseEvent(0,0,0,0,0)}
    }
} finally {
    $taskMouse.PutMouseEvent(0,0,0,0,0);$taskSession.UnlockMachine()
    if($taskCursorReader){$taskCursorReader.StandardInput.Close();if(!$taskCursorReader.WaitForExit(1000)){$taskCursorReader.Kill()};$taskCursorReader.Dispose()}
}
Write-Output "VM input: $Action at $X,$Y wheel=$Wheel target=$ToX,$ToY"
