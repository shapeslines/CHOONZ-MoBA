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
    @{ Name = 'frames-missing';       Arguments = @('--frames'); Pattern = '--frames expects' },
    @{ Name = 'frames-empty';         Arguments = @('--frames', ''); Pattern = '--frames expects'; AddScreenshot = $true },
    @{ Name = 'frames-text';          Arguments = @('--frames', 'abc'); Pattern = '--frames expects'; AddScreenshot = $true },
    @{ Name = 'frames-zero';          Arguments = @('--frames', '0'); Pattern = '--frames expects'; AddScreenshot = $true },
    @{ Name = 'frames-negative';      Arguments = @('--frames', '-1'); Pattern = '--frames expects'; AddScreenshot = $true },
    @{ Name = 'frames-plus';          Arguments = @('--frames', '+1'); Pattern = '--frames expects'; AddScreenshot = $true },
    @{ Name = 'frames-leading-zero';  Arguments = @('--frames', '01'); Pattern = '--frames expects'; AddScreenshot = $true },
    @{ Name = 'frames-leading-space'; Arguments = @('--frames', ' 1'); Pattern = '--frames expects'; AddScreenshot = $true },
    @{ Name = 'frames-trailing-space'; Arguments = @('--frames', '1 '); Pattern = '--frames expects'; AddScreenshot = $true },
    @{ Name = 'frames-decimal';       Arguments = @('--frames', '1.0'); Pattern = '--frames expects'; AddScreenshot = $true },
    @{ Name = 'frames-trailing-text'; Arguments = @('--frames', '1x'); Pattern = '--frames expects'; AddScreenshot = $true },
    @{ Name = 'frames-int-overflow';  Arguments = @('--frames', '2147483648'); Pattern = '--frames expects'; AddScreenshot = $true },
    @{ Name = 'frames-huge-overflow'; Arguments = @('--frames', '999999999999999999999999'); Pattern = '--frames expects'; AddScreenshot = $true },
    @{ Name = 'frames-option-value';  Arguments = @('--frames', '--orbit'); Pattern = '--frames expects'; AddScreenshot = $true },
    @{ Name = 'frames-duplicate';     Arguments = @('--frames', '1', '--frames', '2'); Pattern = '--frames expects'; AddScreenshot = $true },
    @{ Name = 'screenshot-missing';   Arguments = @('--screenshot'); Pattern = '--screenshot expects' },
    @{ Name = 'screenshot-empty';     Arguments = @('--screenshot', ''); Pattern = '--screenshot expects' },
    @{ Name = 'screenshot-option';    Arguments = @('--screenshot', '--orbit'); Pattern = '--screenshot expects' },
    @{ Name = 'screenshot-duplicate'; Arguments = @('--screenshot', '{shot}', '--screenshot', '{shot2}'); Pattern = '--screenshot expects' },
    @{ Name = 'orbit-duplicate';      Arguments = @('--orbit', '--orbit'); Pattern = 'duplicate option'; AddScreenshot = $true },
    @{ Name = 'unknown-option';       Arguments = @('--unknown'); Pattern = 'unknown option'; AddScreenshot = $true },
    @{ Name = 'extra-positional';     Arguments = @('unexpected'); Pattern = 'unknown option'; AddScreenshot = $true },
    @{ Name = 'self-check-combined';  Arguments = @('--sim-self-check'); Pattern = 'unknown option'; AddScreenshot = $true }
)

$failures = 0
try {
    foreach ($case in $cases) {
        $caseDir = Join-Path $caseRoot $case.Name
        New-Item -ItemType Directory -Path $caseDir | Out-Null
        $screenshot = Join-Path $caseDir 'unexpected.bmp'
        $screenshot2 = Join-Path $caseDir 'unexpected-2.bmp'
        [string[]]$arguments = @($case.Arguments | ForEach-Object {
            if ($_ -eq '{shot}') { $screenshot }
            elseif ($_ -eq '{shot2}') { $screenshot2 }
            else { $_ }
        })
        if ($case.AddScreenshot) {
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
              $joined -match $case.Pattern -and
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
Write-Output "sandbox CLI: $($cases.Count) malformed grammar cases passed"
exit 0
