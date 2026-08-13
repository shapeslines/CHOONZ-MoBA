param(
    [Parameter(Mandatory = $true)][string]$Exe
)

$ErrorActionPreference = 'Stop'

# The visualizer uses a fixed path buffer. A long caller-provided directory must
# fail before snprintf truncation can redirect a BMP write to another path.
$longDirectory = 'x' * 600
$output = & $Exe $longDirectory 2>&1
$actual = $LASTEXITCODE
$text = $output -join [Environment]::NewLine

if ($actual -ne 1) {
    throw "Expected exit 1 for an overlong output directory, got ${actual}: ${text}"
}
if ($text -notmatch 'output path too long') {
    throw "Expected an explicit output-path diagnostic: ${text}"
}
if ($text -match 'done\.') {
    throw "Overlong output directory reported success: ${text}"
}
if ($text -match 'wrote ') {
    throw "Overlong output directory reached a BMP write: ${text}"
}

Write-Output 'visualize output path guard: PASS'
