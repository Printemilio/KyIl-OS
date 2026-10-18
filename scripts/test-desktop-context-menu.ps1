# Run on a desktop with no windows open. FirstWindowId defaults to a fresh boot.
# Uses actual PS/2 input and unmodified VM captures; no redraw/test-only guest API.
param(
    [int]$FirstWindowId=1,
    [string]$OutputDirectory='build/context-menu/replay',
    [string]$Python='C:/Users/emili/AppData/Local/hermes/hermes-agent/venv/Scripts/python.exe'
)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
function Click([int]$x,[int]$y,[int]$button=1) {
    & "$PSScriptRoot/vm-browser-input.ps1" -Action click -X $x -Y $y -Button $button -VerifyCursor
}
function Capture([string]$name) {
    $path=Join-Path $OutputDirectory "$name.png"
    & "$PSScriptRoot/vm-browser-input.ps1" -Action screenshot -Screenshot $path
    return $path
}
function Assert-Pixels([string]$before,[string]$after,[int[]]$region,[string[]]$extra=@()) {
    & $Python "$PSScriptRoot/check-context-menu-pixels.py" $before $after @region @extra
    if($LASTEXITCODE){throw "Pixel regression: $before -> $after"}
}
# Wait for an observable desktop, not a guessed boot delay. Retry is limited
# to initial readiness; comparisons below use unmodified captures once each.
$bootDeadline=[DateTime]::UtcNow.AddSeconds(90)
do {
    $readyImage=Capture 'desktop-ready'
    $positionJson=& $Python "$PSScriptRoot/vm-cursor-position.py" $readyImage 2>$null
    $desktopReady=$LASTEXITCODE -eq 0
    if(!$desktopReady){Start-Sleep -Milliseconds 500}
} while(!$desktopReady -and [DateTime]::UtcNow -lt $bootDeadline)
if(!$desktopReady){throw 'Desktop/cursor never became observable'}
foreach($case in @(
    @{name='editor';item=379;x=46;y=94;w=1016;h=736},
    @{name='gfm';item=265;x=74;y=116;w=1096;h=698},
    @{name='browser';item=303;x=102;y=138;w=1116;h=816}
)) {
    $case.x=18+($FirstWindowId%4)*28
    $case.y=72+($FirstWindowId%4)*22
    ++$FirstWindowId
    Click 1250 200 2; Click 1310 $case.item
    Start-Sleep -Seconds 2
    if($case.name -eq 'browser') {
        & "$PSScriptRoot/vm-browser-input.ps1" -Action keys -ScanCodes @('1d','26','a6','9d')
        & "$PSScriptRoot/vm-browser-input.ps1" -Action keys -Text 'file:///usr/linux-tests/webkit-render.html'
        & "$PSScriptRoot/vm-browser-input.ps1" -Action keys -ScanCodes @('1c','9c')
        $deadline=[DateTime]::UtcNow.AddSeconds(90)
        do {
            Start-Sleep -Milliseconds 500
            $ready=Select-String 'Suchu browser: page=1 frame=[2-9].*nonblack=[1-9]' 'build/vex-serial.log' -Quiet
        } while(!$ready -and [DateTime]::UtcNow -lt $deadline)
        if(!$ready){throw 'WebKit did not produce a real document frame'}
    }
    $before=Capture "$($case.name)-before"
    Click 1250 200 2
    & "$PSScriptRoot/vm-browser-input.ps1" -Action move -X 1420 -Y 450 -VerifyCursor
    $menu=Capture "$($case.name)-menu"
    if($case.name -eq 'browser') {
        # Native toolbar remains intact; Web content is intentionally animated.
        Assert-Pixels $before $menu @($case.x,($case.y+46),$case.w,80) @('--require-red',"$($case.x+$case.w-22)","$($case.y+23)")
    } else {
        $extra=if($case.name -eq 'editor'){@('--exclude',"$($case.x+64)","$($case.y+125)",'6','25')}else{@()}
        $extra+=@('--require-red',"$($case.x+$case.w-22)","$($case.y+23)")
        Assert-Pixels $before $menu @($case.x,$case.y,$case.w,$case.h) $extra
    }
    & "$PSScriptRoot/vm-browser-input.ps1" -Action move -X 1600 -Y 650 -VerifyCursor
    $moved=Capture "$($case.name)-moved"
    Assert-Pixels $menu $moved @(1250,200,250,206)
    Assert-Pixels $before $moved @(1220,65,690,950) @('--exclude','1250','200','250','206','--exclude','1600','650','17','17')
    if($case.name -eq 'browser') {
        Click 30 80 2
        & "$PSScriptRoot/vm-browser-input.ps1" -Action move -X 1600 -Y 650 -VerifyCursor
        $a=Capture 'browser-overlap-a'
        if($case.x -ge 30 -and $case.x -lt 280 -and $case.y -ge 80 -and $case.y -lt 286) {
            $hx=$case.x+1;$hy=$case.y+1
            & "$PSScriptRoot/vm-browser-input.ps1" -Action move -X $hx -Y $hy -VerifyCursor
            $hover=Capture 'browser-menu-over-resize'
            Assert-Pixels $a $hover @(30,80,250,206) @('--exclude',"$hx","$hy",'17','17')
            & "$PSScriptRoot/vm-browser-input.ps1" -Action move -X 1600 -Y 650 -VerifyCursor
        }
        Start-Sleep -Seconds 3
        $b=Capture 'browser-overlap-b'
        Assert-Pixels $a $b @(30,80,250,206)
        Assert-Pixels $a $b @(($case.x+8),300,1100,($case.y+$case.h-306)) @('--expect-change')
    }
    Click 1780 1000
    $closed=Capture "$($case.name)-menu-dismissed"
    Assert-Pixels $before $closed @(1220,65,690,950) @('--exclude','1310',"$($case.item)",'17','17','--exclude','1780','1000','17','17')
    Click ($case.x+$case.w-22) ($case.y+23)
}
Write-Output 'PASS: Editor/GFM/Browser menu composition, anchor, trails, dismissal, live WebKit frames'
