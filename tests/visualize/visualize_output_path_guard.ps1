param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$WorkDir
)

$ErrorActionPreference = 'Stop'
$caseRoot = Join-Path $WorkDir 'visualize-output-path-guard'
if (Test-Path -LiteralPath $caseRoot) {
    Remove-Item -LiteralPath $caseRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $caseRoot | Out-Null

try {
    $overlongDir = Join-Path $caseRoot ('x' * 600)
    $savedPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = @(& $Exe $overlongDir 2>&1 | ForEach-Object { "$_" })
    $actualExit = $LASTEXITCODE
    $ErrorActionPreference = $savedPreference
    $joined = $output -join "`n"
    $created = @(Get-ChildItem -LiteralPath $caseRoot -Force)

    if ($actualExit -ne 1 -or $joined -notmatch 'ERROR: output path too long' -or
        $joined -match '(?im)^\s*wrote |^done\.$' -or $created.Count -ne 0) {
        Write-Output "FAIL visualize output guard exit=$actualExit files=$($created.Count) output=$joined"
        exit 1
    }
} finally {
    Remove-Item -LiteralPath $caseRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Output 'visualize output path guard: truncated path rejected before file creation'
exit 0
