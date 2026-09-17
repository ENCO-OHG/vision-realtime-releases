param(
    [Parameter(Mandatory = $true)]
    [string]$TemplatePath,
    [Parameter(Mandatory = $true)]
    [string]$ConfigPath,
    [string]$GatewayTargetId,
    [string]$ControllerId,
    [string]$ControllerGeneration,
    [switch]$EmitToken
)

$ErrorActionPreference = 'Stop'

if (Test-Path -LiteralPath $ConfigPath -PathType Leaf) {
    exit 0
}

$template = Get-Content -LiteralPath $TemplatePath -Raw | ConvertFrom-Json
if ($template.credentials.controller.token -ne '__GENERATE_SECURE_TOKEN__') {
    throw 'Configuration template does not contain the expected token placeholder.'
}
if ($template.credentials.controller.gatewayTargetId -ne '__GATEWAY_TARGET_ID__' -or $template.credentials.controller.controllerId -ne '__CONTROLLER_ID__') {
    throw 'Configuration template does not contain the expected controller identity placeholders.'
}

$gatewayTargetGuid = [Guid]::Empty
if (-not [Guid]::TryParse($GatewayTargetId, [ref]$gatewayTargetGuid)) {
    throw 'GatewayTargetId must be a UUID.'
}
$controllerGuid = [Guid]::Empty
if (-not [Guid]::TryParse($ControllerId, [ref]$controllerGuid)) {
    throw 'ControllerId must be a UUID.'
}
$controllerGenerationValue = [UInt64]0
if (-not [UInt64]::TryParse($ControllerGeneration, [ref]$controllerGenerationValue) -or $controllerGenerationValue -gt 9007199254740991) {
    throw 'ControllerGeneration must be a non-negative safe integer.'
}

$config = $template
$tokenBytes = New-Object byte[] 32
$generator = [System.Security.Cryptography.RandomNumberGenerator]::Create()
try {
    $generator.GetBytes($tokenBytes)
} finally {
    $generator.Dispose()
}
$config.credentials.controller.token = [Convert]::ToBase64String($tokenBytes)
$config.credentials.controller.gatewayTargetId = $gatewayTargetGuid.ToString()
$config.credentials.controller.controllerId = $controllerGuid.ToString()
$config.credentials.controller.controllerGeneration = $controllerGenerationValue

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
if ($EmitToken) {
    Write-Output $config.credentials.controller.token
}
