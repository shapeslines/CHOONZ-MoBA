param(
    [Parameter(Mandatory = $true)][string]$Exe
)

$ErrorActionPreference = 'Stop'

function Invoke-ExpectRejected {
    param([string[]]$Arguments)

    $savedPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = & $Exe @Arguments 2>&1
    $actual = $LASTEXITCODE
    $ErrorActionPreference = $savedPreference
    $text = $output -join [Environment]::NewLine

    if ($actual -ne 2) {
        throw "Expected exit 2, got ${actual}: $($Arguments -join ' ')`n$text"
    }
    if ($text -notmatch '--frames expects a positive integer') {
        throw "Missing --frames diagnostic for: $($Arguments -join ' ')`n$text"
    }
    if ($text -match 'window open|failed to open window|renderer') {
        throw "Invalid --frames input reached window or renderer setup: $($Arguments -join ' ')`n$text"
    }
}

Invoke-ExpectRejected @('--frames')
Invoke-ExpectRejected @('--frames', '')
Invoke-ExpectRejected @('--frames', 'not-a-number')
Invoke-ExpectRejected @('--frames', '0')
Invoke-ExpectRejected @('--frames', '-1')
Invoke-ExpectRejected @('--frames', '+1')
Invoke-ExpectRejected @('--frames', '1x')
Invoke-ExpectRejected @('--frames', '999999999999999999999999')
Invoke-ExpectRejected @('--frames', '--orbit')

Write-Host 'sandbox_cli_errors: malformed --frames values rejected before window setup'
