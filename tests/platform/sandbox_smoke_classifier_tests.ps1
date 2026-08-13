param(
    [Parameter(Mandatory = $true)] [string]$Classifier,
    [Parameter(Mandatory = $true)] [string]$WorkDir
)

$ErrorActionPreference = 'Stop'
$caseRoot = Join-Path $WorkDir 'sandbox-smoke-classifier-cases'
if (Test-Path -LiteralPath $caseRoot) {
    Remove-Item -LiteralPath $caseRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $caseRoot | Out-Null
$script:failureCount = 0
$utf8 = New-Object System.Text.UTF8Encoding($false)

function Write-BmpStub([string]$path) {
    $bytes = New-Object byte[] 64
    $bytes[0] = [byte][char]'B'
    $bytes[1] = [byte][char]'M'
    [System.IO.File]::WriteAllBytes($path, $bytes)
}

function Run-Case([string]$name, [string]$logText, [int]$processCode,
                  [bool]$withScreenshot, [string]$expectedMarker, [int]$expectedExit) {
    $dir = Join-Path $caseRoot $name
    New-Item -ItemType Directory -Path $dir | Out-Null
    $log = Join-Path $dir 'sandbox.log'
    $shot = Join-Path $dir 'out.bmp'
    [System.IO.File]::WriteAllText($log, $logText, $utf8)
    if ($withScreenshot) { Write-BmpStub $shot }

    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $Classifier,
                   '-LogPath', $log, '-ScreenshotPath', $shot,
                   '-ProcessExitCode', $processCode)
    $result = @(& powershell.exe @arguments 2>&1 | ForEach-Object { "$_" })
    $actualExit = $LASTEXITCODE
    $joined = $result -join "`n"
    $markerMatches = if ($expectedMarker -eq 'SANDBOX_SMOKE=FAIL') {
        $joined -match '(?m)^SANDBOX_SMOKE=FAIL:'
    } else {
        $result -contains $expectedMarker
    }
    if ($actualExit -ne $expectedExit -or -not $markerMatches) {
        Write-Output "FAIL $name exit=$actualExit expected=$expectedExit output=$joined"
        $script:failureCount += 1
    } else {
        Write-Output "ok $name -> $expectedMarker"
    }
}

$success = @'
renderer: Vulkan 1.3 up | validation=on | GPU: Test GPU (discrete) | swapchain 1280x720 x3 | 3 pipeline(s)
sandbox: window open (1280x720). auto-quit mode
reached 90 frames -> quit
screenshot: out.bmp (1280x720)
sandbox: clean exit after 90 frames (0.40s)
'@
$physical = @'
renderer: no Vulkan physical devices
sandbox: renderer unavailable (null backend / no Vulkan)
sandbox: window open (1280x720). auto-quit mode
reached 90 frames -> quit
sandbox: clean exit after 90 frames (0.60s)
'@
$driver = @'
renderer: no Vulkan physical device or compatible driver
sandbox: renderer unavailable (null backend / no Vulkan)
sandbox: window open (1280x720). auto-quit mode
reached 90 frames -> quit
sandbox: clean exit after 90 frames (0.60s)
'@
$minimum = "renderer: no device meets the minimum spec (Vulkan 1.3 + dynamicRendering + synchronization2 + graphics/present) $([char]0x2014) ADR-0012`n" +
           "sandbox: renderer unavailable (null backend / no Vulkan)`n" +
           "sandbox: window open (1280x720). auto-quit mode`n" +
           "reached 90 frames -> quit`n" +
           "sandbox: clean exit after 90 frames (0.60s)"

Run-Case 'hardware-pass' $success 0 $true 'SANDBOX_SMOKE=PASS' 0
Run-Case 'no-physical-skip' $physical 0 $false 'SANDBOX_SMOKE=SKIP' 0
Run-Case 'no-driver-skip' $driver 0 $false 'SANDBOX_SMOKE=SKIP' 0
Run-Case 'minimum-spec-skip' $minimum 0 $false 'SANDBOX_SMOKE=SKIP' 0
Run-Case 'device-phrase-nonzero' $physical 3 $false 'SANDBOX_SMOKE=FAIL' 1
Run-Case 'device-phrase-plus-validation-error' ($physical + "`nvalidation: ERROR injected") 0 $false 'SANDBOX_SMOKE=FAIL' 1
Run-Case 'device-phrase-plus-vk-warning' ($physical + "`n[vk WARN] injected") 0 $false 'SANDBOX_SMOKE=FAIL' 1
Run-Case 'device-phrase-missing-clean-exit' "renderer: no Vulkan physical devices`nsandbox: renderer unavailable (null backend / no Vulkan)" 0 $false 'SANDBOX_SMOKE=FAIL' 1
Run-Case 'unknown-missing-screenshot' "sandbox: renderer unavailable (null backend / no Vulkan)`nreached 90 frames -> quit`nsandbox: clean exit after 90 frames (0.60s)" 0 $false 'SANDBOX_SMOKE=FAIL' 1
Run-Case 'hardware-missing-screenshot' $success 0 $false 'SANDBOX_SMOKE=FAIL' 1
Run-Case 'failure-with-stale-screenshot' ($success + "`nsandbox: capture FAILED") 0 $true 'SANDBOX_SMOKE=FAIL' 1
Run-Case 'hardware-plus-vk-warning' ($success + "`n[vk WARN] injected") 0 $true 'SANDBOX_SMOKE=FAIL' 1
Run-Case 'hardware-plus-vk-error' ($success + "`n[vk ERROR] injected") 0 $true 'SANDBOX_SMOKE=FAIL' 1
Run-Case 'substring-is-not-device-gate' ($physical -replace 'renderer: no Vulkan physical devices', 'prefix renderer: no Vulkan physical devices suffix') 0 $false 'SANDBOX_SMOKE=FAIL' 1
$oversizedLog = ('x' * (4 * 1024 * 1024)) + "`n" + $physical
Run-Case 'oversized-log' $oversizedLog 0 $false 'SANDBOX_SMOKE=FAIL' 1

Remove-Item -LiteralPath $caseRoot -Recurse -Force
if ($script:failureCount -ne 0) {
    Write-Output "$script:failureCount sandbox smoke classifier case(s) failed"
    exit 1
}
Write-Output "sandbox smoke classifier: 14 adversarial cases passed; oversized log bound passed"
exit 0
