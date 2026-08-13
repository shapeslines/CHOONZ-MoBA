# tools/fresh-walk.ps1 - the FIXES.md "stranger's walk", scripted (G36).
#
# Clone -> configure (dev preset) -> build Debug -> ctest -> sandbox screenshot,
# exactly as a stranger would follow the README. Fails loudly on any break with a
# message naming the step. Used by CI (fresh-walk job) and runnable locally:
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/fresh-walk.ps1
#
# Requires: git, cmake, ninja, MSVC (located via vswhere), and the Vulkan SDK
# (VULKAN_SDK set) to exercise the real renderer. Exit code 0 = the core path works.
#
# NOTE: keep this file ASCII-only. Windows PowerShell 5.1 reads BOM-less UTF-8 as
# ANSI, and a UTF-8 em-dash byte decodes as a double-quote in cp1252, breaking the
# parser (the repo's .bat lesson, same family).

param(
    [string]$CloneDir = (Join-Path $env:TEMP "moba-fresh-walk"),
    [switch]$Keep
)

$ErrorActionPreference = 'Stop'

function Fail([string]$msg) {
    Write-Output "FRESH-WALK FAIL: $msg"
    exit 1
}

# 0. Locate the VS install + vcvars, like .github/workflows/ci.yml does (Node-free).
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { Fail "vswhere not found at $vswhere" }
$vspath = & $vswhere -latest -property installationPath
if (-not $vspath) { Fail "no Visual Studio install found" }
$vcvars = Join-Path $vspath "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) { Fail "vcvars64.bat not found at $vcvars" }

function In-VsEnv([string]$dir, [string]$cmd) {
    # vcvars env does not persist across process boundaries; run each command inside
    # a single cmd shell that sources vcvars first and cd's to the target dir.
    $inner = $cmd -replace '"', '\"'
    cmd /c "call `"$vcvars`" >nul 2>&1 && cd /d `"$dir`" && $inner"
    if ($LASTEXITCODE -ne 0) { Fail "step failed (exit $LASTEXITCODE): $cmd" }
}

# 1. Clone fresh (no local state, byte-clean like the FIXES.md sweep).
if (Test-Path $CloneDir) { Remove-Item -Recurse -Force $CloneDir }
$repoRoot = Split-Path $PSScriptRoot -Parent
git clone --quiet "file://$repoRoot" $CloneDir
if ($LASTEXITCODE -ne 0) { Fail "git clone of $repoRoot" }

# 2. Configure with the dev preset (the README path).
In-VsEnv $CloneDir "cmake --preset dev"

# 3. Build Debug (the README path).
In-VsEnv $CloneDir "cmake --build build --config Debug"

# 4. Test.
In-VsEnv $CloneDir "ctest --test-dir build -C Debug --output-on-failure --no-tests=error"

# 5. The runnable DoD: sandbox renders 90 frames and writes a screenshot. The
#    documented command needs --frames (without it the window runs forever).
#    Invoked through cmd so native stderr (the renderer log) cannot interact with
#    $ErrorActionPreference under Windows PowerShell 5.1.
$sandbox = Join-Path $CloneDir "build\tools\sandbox\Debug\sandbox.exe"
if (-not (Test-Path $sandbox)) { Fail "sandbox.exe not at the README path" }
$log = Join-Path $CloneDir "sandbox.log"
cmd /c "cd /d `"$CloneDir`" && `"$sandbox`" --frames 90 --screenshot out.bmp > `"$log`" 2>&1"
$code = $LASTEXITCODE
$output = Get-Content $log -Raw
$screenshot = Join-Path $CloneDir "out.bmp"
$noDevice = $output -match 'renderer: no Vulkan physical device(?:s| or compatible driver)' -or
            $output -match 'renderer: no device meets the minimum spec'
if ($noDevice -and -not (Test-Path $screenshot)) {
    Write-Output "SKIP: no Vulkan driver/1.3 device - render path compiled but not executed"
} elseif ($code -ne 0) {
    Fail "sandbox exited $code. Log: $output"
} elseif (-not (Test-Path $screenshot)) {
    Fail "sandbox exited 0 but produced no out.bmp"
} else {
    $len = (Get-Item $screenshot).Length
    Write-Output "sandbox: 90 validation-clean frames, screenshot $len bytes"
}

# 6. Hygiene: leave the temp clone only if asked.
if (-not $Keep) { Remove-Item -Recurse -Force $CloneDir }

Write-Output "FRESH-WALK OK - the stranger's path works end to end."
exit 0
