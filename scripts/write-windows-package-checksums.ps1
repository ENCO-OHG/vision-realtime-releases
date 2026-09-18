[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$distRoot = Join-Path $repositoryRoot 'packaging\dist'
$version = (Get-Content -LiteralPath (Join-Path $repositoryRoot 'package.json') -Raw | ConvertFrom-Json).version
$assets = @(
    (Join-Path $distRoot "vision-realtime-$version-windows-x64-service.zip"),
    (Join-Path $distRoot "vision-realtime-$version-windows-x64-setup.exe")
)
foreach ($asset in $assets) {
    if (-not (Test-Path -LiteralPath $asset -PathType Leaf)) { throw "Release asset not found: $asset" }
}

$hashLines = $assets | ForEach-Object {
    $artifactHash = Get-FileHash -Algorithm SHA256 -LiteralPath $_
    "$($artifactHash.Hash.ToLowerInvariant())  $([System.IO.Path]::GetFileName($_))"
}
$checksumPath = Join-Path $distRoot 'SHA256SUMS.txt'
[System.IO.File]::WriteAllText($checksumPath, ($hashLines -join [Environment]::NewLine) + [Environment]::NewLine, [System.Text.UTF8Encoding]::new($false))
Write-Host "Checksums: $checksumPath"
