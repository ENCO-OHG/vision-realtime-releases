param(
    [Parameter(Mandatory = $true)]
    [string]$ServiceName,
    [Parameter(Mandatory = $true)]
    [string]$ConfigPath,
    [Parameter(Mandatory = $true)]
    [string]$ExpectedVersion,
    [int]$TimeoutSeconds = 20
)

$ErrorActionPreference = 'Stop'
$config = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
$probeAddress = [string]$config.listenAddress
if (-not $probeAddress -or $probeAddress -in @('*', '0.0.0.0', '::', '::0')) {
    $probeAddress = '127.0.0.1'
}
$port = [int]$config.port
if ($port -lt 1 -or $port -gt 65535) {
    throw "Vision Realtime configuration has an invalid port: $port"
}
$healthUrl = [UriBuilder]::new('http', $probeAddress, $port, '/api/v1/health').Uri.AbsoluteUri
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$lastError = 'service verification timed out'

do {
    try {
        $service = Get-Service -Name $ServiceName -ErrorAction Stop
        if ($service.Status -ne 'Running') {
            $lastError = "service status is $($service.Status)"
        } else {
            $health = Invoke-RestMethod -Uri $healthUrl -TimeoutSec 2
            if ($health.ok -eq $true -and [string]$health.version -eq $ExpectedVersion) {
                exit 0
            }
            $lastError = "health version '$($health.version)' does not match '$ExpectedVersion'"
        }
    } catch {
        $lastError = $_.Exception.Message
    }
    Start-Sleep -Milliseconds 500
} while ((Get-Date) -lt $deadline)

throw "Vision Realtime service verification failed: $lastError"
