[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$MakensisPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$packagingRoot = Join-Path $repositoryRoot 'packaging'
$stageRoot = Join-Path $packagingRoot 'build\windows-x64'
$distRoot = Join-Path $packagingRoot 'dist'
$gatewaySource = Join-Path $stageRoot 'bin\vision-realtime.exe'
$lib60870Source = Join-Path $stageRoot 'bin\lib60870.dll'
$serviceWrapper = Join-Path $stageRoot 'VisionRealtime.exe'

foreach ($requiredFile in @($gatewaySource, $lib60870Source, $serviceWrapper, $MakensisPath)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required packaging input not found: $requiredFile"
    }
}

$backendOutput = & $gatewaySource version --config (Join-Path $stageRoot 'defaults\gateway.json') --json
if ($LASTEXITCODE -ne 0) {
    throw "Gateway backend inspection failed with exit code $LASTEXITCODE."
}
try {
    $gatewayInfo = $backendOutput | ConvertFrom-Json
} catch {
    throw "Gateway did not return valid version JSON. Refusing to finalize an unverified binary."
}
if ($gatewayInfo.backend -ne 'lib60870') {
    throw "Production Guard: refusing to package backend '$($gatewayInfo.backend)'; expected 'lib60870'."
}

New-Item -ItemType Directory -Force -Path $distRoot | Out-Null
$servicePackagePath = Join-Path $distRoot "vision-realtime-$($gatewayInfo.version)-windows-x64-service.zip"
if (Test-Path -LiteralPath $servicePackagePath) { Remove-Item -LiteralPath $servicePackagePath -Force }
Compress-Archive -Path (Join-Path $stageRoot '*') -DestinationPath $servicePackagePath -CompressionLevel Optimal

$setupPath = Join-Path $distRoot "vision-realtime-$($gatewayInfo.version)-windows-x64-setup.exe"
& $MakensisPath '/WX' "/DPRODUCT_VERSION=$($gatewayInfo.version)" "/DSTAGE_DIR=$stageRoot" "/DOUTPUT_FILE=$setupPath" (Join-Path $packagingRoot 'windows\gateway-setup.nsi')
if ($LASTEXITCODE -ne 0) { throw "makensis failed with exit code $LASTEXITCODE." }
if (-not (Test-Path -LiteralPath $setupPath -PathType Leaf)) { throw "makensis did not create $setupPath" }

Write-Host "Created: $servicePackagePath"
Write-Host "Created: $setupPath"
