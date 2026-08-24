# Vision Realtime IEC-104 Gateway Deployment

## Product Boundary

Vision Realtime is an IEC-104 gateway product from EN-CO OHG. It is installed and operated separately from client applications, which connect to the service but do not start, stop, supervise, or update it.

The production gateway links to `lib60870-C`. When built from the public GPLv3 code, the gateway product must be distributed under GPLv3-compatible terms with matching corresponding source, exact dependency source, build scripts, licenses, and third-party notices. A commercial `lib60870-C` license from MZ Automation is a separate alternative distribution model.

WinSW is a separately licensed third-party Windows service wrapper. It is a packaging/platform adapter, not part of the gateway core and not part of `lib60870-C`. Release notices must identify WinSW and `lib60870-C` independently.

## Runtime Model

```text
Client -- HTTP/WebSocket --> Vision Realtime IEC-104 Gateway -- IEC-104/TCP --> RTU
              API port 24104                      RTU port 2404
```

The client always opens the API connection to the gateway. The gateway never connects back to the client. The gateway owns its service and IEC-104 connection lifecycles.

Production package behavior:

- Device/tag configuration and desired active state are atomically persisted as JSON in `C:\ProgramData\EN-CO OHG\Vision Realtime\state\vision-realtime-state.json`.
- Persisted devices are restored after gateway service and Windows restarts without requiring the client to recreate them.
- Client disconnect or shutdown does not stop gateway-managed RTU connections.
- Standard uninstall retains configuration and device state; explicit purge removes them.
- Mock backend builds are never released as production packages.

## Feature Status

Available API model:

- shared bearer-token authentication
- multiple gateway device IDs in the runtime registry
- device configure/start/stop/status/write/interrogate operations
- WebSocket events
- one in-flight write per IEC-104 connection; a confirmation timeout reconnects so a late confirmation cannot be matched to a later command on the same IOA

Still planned:

- isolated multi-client subscriptions instead of global event broadcast
- client pairing, identities, credentials, and permissions
- one explicit configuration master with controlled transfer
- gateway-side enforcement of read/write/configuration roles

Until those planned functions ship, do not describe a deployment as paired, permission-isolated, or config-master controlled. Separate client installations using one shared token are not the intended final multi-client security model.

Planned stable identities are `gatewayDeviceId`, `instanceId`, `sessionId`, and `commandId` for device ownership, client identity, connection identity, idempotency, and auditing.

## Gateway CLI

The same executable provides runtime, diagnostics, and Windows service management. `--config` loads JSON configuration before command-line overrides are applied.

Implemented:

```text
vision-realtime [run] [--config <json>] [options]
vision-realtime version|status|doctor [--config <json>] [--json]
vision-realtime service install|start|stop|restart|status|uninstall
```

`status` performs offline configuration/state inspection and reports persisted device count. `doctor` validates resolved configuration, the state file, and the linked backend. Windows `service` commands locate the packaged WinSW wrapper and delegate the requested action to it.

Planned administration commands:

```text
vision-realtime setup
vision-realtime client pairing-code create
vision-realtime client list|show|enable|disable|revoke
vision-realtime client permissions set
vision-realtime config-master show|set|clear
```

The planned commands must not be included in operational automation until implemented. Multi-client administrative commands should eventually support scriptable JSON/no-color/quiet/non-interactive modes where applicable.

## Windows Package

Initial production target: Windows x64.

```text
C:\Program Files\EN-CO OHG\Vision Realtime\bin\vision-realtime.exe
C:\Program Files\EN-CO OHG\Vision Realtime\VisionRealtime.exe
C:\Program Files\EN-CO OHG\Vision Realtime\VisionRealtime.xml
C:\ProgramData\EN-CO OHG\Vision Realtime\config\gateway.json
C:\ProgramData\EN-CO OHG\Vision Realtime\state\vision-realtime-state.json
C:\ProgramData\EN-CO OHG\Vision Realtime\logs\vision-realtime.log
C:\ProgramData\EN-CO OHG\Vision Realtime\logs\service\
```

Program binaries, WinSW wrapper/configuration, licenses, and notices are installed under `Program Files`. Mutable configuration, token material, atomic JSON device state, and logs are stored under the exact `ProgramData` paths above; upgrades do not overwrite them. The installer, executable, and Windows uninstall entry use the Vision Realtime app icon.

WinSW registers and supervises service ID `VisionRealtime` through SCM, starts it automatically with delayed auto-start, and runs it as `NT AUTHORITY\LocalService`. The gateway CLI's implemented `service install|start|stop|restart|status|uninstall` commands delegate to `VisionRealtime.exe`. Service recovery restarts unexpected failures without creating a second gateway process. The installer grants LocalService modify access to the Vision Realtime `ProgramData` tree.

Default network policy:

- listen on `127.0.0.1:24104` for a same-host client
- create no inbound firewall rule for local-only mode
- permit outbound TCP to configured RTUs, normally destination port `2404`
- for remote API access, bind deliberately to a LAN address and restrict inbound TCP `24104` to named client hosts
- do not expose the current plain-HTTP bearer-token API directly to the Internet; use a trusted private network, VPN, or controlled TLS termination

Installer requirements:

- publish setup executable, matching GitHub source archives from the public release tag, and `SHA256SUMS.txt`
- carry Authenticode signing where release infrastructure provides it
- preserve `ProgramData` on normal upgrade and uninstall
- remove service registration and installed binaries on normal uninstall
- retain configuration, credentials, state, and logs on normal uninstall
- expose the uninstaller purge checkbox and support unattended purge with `Uninstall.exe /S /PURGE`
- verify service start, `/health`, `/version`, `backend=lib60870`, restart persistence, and one RTU connection before release

Operator steps are documented bilingually in [QUICKSTART_WINDOWS.md](QUICKSTART_WINDOWS.md).

## Linux Target

Future official target: Debian/Ubuntu x86_64 with systemd.

```text
/usr/bin/vision-realtime
/etc/vision-realtime/gateway.json
/var/lib/vision-realtime/vision-realtime-state.json
/var/log/vision-realtime/
/run/vision-realtime/admin.sock
```

The first package format should be `.deb`. Removal keeps state and purge removes it according to Debian conventions. The local admin Unix-domain socket must use restrictive ownership/permissions and validate peer credentials where supported.

## Release Artifacts

Before publishing binary packages, follow [RELEASE_COMPLIANCE.md](RELEASE_COMPLIANCE.md).

Windows:

```text
vision-realtime-<version>-windows-x64-setup.exe
vision-realtime-<version>-windows-x64-service.zip
SHA256SUMS.txt
LICENSE
THIRD-PARTY-NOTICES
```

GitHub's automatically attached `Source code (zip)` and `Source code (tar.gz)` archives are generated from the matching tag in the public `vision-realtime-releases` repository.

Future Linux:

```text
vision-realtime-<version>-amd64.deb
vision-realtime-<version>-source.tar.gz
SHA256SUMS.txt
LICENSE
THIRD-PARTY-NOTICES
```

## Production Blockers

Before enabling production writes or declaring multi-client readiness:

- Validate real IEC-104 activation-confirmation correlation against supported RTUs and a negative-confirmation test server.
- Validate select-before-operate against RTUs that positively and negatively confirm both phases.
- Extend the current single in-flight command guard to a bounded per-connection queue for multiple writers.
- Replace global WebSocket broadcasting with client/device subscription filtering.
- Implement gateway-side client credentials, permissions, pairing, and config-master transfer.
- Validate firewall behavior on the supported Windows versions.
