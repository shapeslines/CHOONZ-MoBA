param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$WorkDir
)

$ErrorActionPreference = 'Stop'
$caseRoot = Join-Path $WorkDir 'sandbox-cli-error-cases'
if (Test-Path -LiteralPath $caseRoot) {
    Remove-Item -LiteralPath $caseRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $caseRoot | Out-Null

$cases = @(
    @{ Name = 'missing';          Arguments = @('--frames') },
    @{ Name = 'empty';            Arguments = @('--frames', '') },
    @{ Name = 'text';             Arguments = @('--frames', 'abc') },
    @{ Name = 'zero';             Arguments = @('--frames', '0') },
    @{ Name = 'negative';         Arguments = @('--frames', '-1') },
    @{ Name = 'plus';             Arguments = @('--frames', '+1') },
    @{ Name = 'leading-zero';     Arguments = @('--frames', '01') },
    @{ Name = 'leading-space';    Arguments = @('--frames', ' 1') },
    @{ Name = 'trailing-space';   Arguments = @('--frames', '1 ') },
    @{ Name = 'decimal';          Arguments = @('--frames', '1.0') },
    @{ Name = 'trailing-text';    Arguments = @('--frames', '1x') },
    @{ Name = 'int-overflow';     Arguments = @('--frames', '2147483648') },
    @{ Name = 'huge-overflow';    Arguments = @('--frames', '999999999999999999999999') },
    @{ Name = 'option-as-value';  Arguments = @('--frames', '--orbit') }
)

$failures = 0
try {
    foreach ($case in $cases) {
        $caseDir = Join-Path $caseRoot $case.Name
        New-Item -ItemType Directory -Path $caseDir | Out-Null
        $screenshot = Join-Path $caseDir 'unexpected.bmp'
        [string[]]$arguments = @($case.Arguments)
        if ($case.Name -ne 'missing') {
            $arguments += @('--screenshot', $screenshot)
        }

        $savedPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        $output = @(& $Exe @arguments 2>&1 | ForEach-Object { "$_" })
        $actualExit = $LASTEXITCODE
        $ErrorActionPreference = $savedPreference
        $joined = $output -join "`n"
        $created = @(Get-ChildItem -LiteralPath $caseDir -Force)

        $ok = $actualExit -eq 2 -and
              $joined -match 'sandbox: --frames expects a canonical positive integer' -and
              $joined -notmatch '(?im)^(?:platform|renderer|validation|sandbox: window open)' -and
              $joined -notmatch '(?i)Vulkan|typed cube|sim_oracle' -and
              $created.Count -eq 0
        if (-not $ok) {
            Write-Output "FAIL $($case.Name) exit=$actualExit files=$($created.Count) output=$joined"
            $failures += 1
        }
    }
} finally {
    Remove-Item -LiteralPath $caseRoot -Recurse -Force -ErrorAction SilentlyContinue
}

if ($failures -ne 0) {
    Write-Output "$failures sandbox CLI case(s) failed"
    exit 1
}
Write-Output "sandbox CLI: $($cases.Count) malformed frame cases passed"
exit 0
