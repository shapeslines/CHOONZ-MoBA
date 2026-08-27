param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$WorkDir
)

$ErrorActionPreference = 'Stop'

function Require([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$resolvedWork = [System.IO.Path]::GetFullPath($WorkDir)
$caseName = 'cooker-cli-' + [System.Guid]::NewGuid().ToString('N')
$caseDir = [System.IO.Path]::GetFullPath((Join-Path $resolvedWork $caseName))
$workPrefix = $resolvedWork.TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
Require ($caseDir.StartsWith($workPrefix, [System.StringComparison]::OrdinalIgnoreCase)) 'Refusing to use a test directory outside WorkDir.'

New-Item -ItemType Directory -Path $caseDir -Force | Out-Null
try {
    $source = Join-Path $caseDir 'pixel.tga'
    $first = Join-Path $caseDir 'first.mba'
    $second = Join-Path $caseDir 'second.mba'
    $malformed = Join-Path $caseDir 'malformed.tga'
    $rejected = Join-Path $caseDir 'rejected.mba'

    [byte[]]$tga = 0,0,2,0,0,0,0,0,0,0,0,0,1,0,1,0,32,40,0x11,0x22,0x33,0x44
    [System.IO.File]::WriteAllBytes($source, $tga)

    & $Exe --input $source --asset 'textures/pixel.mba' --output $first
    Require ($LASTEXITCODE -eq 0) 'The first TGA bake failed.'
    & $Exe --input $source --asset 'textures/pixel.mba' --output $second
    Require ($LASTEXITCODE -eq 0) 'The repeated TGA bake failed.'

    [byte[]]$firstBytes = [System.IO.File]::ReadAllBytes($first)
    [byte[]]$secondBytes = [System.IO.File]::ReadAllBytes($second)
    Require ($firstBytes.Length -eq $secondBytes.Length) 'Repeated bakes have different byte counts.'
    for ($index = 0; $index -lt $firstBytes.Length; ++$index) {
        Require ($firstBytes[$index] -eq $secondBytes[$index]) 'Repeated bakes are not byte-identical.'
    }
    Require ($firstBytes.Length -eq 52) 'Unexpected one-pixel .mba size.'
    Require ($firstBytes[0] -eq [byte][char]'M' -and $firstBytes[1] -eq [byte][char]'B' -and
             $firstBytes[2] -eq [byte][char]'A' -and $firstBytes[3] -eq 0) 'Missing .mba magic.'
    Require ($firstBytes[4] -eq 1 -and $firstBytes[5] -eq 0 -and
             $firstBytes[6] -eq 0 -and $firstBytes[7] -eq 0) 'Unexpected .mba version.'
    Require ($firstBytes[48] -eq 0x33 -and $firstBytes[49] -eq 0x22 -and
             $firstBytes[50] -eq 0x11 -and $firstBytes[51] -eq 0x44) 'TGA RGBA conversion is incorrect.'

    [System.IO.File]::WriteAllBytes($malformed, [byte[]](0,0,2))
    & $Exe --input $malformed --asset 'textures/rejected.mba' --output $rejected
    Require ($LASTEXITCODE -ne 0) 'Cooker accepted malformed TGA input.'

    Write-Output 'cooker CLI: deterministic TGA bake and malformed-input rejection passed'
}
finally {
    if (Test-Path -LiteralPath $caseDir) {
        Remove-Item -LiteralPath $caseDir -Recurse -Force
    }
}
