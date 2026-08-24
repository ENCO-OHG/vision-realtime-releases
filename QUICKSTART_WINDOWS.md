# Vision Realtime IEC-104 Gateway: Windows Quickstart

Vision Realtime by EN-CO OHG is a separately installed product and an independent Windows service. Client applications do not start, stop, or update it.

### 1. Verify and install the installer

Obtain the installer and matching `SHA256SUMS.txt` from the same release. Use GitHub's automatically attached source archive from that release if source code is required. Verify in PowerShell:

```powershell
Get-AuthenticodeSignature .\vision-realtime-<version>-windows-x64-setup.exe | Format-List Status,SignerCertificate
Get-FileHash .\vision-realtime-<version>-windows-x64-setup.exe -Algorithm SHA256
```

The SHA-256 hash must match `SHA256SUMS.txt`. If the release is signed, the expected Authenticode signature must also be valid; beta/internal packages may still report `NotSigned`. Run the installer as Administrator and confirm its displayed destination.

Default paths:

```text
Gateway EXE:    C:\Program Files\EN-CO OHG\Vision Realtime\bin\vision-realtime.exe
Configuration:  C:\ProgramData\EN-CO OHG\Vision Realtime\config\gateway.json
Device state:   C:\ProgramData\EN-CO OHG\Vision Realtime\state\vision-realtime-state.json
Gateway log:    C:\ProgramData\EN-CO OHG\Vision Realtime\logs\vision-realtime.log
Service logs:   C:\ProgramData\EN-CO OHG\Vision Realtime\logs\service\
```

`ProgramData` is hidden by default. Never paste secrets such as the gateway token into tickets or logs.

On first installation, setup displays the randomly generated Gateway Token exactly once in a selectable field. Store it securely and enter it as the **Gateway Token** in the client device. Upgrades preserve the existing token and do not display it again; silent installations never display secrets.

### 2. Verify the service and diagnostics

Open PowerShell as Administrator:

```powershell
$exe = "C:\Program Files\EN-CO OHG\Vision Realtime\bin\vision-realtime.exe"
& $exe service status
& $exe service start
& $exe service restart
```

`service install|start|stop|restart|status|uninstall` is implemented and delegates to the `VisionRealtime.exe` WinSW wrapper. The service ID and display name are `VisionRealtime`; administrative actions require an elevated PowerShell. Setup, the installed application, and the uninstall entry use the Vision Realtime app icon.

Check the CLI and local HTTP API:

```powershell
$config = "C:\ProgramData\EN-CO OHG\Vision Realtime\config\gateway.json"
& $exe version
& $exe status --config $config
& $exe doctor --config $config
Invoke-RestMethod http://127.0.0.1:24104/api/v1/health
Invoke-RestMethod http://127.0.0.1:24104/api/v1/version
```

`status --config` inspects the resolved configuration and reports the number of persisted devices; `doctor --config` checks configuration, state file, and backend. Use `service status` for the running service and `/health` for API reachability. Production packages report `backend=lib60870`.

Get the status of a configured gateway device (replace device ID and token):

```powershell
$headers = @{ Authorization = "Bearer <gateway-token>" }
Invoke-RestMethod -Headers $headers http://127.0.0.1:24104/api/v1/devices/<device-id>/status
```

### 3. Configure the gateway and client

Use the token shown during first installation. If it is needed again for an existing installation, an administrator can read it without changing the configuration:

```powershell
$gatewayConfig = Get-Content "C:\ProgramData\EN-CO OHG\Vision Realtime\config\gateway.json" -Raw | ConvertFrom-Json
$gatewayConfig.authToken
```

For a client on the same machine use:

```text
Gateway URL:    http://127.0.0.1:24104
Gateway Token:  the same token configured in the gateway
```

Create an `IEC104Client` device in the client application. Set the RTU address, RTU port (IEC-104 default: TCP `2404`), common address, and COT/CA/IOA sizes to match the RTU. Add tags with the correct IOA and monitor/command type. Gateway API port `24104` and RTU port `2404` belong to different connections.

