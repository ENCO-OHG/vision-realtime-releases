# Vision Realtime IEC-104 Gateway: macOS Developer Quickstart

This guide runs the native `lib60870-C` gateway locally for development and RTU validation. macOS has no packaged Vision Realtime service; the supported production package target is Windows.

## 1. Prerequisites

Install the Xcode Command Line Tools and CMake. The repository must contain the vendored `lib60870-C` source at `third_party/lib60870/lib60870-C`.

```sh
xcode-select --install
cmake --version
```

## 2. Build the real backend

From the repository root:

```sh
cmake --preset iec104-lib60870-macos-release -S native
cmake --build build/vision-realtime-lib60870-macos
ctest --test-dir build/vision-realtime-lib60870-macos --output-on-failure
```

The executable is `build/vision-realtime-lib60870-macos/vision-realtime`.

## 3. Configure the gateway

Create a local configuration that is not committed:

```sh
cp native/config.example.json native/config.json
```

Set these controller values in `native/config.json` from the Vision One IEC-104 device settings:

```json
{
  "credentials": {
    "controller": {
      "token": "<gateway-token>",
      "gatewayTargetId": "<gateway-target-id>",
      "controllerId": "<vision-one-controller-id>",
      "controllerGeneration": 1
    },
    "operators": []
  }
}
```

`gatewayTargetId`, `controllerId`, and `controllerGeneration` must exactly match Vision One. Vision One uses the controller token as its Gateway Token and can replace desired state only with the matching controller identity. Optional operator credentials permit events, status, writes, and interrogation, but cannot replace desired state.

Set the RTU address, port, common address, field sizes, and tag IOAs in Vision One. The gateway receives its device and tag configuration through the Vision One desired-state synchronization.

## 4. Start and verify

```sh
./build/vision-realtime-lib60870-macos/vision-realtime run --config native/config.json
```

In another terminal:

```sh
./build/vision-realtime-lib60870-macos/vision-realtime --version
./build/vision-realtime-lib60870-macos/vision-realtime doctor --config native/config.json --json
curl http://127.0.0.1:24104/api/v1/health
curl http://127.0.0.1:24104/api/v1/version
```

Stop the foreground gateway with `Ctrl+C`.

## 5. Logs and state

The example configuration writes logs to `logs/` and persistent desired state to `vision-realtime-state.json` relative to the repository root. These are local runtime files and must not be committed.

If Vision One reports `unauthorized`, verify the controller token. If it reports `controller-identity-mismatch`, verify all three controller identity fields. For RTUs that remain `connecting` or `disconnected`, verify routing, firewall rules, RTU master admission, IEC-104 addresses, and COT/CA/IOA sizes.
