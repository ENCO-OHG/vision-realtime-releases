# Vision Realtime IEC-104 Gateway

Vision Realtime by EN-CO OHG is a standalone C++20 service that connects applications to IEC 60870-5-104 RTUs through `lib60870-C`.

## Deployment Model

```text
Application -- HTTP/WebSocket API --> Vision Realtime IEC-104 Gateway -- IEC-104/TCP --> RTU
                 default 24104                         default 2404
```

The gateway is a separate product with its own installer, Windows service, lifecycle, configuration, state, logs, and upgrade path. Applications connect to an already running gateway; they do not start or own the gateway service. The default local gateway URL is `http://127.0.0.1:24104`, and API routes are under `/api/v1`.

For Windows installation, commissioning, firewall, upgrade, removal, and troubleshooting, see [QUICKSTART_WINDOWS.md](QUICKSTART_WINDOWS.md). Architecture and packaging requirements are in [DEPLOYMENT.md](DEPLOYMENT.md).

## Versioning

Vision Realtime starts its independent product version line at `1.0.0-beta.1`. Earlier internal beta builds used the Vision One application version; future Vision Realtime releases are versioned independently.

## Current Contract

```http
GET  /api/v1/health
GET  /api/v1/version
POST /api/v1/devices/:deviceId/config
POST /api/v1/devices/:deviceId/start
POST /api/v1/devices/:deviceId/stop
GET  /api/v1/devices/:deviceId/status
POST /api/v1/devices/:deviceId/write
POST /api/v1/devices/:deviceId/interrogate
WS   /api/v1/events
```

The API supports optional bearer-token authentication. Production deployments should configure a strong token. `/health` and `/version` identify the running version/backend; device operations and WebSocket access use the configured token.

The production Windows package atomically persists gateway-managed device/tag configuration and desired active state as JSON in `C:\ProgramData\EN-CO OHG\Vision Realtime\state\vision-realtime-state.json`. It restores persisted devices and their desired active state after service or Windows restarts.

Multi-client isolation, per-client pairing and permissions, and configuration-master ownership are planned and must not be presented as available. The current API uses a shared bearer token and globally broadcasts WebSocket events.

## Native Gateway

`native/` contains the C++20 implementation. The default developer build uses a dependency-free mock backend. A production-capable build must enable the real backend with `VISION_REALTIME_WITH_LIB60870=ON`; mock backend builds must not be shipped for production RTUs.

Build the mock backend:

```powershell
cmake -S native -B build/vision-realtime-mock
cmake --build build/vision-realtime-mock --config Release
```

Build the real backend after placing `lib60870-C` at `third_party/lib60870/lib60870-C`:

```powershell
Push-Location native
cmake --preset iec104-lib60870-release
cmake --build --preset iec104-lib60870-release
Pop-Location
```

Run a development build directly:

```powershell
.\build\vision-realtime-lib60870\Release\vision-realtime.exe run --config native\config.example.json
```

Implemented CLI and Windows service commands:

```text
vision-realtime [run] [--config <json>] [options]
vision-realtime version|status|doctor [--config <json>] [--json]
vision-realtime service install|start|stop|restart|status|uninstall
```

`status --config` inspects resolved settings and persisted device count. `doctor --config` validates configuration, state, and backend. On Windows, `service` commands delegate to the packaged `VisionRealtime.exe` WinSW wrapper. The installed executable is `C:\Program Files\EN-CO OHG\Vision Realtime\bin\vision-realtime.exe`, and the service loads `C:\ProgramData\EN-CO OHG\Vision Realtime\config\gateway.json` through `--config`. The installer, executable, and Windows uninstall entry use the Vision Realtime app icon.

## Mock Gateway

`mock-gateway.mjs` implements the API contract without a real IEC-104 peer for driver/UI development:

```powershell
$env:IEC104_GATEWAY_LISTEN = "127.0.0.1"
$env:IEC104_GATEWAY_PORT = "24104"
$env:IEC104_GATEWAY_TOKEN = "change-me"
node mock-gateway.mjs
```

The mock accepts device configuration, emits simulated values/status, and echoes writes. Do not use it for production validation.

## Real Backend Scope

The `lib60870-C` backend currently supports TCP reconnect, StartDT, configurable addresses/sizes/APCI timers, general interrogation, optional clock synchronization, common monitor types, and `C_SC_NA_1`, `C_DC_NA_1`, `C_SE_NA_1`, `C_SE_NB_1`, and `C_SE_NC_1` commands.

For RTU commissioning, set the `IEC104Client` gateway URL/token, RTU address and normally TCP port `2404`, common address, COT/CA/IOA sizes, and tag IOAs/types. If a device remains `connecting` or `disconnected`, verify routing/firewalls, RTU master admission, addresses, and field sizes.

## Logging

The Windows package writes the gateway log to `C:\ProgramData\EN-CO OHG\Vision Realtime\logs\vision-realtime.log` and WinSW wrapper logs under `C:\ProgramData\EN-CO OHG\Vision Realtime\logs\service\`. The gateway log rotates at 1 MB and keeps five rotated files (`vision-realtime.1.log` through `.5.log`). Use `--no-color` or `NO_COLOR` for plain console output.

## Licensing Boundary

Vision Realtime is distributed as a separate IEC-104 gateway product by EN-CO OHG. The public `lib60870-C` repository is GPLv3/commercial dual-licensed. A binary built against the public GPLv3 code requires a GPLv3-compatible gateway distribution with matching corresponding source, build material, licenses, and third-party notices. A suitable commercial license from MZ Automation is the alternative for a different distribution model.

Before publishing binary packages, follow [RELEASE_COMPLIANCE.md](RELEASE_COMPLIANCE.md).

WinSW is an independent third-party Windows service wrapper and platform adapter. It is not gateway protocol code and is not part of `lib60870-C`; Windows packages must carry its own license and notice separately.
