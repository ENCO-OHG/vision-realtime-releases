param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Join-Path ([System.IO.Path]::GetTempPath()) ("vision-realtime-config-test-" + [Guid]::NewGuid().ToString('N'))
$templatePath = Join-Path $PSScriptRoot 'gateway.json'
$initializerPath = Join-Path $PSScriptRoot 'initialize-config.ps1'
$configPath = Join-Path $root 'gateway.json'
$gatewayTargetId = '11111111-1111-4111-8111-111111111111'
$controllerId = '22222222-2222-4222-8222-222222222222'

try {
    [System.IO.Directory]::CreateDirectory($root) | Out-Null
    $token = & $initializerPath -TemplatePath $templatePath -ConfigPath $configPath -GatewayTargetId $gatewayTargetId -ControllerId $controllerId -ControllerGeneration '1' -EmitToken

    $config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
    if ($config.PSObject.Properties.Name -contains 'authToken') { throw 'Generated configuration must not contain authToken.' }
    if ($config.credentials.controller.id -ne 'controller-local') { throw 'Generated configuration has an unexpected controller credential ID.' }
    if ($config.credentials.controller.token -ne $token) { throw 'Generated controller token does not match emitted token.' }
    if ($config.credentials.controller.token -match '__GENERATE_SECURE_TOKEN__') { throw 'Generated controller token was not replaced.' }
    if ($config.credentials.controller.gatewayTargetId -ne $gatewayTargetId) { throw 'Generated gateway target ID differs from the supplied value.' }
    if ($config.credentials.controller.controllerId -ne $controllerId) { throw 'Generated controller ID differs from the supplied value.' }
    if ($config.credentials.controller.controllerGeneration -ne 1) { throw 'Generated controller generation differs from the supplied value.' }
    if ($null -eq $config.credentials.operators -or $config.credentials.operators.Count -ne 0) { throw 'Generated configuration must contain an empty operators array.' }

    $invalidConfigPath = Join-Path $root 'invalid-gateway.json'
    $invalidConfigurationRejected = $false
    try {
        & $initializerPath -TemplatePath $templatePath -ConfigPath $invalidConfigPath -GatewayTargetId 'not-a-uuid' -ControllerId $controllerId -ControllerGeneration '1' *> $null
    } catch {
        $invalidConfigurationRejected = $true
    }
    if (-not $invalidConfigurationRejected) { throw 'Configuration initializer accepted an invalid gateway target ID.' }

    Write-Host 'Windows configuration initializer smoke test passed.'
} finally {
    if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
}
