# Verify the pre-baked toolchain on a self-hosted Windows runner. Never installs.
#
# The fleet's Windows runner guests carry no build toolchain by default; anything
# this repo needs must be baked at provisioning through a runner-manager
# capability-change request (.github/runner-ci-request.json). This script is the
# fail-fast: it names exactly what is missing so the failure reads as "runner not
# provisioned", not as a code failure. Exit 0 = everything present.
#
# Usage: ./tools/verify-runner-toolchain.ps1 [-SkipVulkan]
param(
    [switch]$SkipVulkan
)

# Native tools (vcvars64, vswhere) write benign lines to stderr; under Windows PowerShell 5.1 a
# 'Stop' preference turns those into terminating NativeCommandErrors. Exit codes decide here.
$ErrorActionPreference = 'Continue'
$missing = @()

$programFilesX86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
$vswhere = if ($programFilesX86) { Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe' } else { $null }
$vsPath = $null
if ($vswhere -and (Test-Path -LiteralPath $vswhere)) {
    $vsPath = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null | Select-Object -First 1)
}
if ([string]::IsNullOrWhiteSpace($vsPath)) {
    $missing += 'Visual Studio 2022 Build Tools or Community with the "Desktop development with C++" (MSVC x64) workload'
} else {
    $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path -LiteralPath $vcvars)) { $missing += "vcvars64.bat under $vsPath" }
    else {
        $cmakeOut = (& $env:ComSpec /d /c "call `"$vcvars`" >nul && cmake --version" 2>&1 | ForEach-Object { "$_" }) -join "`n"
        if ($LASTEXITCODE -ne 0 -or $cmakeOut -notmatch 'cmake version') { $missing += 'CMake >= 3.28 on the vcvars64 PATH' }
        $ninjaOut = (& $env:ComSpec /d /c "call `"$vcvars`" >nul && ninja --version" 2>&1 | ForEach-Object { "$_" }) -join "`n"
        if ($LASTEXITCODE -ne 0 -or $ninjaOut -notmatch '(?m)^\d+\.\d+') { $missing += 'Ninja on the vcvars64 PATH' }
    }
}

if (-not $SkipVulkan) {
    $sdk = $env:VULKAN_SDK
    if ([string]::IsNullOrWhiteSpace($sdk)) { $sdk = 'C:\VulkanSDK\1.4.357.0' }
    if (-not (Test-Path -LiteralPath (Join-Path $sdk 'Bin\glslc.exe'))) {
        $missing += 'LunarG Vulkan SDK 1.4.357.0 (VULKAN_SDK set, Bin\glslc.exe present)'
    } else {
        "VULKAN_SDK=$sdk" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding ascii
        "PATH=$sdk\Bin;$env:PATH" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding ascii
    }
}

if ($missing.Count -gt 0) {
    Write-Output 'RUNNER_TOOLCHAIN=MISSING'
    Write-Output 'This self-hosted runner is not provisioned for CHOONZ-MoBA. Missing:'
    $missing | ForEach-Object { Write-Output "  - $_" }
    Write-Output 'Provisioning is a runner-manager capability-change: see .github/runner-ci-request.json and docs/ci-runner-handoff.md.'
    if ($env:GITHUB_STEP_SUMMARY) {
        ("RUNNER_TOOLCHAIN=MISSING - " + ($missing -join '; ')) | Out-File -Append $env:GITHUB_STEP_SUMMARY
    }
    exit 1
}

Write-Output "RUNNER_TOOLCHAIN=OK vs=$vsPath vulkan=$(if ($SkipVulkan) { 'skipped' } else { $sdk })"
exit 0
