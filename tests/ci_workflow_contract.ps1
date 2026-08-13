param(
    [Parameter(Mandatory = $true)]
    [string]$Workflow
)

$ErrorActionPreference = 'Stop'
$text = [System.IO.File]::ReadAllText($Workflow)
$checkoutSha = '93cb6efe18208431cddfb8368fd83d5badbf9bfd'

function Require([bool]$condition, [string]$message) {
    if (-not $condition) {
        Write-Error $message
        exit 1
    }
}

Require ($text -match '(?m)^  push:\s*$') 'CI workflow lost the push entrypoint'
Require ($text -match '(?m)^  pull_request:\s*$') 'CI workflow lost the pull_request entrypoint'
Require ($text -match '(?m)^  workflow_dispatch:\s*$') 'CI workflow lacks workflow_dispatch recovery'
Require ($text -match '(?ms)^permissions:\s*\r?\n  contents: read\s*$') 'CI workflow lacks global contents: read permission'

$checkoutRefs = @([regex]::Matches($text, 'actions/checkout@([^\s#]+)'))
Require ($checkoutRefs.Count -eq 3) "expected exactly 3 checkout uses, found $($checkoutRefs.Count)"
foreach ($match in $checkoutRefs) {
    Require ($match.Groups[1].Value -eq $checkoutSha) "checkout is not pinned to v5.0.1 SHA: $($match.Value)"
}

$installLines = @($text -split "`r?`n" | Where-Object { $_ -match '^\s*winget install --id KhronosGroup\.VulkanSDK' })
Require ($installLines.Count -eq 2) "expected exactly 2 Vulkan SDK install commands, found $($installLines.Count)"
foreach ($line in $installLines) {
    Require ($line -match '(?:^|\s)--source\s+winget(?:\s|$)') "winget install does not select the community source: $line"
}

Write-Output 'CI workflow contract PASS: push, pull_request, workflow_dispatch, least privilege, pins, and winget source'
exit 0
