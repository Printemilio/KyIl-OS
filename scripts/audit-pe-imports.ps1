param(
    [Parameter(Mandatory=$true)][string[]]$Path,
    [string[]]$Search=@(),
    [string]$Census,
    [string]$ApiSchema="$env:SystemRoot/System32/apisetschema.dll",
    [string]$Out='build/windows/pe-import-graph.json'
)
$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot/..").Path
$python=(Get-Command python -ErrorAction Stop).Source
$arguments=@("$PSScriptRoot/audit-pe-graph.py")
foreach($image in $Path){$resolved=(Resolve-Path -LiteralPath $image).Path;$arguments+=$resolved;$Search+=Split-Path -Parent $resolved}
$Search+="$env:SystemRoot/System32"
foreach($directory in ($Search | Select-Object -Unique)){$arguments+=@('--search',$directory)}
if($ApiSchema){$arguments+=@('--api-schema',$ApiSchema)}
if($Census){$arguments+=@('--census',(Resolve-Path -LiteralPath $Census).Path)}
$output=if([IO.Path]::IsPathRooted($Out)){$Out}else{Join-Path $root $Out}
$arguments+=@('--repo',$root,'--out',$output)
& $python @arguments
if($LASTEXITCODE){throw 'Recursive PE inventory failed'}
Write-Host "Inventory: $output"
Write-Host 'Registered exports are partial contracts; host DLLs and API sets are reference data, not implementations.'
