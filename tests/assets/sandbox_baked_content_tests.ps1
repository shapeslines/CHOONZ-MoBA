param(
    [Parameter(Mandatory = $true)][string]$Cmake,
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [Parameter(Mandatory = $true)][string]$Configuration,
    [Parameter(Mandatory = $true)][string]$Cooker,
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Baked,
    [Parameter(Mandatory = $true)][string]$RecordSource,
    [Parameter(Mandatory = $true)][string]$RecordBaked,
    [Parameter(Mandatory = $true)][string]$RecordGolden,
    [Parameter(Mandatory = $true)][string]$WorkDir
)

$ErrorActionPreference = 'Stop'

function Require([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Require-EqualBytes([byte[]]$Actual, [byte[]]$Expected, [string]$Message) {
    Require ($Actual.Length -eq $Expected.Length) $Message
    for ($index = 0; $index -lt $Actual.Length; ++$index) {
        Require ($Actual[$index] -eq $Expected[$index]) $Message
    }
}

Require (-not [string]::IsNullOrWhiteSpace($Configuration)) 'CTest did not provide a build configuration.'
Require (Test-Path -LiteralPath $Cmake) 'CMake executable is missing.'
Require (Test-Path -LiteralPath $Cooker) 'Cooker executable is missing.'
Require (Test-Path -LiteralPath $Source) 'Sandbox TGA source is missing.'

& $Cmake --build $BuildDir --config $Configuration --target content
Require ($LASTEXITCODE -eq 0) 'CMake content target failed.'
Require (Test-Path -LiteralPath $Baked) 'CMake content target did not produce uv_test.mba.'
Require (Test-Path -LiteralPath $RecordBaked) 'CMake content target did not produce hero_test.mba.'
Require (Test-Path -LiteralPath $RecordGolden) 'Checked-in hero_test.mba golden is missing.'

$resolvedWork = [System.IO.Path]::GetFullPath($WorkDir)
$caseName = 'sandbox-baked-content-' + [System.Guid]::NewGuid().ToString('N')
$caseDir = [System.IO.Path]::GetFullPath((Join-Path $resolvedWork $caseName))
$workPrefix = $resolvedWork.TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
Require ($caseDir.StartsWith($workPrefix, [System.StringComparison]::OrdinalIgnoreCase)) 'Refusing to use a test directory outside WorkDir.'

New-Item -ItemType Directory -Path $caseDir -Force | Out-Null
try {
    $first = Join-Path $caseDir 'first.mba'
    $second = Join-Path $caseDir 'second.mba'

    & $Cooker --input $Source --asset 'uv_test.mba' --output $first
    Require ($LASTEXITCODE -eq 0) 'The first sandbox texture bake failed.'
    & $Cooker --input $Source --asset 'uv_test.mba' --output $second
    Require ($LASTEXITCODE -eq 0) 'The repeated sandbox texture bake failed.'

    [byte[]]$cooked = [System.IO.File]::ReadAllBytes($Baked)
    [byte[]]$firstBytes = [System.IO.File]::ReadAllBytes($first)
    [byte[]]$secondBytes = [System.IO.File]::ReadAllBytes($second)
    Require ($cooked.Length -ge 52) 'CMake-produced .mba is too small for a texture asset.'
    Require ($cooked[0] -eq [byte][char]'M' -and $cooked[1] -eq [byte][char]'B' -and
             $cooked[2] -eq [byte][char]'A' -and $cooked[3] -eq 0) 'CMake-produced output lacks .mba magic.'
    Require-EqualBytes $firstBytes $secondBytes 'Repeated sandbox texture bakes are not byte-identical.'
    Require-EqualBytes $cooked $firstBytes 'CMake content output differs from the deterministic sandbox texture bake.'

    # ADR-0016: the cooked hero record equals a fresh bake and the independent Python golden.
    $recordFirst = Join-Path $caseDir 'hero_first.mba'
    & $Cooker --kind hero --input $RecordSource --asset 'hero_test.mba' --output $recordFirst
    Require ($LASTEXITCODE -eq 0) 'The sandbox hero record bake failed.'
    [byte[]]$recordCooked = [System.IO.File]::ReadAllBytes($RecordBaked)
    [byte[]]$recordFirstBytes = [System.IO.File]::ReadAllBytes($recordFirst)
    [byte[]]$recordGolden = [System.IO.File]::ReadAllBytes($RecordGolden)
    Require-EqualBytes $recordCooked $recordFirstBytes 'CMake hero record differs from the deterministic bake.'
    Require-EqualBytes $recordCooked $recordGolden 'CMake hero record differs from the independent Python golden.'

    Write-Output 'sandbox baked content: CMake output matches deterministic cooker bytes'
}
finally {
    if (Test-Path -LiteralPath $caseDir) {
        Remove-Item -LiteralPath $caseDir -Recurse -Force
    }
}
