param(
    [Parameter(Mandatory = $true)]
    [string]$TemplatePath,
    [Parameter(Mandatory = $true)]
    [string]$ConfigPath,
    [string]$LegacyConfigPath,
    [switch]$EmitToken
)

$ErrorActionPreference = 'Stop'

if (Test-Path -LiteralPath $ConfigPath -PathType Leaf) {
    exit 0
}

$template = Get-Content -LiteralPath $TemplatePath -Raw | ConvertFrom-Json
if ($template.authToken -ne '__GENERATE_SECURE_TOKEN__') {
    throw 'Configuration template does not contain the expected token placeholder.'
}

$generatedToken = $false
if ($LegacyConfigPath -and (Test-Path -LiteralPath $LegacyConfigPath -PathType Leaf)) {
    $config = Get-Content -LiteralPath $LegacyConfigPath -Raw | ConvertFrom-Json
    if (-not $config.authToken -or $config.authToken -eq '__GENERATE_SECURE_TOKEN__') {
        throw 'Legacy configuration does not contain a secure authentication token.'
    }

    $legacyStatePath = [string]$config.stateFile
    $config | Add-Member -NotePropertyName logDir -NotePropertyValue $template.logDir -Force
    $config | Add-Member -NotePropertyName stateFile -NotePropertyValue $template.stateFile -Force
    if ($legacyStatePath -and (Test-Path -LiteralPath $legacyStatePath -PathType Leaf)) {
        [System.IO.Directory]::CreateDirectory((Split-Path -Parent $template.stateFile)) | Out-Null
        Copy-Item -LiteralPath $legacyStatePath -Destination $template.stateFile -Force
    }
} else {
    $config = $template
    $tokenBytes = New-Object byte[] 32
    $generator = [System.Security.Cryptography.RandomNumberGenerator]::Create()
    try {
        $generator.GetBytes($tokenBytes)
    } finally {
        $generator.Dispose()
    }
    $config.authToken = [Convert]::ToBase64String($tokenBytes)
    $generatedToken = $true
}

$json = $config | ConvertTo-Json -Depth 8
$parent = Split-Path -Parent $ConfigPath
[System.IO.Directory]::CreateDirectory($parent) | Out-Null
$temporaryPath = "$ConfigPath.tmp"
try {
    [System.IO.File]::WriteAllText($temporaryPath, $json + [Environment]::NewLine, [System.Text.UTF8Encoding]::new($false))
    [System.IO.File]::Move($temporaryPath, $ConfigPath)
} finally {
    if (Test-Path -LiteralPath $temporaryPath -PathType Leaf) {
        Remove-Item -LiteralPath $temporaryPath -Force
    }
}
if ($EmitToken -and $generatedToken) {
    Write-Output $config.authToken
}
