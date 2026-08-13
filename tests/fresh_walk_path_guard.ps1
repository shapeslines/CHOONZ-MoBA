param(
    [Parameter(Mandatory = $true)]
    [string]$Script,
    [Parameter(Mandatory = $true)]
    [string]$OutsideDir
)

$ErrorActionPreference = 'Stop'
$tempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\', '/')
$nonce = "moba-fresh-walk-guard-$PID-$([Guid]::NewGuid().ToString('N'))"
$fixtureRoot = Join-Path $tempRoot $nonce
$nested = Join-Path $fixtureRoot 'nested'
$filePath = Join-Path $tempRoot "$nonce-file"
$junctionPath = Join-Path $tempRoot "$nonce-junction"
$junctionTarget = Join-Path $tempRoot "$nonce-target"

function Invoke-Rejected([string]$name, [string]$path, [string]$pattern) {
    $output = @(
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Script -CloneDir $path 2>&1
    )
    $exitCode = $LASTEXITCODE
    $text = $output -join "`n"
    if ($exitCode -eq 0) {
        throw "${name}: fresh-walk accepted unsafe CloneDir $path"
    }
    if ($text -notmatch $pattern) {
        throw "${name}: unexpected rejection: $text"
    }
}

try {
    New-Item -ItemType Directory -Path $nested -Force | Out-Null
    $nestedSentinel = Join-Path $nested 'sentinel.txt'
    [System.IO.File]::WriteAllText($nestedSentinel, 'nested sentinel')

    [System.IO.File]::WriteAllText($filePath, 'file sentinel')

    New-Item -ItemType Directory -Path $junctionTarget -Force | Out-Null
    $junctionSentinel = Join-Path $junctionTarget 'sentinel.txt'
    [System.IO.File]::WriteAllText($junctionSentinel, 'junction sentinel')
    New-Item -ItemType Junction -Path $junctionPath -Target $junctionTarget | Out-Null

    $outsideSentinel = Join-Path $OutsideDir 'CMakeLists.txt'
    if (-not (Test-Path -LiteralPath $outsideSentinel -PathType Leaf)) {
        throw "outside sentinel is missing: $outsideSentinel"
    }

    $systemRoot = [System.IO.Path]::GetPathRoot($tempRoot)
    Invoke-Rejected 'root' $systemRoot 'CloneDir must be a non-root direct child'
    Invoke-Rejected 'temp root' $tempRoot 'CloneDir must be a non-root direct child'
    Invoke-Rejected 'source repository or outside temp' $OutsideDir 'CloneDir must (?:be a non-root direct child|not be the source repository)'
    Invoke-Rejected 'nested path' $nested 'CloneDir must be a non-root direct child'
    Invoke-Rejected 'file' $filePath 'CloneDir exists but is not a directory'
    Invoke-Rejected 'junction' $junctionPath 'CloneDir must not be a reparse point'

    if ([System.IO.File]::ReadAllText($nestedSentinel) -ne 'nested sentinel') {
        throw 'nested sentinel changed'
    }
    if ([System.IO.File]::ReadAllText($filePath) -ne 'file sentinel') {
        throw 'file sentinel changed'
    }
    if ([System.IO.File]::ReadAllText($junctionSentinel) -ne 'junction sentinel') {
        throw 'junction target sentinel changed'
    }
    if (-not (Test-Path -LiteralPath $outsideSentinel -PathType Leaf)) {
        throw 'outside sentinel changed'
    }

    Write-Output 'fresh-walk path guard PASS: root, temp root, outside, nested, file, and junction rejected without mutation'
    exit 0
} finally {
    foreach ($path in @($junctionPath, $fixtureRoot, $filePath, $junctionTarget)) {
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Recurse -Force
        }
    }
}
