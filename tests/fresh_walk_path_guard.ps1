param(
    [Parameter(Mandatory = $true)]
    [string]$Script,
    [Parameter(Mandatory = $true)]
    [string]$OutsideDir
)

$ErrorActionPreference = 'Stop'
$output = @(
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Script -CloneDir $OutsideDir 2>&1
)
$exitCode = $LASTEXITCODE
$text = $output -join "`n"

if ($exitCode -eq 0) {
    Write-Error "fresh-walk accepted an unsafe CloneDir: $OutsideDir"
    exit 1
}
if ($text -notmatch 'CloneDir must be a non-root direct child of the system temp directory') {
    Write-Error "fresh-walk rejected the path for an unexpected reason: $text"
    exit 1
}

Write-Output "fresh-walk path guard rejected unsafe CloneDir before cleanup"
exit 0
