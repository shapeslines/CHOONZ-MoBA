# tools/local-ci.ps1 - canonical local /WX configure, build, test, and evidence gate.
#
# This script is intentionally Windows PowerShell 5.1 compatible and ASCII-only.
# It locates Visual Studio through vswhere, enters vcvars64 for every native stage,
# always reconfigures the ci preset, and writes only non-secret local evidence under
# the ignored out/ directory.

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'

$script:repoRoot = Split-Path -Parent $PSScriptRoot
$script:reportDirectory = Join-Path $script:repoRoot 'out'
$script:reportPath = Join-Path $script:reportDirectory ("local-ci-{0}.json" -f $Configuration)
$script:startedUtc = [DateTime]::UtcNow
$script:stages = @()
$script:failure = $null
$script:gitHead = $null
$script:gitBranch = $null
$script:gitClean = $null
$script:visualStudioVersion = $null
$script:cmakeVersion = $null
$script:ninjaVersion = $null
$script:vcvarsPath = $null

function Get-GitValue {
    param([string[]]$GitArguments)

    $commandText = 'git -C "' + $script:repoRoot + '" ' + ($GitArguments -join ' ') + ' 2>nul'
    $output = @(& $env:ComSpec /d /c $commandText)
    if ($LASTEXITCODE -ne 0) {
        return $null
    }
    return (($output | ForEach-Object { [string]$_ }) -join "`n").Trim()
}

function Get-NativeFirstLine {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )
    # vcvars64/vswhere emit benign stderr lines; under PS 5.1 the script-level 'Stop'
    # preference would turn them into terminating NativeCommandErrors. Exit codes decide.
    $ErrorActionPreference = 'Continue'

    $output = @(& $FilePath $Arguments 2>$null)
    if ($LASTEXITCODE -ne 0 -or $output.Count -eq 0) {
        return $null
    }
    return ([string]$output[0]).Trim()
}

function Get-VsToolVersion {
    param([string]$CommandText)
    # vcvars64/vswhere emit benign stderr lines; under PS 5.1 the script-level 'Stop'
    # preference would turn them into terminating NativeCommandErrors. Exit codes decide.
    $ErrorActionPreference = 'Continue'

    $inner = "call `"$script:vcvarsPath`" >nul && cd /d `"$script:repoRoot`" && $CommandText"
    $output = @(& $env:ComSpec /d /c $inner 2>$null)
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0 -or $output.Count -eq 0) {
        return "unavailable (exit $exitCode)"
    }
    return ([string]$output[0]).Trim()
}

function Invoke-VsStage {
    param(
        [string]$Name,
        [string]$CommandText
    )
    # vcvars64/vswhere emit benign stderr lines; under PS 5.1 the script-level 'Stop'
    # preference would turn them into terminating NativeCommandErrors. Exit codes decide.
    $ErrorActionPreference = 'Continue'

    $stageStartedUtc = [DateTime]::UtcNow
    Write-Output "LOCAL_CI_STAGE_START name=$Name"
    $inner = "call `"$script:vcvarsPath`" >nul && cd /d `"$script:repoRoot`" && $CommandText"
    & $env:ComSpec /d /c $inner
    $exitCode = $LASTEXITCODE
    $durationMs = [int64][Math]::Round(([DateTime]::UtcNow - $stageStartedUtc).TotalMilliseconds)
    $stageOutcome = if ($exitCode -eq 0) { 'passed' } else { 'failed' }
    $script:stages += [pscustomobject][ordered]@{
        name = $Name
        command = $CommandText
        exitCode = [int]$exitCode
        durationMs = $durationMs
        outcome = $stageOutcome
    }
    Write-Output "LOCAL_CI_STAGE_RESULT name=$Name outcome=$stageOutcome exitCode=$exitCode durationMs=$durationMs"

    if ($exitCode -ne 0) {
        throw "$Name stage failed with exit code $exitCode."
    }
}

function Write-LocalCiReport {
    param([string]$Outcome)

    $finishedUtc = [DateTime]::UtcNow
    $durationMs = [int64][Math]::Round(($finishedUtc - $script:startedUtc).TotalMilliseconds)
    $report = [ordered]@{
        schemaVersion = 1
        startedUtc = $script:startedUtc.ToString('o')
        finishedUtc = $finishedUtc.ToString('o')
        durationMs = $durationMs
        repository = [ordered]@{
            head = $script:gitHead
            branch = $script:gitBranch
            clean = $script:gitClean
        }
        toolchain = [ordered]@{
            visualStudio = $script:visualStudioVersion
            cmake = $script:cmakeVersion
            ninja = $script:ninjaVersion
        }
        configuration = $Configuration
        stages = @($script:stages)
        outcome = $Outcome
        failure = $script:failure
    }

    New-Item -ItemType Directory -Path $script:reportDirectory -Force | Out-Null
    $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $script:reportPath -Encoding UTF8
    return $durationMs
}

$exitCode = 1
try {
    $script:gitHead = Get-GitValue @('rev-parse', 'HEAD')
    $script:gitBranch = Get-GitValue @('branch', '--show-current')
    $gitStatus = Get-GitValue @('status', '--porcelain')
    if ($null -ne $gitStatus) {
        $script:gitClean = [bool][string]::IsNullOrWhiteSpace($gitStatus)
    }

    $programFilesX86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
    if ([string]::IsNullOrWhiteSpace($programFilesX86)) {
        throw 'ProgramFiles(x86) is unavailable; cannot locate vswhere.'
    }
    $vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'vswhere was not found; install the Visual Studio C++ workload.'
    }

    $vsPath = Get-NativeFirstLine -FilePath $vswhere -Arguments @('-latest', '-property', 'installationPath')
    $script:visualStudioVersion = Get-NativeFirstLine -FilePath $vswhere -Arguments @('-latest', '-property', 'installationVersion')
    if ([string]::IsNullOrWhiteSpace($vsPath)) {
        throw 'No Visual Studio installation was returned by vswhere.'
    }
    $script:vcvarsPath = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path -LiteralPath $script:vcvarsPath)) {
        throw 'Visual Studio is missing VC\Auxiliary\Build\vcvars64.bat.'
    }

    $script:cmakeVersion = Get-VsToolVersion 'cmake --version'
    $script:ninjaVersion = Get-VsToolVersion 'ninja --version'

    Invoke-VsStage -Name 'configure' -CommandText 'cmake --preset ci'
    Invoke-VsStage -Name 'build' -CommandText ("cmake --build build-ci --config {0}" -f $Configuration)
    Invoke-VsStage -Name 'test' -CommandText ("ctest --test-dir build-ci -C {0} --output-on-failure --no-tests=error" -f $Configuration)
    $exitCode = 0
} catch {
    $script:failure = $_.Exception.Message
    Write-Output "LOCAL_CI_FAILURE $script:failure"
} finally {
    $outcome = if ($exitCode -eq 0) { 'passed' } else { 'failed' }
    try {
        $durationMs = Write-LocalCiReport -Outcome $outcome
        Write-Output "LOCAL_CI outcome=$outcome configuration=$Configuration head=$script:gitHead branch=$script:gitBranch clean=$script:gitClean durationMs=$durationMs report=$script:reportPath"
    } catch {
        $exitCode = 1
        Write-Output "LOCAL_CI_REPORT_FAILURE $($_.Exception.Message)"
    }
}

exit $exitCode
