param(
    [Parameter(Mandatory = $true)]
    [string]$Script,
    [Parameter(Mandatory = $true)]
    [string]$GuardScript,
    [Parameter(Mandatory = $true)]
    [string]$OutsideDir
)

$ErrorActionPreference = 'Stop'
. $GuardScript
$tempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\', '/')
$nonce = "moba-fresh-walk-guard-$PID-$([Guid]::NewGuid().ToString('N'))"
$fixtureRoot = Join-Path $tempRoot $nonce
$nested = Join-Path $fixtureRoot 'nested'
$existingDir = Join-Path $tempRoot "$nonce-existing"
$filePath = Join-Path $tempRoot "$nonce-file"
$junctionPath = Join-Path $tempRoot "$nonce-junction"
$junctionTarget = Join-Path $tempRoot "$nonce-target"
$swapPath = Join-Path $tempRoot "$nonce-swap"
$swapTarget = Join-Path $tempRoot "$nonce-swap-target"
$replacementPath = Join-Path $tempRoot "$nonce-replacement"
$intervalPath = Join-Path $tempRoot "$nonce-interval"
$intervalMoved = Join-Path $tempRoot "$nonce-interval-moved"

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

    New-Item -ItemType Directory -Path $existingDir | Out-Null
    $existingSentinel = Join-Path $existingDir 'sentinel.txt'
    [System.IO.File]::WriteAllText($existingSentinel, 'existing sentinel')

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
    Invoke-Rejected 'pre-existing directory' $existingDir 'CloneDir must not already exist'
    Invoke-Rejected 'file' $filePath 'CloneDir exists but is not a directory'
    Invoke-Rejected 'junction' $junctionPath 'CloneDir must not be a reparse point'

    # Record a real invocation lease, then replace its path with a junction. The
    # final cleanup check must reject the swap and leave the target untouched.
    $swapLease = New-FreshWalkLease $swapPath $OutsideDir
    New-Item -ItemType Directory -Path (Join-Path $swapPath '.git') -Force | Out-Null
    Initialize-FreshWalkLease $swapLease
    Remove-Item -LiteralPath $swapPath -Recurse -Force
    New-Item -ItemType Directory -Path $swapTarget | Out-Null
    $swapSentinel = Join-Path $swapTarget 'sentinel.txt'
    [System.IO.File]::WriteAllText($swapSentinel, 'swap sentinel')
    New-Item -ItemType Junction -Path $swapPath -Target $swapTarget | Out-Null
    try {
        Remove-FreshWalkLease $swapLease
        throw 'cleanup accepted a junction swapped in after lease creation'
    } catch {
        if ($_.Exception.Message -notmatch 'reparse point') { throw }
    }

    # A replacement by a normal directory is not a reparse point, so the stable
    # file identity must independently stop deletion.
    $replacementLease = New-FreshWalkLease $replacementPath $OutsideDir
    New-Item -ItemType Directory -Path (Join-Path $replacementPath '.git') -Force | Out-Null
    Initialize-FreshWalkLease $replacementLease
    Remove-Item -LiteralPath $replacementPath -Recurse -Force
    New-Item -ItemType Directory -Path (Join-Path $replacementPath '.git') -Force | Out-Null
    $replacementSentinel = Join-Path $replacementPath 'sentinel.txt'
    [System.IO.File]::WriteAllText($replacementSentinel, 'replacement sentinel')
    try {
        Remove-FreshWalkLease $replacementLease
        throw 'cleanup accepted a different directory at the leased path'
    } catch {
        if ($_.Exception.Message -notmatch 'identity changed') { throw }
    }

    # Attempt the swap in the exact interval after lease validation and before
    # cleanup. The live non-delete-sharing handle must make even the first rename
    # step fail, after which handle-bound disposition removes the leased object.
    $intervalLease = New-FreshWalkLease $intervalPath $OutsideDir
    New-Item -ItemType Directory -Path (Join-Path $intervalPath '.git') -Force | Out-Null
    Initialize-FreshWalkLease $intervalLease
    $intervalState = [pscustomobject]@{ MoveBlocked = $false }
    Remove-FreshWalkLease $intervalLease {
        try {
            [System.IO.Directory]::Move($intervalPath, $intervalMoved)
            throw 'post-validation directory move unexpectedly succeeded'
        } catch [System.IO.IOException] {
            $intervalState.MoveBlocked = $true
        }
    }
    if (-not $intervalState.MoveBlocked) {
        throw 'post-validation path swap was not blocked by the cleanup handle'
    }
    if ((Test-Path -LiteralPath $intervalPath) -or
        (Test-Path -LiteralPath $intervalMoved)) {
        throw 'handle-bound cleanup left a post-validation race fixture behind'
    }

    if ([System.IO.File]::ReadAllText($nestedSentinel) -ne 'nested sentinel') {
        throw 'nested sentinel changed'
    }
    if ([System.IO.File]::ReadAllText($filePath) -ne 'file sentinel') {
        throw 'file sentinel changed'
    }
    if ([System.IO.File]::ReadAllText($existingSentinel) -ne 'existing sentinel') {
        throw 'existing-directory sentinel changed'
    }
    if ([System.IO.File]::ReadAllText($junctionSentinel) -ne 'junction sentinel') {
        throw 'junction target sentinel changed'
    }
    if ([System.IO.File]::ReadAllText($swapSentinel) -ne 'swap sentinel') {
        throw 'swap target sentinel changed'
    }
    if ([System.IO.File]::ReadAllText($replacementSentinel) -ne 'replacement sentinel') {
        throw 'replacement-directory sentinel changed'
    }
    if (-not (Test-Path -LiteralPath $outsideSentinel -PathType Leaf)) {
        throw 'outside sentinel changed'
    }

    Write-Output 'fresh-walk path guard PASS: unsafe locations, existing targets, and cleanup swaps rejected without deletion'
    exit 0
} finally {
    foreach ($link in @($junctionPath, $swapPath)) {
        if (Test-Path -LiteralPath $link) {
            [System.IO.Directory]::Delete($link)
        }
    }
    foreach ($path in @($fixtureRoot, $existingDir, $filePath, $junctionTarget,
                         $swapTarget, $replacementPath, $intervalPath, $intervalMoved)) {
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Recurse -Force
        }
    }
}
