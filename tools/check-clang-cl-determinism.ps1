# Build and run the canonical sim oracle with clang-cl, enabling UBSan only after
# the installed compiler/runtime proves that a real signed-overflow tripwire halts.
# Keep this file ASCII-only for Windows PowerShell 5.1.

param(
    [string]$SourceDir,
    [string]$BuildDir,
    [switch]$RequireCompiler,
    [switch]$RequireUbsan
)

$ErrorActionPreference = 'Stop'

function Fail([string]$message) {
    Write-Output "CLANG_CL_DETERMINISM=FAIL: $message"
    exit 1
}

if (-not $SourceDir) { $SourceDir = Split-Path $PSScriptRoot -Parent }
$SourceDir = [System.IO.Path]::GetFullPath($SourceDir)
if (-not $BuildDir) { $BuildDir = Join-Path $SourceDir 'out\clang-cl-ubsan' }
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    if ($RequireCompiler -or $RequireUbsan) { Fail "vswhere not found at $vswhere" }
    Write-Output 'CLANG_CL_DETERMINISM=UNAVAILABLE: reason=vswhere-not-found'
    exit 0
}
$vsPath = & $vswhere -latest -property installationPath
if (-not $vsPath) {
    if ($RequireCompiler -or $RequireUbsan) { Fail 'no Visual Studio installation found' }
    Write-Output 'CLANG_CL_DETERMINISM=UNAVAILABLE: reason=visual-studio-not-found'
    exit 0
}
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars -PathType Leaf)) { Fail "vcvars64.bat not found at $vcvars" }

$clangCandidates = @(
    (Join-Path $vsPath 'VC\Tools\Llvm\x64\bin\clang-cl.exe'),
    (Join-Path $env:ProgramFiles 'LLVM\bin\clang-cl.exe')
)
$pathClang = Get-Command clang-cl.exe -ErrorAction SilentlyContinue
if ($pathClang) { $clangCandidates += $pathClang.Source }
$clang = @($clangCandidates | Select-Object -Unique | Where-Object {
    Test-Path -LiteralPath $_ -PathType Leaf
} | Select-Object -First 1)
if ($clang.Count -eq 0) {
    if ($RequireCompiler -or $RequireUbsan) {
        Fail 'clang-cl not found in Visual Studio, Program Files, or PATH'
    }
    Write-Output 'CLANG_CL_DETERMINISM=UNAVAILABLE: reason=clang-cl-not-installed'
    exit 0
}
$clang = $clang[0]

$version = @(& $clang --version | ForEach-Object { "$_" })[0]
Write-Output "clang-cl capability: $version"

$script:LastVsExitCode = 0
function Invoke-VsCommand([string]$command, [switch]$AllowFailure) {
    $commandLine = "call `"$vcvars`" >nul && $command"
    & $env:ComSpec /d /s /c $commandLine
    $script:LastVsExitCode = $LASTEXITCODE
    if ($script:LastVsExitCode -ne 0 -and -not $AllowFailure) {
        Fail "command exited $($script:LastVsExitCode): $command"
    }
}

if (-not (Test-Path -LiteralPath $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}
$tripwireSource = Join-Path $SourceDir 'tests\sim\sim_ubsan_tripwire.cpp'
$capabilityExe = Join-Path $BuildDir 'ubsan-capability.exe'
$capabilityObj = Join-Path $BuildDir 'ubsan-capability.obj'
$compileTripwire =
    "`"$clang`" /nologo /std:c++17 /W4 /WX -fsanitize=undefined " +
    "-fno-sanitize-recover=undefined /Fo`"$capabilityObj`" " +
    "/Fe`"$capabilityExe`" `"$tripwireSource`""
Invoke-VsCommand $compileTripwire -AllowFailure
$compileCode = $script:LastVsExitCode
$ubsanSupported = $false
if ($compileCode -eq 0) {
    $capabilityLog = Join-Path $BuildDir 'ubsan-capability.log'
    & $env:ComSpec /d /s /c "`"$capabilityExe`" > `"$capabilityLog`" 2>&1"
    $tripwireCode = $LASTEXITCODE
    $tripwireOutput = [System.IO.File]::ReadAllText(
        $capabilityLog, [System.Text.Encoding]::UTF8)
    Write-Output ($tripwireOutput.Trim())
    if ($tripwireCode -ne 0 -and
            $tripwireOutput.Contains('runtime error: signed integer overflow')) {
        $ubsanSupported = $true
        Write-Output 'clang-cl UBSan capability: supported (compile, link, runtime tripwire)'
    } else {
        Write-Output "clang-cl UBSan capability: unsupported (tripwire exit=$tripwireCode)"
    }
} else {
    Write-Output "clang-cl UBSan capability: unsupported (compile/link exit=$compileCode)"
}

$ubsanValue = if ($ubsanSupported) { 'ON' } else { 'OFF' }
$clangCmake = $clang.Replace('\', '/')
$configure =
    "cmake --fresh -S `"$SourceDir`" -B `"$BuildDir`" -G `"Ninja Multi-Config`" " +
    "-DCMAKE_CXX_COMPILER=`"$clangCmake`" " +
    "-DCMAKE_CONFIGURATION_TYPES=`"Debug;RelWithDebInfo;Release`" " +
    "-DMOBA_WERROR=ON -DMOBA_VULKAN_REQUIRED=OFF -DMOBA_UBSAN=$ubsanValue"
Invoke-VsCommand $configure

$targets = 'sim_oracle_probe'
if ($ubsanSupported) { $targets += ' sim_ubsan_tripwire' }
Invoke-VsCommand "cmake --build `"$BuildDir`" --config RelWithDebInfo --target $targets"

$testRegex =
    '^(sim_boundary|sim_compiler_policy|sim_compiler_policy_selftest|sim_isolation_selftest)$'
if ($ubsanSupported) {
    $testRegex =
        '^(sim_ubsan_oracle|sim_ubsan_tripwire|sim_boundary|sim_compiler_policy|sim_compiler_policy_selftest|sim_isolation_selftest)$'
}
$testCommand =
    "ctest --test-dir `"$BuildDir`" -C RelWithDebInfo -R `"$testRegex`" " +
    '--output-on-failure --no-tests=error'
Invoke-VsCommand $testCommand

$probe = Join-Path $BuildDir 'tests\RelWithDebInfo\sim_oracle_probe.exe'
$probeLog = Join-Path $BuildDir 'sim-oracle.log'
& $env:ComSpec /d /s /c "`"$probe`" --sim-self-check > `"$probeLog`" 2>&1"
if ($LASTEXITCODE -ne 0) { Fail "clang-cl oracle exited $LASTEXITCODE" }
$oracle = [System.IO.File]::ReadAllText(
    $probeLog, [System.Text.Encoding]::UTF8).Trim()
$expected =
    'sim_oracle ticks=10000 commands=923 final=0x36e6de56cb662dba stream=0xb6067f3f0955b292 logic=0x5b47e648953a63fc'
if ($oracle -ne $expected) { Fail "unexpected oracle output: $oracle" }
Write-Output $oracle

if ($RequireUbsan -and -not $ubsanSupported) {
    Fail 'UBSan support was required but the compile/link/runtime capability probe failed'
}
Write-Output "CLANG_CL_DETERMINISM=PASS: ubsan=$($ubsanValue.ToLower())"
exit 0
