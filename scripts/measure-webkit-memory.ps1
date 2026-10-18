param(
    [string]$Log='build/vex-serial.log',
    [int]$Producer=14,
    [string]$Output='build/webkit-memory-measurement.json'
)
$ErrorActionPreference='Stop'
$taskText=Get-Content -LiteralPath $Log -Raw
# Compare the same point in the producer lifecycle. The UI's later mmap
# already includes committed bitmap pages and cannot be mixed into this series.
$taskSamples=[regex]::Matches($taskText,"wk2-shm-mmap: pid=$Producer\s[^`r`n]*free=(\d+)")
if($taskSamples.Count -lt 24){throw 'At least 24 producer samples are required.'}
$taskValues=@($taskSamples | ForEach-Object {[long]$_.Groups[1].Value})
$taskWindows=@()
foreach($taskLength in @(24,64,128)){
    if($taskValues.Count -ge $taskLength){
        $taskSlice=$taskValues[($taskValues.Count-$taskLength)..($taskValues.Count-1)]
        $taskRange=$taskSlice | Measure-Object -Minimum -Maximum
        $taskWindows+=@{samples=$taskLength;freeFirst=$taskSlice[0];freeLast=$taskSlice[-1];
            netLossKiB=($taskSlice[0]-$taskSlice[-1])*4;
            freeRangePages=$taskRange.Maximum-$taskRange.Minimum}
    }
}
$taskReport=[ordered]@{
    log=$Log;producer=$Producer;samples=$taskValues.Count;pageBytes=4096;
    windows=$taskWindows;
    categoryTail=@(($taskText -split "`r?`n" | Where-Object {
        $_ -match 'memory-category:|memory-resident:|Vex GL memory:|Vex libc memory:|WebKit UI libc memory:'
    }) | Select-Object -Last 24)
}
$taskReport | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $Output -Encoding utf8
$taskReport.windows | Format-Table
Write-Output "Saved $Output; no leak classification inferred from PMM alone."
