param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$WorkDir
)

$ErrorActionPreference = 'Stop'
$null = $WorkDir

$invalidCases = @(
    @{ Name = 'suite-missing';          Arguments = @('--suite'); Marker = '--suite requires a selector value' },
    @{ Name = 'suite-empty';            Arguments = @('--suite', ''); Marker = '--suite requires a selector value' },
    @{ Name = 'suite-option-as-value';  Arguments = @('--suite', '--list'); Marker = '--suite requires a selector value' },
    @{ Name = 'filter-missing';         Arguments = @('--filter'); Marker = '--filter requires a selector value' },
    @{ Name = 'filter-option-as-value'; Arguments = @('--filter', '--list'); Marker = '--filter requires a selector value' },
    @{ Name = 'unknown';                Arguments = @('--unknown'); Marker = "unknown test harness option '--unknown'" },
    @{ Name = 'unknown-after-suite';    Arguments = @('--suite', 'containers', '--unknown'); Marker = "unknown test harness option '--unknown'" }
)

$failures = 0
foreach ($case in $invalidCases) {
    [string[]]$arguments = @($case.Arguments)
    $savedPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = @(& $Exe @arguments 2>&1 | ForEach-Object { "$_" })
    $actualExit = $LASTEXITCODE
    $ErrorActionPreference = $savedPreference
    $joined = $output -join "`n"
    if ($actualExit -ne 2 -or $joined -notmatch [regex]::Escape($case.Marker) -or
        $joined -match '(?m)^\[') {
        Write-Output "FAIL $($case.Name) exit=$actualExit output=$joined"
        $failures += 1
    }
}

$savedPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
$filterOutput = @(& $Exe --filter array_str_edge_cases 2>&1 | ForEach-Object { "$_" })
$filterExit = $LASTEXITCODE
$listOutput = @(& $Exe --list 2>&1 | ForEach-Object { "$_" })
$listExit = $LASTEXITCODE
$ErrorActionPreference = $savedPreference
if ($filterExit -ne 0 -or ($filterOutput -join "`n") -notmatch '\[containers\].*array_str_edge_cases.*ok') {
    Write-Output "FAIL valid-filter exit=$filterExit output=$($filterOutput -join "`n")"
    $failures += 1
}
if ($listExit -ne 0 -or ($listOutput -join "`n") -notmatch '(?m)^containers\.array_str_edge_cases$') {
    Write-Output "FAIL valid-list exit=$listExit output=$($listOutput -join "`n")"
    $failures += 1
}

if ($failures -ne 0) {
    Write-Output "$failures test harness CLI case(s) failed"
    exit 1
}
Write-Output "test harness CLI: $($invalidCases.Count) malformed cases and valid controls passed"
exit 0