The production package atomically persists gateway device/tag configuration and desired active state as JSON in `C:\ProgramData\EN-CO OHG\Vision Realtime\state\vision-realtime-state.json`. The service loads this file at startup and restores devices and their desired active state after service or Windows restarts. Verify this during commissioning with a test device and `& $exe service restart`.

Multi-client operation, client pairing/permissions, and the configuration-master role are still planned. The current shared bearer token does not provide those features.

### 4. Firewall: local or remote

Local use on `127.0.0.1` needs no inbound Windows Firewall rule for port `24104`. The gateway host must reach the RTU over TCP `2404`; RTU and network firewalls must permit that connection.

Enable remote access only when the gateway explicitly listens on a LAN address or `0.0.0.0`. Restrict access to known client hosts:

```powershell
New-NetFirewallRule -DisplayName "Vision Realtime IEC-104 Gateway API" -Direction Inbound -Action Allow -Protocol TCP -LocalPort 24104 -RemoteAddress <client-ip>
Test-NetConnection <rtu-ip> -Port 2404
```

The native API currently uses HTTP with a bearer token. Never expose port `24104` to the Internet; use a VPN or controlled TLS termination across remote networks. Do not add a remote rule for local-only operation.

### 5. Logs, upgrade, and removal

Follow the current log:

```powershell
Get-Content "C:\ProgramData\EN-CO OHG\Vision Realtime\logs\vision-realtime.log" -Tail 100 -Wait
```

The gateway rotates `vision-realtime.log` at 1 MB and retains up to five rotated files. WinSW writes separate wrapper logs under `C:\ProgramData\EN-CO OHG\Vision Realtime\logs\service\`. Before an upgrade, back up `config\gateway.json` and `state\vision-realtime-state.json`; run the new installer as Administrator, then verify version, service, health, and one device. A normal in-place upgrade preserves configuration, state, and logs. When replacing the former `VisionOneIec104Gateway` service, setup migrates its token, network configuration, and device state; the old ProgramData remains as a recovery copy initially.

Standard removal through **Installed apps** removes the program and service but retains configuration, token, device state, and logs for later reinstallation. To remove all data, select **Purge all gateway data from ProgramData** in the uninstaller. For unattended removal run as Administrator:

```powershell
& "C:\Program Files\EN-CO OHG\Vision Realtime\Uninstall.exe" /S /PURGE
```

`/S` alone uninstalls while retaining data; `/S /PURGE` also removes the complete gateway directory under `ProgramData`. Purge is irreversible. Back up configuration and state first if required. Remove any manually created firewall rule separately.

### 6. Troubleshooting

- Service does not start: inspect `Get-Service` and Windows Event Viewer, then read the gateway log and run `doctor`.
- `/health` is unreachable: check service state, listen address, port ownership with `Get-NetTCPConnection -LocalPort 24104`, and firewall rules.
- HTTP `401`: make the gateway and client tokens identical; avoid copied whitespace.
- Device remains `connecting`/`disconnected`: run `Test-NetConnection <rtu-ip> -Port 2404`; verify RTU permission for this master, common address, and COT/CA/IOA sizes.
- Device state is missing after restart: inspect `state\vision-realtime-state.json`, JSON validity, and `NT AUTHORITY\LocalService` write access to the Vision Realtime `ProgramData` tree; then run `doctor --config $config`.
- Local remote test succeeds but the client cannot connect: the listen address must not be `127.0.0.1`; check the inbound rule and its `RemoteAddress` restriction.

## Notices

Vision Realtime is a separate IEC-104 gateway product from EN-CO OHG. Production builds using the public `lib60870-C` code are distributed under GPLv3-compatible terms with corresponding source and notices; commercial `lib60870-C` licensing is an alternative distribution model. `lib60870-C` is a third-party IEC 60870-5-104 implementation from MZ Automation.

WinSW is a separate third-party Windows service wrapper, not part of the gateway core and not part of `lib60870-C`. Its own license and notice must accompany the Windows package. See the package `LICENSE` and `THIRD-PARTY-NOTICES` files for the exact versions and terms.
