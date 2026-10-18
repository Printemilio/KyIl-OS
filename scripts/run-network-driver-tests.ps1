param([ValidateSet(1,4)][int]$CpuCount=1,
      [string]$Python='C:/Users/emili/AppData/Local/hermes/hermes-agent/venv/Scripts/python.exe')
$ErrorActionPreference='Stop'
$Root=(Resolve-Path "$PSScriptRoot/..").Path
$VBox='C:/Program Files/Oracle/VirtualBox/VBoxManage.exe'
$Dir=Join-Path $Root "build/e1000-tests/cpu$CpuCount"
New-Item -ItemType Directory -Force $Dir | Out-Null
$provenance=[ordered]@{cpu_count=$CpuCount;utc=[DateTime]::UtcNow.ToString('o');
    kernel_sha256=(Get-FileHash "$Root/build/vex-os" -Algorithm SHA256).Hash;
    iso_sha256=(Get-FileHash "$Root/build/vex-os.iso" -Algorithm SHA256).Hash;
    driver_sha256=(Get-FileHash "$Root/drivers/e1000.driver" -Algorithm SHA256).Hash}
$provenance | ConvertTo-Json | Set-Content "$Dir/provenance.json"
function Keys([string]$Text) {
    & "$PSScriptRoot/vm-browser-input.ps1" -Action keys -Text $Text
    & "$PSScriptRoot/vm-browser-input.ps1" -Action keys -ScanCodes @('1c','9c')
}
function Wait-Log([string]$Pattern,[int]$Seconds=60) {
    $until=[DateTime]::UtcNow.AddSeconds($Seconds)
    do {
        if(Test-Path "$Root/build/vex-serial.log") {
            if(Select-String 'KERNEL PANIC|QUARANTINED|datapath fault' "$Root/build/vex-serial.log" -Quiet){throw 'Kernel/driver fault during real network run'}
            if(Select-String $Pattern "$Root/build/vex-serial.log" -Quiet){return}
        }
        Start-Sleep -Milliseconds 200
    }while([DateTime]::UtcNow -lt $until)
    throw "Missing actual guest result: $Pattern"
}
try {
    & $VBox modifyvm Vex-OS --uart2 off --nictrace1 on --nictracefile1 "$Dir/network.pcap"
    if($LASTEXITCODE){throw 'NAT capture configuration failed'}
    & "$PSScriptRoot/run-virtualbox.ps1" -Headless -CpuCount $CpuCount
    Wait-Log 'cap-trace: create pid=5 '
    Wait-Log 'dhcp: real ACK'
    if(!(Select-String 'driver-net: active id=' "$Root/build/vex-serial.log" -Quiet)){throw 'Archive NIC not active'}
    & "$PSScriptRoot/vm-browser-input.ps1" -Action move -X 1600 -Y 300
    & "$PSScriptRoot/vm-browser-input.ps1" -Action click -X 1250 -Y 200 -Button 2 -VerifyCursor
    & "$PSScriptRoot/vm-browser-input.ps1" -Action click -X 1310 -Y 227 -VerifyCursor
    & "$PSScriptRoot/vm-browser-input.ps1" -Action click -X 180 -Y 400 -VerifyCursor
    Keys 'driver netcycle'
    Wait-Log 'driver-net-test: PASS' 45
    # Query the actual carrier after reset cycles, never invent a ready link.
    for($i=0;$i -lt 6;$i++) {
        Keys 'driver netstats'
        if(Select-String 'driver-net-stats: id=.* link=1 ' "$Root/build/vex-serial.log" -Quiet){break}
        Start-Sleep -Milliseconds 200
    }
    Wait-Log 'driver-net-stats: id=.* link=1 '
    Keys 'resolve example.com'
    # DNS result is confirmed by the subsequent real browser response and PCAP.
    Start-Sleep -Seconds 2
    & "$PSScriptRoot/vm-browser-input.ps1" -Action screenshot -Screenshot "build/e1000-tests/cpu$CpuCount/dns.png"
    Keys 'browser'
    Wait-Log 'webkit_linux_main: entering main loop' 90
    & "$PSScriptRoot/vm-browser-input.ps1" -Action click -X 550 -Y 573 -VerifyCursor
    $Uri="https://example.com/?driver=e1000-cpu$CpuCount"
    Keys $Uri
    Wait-Log "browser response:.*driver=e1000-cpu$CpuCount.*status=200" 90
    Wait-Log 'browser navigation timing:' 45
    $visible=$false;$until=[DateTime]::UtcNow.AddSeconds(30)
    do {
        & "$PSScriptRoot/vm-browser-input.ps1" -Action screenshot -Screenshot "build/e1000-tests/cpu$CpuCount/browser-https.png"
        $metrics=& $Python "$PSScriptRoot/check-network-browser-frame.py" "$Dir/browser-https.png"
        if(!$LASTEXITCODE){$visible=$true;$metrics|Set-Content "$Dir/frame-metrics.json";break}
        Start-Sleep -Milliseconds 200
    }while([DateTime]::UtcNow -lt $until)
    if(!$visible){throw 'HTTPS loaded but real WebKit content not yet visible'}
    & "$PSScriptRoot/vm-browser-input.ps1" -Action click -X 1140 -Y 117 -VerifyCursor
    & "$PSScriptRoot/vm-browser-input.ps1" -Action click -X 180 -Y 400 -VerifyCursor
    Keys 'driver netstats'
    Wait-Log 'native_TX_delta=0 native_RX_delta=0 native_IRQ_delta=0'
    Keys 'driver dmatest'
    Wait-Log 'driver-dma-test: PASS'
    & "$PSScriptRoot/vm-browser-input.ps1" -Action screenshot -Screenshot "build/e1000-tests/cpu$CpuCount/exclusive-counters.png"
    Copy-Item "$Root/build/vex-serial.log" "$Dir/serial.log" -Force
    $last=(Select-String 'driver-net-stats:' "$Dir/serial.log" | Select-Object -Last 1).Line
    if($last -notmatch 'TX=[1-9][0-9]* RX=[1-9][0-9]*' -or $last -notmatch 'native_TX_delta=0 native_RX_delta=0 native_IRQ_delta=0'){throw 'No exclusive archive datapath proof'}
    Write-Output "PASS: real DHCP/DNS/TCP/TLS/browser + lifecycle on $CpuCount vCPU"
} finally {
    if(Test-Path "$Root/build/vex-serial.log"){Copy-Item "$Root/build/vex-serial.log" "$Dir/serial.log" -Force}
    & $VBox controlvm Vex-OS poweroff 2>$null | Out-Null
    & $VBox modifyvm Vex-OS --uart2 off --nictrace1 off --cpus 4
}
