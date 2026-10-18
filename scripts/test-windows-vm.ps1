param(
    [string]$VmName='KyIlOS-Legacy-Test',
    [ValidateSet(1,4)][int]$Cpus=4,
    [string]$Iso='build/kyilos-windows-process.iso',
    [string[]]$Programs=@('/usr/pe-tests/windows-process-parent.exe'),
    [string[]]$Expected=@('windows-process:.* PASS'),
    [int]$TimeoutSeconds=120,
    [string]$LogPrefix='windows-process',
    [switch]$LeaveRunning
)
$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot/..").Path
$vbox='C:/Program Files/Oracle/VirtualBox/VBoxManage.exe'
function VBox([string[]]$arg) {& $vbox @arg;if($LASTEXITCODE){throw "VBox command failed: $($arg[0])"}}
function Keys([string]$text) {
    & "$PSScriptRoot/vm-browser-input.ps1" -VmName $VmName -Action keys -Text $text
    if($LASTEXITCODE){throw 'Guest command keyboard delivery failed; no execution result may be inferred'}
    & "$PSScriptRoot/vm-browser-input.ps1" -VmName $VmName -Action keys -ScanCodes @('1c','9c')
    if($LASTEXITCODE){throw 'Guest command Enter delivery failed; no execution result may be inferred'}
}
function ReadSerial {
    if(-not(Test-Path -LiteralPath $serial)){return ''}
    $stream=[IO.File]::Open($serial,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
    $reader=[IO.StreamReader]::new($stream)
    try {return $reader.ReadToEnd()} finally {$reader.Dispose()}
}
function AwaitLog([string]$pattern,[long]$offset=0) {
    # An exiting child may interleave its UART line with the runner on SMP.
    # Recognize the ordered final validation sequence, not a contiguous prefix.
    # Explicit failures are still checked first on every serial snapshot.
    if($pattern -eq 'windows-runner: native GS.* PASS') {
        $pattern='(?s)native GS.*?Linux TLS-FPU.*?W_X / faults / memory ownership PASS'
    }
    $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while([DateTime]::UtcNow -lt $deadline) {
        $text=ReadSerial
        $tail=if($offset -le $text.Length){$text.Substring([int]$offset)}else{$text}
        if($tail -match '(?m)^(?:windows-runner|native-execution|native-fs-roots|native-process-resource|native-thread-control|threads|process(?:-child)?|module|encoding|platform|version|condition|once|nt-core|memory-query|unwind|heap-walk|private-heap|chrome-contract|inspection|cwd|namespace-mutation|affinity|process-times|shutdown-order|memory-usage|memory-counter|rng|virtual-memory|multimedia|registry-win32|loader-lock|security-data|ultra-space|protected-pipe|startup-contract|handle-values|shared-event|multiple-wait|fls|file-batch|file-lock|job|wer|contract-batch|async-contract|system-contract|profile|seh|slist|registry):[^\r\n]*FAIL|windows-export\|[^\r\n]*\|absent|KERNEL PANIC|kernel panic'){throw "Guest failure; see $serial"}
        if($tail -match $pattern){return}
        Start-Sleep -Milliseconds 200
    }
    throw "Guest timeout awaiting $pattern; see $serial"
}
if($Programs.Count -ne $Expected.Count){throw 'Every program needs one expected marker'}
$state=(& $vbox showvminfo $VmName --machinereadable | Select-String '^VMState=').ToString()
if($LASTEXITCODE -or $state -ne 'VMState="poweroff"'){throw 'Dedicated VM must be powered off before changing its ISO/CPU configuration'}
$image=(Resolve-Path -LiteralPath (Join-Path $root $Iso)).Path
$stamp=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
$serial=Join-Path $root "build/windows/$LogPrefix-$Cpus-cpu-$stamp.log"
VBox @('modifyvm',$VmName,'--cpus',"$Cpus",'--uartmode1','file',$serial)
VBox @('storageattach',$VmName,'--storagectl','IDE','--port','1','--device','0','--type','dvddrive','--medium',$image)
VBox @('startvm',$VmName,'--type','headless')
try {
    AwaitLog 'desktop: GUI service ready'
    & "$PSScriptRoot/vm-browser-input.ps1" -VmName $VmName -Action click -X 700 -Y 480 -Button 2
    & "$PSScriptRoot/vm-browser-input.ps1" -VmName $VmName -Action click -X 790 -Y 505
    & "$PSScriptRoot/vm-browser-input.ps1" -VmName $VmName -Action click -X 220 -Y 638
    for($i=0;$i -lt $Programs.Count;$i++) {
        $offset=(ReadSerial).Length
        Keys "run $($Programs[$i])"
        if($Programs[$i] -match '/chrome-host-validation\.exe$' -and $Expected[$i] -match 'load failed') {
            AwaitLog 'windows: load failed: Windows APIs not implemented' $offset
            $tail=(ReadSerial).Substring([int]$offset)
            if($tail -notmatch 'missing import [^\r\n]+caller=chrome-host-validation\.exe'){throw 'Chrome failed before the expected static-import barrier'}
        }else {AwaitLog $Expected[$i] $offset}
        if($Programs[$i] -match '/(?:chrome|chrome-host-validation)\.exe$' -and $Expected[$i] -match 'windows-entry:|windows-fatal-frame:'){
            $tail=(ReadSerial).Substring([int]$offset)
            $entryObserved=$tail -match '(?m)^windows-entry: (?:chrome|chrome-host-validation)\.exe '
            Write-Host "CHROME EXECUTION TRACE $Cpus CPU $($Programs[$i]); actual EXE entry observed=$entryObserved; GUI success is NOT asserted"
        }
        elseif($Expected[$i] -match 'load failed') {Write-Host "EXPECTED LOAD REFUSAL $Cpus CPU $($Programs[$i]) (entry point not reached)"}
        else {Write-Host "PASS $Cpus CPU $($Programs[$i])"}
    }
} finally {
    if(-not $LeaveRunning) {
        Keys 'shutdown'
        $deadline=[DateTime]::UtcNow.AddSeconds(30)
        do {
            $snapshot=& $vbox showvminfo $VmName --machinereadable 2>$null | Select-String '^VMState='
            if($snapshot){$state=$snapshot.ToString()}
            if($state -eq 'VMState="poweroff"'){break};Start-Sleep -Milliseconds 200
        }while([DateTime]::UtcNow -lt $deadline)
        if($state -ne 'VMState="poweroff"'){Write-Warning 'Guest did not shut down: ISO must not be overwritten while the VM is running'}
    }
    Write-Host "Raw serial evidence: $serial"
}


