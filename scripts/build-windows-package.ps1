[CmdletBinding()]
param(
    [string]$MakensisPath,
    [string]$NativeBuildDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$nativeRoot = Join-Path $repositoryRoot 'native'
$packagingRoot = Join-Path $repositoryRoot 'packaging'
$stageRoot = Join-Path $packagingRoot 'build\windows-x64'
$distRoot = Join-Path $packagingRoot 'dist'
$winSwSource = Join-Path $repositoryRoot 'third_party\winsw\winsw-x64.exe'
$iconSource = Join-Path $repositoryRoot 'assets\icon.ico'
$lib60870SourceRoot = Join-Path $repositoryRoot 'third_party\lib60870\lib60870-C'
$gplLicensePath = Join-Path $repositoryRoot 'licenses\GPL-3.0.txt'
$expectedWinSwHash = '05B82D46AD331CC16BDC00DE5C6332C1EF818DF8CEEFCD49C726553209B3A0DA'
$expectedGplHash = '3972DC9744F6499F0F9B2DBF76696F2AE7AD8AF9B23DDE66D6AF86C9DFB36986'

if (-not (Test-Path -LiteralPath (Join-Path $lib60870SourceRoot 'CMakeLists.txt') -PathType Leaf)) {
    throw "Production Guard: required local lib60870-C source tree not found: $lib60870SourceRoot"
}
if (-not (Test-Path -LiteralPath $gplLicensePath -PathType Leaf)) {
    throw "Production Guard: complete GPLv3 license text not found: $gplLicensePath"
}
$gplContent = [System.IO.File]::ReadAllText($gplLicensePath).Replace("`r`n", "`n")
$sha256 = [System.Security.Cryptography.SHA256]::Create()
try {
    $gplHash = [System.BitConverter]::ToString(
        $sha256.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($gplContent))
    ).Replace('-', '')
} finally {
    $sha256.Dispose()
}
if ($gplHash -ne $expectedGplHash) {
    throw "Production Guard: GPL-3.0.txt is not the complete unmodified GNU GPLv3 text. Expected $expectedGplHash, found $gplHash."
}
$forbiddenLib60870Outputs = @(Get-ChildItem -LiteralPath $lib60870SourceRoot -Directory -Recurse -Force |
    Where-Object { $_.Name -match '^(build|dist)([-_].*)?$' })
if ($forbiddenLib60870Outputs.Count -gt 0) {
    throw "Production Guard: build/dist directory found in the exact lib60870-C source tree: $($forbiddenLib60870Outputs[0].FullName)"
}

if (-not $NativeBuildDir) {
    $NativeBuildDir = Join-Path $packagingRoot 'build\native-lib60870'
}
$NativeBuildDir = [System.IO.Path]::GetFullPath($NativeBuildDir)

& cmake --preset iec104-lib60870-release -S $nativeRoot -B $NativeBuildDir
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE." }
& cmake --build $NativeBuildDir --config Release --clean-first
if ($LASTEXITCODE -ne 0) { throw "CMake build failed with exit code $LASTEXITCODE." }

$releaseDir = Join-Path $NativeBuildDir 'Release'
$gatewaySource = Join-Path $releaseDir 'vision-realtime.exe'
$lib60870Source = Join-Path $releaseDir 'lib60870.dll'
foreach ($requiredFile in @($gatewaySource, $lib60870Source, $winSwSource, $iconSource)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required offline packaging input not found: $requiredFile"
    }
}

$backendOutput = & $gatewaySource version --json
if ($LASTEXITCODE -ne 0) {
    throw "Gateway backend inspection failed with exit code $LASTEXITCODE."
}
try {
    $gatewayInfo = $backendOutput | ConvertFrom-Json
} catch {
    throw "Gateway did not return valid version JSON. Refusing to package an unverified binary."
}
if ($gatewayInfo.backend -ne 'lib60870') {
    throw "Production Guard: refusing to package backend '$($gatewayInfo.backend)'; expected 'lib60870'."
}
if (-not $gatewayInfo.version -or $gatewayInfo.version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?$') {
    throw "Gateway returned an invalid package version: '$($gatewayInfo.version)'."
}

$winSwVersion = (Get-Item -LiteralPath $winSwSource).VersionInfo.ProductVersion
if (-not $winSwVersion.StartsWith('2.12.0')) {
    throw "Offline WinSW input must be version 2.12.0; found '$winSwVersion'."
}
$winSwHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $winSwSource).Hash
if ($winSwHash -ne $expectedWinSwHash) {
    throw "Offline WinSW 2.12.0 hash mismatch. Expected $expectedWinSwHash, found $winSwHash."
}

