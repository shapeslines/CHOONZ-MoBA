# Shared strict classifier for the hosted Vulkan smoke gate.
# Exit 0 with SANDBOX_SMOKE=PASS or =SKIP; exit 1 with =FAIL.

param(
    [Parameter(Mandatory = $true)] [string]$LogPath,
    [Parameter(Mandatory = $true)] [string]$ScreenshotPath,
    [Parameter(Mandatory = $true)] [int]$ProcessExitCode
)

$ErrorActionPreference = 'Stop'
$maxLogBytes = 4 * 1024 * 1024

function Reject([string]$reason) {
    Write-Output "SANDBOX_SMOKE=FAIL: $reason"
    exit 1
}

if (-not (Test-Path -LiteralPath $LogPath -PathType Leaf)) {
    Reject "sandbox log is missing: $LogPath"
}

# The executable is compiled as UTF-8. Decode explicitly so Windows PowerShell 5.1
# does not reinterpret the ADR-0012 em dash through the system ANSI code page.
# Read one byte beyond the cap so a growing or oversized log fails before the
# classifier materializes an unbounded string.
$logStream = [System.IO.File]::OpenRead($LogPath)
try {
    $logBuffer = New-Object byte[] ($maxLogBytes + 1)
    $logRead = 0
    while ($logRead -lt $logBuffer.Length) {
        $chunk = $logStream.Read($logBuffer, $logRead, $logBuffer.Length - $logRead)
        if ($chunk -eq 0) { break }
        $logRead += $chunk
    }
    $logTooLarge = $logRead -gt $maxLogBytes -or $logStream.Length -gt $maxLogBytes
    if (-not $logTooLarge) {
        $output = [System.Text.Encoding]::UTF8.GetString($logBuffer, 0, $logRead)
    }
} finally {
    $logStream.Dispose()
}
if ($logTooLarge) {
    Reject "sandbox log exceeds $maxLogBytes bytes"
}
$lines = @($output -split "`r?`n" | ForEach-Object { $_.Trim() } |
           Where-Object { $_.Length -ne 0 })

# No log phrase may pardon a process failure. This check deliberately precedes all
# device-gate matching so a validation abort cannot be hidden by an earlier message.
if ($ProcessExitCode -ne 0) {
    Reject "sandbox exited $ProcessExitCode"
}

$badDiagnostics = @($lines | Where-Object {
    $_ -match '(?i)\b(?:failed|failure|fatal|error)\b' -or
    $_ -match '(?i)^validation(?:-layer)?\s*:' -or
    $_ -match '^\[vk (?:WARN|ERROR)\]'
})
if ($badDiagnostics.Count -ne 0) {
    Reject "sandbox logged a failure diagnostic: $($badDiagnostics[0])"
}

$deviceGatePatterns = @(
    '^renderer: no Vulkan physical device or compatible driver$',
    '^renderer: no Vulkan physical devices$',
    '^renderer: no device meets the minimum spec \(Vulkan 1\.3 \+ dynamicRendering \+ synchronization2 \+ graphics/present\) \u2014 ADR-0012$'
)
$deviceGates = @($lines | Where-Object {
    $line = $_
    @($deviceGatePatterns | Where-Object { $line -match $_ }).Count -ne 0
})

$reached90 = @($lines | Where-Object { $_ -eq 'reached 90 frames -> quit' })
$clean90 = @($lines | Where-Object {
    $_ -match '^sandbox: clean exit after 90 frames \([0-9]+(?:\.[0-9]+)?s\)$'
})
if ($reached90.Count -ne 1 -or $clean90.Count -ne 1) {
    Reject "sandbox did not complete the exact 90-frame clean-exit contract"
}

$hasScreenshot = Test-Path -LiteralPath $ScreenshotPath -PathType Leaf
if ($hasScreenshot) {
    if ($deviceGates.Count -ne 0) {
        Reject "device-gate log and screenshot cannot both classify as success"
    }
    $item = Get-Item -LiteralPath $ScreenshotPath
    if ($item.Length -le 54) {
        Reject "screenshot is empty or shorter than a BMP header"
    }
    $stream = [System.IO.File]::OpenRead($ScreenshotPath)
    try {
        $first = $stream.ReadByte()
        $second = $stream.ReadByte()
    } finally {
        $stream.Dispose()
    }
    if ($first -ne [byte][char]'B' -or $second -ne [byte][char]'M') {
        Reject "screenshot is not a BMP"
    }
    $rendererUp = @($lines | Where-Object {
        $_ -match '^renderer: Vulkan 1\.3 up \| validation=on \| GPU: '
    })
    $captured = @($lines | Where-Object {
        $_ -match '^screenshot: .+ \([1-9][0-9]*x[1-9][0-9]*\)$'
    })
    if ($rendererUp.Count -ne 1 -or $captured.Count -ne 1) {
        Reject "usable hardware did not log validation-on renderer and screenshot success"
    }
    Write-Output "SANDBOX_SMOKE=PASS"
    exit 0
}

# A missing screenshot is a skip only for one exact renderer terminal gate, followed
# by the sandbox's explicit null-renderer continuation and otherwise clean completion.
if ($deviceGates.Count -ne 1) {
    Reject "missing screenshot without exactly one recognized Vulkan device gate"
}
$unavailable = @($lines | Where-Object {
    $_ -eq 'sandbox: renderer unavailable (null backend / no Vulkan)'
})
$rendererUp = @($lines | Where-Object { $_ -match '^renderer: Vulkan 1\.3 up \|' })
if ($unavailable.Count -ne 1 -or $rendererUp.Count -ne 0) {
    Reject "device-gated run did not follow the exact null-renderer path"
}

Write-Output "SANDBOX_SMOKE=SKIP"
exit 0
