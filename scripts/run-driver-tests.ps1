param([ValidateSet(1,4)][int]$CpuCount=1,
      [ValidatePattern('^[a-z0-9-]*$')][string]$EvidenceLabel='',
      [string]$Python='C:/Users/emili/AppData/Local/hermes/hermes-agent/venv/Scripts/python.exe')
$ErrorActionPreference='Stop'
$Root=(Resolve-Path "$PSScriptRoot/..").Path
$VBox='C:/Program Files/Oracle/VirtualBox/VBoxManage.exe'
$Dir=Join-Path $Root "build/driver-tests/${EvidenceLabel}cpu$CpuCount"
New-Item -ItemType Directory -Force $Dir | Out-Null
$Arm=Join-Path $Root 'build/driver-tests/uart-peer.arm'
$Peer=$null
function GuestKeys([string]$Text) {
  & "$PSScriptRoot/vm-browser-input.ps1" -Action keys -Text $Text
  & "$PSScriptRoot/vm-browser-input.ps1" -Action keys -ScanCodes @('1c','9c')
}
try {
  $state=& $VBox showvminfo Vex-OS --machinereadable
  if($state -contains 'VMState="running"'){& $VBox controlvm Vex-OS poweroff | Out-Null}
  if(Test-Path $Arm){Remove-Item -LiteralPath $Arm}
  & $VBox modifyvm Vex-OS --uart2 0x2F8 3 --uartmode2 tcpclient '127.0.0.1:45672'
  if($LASTEXITCODE){throw 'UART configuration failed'}
  $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$Python
  foreach($argument in @((Join-Path $PSScriptRoot 'driver-uart-peer.py'),'--listen','--log',(Join-Path $Dir 'peer.log'),'--arm-file',$Arm)){
    $info.ArgumentList.Add($argument)
  }
  $info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle='Hidden';$info.WorkingDirectory=$Root
  $Peer=[Diagnostics.Process]::Start($info)
  & "$PSScriptRoot/run-virtualbox.ps1" -Headless -CpuCount $CpuCount
  $deadline=[DateTime]::UtcNow.AddSeconds(90)
  do {
    if(Test-Path "$Root/build/vex-serial.log"){
      if(Select-String 'cap-trace: create pid=5 ' "$Root/build/vex-serial.log" -Quiet){break}
    }
    Start-Sleep -Milliseconds 250
  }while([DateTime]::UtcNow -lt $deadline)
  if([DateTime]::UtcNow -ge $deadline){throw 'Desktop boot timeout'}
  & "$PSScriptRoot/vm-browser-input.ps1" -Action move -X 1600 -Y 300
  & "$PSScriptRoot/vm-browser-input.ps1" -Action click -X 1250 -Y 200 -Button 2 -VerifyCursor
  & "$PSScriptRoot/vm-browser-input.ps1" -Action click -X 1310 -Y 227 -VerifyCursor
  & "$PSScriptRoot/vm-browser-input.ps1" -Action click -X 180 -Y 400 -VerifyCursor
  Set-Content -LiteralPath $Arm -Value 'armed'
  GuestKeys 'driver load /drivers/dma-memory-test.driver'
  $deadline=[DateTime]::UtcNow.AddSeconds(10)
  do {
    if(Select-String 'driver-result: denied|KERNEL PANIC' "$Root/build/vex-serial.log" -Quiet){break}
    Start-Sleep -Milliseconds 100
  }while([DateTime]::UtcNow -lt $deadline)
  if(!(Select-String 'driver-result: denied' "$Root/build/vex-serial.log" -Quiet)){throw 'Memory test archive was not denied device authority'}
  GuestKeys 'driver dmatest'
  $deadline=[DateTime]::UtcNow.AddSeconds(15)
  do {
    if(Select-String 'driver-dma-test: PASS|driver-dma-test: FAILED|KERNEL PANIC' "$Root/build/vex-serial.log" -Quiet){break}
    Start-Sleep -Milliseconds 100
  }while([DateTime]::UtcNow -lt $deadline)
  if(!(Select-String 'driver-dma-test: PASS' "$Root/build/vex-serial.log" -Quiet)){
    Copy-Item "$Root/build/vex-serial.log" "$Dir/serial.log" -Force
    throw 'Real ORA/PMM/VMM DMA memory smoke failed'
  }
  GuestKeys 'driver selftest'
  $deadline=[DateTime]::UtcNow.AddSeconds(30)
  do {
    if(Select-String 'driver-test: PASS|driver-test: FAILED|KERNEL PANIC' "$Root/build/vex-serial.log" -Quiet){break}
    Start-Sleep -Milliseconds 100
  }while([DateTime]::UtcNow -lt $deadline)
  Copy-Item "$Root/build/vex-serial.log" "$Dir/serial.log" -Force
  & "$PSScriptRoot/vm-browser-input.ps1" -Action screenshot -Screenshot "build/driver-tests/${EvidenceLabel}cpu$CpuCount/screen.png"
  if(!(Test-Path "$Dir/screen.png")){throw 'Hardware test screenshot was not captured'}
  if(!(Select-String 'driver-test: PASS' "$Dir/serial.log" -Quiet) -or
     (Select-String 'driver-test: FAILED|KERNEL PANIC' "$Dir/serial.log" -Quiet)){throw 'Driver hardware test failed'}
  Write-Output "PASS: real driver archives on $CpuCount vCPU"
} finally {
  & $VBox controlvm Vex-OS poweroff 2>$null | Out-Null
  if($Peer){if(!$Peer.WaitForExit(3000)){$Peer.Kill()};$Peer.Dispose()}
  if(Test-Path $Arm){Remove-Item -LiteralPath $Arm}
  & $VBox modifyvm Vex-OS --uart2 off --cpus 4
}
