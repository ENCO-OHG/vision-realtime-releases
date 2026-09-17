# Vision Realtime IEC-104 Gateway

Vision Realtime by EN-CO OHG is a C++20 IEC 60870-5-104 gateway service. It exposes a local HTTP/WebSocket API and connects to RTUs through `lib60870-C`.

## Overview

```text
Application -- HTTP/WebSocket API --> Vision Realtime -- IEC-104/TCP --> RTU
                 default 24104              default 2404
```

The default local gateway URL is `http://127.0.0.1:24104`. API routes are under `/api/v1`. Windows packages install the gateway as a Windows service with configuration, state, and logs under `C:\ProgramData\EN-CO OHG\Vision Realtime`.

For Windows installation, commissioning, firewall, upgrade, removal, and troubleshooting, see [QUICKSTART_WINDOWS.md](QUICKSTART_WINDOWS.md). Architecture and packaging requirements are in [DEPLOYMENT.md](DEPLOYMENT.md).

## Current Contract

```http
GET  /api/v1/health
GET  /api/v1/version
PUT  /api/v1/desired-state
GET  /api/v1/devices/:deviceId/status
POST /api/v1/devices/:deviceId/write
POST /api/v1/devices/:deviceId/interrogate
WS   /api/v1/events
```

The API uses local bearer-token credentials. The controller credential is required for complete revisioned desired-state replacement. Operator credentials can receive events and use data-plane operations. `/version` reports the supported desired-state and authentication capabilities.

The production Windows package atomically persists gateway-managed device/tag configuration and desired active state as JSON in `C:\ProgramData\EN-CO OHG\Vision Realtime\state\vision-realtime-state.json`. It restores persisted devices and their desired active state after service or Windows restarts.

## Native Gateway

`native/` contains the C++20 implementation. The default developer build uses a dependency-free mock backend. A production-capable build must enable the real backend with `VISION_REALTIME_WITH_LIB60870=ON`; mock backend builds must not be shipped for production RTUs.

Build the mock backend:

```powershell
cmake -S native -B build/vision-realtime-mock
cmake --build build/vision-realtime-mock --config Release
```

Build the real backend after placing `lib60870-C` at `third_party/lib60870/lib60870-C`.

Windows requires CMake, Visual Studio 2026 with the C++ Build Tools, and a Windows SDK:

```powershell
Push-Location native
cmake --preset iec104-lib60870-release
cmake --build --preset iec104-lib60870-release
Pop-Location
```

macOS requires CMake and the Xcode Command Line Tools:

```sh
cmake --preset iec104-lib60870-macos-release -S native
cmake --build build/vision-realtime-lib60870-macos
```

Run a Windows development build directly:

```powershell
.\build\vision-realtime-lib60870\Release\vision-realtime.exe run --config native\config.example.json
```

Run a macOS development build directly:

```sh
./build/vision-realtime-lib60870-macos/vision-realtime run --config native/config.example.json
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

Vision Realtime uses `lib60870-C`, which is GPLv3/commercial dual-licensed by MZ Automation. Binary releases built with the public GPLv3 source require matching source code, build material, licenses, and third-party notices.

Before publishing binary packages, follow [RELEASE_COMPLIANCE.md](RELEASE_COMPLIANCE.md).

WinSW is an independent third-party Windows service wrapper and platform adapter. It is not gateway protocol code and is not part of `lib60870-C`; Windows packages must carry its own license and notice separately.
