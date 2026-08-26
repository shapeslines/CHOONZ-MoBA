param(
    [Parameter(Mandatory = $true)][string]$Cooker,
    [Parameter(Mandatory = $true)][string]$SourceTga,
    [Parameter(Mandatory = $true)][string]$WorkDir,
    [Parameter(Mandatory = $true)][string]$GuardScript,
    [string]$SecondCooker = ''
)

$ErrorActionPreference = 'Stop'

function Fail([string]$Message) {
    throw "cooker CLI test: $Message"
}

if (-not (Test-Path -LiteralPath $GuardScript -PathType Leaf)) {
    Fail "cleanup guard is missing: $GuardScript"
}
. $GuardScript

function Write-Utf8([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}

function Get-Sha256Hex([string]$Path) {
    # CTest can run this script with a reduced PowerShell command surface, so use
    # the .NET BCL directly instead of an optional hash cmdlet.
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    $stream = $null
    try {
        $stream = [IO.File]::OpenRead($Path)
        $hash = $algorithm.ComputeHash($stream)
        return ([BitConverter]::ToString($hash)).Replace('-', '')
    }
    finally {
        if ($null -ne $stream) { $stream.Dispose() }
        $algorithm.Dispose()
    }
}

function Invoke-Cooker([string]$Exe, [string[]]$Arguments, [int]$Expected) {
    $savedPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = @(& $Exe @Arguments 2>&1)
        $actual = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedPreference
    }
    if ($actual -ne $Expected) {
        Fail "expected exit $Expected, got $actual; output=$($output -join ' | ')"
    }
    return ($output -join "`n")
}

function New-Roots([string]$Parent, [string]$Name) {
    $root = Join-Path $Parent $Name
    $source = Join-Path $root 'source'
    $out = Join-Path $root 'baked'
    $generated = Join-Path $root 'generated'
    $generatedAssets = Join-Path $generated 'assets'
    [IO.Directory]::CreateDirectory($source) | Out-Null
    [IO.Directory]::CreateDirectory($out) | Out-Null
    [IO.Directory]::CreateDirectory($generatedAssets) | Out-Null
    return [pscustomobject]@{
        Root = $root
        Source = $source
        Out = $out
        Generated = $generated
        Manifest = (Join-Path $root 'manifest.txt')
    }
}

function Write-Wav([string]$Path) {
    [byte[]]$bytes = @(
        0x52,0x49,0x46,0x46, 0x28,0x00,0x00,0x00, 0x57,0x41,0x56,0x45,
        0x66,0x6d,0x74,0x20, 0x10,0x00,0x00,0x00, 0x01,0x00,0x02,0x00,
        0x80,0xbb,0x00,0x00, 0x00,0xee,0x02,0x00, 0x04,0x00,0x10,0x00,
        0x64,0x61,0x74,0x61, 0x04,0x00,0x00,0x00, 0x01,0x02,0x03,0x04
    )
    [IO.File]::WriteAllBytes($Path, $bytes)
}

function Populate-Sources($Roots) {
    [IO.File]::Copy($SourceTga, (Join-Path $Roots.Source 'uv_test.tga'), $true)
    Write-Wav (Join-Path $Roots.Source 'tone.wav')
    Write-Utf8 $Roots.Manifest "tone.wav`nuv_test.tga`n"
}