if (Test-Path -LiteralPath $stageRoot) {
    Remove-Item -LiteralPath $stageRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path (Join-Path $stageRoot 'bin'), (Join-Path $stageRoot 'defaults'), (Join-Path $stageRoot 'licenses'), $distRoot | Out-Null

Copy-Item -LiteralPath $gatewaySource -Destination (Join-Path $stageRoot 'bin\vision-realtime.exe')
Copy-Item -LiteralPath $lib60870Source -Destination (Join-Path $stageRoot 'bin\lib60870.dll')
Copy-Item -LiteralPath $winSwSource -Destination (Join-Path $stageRoot 'VisionRealtime.exe')
Copy-Item -LiteralPath (Join-Path $packagingRoot 'windows\VisionRealtime.xml') -Destination $stageRoot
Copy-Item -LiteralPath $iconSource -Destination (Join-Path $stageRoot 'icon.ico')
Copy-Item -LiteralPath (Join-Path $packagingRoot 'windows\gateway.json') -Destination (Join-Path $stageRoot 'defaults\gateway.json')
Copy-Item -LiteralPath (Join-Path $packagingRoot 'windows\initialize-config.ps1') -Destination $stageRoot
Copy-Item -LiteralPath (Join-Path $packagingRoot 'windows\verify-service.ps1') -Destination $stageRoot
Copy-Item -LiteralPath (Join-Path $packagingRoot 'windows\QUICKSTART.txt') -Destination $stageRoot
Copy-Item -Path (Join-Path $repositoryRoot 'licenses\*') -Destination (Join-Path $stageRoot 'licenses')

$stagedBackendOutput = & (Join-Path $stageRoot 'bin\vision-realtime.exe') version --json
if ($LASTEXITCODE -ne 0 -or ($stagedBackendOutput | ConvertFrom-Json).backend -ne 'lib60870') {
    throw 'Production Guard: the staged gateway is not a working lib60870 build.'
}

$servicePackageName = "vision-realtime-$($gatewayInfo.version)-windows-x64-service.zip"
$servicePackagePath = Join-Path $distRoot $servicePackageName
if (Test-Path -LiteralPath $servicePackagePath) { Remove-Item -LiteralPath $servicePackagePath -Force }
Compress-Archive -Path (Join-Path $stageRoot '*') -DestinationPath $servicePackagePath -CompressionLevel Optimal

if (-not $MakensisPath) {
    $makensisCommand = Get-Command makensis.exe -ErrorAction SilentlyContinue
    if ($makensisCommand) { $MakensisPath = $makensisCommand.Source }
}
if (-not $MakensisPath -or -not (Test-Path -LiteralPath $MakensisPath -PathType Leaf)) {
    throw 'makensis.exe was not found. Install NSIS or pass -MakensisPath C:\path\to\makensis.exe. No network download is performed.'
}

$setupName = "vision-realtime-$($gatewayInfo.version)-windows-x64-setup.exe"
$setupPath = Join-Path $distRoot $setupName
& $MakensisPath '/WX' "/DPRODUCT_VERSION=$($gatewayInfo.version)" "/DSTAGE_DIR=$stageRoot" "/DOUTPUT_FILE=$setupPath" (Join-Path $packagingRoot 'windows\gateway-setup.nsi')
if ($LASTEXITCODE -ne 0) { throw "makensis failed with exit code $LASTEXITCODE." }
if (-not (Test-Path -LiteralPath $setupPath -PathType Leaf)) { throw "makensis did not create $setupPath" }

$hashLines = @($servicePackagePath, $setupPath) | ForEach-Object {
    $artifactHash = Get-FileHash -Algorithm SHA256 -LiteralPath $_
    "$($artifactHash.Hash.ToLowerInvariant())  $([System.IO.Path]::GetFileName($_))"
}
[System.IO.File]::WriteAllText(
    (Join-Path $distRoot 'SHA256SUMS.txt'),
    ($hashLines -join [Environment]::NewLine) + [Environment]::NewLine,
    [System.Text.UTF8Encoding]::new($false)
)

Write-Host "Created: $servicePackagePath"
Write-Host "Created: $setupPath"
Write-Host "Checksums: $(Join-Path $distRoot 'SHA256SUMS.txt')"
