param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Replay,
    [Parameter(Mandatory = $true)][string]$WorkDir
)

$ErrorActionPreference = 'Stop'

function Invoke-ExpectExit {
    param(
        [int]$Expected,
        [string[]]$Arguments
    )
    $savedPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = & $Exe @Arguments 2>&1
    $actual = $LASTEXITCODE
    $ErrorActionPreference = $savedPreference
    if ($actual -ne $Expected) {
        Write-Host ($output -join [Environment]::NewLine)
        throw "Expected exit $Expected, got ${actual}: $($Arguments -join ' ')"
    }
}

function Copy-ReplayBytes {
    param([string]$Name)
    $path = Join-Path $WorkDir $Name
    [IO.File]::WriteAllBytes($path, [IO.File]::ReadAllBytes($Replay))
    return $path
}

function Invoke-ExpectRecordFailure {
    param(
        [string]$Name,
        [string]$Option,
        [string]$Value
    )
    $path = Join-Path $WorkDir "moba_replay_rejected_${Name}.mbr"
    $null = $created.Add($path)
    Invoke-ExpectExit 1 @('record', '--out', $path, $Option, $Value)
    if (Test-Path -LiteralPath $path) {
        throw "Malformed numeric option created output: $Option '$Value'"
    }
}

function Invoke-ExpectUsageNoSideEffect {
    param(
        [string]$Name,
        [string[]]$Arguments
    )
    $caseDir = Join-Path $WorkDir "moba_replay_grammar_$Name"
    New-Item -ItemType Directory -Path $caseDir | Out-Null
    try {
        Push-Location $caseDir
        try {
            $savedPreference = $ErrorActionPreference
            $ErrorActionPreference = 'Continue'
            $output = @(& $Exe @Arguments 2>&1 | ForEach-Object { "$_" })
            $actual = $LASTEXITCODE
            $ErrorActionPreference = $savedPreference
        } finally {
            Pop-Location
        }
        $text = $output -join "`n"
        $children = @(Get-ChildItem -LiteralPath $caseDir -Force)
        if ($actual -ne 1 -or $children.Count -ne 0 -or
            $text -match '(?i)cannot stat|cannot read|recorded ticks|atomically write') {
            throw "Malformed replay grammar reached I/O: $Name exit=$actual children=$($children.Count) output=$text"
        }
    } finally {
        Remove-Item -LiteralPath $caseDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

$created = [Collections.Generic.List[string]]::new()
try {
    Invoke-ExpectExit 1 @('verify', (Join-Path $WorkDir 'does-not-exist.mbr'))
    Invoke-ExpectExit 1 @('record', '--ticks', '1')

    $grammarOut1 = 'grammar-out-1.mbr'
    $grammarOut2 = 'grammar-out-2.mbr'
    Invoke-ExpectUsageNoSideEffect 'out-missing' @('record', '--out')
    Invoke-ExpectUsageNoSideEffect 'out-empty' @('record', '--out', '')
    Invoke-ExpectUsageNoSideEffect 'out-option-shaped' @('record', '--out', '--ticks')
    Invoke-ExpectUsageNoSideEffect 'out-duplicate' @('record', '--out', $grammarOut1,
                                                     '--out', $grammarOut2)
    Invoke-ExpectUsageNoSideEffect 'unknown-option' @('record', '--out', $grammarOut1,
                                                      '--unknown')
    Invoke-ExpectUsageNoSideEffect 'extra-positional' @('record', '--out', $grammarOut1,
                                                       'unexpected')
    Invoke-ExpectUsageNoSideEffect 'ticks-missing' @('record', '--out', $grammarOut1, '--ticks')
    Invoke-ExpectUsageNoSideEffect 'ticks-option-shaped' @('record', '--out', $grammarOut1,
                                                           '--ticks', '--seed')
    Invoke-ExpectUsageNoSideEffect 'inspect-option-shaped' @('inspect', '--help')
    Invoke-ExpectUsageNoSideEffect 'verify-option-shaped' @('verify', '--help')
    Invoke-ExpectUsageNoSideEffect 'inspect-extra' @('inspect', $Replay, 'unexpected')
    Invoke-ExpectUsageNoSideEffect 'help-extra' @('--help', 'unexpected')

    $numericValues = @(
        '+1', '-1', ' 1', '1 ', '1x', '01', '00', '18446744073709551616'
    )
    $caseIndex = 0
    foreach ($option in @('--ticks', '--seed', '--players')) {
        foreach ($value in $numericValues) {
            Invoke-ExpectRecordFailure "$($option.TrimStart('-'))-$caseIndex" $option $value
            $caseIndex += 1
        }
    }
    Invoke-ExpectRecordFailure 'ticks-out-of-range' '--ticks' '1000001'
    Invoke-ExpectRecordFailure 'players-zero' '--players' '0'
    Invoke-ExpectRecordFailure 'players-out-of-range' '--players' '11'

    $sentinelPath = Join-Path $WorkDir 'moba_replay_existing_sentinel.mbr'
    $sentinelBytes = [System.Text.Encoding]::ASCII.GetBytes('existing replay sentinel')
    [IO.File]::WriteAllBytes($sentinelPath, $sentinelBytes)
    $null = $created.Add($sentinelPath)
    Invoke-ExpectExit 1 @('record', '--out', $sentinelPath, '--ticks', '+1')
    $afterBytes = [IO.File]::ReadAllBytes($sentinelPath)
    if ([Convert]::ToBase64String($afterBytes) -ne [Convert]::ToBase64String($sentinelBytes)) {
        throw 'Malformed replay numeric input changed an existing output file'
    }

    $badMagic = Copy-ReplayBytes 'moba_replay_bad_magic.mbr'
    $created.Add($badMagic)
    $bytes = [IO.File]::ReadAllBytes($badMagic)
    $bytes[0] = $bytes[0] -bxor 1
    [IO.File]::WriteAllBytes($badMagic, $bytes)
    Invoke-ExpectExit 2 @('inspect', $badMagic)

    $badVersion = Copy-ReplayBytes 'moba_replay_bad_version.mbr'
    $created.Add($badVersion)
    $bytes = [IO.File]::ReadAllBytes($badVersion)
    $bytes[8] = 2
    [IO.File]::WriteAllBytes($badVersion, $bytes)
    Invoke-ExpectExit 2 @('verify', $badVersion)

    $badLogic = Copy-ReplayBytes 'moba_replay_bad_logic.mbr'
    $created.Add($badLogic)
    $bytes = [IO.File]::ReadAllBytes($badLogic)
    $bytes[12] = $bytes[12] -bxor 1
    [IO.File]::WriteAllBytes($badLogic, $bytes)
    Invoke-ExpectExit 2 @('verify', $badLogic)

    $m31Logic = Copy-ReplayBytes 'moba_replay_m31_logic.mbr'
    $created.Add($m31Logic)
    $bytes = [IO.File]::ReadAllBytes($m31Logic)
    # Exact little-endian M3.1 logic hash: 0x7902599e173f87a6.
    [byte[]]$oldLogic = 0xa6, 0x87, 0x3f, 0x17, 0x9e, 0x59, 0x02, 0x79
    [Array]::Copy($oldLogic, 0, $bytes, 12, $oldLogic.Length)
    [IO.File]::WriteAllBytes($m31Logic, $bytes)
    Invoke-ExpectExit 2 @('verify', $m31Logic)

    $badRate = Copy-ReplayBytes 'moba_replay_bad_rate.mbr'
    $created.Add($badRate)
    $bytes = [IO.File]::ReadAllBytes($badRate)
    $bytes[28] = 60
    [IO.File]::WriteAllBytes($badRate, $bytes)
    Invoke-ExpectExit 2 @('verify', $badRate)

    $truncated = Copy-ReplayBytes 'moba_replay_truncated.mbr'
    $created.Add($truncated)
    $bytes = [IO.File]::ReadAllBytes($truncated)
    [IO.File]::WriteAllBytes($truncated, $bytes[0..($bytes.Length - 2)])
    Invoke-ExpectExit 2 @('verify', $truncated)

    # First command starts at byte 56: kind, player, little-endian unit index.
    $unknownCommand = Copy-ReplayBytes 'moba_replay_unknown_command.mbr'
    $created.Add($unknownCommand)
    $bytes = [IO.File]::ReadAllBytes($unknownCommand)
    $bytes[56] = 255
    [IO.File]::WriteAllBytes($unknownCommand, $bytes)
    Invoke-ExpectExit 2 @('verify', $unknownCommand)

    $badPlayer = Copy-ReplayBytes 'moba_replay_bad_player.mbr'
    $created.Add($badPlayer)
    $bytes = [IO.File]::ReadAllBytes($badPlayer)
    $bytes[57] = 2
    [IO.File]::WriteAllBytes($badPlayer, $bytes)
    Invoke-ExpectExit 2 @('verify', $badPlayer)

    $badUnit = Copy-ReplayBytes 'moba_replay_bad_unit.mbr'
    $created.Add($badUnit)
    $bytes = [IO.File]::ReadAllBytes($badUnit)
    $bytes[58] = 64
    $bytes[59] = 0
    [IO.File]::WriteAllBytes($badUnit, $bytes)
    Invoke-ExpectExit 2 @('verify', $badUnit)

    $trailing = Copy-ReplayBytes 'moba_replay_trailing.mbr'
    $created.Add($trailing)
    $bytes = [IO.File]::ReadAllBytes($trailing)
    $expanded = [byte[]]::new($bytes.Length + 1)
    [Array]::Copy($bytes, $expanded, $bytes.Length)
    $expanded[$bytes.Length] = 204
    [IO.File]::WriteAllBytes($trailing, $expanded)
    Invoke-ExpectExit 2 @('inspect', $trailing)

    # The first expected post-tick hash starts at byte 48.
    $divergent = Copy-ReplayBytes 'moba_replay_divergent.mbr'
    $created.Add($divergent)
    $bytes = [IO.File]::ReadAllBytes($divergent)
    $bytes[48] = $bytes[48] -bxor 1
    [IO.File]::WriteAllBytes($divergent, $bytes)
    Invoke-ExpectExit 3 @('verify', $divergent)
}
finally {
    foreach ($path in $created) {
        Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
    }
}