function Snapshot-Files($Roots) {
    $items = @()
    foreach ($base in @($Roots.Out, $Roots.Generated)) {
        $prefix = [IO.Path]::GetFullPath($base).TrimEnd('\') + '\'
        foreach ($file in @(Get-ChildItem -LiteralPath $base -File -Recurse | Sort-Object FullName)) {
            $relative = $file.FullName.Substring($prefix.Length).Replace('\', '/')
            $scope = if ($base -eq $Roots.Out) { 'baked/' } else { 'generated/' }
            $items += [pscustomobject]@{
                Path = $scope + $relative
                Hash = Get-Sha256Hex $file.FullName
                Length = $file.Length
                Ticks = $file.LastWriteTimeUtc.Ticks
            }
        }
    }
    return @($items | Sort-Object Path)
}

function Assert-SameBytes($Expected, $Actual, [string]$Label) {
    if ($Expected.Count -ne $Actual.Count) {
        Fail "$Label file-count mismatch $($Expected.Count) != $($Actual.Count)"
    }
    for ($i = 0; $i -lt $Expected.Count; ++$i) {
        if ($Expected[$i].Path -ne $Actual[$i].Path -or
            $Expected[$i].Length -ne $Actual[$i].Length -or
            $Expected[$i].Hash -ne $Actual[$i].Hash) {
            Fail "$Label differs at $($Expected[$i].Path) / $($Actual[$i].Path)"
        }
    }
}

function Assert-NoPublishedOutputs($Roots, [string]$Label) {
    if ((Snapshot-Files $Roots).Count -ne 0) {
        Fail "$Label published output"
    }
}

function New-TestJunction([string]$Path, [string]$Target) {
    $junction = New-Item -ItemType Junction -Path $Path -Target $Target
    if (($junction.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
        Fail "junction fixture is not a reparse point: $Path"
    }
}

$fixture = Join-Path ([IO.Path]::GetTempPath()) (
    "moba-cooker-cli-" + [Guid]::NewGuid().ToString('N'))
$lease = New-FreshWalkLease $fixture $WorkDir -CreateOwnedDirectory
try {
    $collisionRejected = $false
    try {
        $unexpected = [MobaFreshWalkNative]::CreateOwnedCleanupDirectory(
            [IO.Path]::GetDirectoryName($fixture), [IO.Path]::GetFileName($fixture))
        $unexpected.Dispose()
    }
    catch [IO.IOException] {
        $collisionRejected = $true
    }
    if (-not $collisionRejected) {
        Fail 'atomic fixture acquisition did not reject an existing directory'
    }

    [IO.Directory]::CreateDirectory((Join-Path $fixture '.git')) | Out-Null
    Initialize-FreshWalkLease $lease

    # Exercise the portable digest path in the same shell that runs the CTest
    # entrypoint. This keeps byte-publication comparisons tied to SHA-256 rather
    # than merely proving that two output directories happen to match.
    $hashFixture = Join-Path $fixture 'sha256-portable-fixture.bin'
    [IO.File]::WriteAllBytes($hashFixture, [byte[]]@(0x61, 0x62, 0x63))
    if ((Get-Sha256Hex $hashFixture) -ne
        'BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD') {
        Fail 'portable SHA-256 implementation returned an unexpected digest'
    }

    $first = New-Roots $fixture 'first'
    $second = New-Roots $fixture 'second'
    Populate-Sources $first
    Populate-Sources $second

    $args1 = @('--source-root', $first.Source, '--manifest', $first.Manifest,
               '--out-root', $first.Out, '--generated-root', $first.Generated)
    $args2 = @('--source-root', $second.Source, '--manifest', $second.Manifest,
               '--out-root', $second.Out, '--generated-root', $second.Generated)
    Invoke-Cooker $Cooker $args1 0 | Out-Null
    Invoke-Cooker $Cooker $args2 0 | Out-Null
    $snapshot1 = Snapshot-Files $first
    $snapshot2 = Snapshot-Files $second
    Assert-SameBytes $snapshot1 $snapshot2 'independent cooks'
    if ($snapshot1.Count -ne 3) { Fail "expected two mba files plus header, got $($snapshot1.Count)" }

    Start-Sleep -Milliseconds 1100
    $repeatOutput = Invoke-Cooker $Cooker $args1 0
    if ($repeatOutput -notmatch 'written=0 unchanged=3') {
        Fail "unchanged recook did not report a no-write result: $repeatOutput"
    }
    $repeat = Snapshot-Files $first
    Assert-SameBytes $snapshot1 $repeat 'unchanged recook bytes'
    for ($i = 0; $i -lt $snapshot1.Count; ++$i) {
        if ($snapshot1[$i].Ticks -ne $repeat[$i].Ticks) {
            Fail "unchanged recook rewrote $($snapshot1[$i].Path)"
        }
    }

    if ($SecondCooker) {
        $cross = New-Roots $fixture 'cross-config'
        Populate-Sources $cross
        $crossArgs = @('--source-root', $cross.Source, '--manifest', $cross.Manifest,
                       '--out-root', $cross.Out, '--generated-root', $cross.Generated)
        Invoke-Cooker $SecondCooker $crossArgs 0 | Out-Null
        Assert-SameBytes $snapshot1 (Snapshot-Files $cross) 'cross-configuration cooks'
    }

    $invalid = New-Roots $fixture 'invalid'
    Populate-Sources $invalid
    Write-Utf8 $invalid.Manifest "uv_test.tga`ntone.wav`n"
    $invalidArgs = @('--source-root', $invalid.Source, '--manifest', $invalid.Manifest,
                     '--out-root', $invalid.Out, '--generated-root', $invalid.Generated)
    Invoke-Cooker $Cooker $invalidArgs 2 | Out-Null
    Assert-NoPublishedOutputs $invalid 'unsorted manifest'

    Write-Utf8 $invalid.Manifest "tone.wav`ntone.wav`n"
    Invoke-Cooker $Cooker $invalidArgs 2 | Out-Null
    Assert-NoPublishedOutputs $invalid 'duplicate manifest path'

    Write-Utf8 $invalid.Manifest "../escape.tga`n"
    Invoke-Cooker $Cooker $invalidArgs 2 | Out-Null
    Assert-NoPublishedOutputs $invalid 'traversal manifest path'

    Write-Utf8 $invalid.Manifest "UV_TEST.TGA`n"
    Invoke-Cooker $Cooker $invalidArgs 2 | Out-Null
    Assert-NoPublishedOutputs $invalid 'noncanonical manifest path'

    Write-Utf8 $invalid.Manifest "a-b.tga`na_b.tga`n"
    Invoke-Cooker $Cooker $invalidArgs 2 | Out-Null
    Assert-NoPublishedOutputs $invalid 'symbol collision'

    Write-Utf8 $invalid.Manifest "unsupported.png`n"
    Invoke-Cooker $Cooker $invalidArgs 2 | Out-Null
    Assert-NoPublishedOutputs $invalid 'unsupported type'

    Write-Utf8 $invalid.Manifest (("a" * 600) + ".tga`n")
    Invoke-Cooker $Cooker $invalidArgs 2 | Out-Null
    Assert-NoPublishedOutputs $invalid 'overlong manifest line'

    [IO.File]::WriteAllBytes($invalid.Manifest, [byte[]]::new((4 * 1024 * 1024) + 1))
    Invoke-Cooker $Cooker $invalidArgs 2 | Out-Null
    Assert-NoPublishedOutputs $invalid 'oversized manifest'

    $badTga = New-Roots $fixture 'bad-tga'
    [IO.File]::WriteAllBytes((Join-Path $badTga.Source 'broken.tga'),
                             [byte[]]@(0x00,0x01,0x02,0x03))
    Write-Utf8 $badTga.Manifest "broken.tga`n"
    $badTgaArgs = @('--source-root', $badTga.Source, '--manifest', $badTga.Manifest,
                    '--out-root', $badTga.Out, '--generated-root', $badTga.Generated)
    Invoke-Cooker $Cooker $badTgaArgs 2 | Out-Null
    Assert-NoPublishedOutputs $badTga 'malformed TGA'

    $badWav = New-Roots $fixture 'bad-wav'
    [IO.File]::WriteAllBytes((Join-Path $badWav.Source 'broken.wav'),
                             [Text.Encoding]::ASCII.GetBytes('not a RIFF file'))
    Write-Utf8 $badWav.Manifest "broken.wav`n"
    $badWavArgs = @('--source-root', $badWav.Source, '--manifest', $badWav.Manifest,
                    '--out-root', $badWav.Out, '--generated-root', $badWav.Generated)
    Invoke-Cooker $Cooker $badWavArgs 2 | Out-Null
    Assert-NoPublishedOutputs $badWav 'malformed WAV'

    $sourceEscape = New-Roots $fixture 'source-reparse'
    $sourceEscapeTarget = Join-Path $fixture 'source-reparse-outside'
    [IO.Directory]::CreateDirectory($sourceEscapeTarget) | Out-Null
    [IO.File]::Copy($SourceTga, (Join-Path $sourceEscapeTarget 'escape.tga'), $true)
    New-TestJunction (Join-Path $sourceEscape.Source 'link') $sourceEscapeTarget
    [IO.Directory]::CreateDirectory((Join-Path $sourceEscape.Out 'link')) | Out-Null
    Write-Utf8 $sourceEscape.Manifest "link/escape.tga`n"
    $sourceEscapeArgs = @('--source-root', $sourceEscape.Source,
                          '--manifest', $sourceEscape.Manifest,
                          '--out-root', $sourceEscape.Out,
                          '--generated-root', $sourceEscape.Generated)
    Invoke-Cooker $Cooker $sourceEscapeArgs 1 | Out-Null
    Assert-NoPublishedOutputs $sourceEscape 'source reparse escape'

    $outputEscape = New-Roots $fixture 'output-reparse'
    $outputSourceParent = Join-Path $outputEscape.Source 'link'
    $outputEscapeTarget = Join-Path $fixture 'output-reparse-outside'
    [IO.Directory]::CreateDirectory($outputSourceParent) | Out-Null
    [IO.Directory]::CreateDirectory($outputEscapeTarget) | Out-Null
    [IO.File]::Copy($SourceTga, (Join-Path $outputSourceParent 'escape.tga'), $true)
    New-TestJunction (Join-Path $outputEscape.Out 'link') $outputEscapeTarget
    Write-Utf8 $outputEscape.Manifest "link/escape.tga`n"
    $outputEscapeArgs = @('--source-root', $outputEscape.Source,
                          '--manifest', $outputEscape.Manifest,
                          '--out-root', $outputEscape.Out,
                          '--generated-root', $outputEscape.Generated)
    Invoke-Cooker $Cooker $outputEscapeArgs 1 | Out-Null
    if (Test-Path -LiteralPath (Join-Path $outputEscapeTarget 'escape.tga.mba')) {
        Fail 'output reparse escape published outside the baked root'
    }
    if (Test-Path -LiteralPath (Join-Path $outputEscape.Generated 'assets\asset_ids.gen.h')) {
        Fail 'output reparse escape published the catalog commit marker'
    }

    $partial = New-Roots $fixture 'partial'
    Populate-Sources $partial
    $blockingTemp = Join-Path $partial.Out 'uv_test.tga.mba.tmp'
    [byte[]]$sentinel = @(0x51,0x52,0x53,0x54)
    [IO.File]::WriteAllBytes($blockingTemp, $sentinel)
    $partialArgs = @('--source-root', $partial.Source, '--manifest', $partial.Manifest,
                     '--out-root', $partial.Out, '--generated-root', $partial.Generated)
    Invoke-Cooker $Cooker $partialArgs 1 | Out-Null
    if (-not (Test-Path -LiteralPath (Join-Path $partial.Out 'tone.wav.mba') -PathType Leaf)) {
        Fail 'partial-publication fixture did not reach the second output'
    }
    if (Test-Path -LiteralPath (Join-Path $partial.Out 'uv_test.tga.mba') -PathType Leaf) {
        Fail 'blocked output was published'
    }
    if (Test-Path -LiteralPath (Join-Path $partial.Generated 'assets\asset_ids.gen.h') -PathType Leaf) {
        Fail 'catalog commit marker published after partial failure'
    }
    if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($blockingTemp)) -ne
        [Convert]::ToBase64String($sentinel)) {
        Fail 'temporary collision sentinel changed'
    }

    $stale = New-Roots $fixture 'stale-marker'
    Populate-Sources $stale
    $staleArgs = @('--source-root', $stale.Source, '--manifest', $stale.Manifest,
                   '--out-root', $stale.Out, '--generated-root', $stale.Generated)
    Invoke-Cooker $Cooker $staleArgs 0 | Out-Null
    $staleHeader = Join-Path $stale.Generated 'assets\asset_ids.gen.h'
    $staleTone = Join-Path $stale.Out 'tone.wav.mba'
    $oldToneHash = Get-Sha256Hex $staleTone

    $toneSource = Join-Path $stale.Source 'tone.wav'
    [byte[]]$toneBytes = [IO.File]::ReadAllBytes($toneSource)
    $toneBytes[$toneBytes.Length - 1] = $toneBytes[$toneBytes.Length - 1] -bxor 0xff
    [IO.File]::WriteAllBytes($toneSource, $toneBytes)
    $tgaSource = Join-Path $stale.Source 'uv_test.tga'
    [byte[]]$tgaBytes = [IO.File]::ReadAllBytes($tgaSource)
    $tgaBytes[$tgaBytes.Length - 1] = $tgaBytes[$tgaBytes.Length - 1] -bxor 0xff
    [IO.File]::WriteAllBytes($tgaSource, $tgaBytes)

    $staleBlockingTemp = Join-Path $stale.Out 'uv_test.tga.mba.tmp'
    [IO.File]::WriteAllBytes($staleBlockingTemp, $sentinel)
    Invoke-Cooker $Cooker $staleArgs 1 | Out-Null
    if ((Get-Sha256Hex $staleTone) -eq $oldToneHash) {
        Fail 'stale-marker fixture did not publish the first changed asset'
    }
    if (Test-Path -LiteralPath $staleHeader -PathType Leaf) {
        Fail 'failed recook retained the previous catalog commit marker'
    }
    if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($staleBlockingTemp)) -ne
        [Convert]::ToBase64String($sentinel)) {
        Fail 'stale-marker temporary collision sentinel changed'
    }

    $marker = New-Roots $fixture 'marker-collision'
    Populate-Sources $marker
    $blockingMarker = Join-Path $marker.Generated 'assets\asset_ids.gen.h.tmp'
    [IO.File]::WriteAllBytes($blockingMarker, $sentinel)
    $markerArgs = @('--source-root', $marker.Source, '--manifest', $marker.Manifest,
                    '--out-root', $marker.Out, '--generated-root', $marker.Generated)
    Invoke-Cooker $Cooker $markerArgs 1 | Out-Null
    if ((Get-ChildItem -LiteralPath $marker.Out -File).Count -ne 2) {
        Fail 'catalog collision fixture did not publish every baked asset first'
    }
    if (Test-Path -LiteralPath (Join-Path $marker.Generated 'assets\asset_ids.gen.h')) {
        Fail 'catalog was published despite its temporary collision'
    }
    if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($blockingMarker)) -ne
        [Convert]::ToBase64String($sentinel)) {
        Fail 'catalog temporary collision sentinel changed'
    }

    $missing = Join-Path $fixture 'missing-source'
    $missingArgs = @('--source-root', $missing, '--manifest', $first.Manifest,
                     '--out-root', $invalid.Out, '--generated-root', $invalid.Generated)
    Invoke-Cooker $Cooker $missingArgs 1 | Out-Null
    $missingManifestArgs = @('--source-root', $first.Source,
                             '--manifest', (Join-Path $fixture 'missing-manifest.txt'),
                             '--out-root', $invalid.Out,
                             '--generated-root', $invalid.Generated)
    Invoke-Cooker $Cooker $missingManifestArgs 1 | Out-Null
    $missingOutArgs = @('--source-root', $first.Source, '--manifest', $first.Manifest,
                        '--out-root', (Join-Path $fixture 'missing-out'),
                        '--generated-root', $invalid.Generated)
    Invoke-Cooker $Cooker $missingOutArgs 1 | Out-Null
    $missingGeneratedArgs = @('--source-root', $first.Source,
                              '--manifest', $first.Manifest,
                              '--out-root', $invalid.Out,
                              '--generated-root', (Join-Path $fixture 'missing-generated'))
    Invoke-Cooker $Cooker $missingGeneratedArgs 1 | Out-Null
    Invoke-Cooker $Cooker @('--source-root') 1 | Out-Null
    Invoke-Cooker $Cooker @('--unknown', 'value') 1 | Out-Null

    Write-Output 'cooker CLI: deterministic texture/sound publication and failure matrix passed'
}
finally {
    if ($null -ne $lease.Identity) {
        Remove-FreshWalkLease $lease
    }
}
