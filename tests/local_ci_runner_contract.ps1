param(
    [Parameter(Mandatory = $true)]
    [string]$Runner,
    [Parameter(Mandatory = $true)]
    [string]$Gate,
    [Parameter(Mandatory = $true)]
    [string]$Readme,
    [Parameter(Mandatory = $true)]
    [string]$PullRequestTemplate
)

$ErrorActionPreference = 'Stop'

function Require([bool]$condition, [string]$message) {
    if (-not $condition) {
        Write-Error $message
        exit 1
    }
}

foreach ($path in @($Runner, $Gate, $Readme, $PullRequestTemplate)) {
    Require (Test-Path -LiteralPath $path) "required local CI contract input is missing: $path"
}

$runnerText = [System.IO.File]::ReadAllText($Runner)
$gateText = [System.IO.File]::ReadAllText($Gate)
$readmeText = [System.IO.File]::ReadAllText($Readme)
$templateText = [System.IO.File]::ReadAllText($PullRequestTemplate)

Require ($runnerText -match "(?s)\[ValidateSet\('Debug',\s*'RelWithDebInfo',\s*'Release'\)\]") 'local CI runner must accept exactly Debug, RelWithDebInfo, and Release'
Require ($runnerText -match '\[string\]\$Configuration\s*=\s*''Debug''') 'local CI runner must default to Debug'
Require ($runnerText -match 'vswhere\.exe') 'local CI runner must locate Visual Studio through vswhere'
Require ($runnerText -match 'vcvars64\.bat') 'local CI runner must source vcvars64'
Require ($runnerText -match 'cmake --preset ci') 'local CI runner must configure the ci preset'
Require ($runnerText -match 'cmake --build build-ci --config') 'local CI runner must build build-ci'
Require ($runnerText -match 'ctest --test-dir build-ci') 'local CI runner must run CTest from build-ci'
Require ($runnerText -match '--no-tests=error') 'local CI runner must fail when CTest discovers no tests'
Require ($runnerText -notmatch '(?i)CMakeCache\.txt') 'local CI runner must not conditionally skip configure based on a CMake cache'

$configureAt = $runnerText.IndexOf('cmake --preset ci', [StringComparison]::Ordinal)
$buildAt = $runnerText.IndexOf('cmake --build build-ci --config', [StringComparison]::Ordinal)
$testAt = $runnerText.IndexOf('ctest --test-dir build-ci', [StringComparison]::Ordinal)
Require ($configureAt -ge 0 -and $configureAt -lt $buildAt -and $buildAt -lt $testAt) 'local CI runner must configure before build and test'

foreach ($field in @(
    'schemaVersion = 1',
    'startedUtc =',
    'finishedUtc =',
    'durationMs =',
    'repository =',
    'head =',
    'branch =',
    'clean =',
    'toolchain =',
    'visualStudio =',
    'cmake =',
    'ninja =',
    'configuration =',
    'stages =',
    'outcome =',
    'failure ='
)) {
    Require ($runnerText.Contains($field)) "local CI report is missing $field"
}

Require ($runnerText -match 'local-ci-\{0\}\.json') 'local CI runner must write a per-configuration JSON report'
Require ($runnerText -notmatch '(?im)^\s*Get-(?:ChildItem|Item|Content)\s+(?:Env:|env:)') 'local CI runner must not enumerate or serialize the environment'
Require ($runnerText -notmatch '(?i)git\s+remote|GITHUB_TOKEN|ACCESS_TOKEN|PASSWORD|commandOutput') 'local CI runner must not collect remote, credential, or command-output data'

Require ($gateText -match 'tools\\local-ci\.ps1') 'ctest-gate must delegate to the local CI runner'
Require ($gateText -match '-Configuration Debug') 'ctest-gate must preserve the Debug pre-push gate'
Require ($gateText -match 'set "RC=%ERRORLEVEL%"') 'ctest-gate must capture the runner exit code'
Require ($gateText -match 'exit /b %RC%') 'ctest-gate must propagate the runner exit code'

Require ($readmeText -match 'tools/local-ci\.ps1\s+-Configuration Debug') 'README must document the canonical local CI runner command'
Require ($templateText -match 'local-ci-<Configuration>\.json') 'PR template must require the local CI report'
Require ($templateText -match '(?i)already-authenticated GitHub path') 'PR template must document the safe GitHub authentication fallback'

Write-Output 'local CI runner contract PASS: bootstrap, report schema, no-secret telemetry, hook delegation, and docs'
exit 0
