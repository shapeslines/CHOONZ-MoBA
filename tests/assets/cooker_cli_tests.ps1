param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$ContentDir,
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

    # ADR-0016 typed records: each authored kind recooks byte-identically and matches
    # the independent Python golden; malformed text is rejected; a bad --kind is usage.
    function Require-EqualFiles([string]$A, [string]$B, [string]$Message) {
        [byte[]]$ab = [System.IO.File]::ReadAllBytes($A)
        [byte[]]$bb = [System.IO.File]::ReadAllBytes($B)
        Require ($ab.Length -eq $bb.Length) $Message
        for ($i = 0; $i -lt $ab.Length; ++$i) { Require ($ab[$i] -eq $bb[$i]) $Message }
    }
    $records = @(
        @{ kind = 'hero';      source = 'hero_test.txt';  asset = 'hero_test.mba';              golden = 'hero_test.mba' },
        @{ kind = 'objective'; source = 'tower_test.txt'; asset = 'objectives/tower_a.mba';     golden = 'tower_test.mba' },
        @{ kind = 'economy';   source = 'gold_rule.txt';  asset = 'economy/gold_hero_kill.mba'; golden = 'gold_rule.mba' }
    )
    foreach ($record in $records) {
        $src = Join-Path $ContentDir $record.source
        $outA = Join-Path $caseDir ($record.kind + '_a.mba')
        $outB = Join-Path $caseDir ($record.kind + '_b.mba')
        & $Exe --kind $record.kind --input $src --asset $record.asset --output $outA
        Require ($LASTEXITCODE -eq 0) ("The " + $record.kind + " record bake failed.")
        & $Exe --kind $record.kind --input $src --asset $record.asset --output $outB
        Require ($LASTEXITCODE -eq 0) ("The repeated " + $record.kind + " record bake failed.")
        Require-EqualFiles $outA $outB ("Repeated " + $record.kind + " bakes are not byte-identical.")
        Require-EqualFiles $outA (Join-Path (Join-Path $ContentDir 'golden') $record.golden) ("The " + $record.kind + " bake differs from the Python golden.")
    }
    $badText = Join-Path $caseDir 'bad_hero.txt'
    [System.IO.File]::WriteAllText($badText, "schema_version = 1`nhero_id = heroes/x`nmax_health = 0`nmove_speed_q16 = 1`nattack_range_q16 = 1`n")
    & $Exe --kind hero --input $badText --asset 'heroes/x.mba' --output (Join-Path $caseDir 'bad_hero.mba')
    Require ($LASTEXITCODE -eq 1) 'Cooker accepted an invalid hero record.'
    & $Exe --kind bogus --input $badText --asset 'heroes/x.mba' --output (Join-Path $caseDir 'bogus.mba')
    Require ($LASTEXITCODE -eq 2) 'Cooker did not treat an unknown --kind as usage.'

    Write-Output 'cooker CLI: deterministic TGA bake and malformed-input rejection passed'
}
finally {
    if (Test-Path -LiteralPath $caseDir) {
        Remove-Item -LiteralPath $caseDir -Recurse -Force
    }
}
