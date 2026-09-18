#include "json_utils.h"
#include "mock_backend.h"
#include "state_store.h"
#include "sha256.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void parserHandlesNestedAndEscapedValues() {
    const JsonValue value = parseJson(R"({"name":"line\n\u0041","items":[true,-1.25e2,{"id":3}]})");
    require(value.find("name") && *value.find("name")->string() == "line\nA", "escaped string was not decoded");
    require(value.find("items") && value.find("items")->array()->size() == 3, "nested array was not parsed");
    require(parseJson(serializeJson(value)).find("items") != nullptr, "serialized JSON was not parseable");
    bool rejected = false;
    try { (void)parseJson("{\"broken\":]"); } catch (const std::exception&) { rejected = true; }
    require(rejected, "invalid JSON was accepted");
}

void sha256MatchesKnownValue() {
    require(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 result differs");
}

DesiredState desiredState() {
    ConnectionConfig connection;
    connection.remoteAddress = "10.0.0.7";
    connection.remotePort = 2405;
    return {.gatewayTargetId = "gateway-1", .controllerId = "controller-1", .controllerGeneration = 3, .revision = 7, .requestId = "request-1", .devices = {{.deviceId = "device-1", .tags = {{.tagId = "tag-1", .ioa = 17, .deviceDataType = "M_SP_NA_1"}}, .connection = connection, .desiredState = "running"}}};
}

void stateRoundTripsAndPreservesDesiredRunning() {
    const auto path = std::filesystem::temp_directory_path() / "vision-realtime-state-store-test.json";
    const auto temporary = std::filesystem::path(path.string() + ".tmp");
    std::filesystem::remove(path);
    std::filesystem::remove(temporary);

    StateStore writer(path.string());
    writer.load();
    writer.replace(desiredState());
    require(!std::filesystem::exists(temporary), "temporary state file remained after save");

    StateStore reader(path.string());
    reader.load();
    const auto devices = reader.devices();
    require(devices.size() == 1, "persisted device count differs");
    require(devices[0].deviceId == "device-1" && devices[0].desiredState == "running", "desired state differs");
    require(devices[0].connection.remoteAddress == "10.0.0.7" && devices[0].tags[0].ioa == 17, "device configuration differs");
    require(reader.desiredState().revision == 7 && reader.desiredState().hash.size() == 64, "desired-state revision or hash differs");
    std::filesystem::remove(path);
}

void stateVersionOneIsExplicitlyRejected() {
    const auto path = std::filesystem::temp_directory_path() / "vision-realtime-v1-state-test.json";
    {
        std::ofstream output(path, std::ios::trunc);
        output << "{\"version\":1,\"devices\":[]}";
    }
    bool rejected = false;
    try {
        StateStore store(path.string());
        store.load();
    } catch (const std::exception& e) {
        rejected = std::string(e.what()).find("state version 1 is not supported") != std::string::npos;
    }
    std::filesystem::remove(path);
    require(rejected, "state version 1 was not explicitly rejected");
}

void staleFenceIsRejected() {
    const auto path = std::filesystem::temp_directory_path() / "vision-realtime-revision-state-test.json";
    std::filesystem::remove(path);
    StateStore store(path.string());
    store.load();
    store.replace(desiredState());
    bool rejected = false;
    require(!store.replace(desiredState()), "matching revision and payload was not idempotent");
    auto changed = desiredState();
    changed.devices[0].desiredState = "stopped";
    try { store.replace(changed); } catch (const std::exception& e) { rejected = std::string(e.what()).find("different payload") != std::string::npos; }
    std::filesystem::remove(path);
    require(rejected, "same revision with a different payload was accepted");
    auto older = desiredState();
    older.revision = 6;
    rejected = false;
    try { store.replace(older); } catch (const std::exception& e) { rejected = std::string(e.what()).find("lower") != std::string::npos; }
    require(rejected, "lower revision was accepted");
}

void desiredStateRejectsUnexpectedFields() {
    bool rejected = false;
    try {
        (void)parseDesiredState(parseJson("{\"gatewayTargetId\":\"gateway-1\",\"controllerId\":\"controller-1\",\"controllerGeneration\":1,\"revision\":1,\"requestId\":\"request-1\",\"devices\":[],\"legacy\":true}"));
    } catch (const std::exception& e) {
        rejected = std::string(e.what()).find("unexpected field legacy") != std::string::npos;
    }
    require(rejected, "desired state accepted an unexpected field");
}

void removedDeviceNoLongerHasStatus() {
    DeviceRegistry registry;
    MockBackend backend(registry);
    ConnectionConfig connection;
    backend.configure("device-1", {}, connection);
    require(backend.status("device-1").status == 200, "configured device had no status");
    backend.remove("device-1");
    require(backend.status("device-1").status == 404, "removed device remained in the registry");
}

void stoppedDeviceRemainsStoppedUntilExplicitStart() {
    DeviceRegistry registry;
    MockBackend backend(registry);
    ConnectionConfig connection;
    backend.configure("device-1", {}, connection);
    backend.start("device-1");
    require(registry.status("device-1").running, "started device was not running");

    backend.stop("device-1");
    require(!registry.status("device-1").running, "stopped device remained running");

    backend.start("device-1");
    require(registry.status("device-1").running, "explicit start did not resume the stopped device");
}

void reconcilingDevicesRejectDataPlaneRequestsWithoutCreatingGhostDevices() {
    DeviceRegistry registry;
    MockBackend backend(registry);
    ConnectionConfig connection;
    const TagConfig tag{.tagId = "tag-1", .ioa = 17, .deviceDataType = "M_SP_NA_1"};
    backend.configure("device-1", {tag}, connection);
    backend.configure("device-2", {tag}, connection);
    backend.start("device-1");
    backend.start("device-2");

    registry.beginReconcile({"device-1"});
    const auto status = backend.status("device-1");
    require(status.status == 200 && status.body.find("\"state\":\"reconciling\"") != std::string::npos, "reconciling device did not report its reconciliation state");
    const auto write = backend.write("device-1", {.requestId = "request-1", .tagId = tag.tagId, .ioa = tag.ioa, .value = 1.0});
    require(write.status == 409 && write.body.find("device_reconciling") != std::string::npos, "reconciling device accepted a write");
    const auto interrogation = backend.interrogate("device-1", 20);
    require(interrogation.status == 409 && interrogation.body.find("device_reconciling") != std::string::npos, "reconciling device accepted an interrogation");

    const auto unaffectedWrite = backend.write("device-2", {.requestId = "request-2", .tagId = tag.tagId, .ioa = tag.ioa, .value = 2.0});
    require(unaffectedWrite.status == 200, "unaffected device was blocked during reconciliation");
    registry.endReconcile({"device-1"});

    const auto missingStatus = backend.status("missing-device");
    require(missingStatus.status == 404 && missingStatus.body.find("unknown-device") != std::string::npos, "missing device status did not return unknown-device");
    const auto missingWrite = backend.write("missing-device", {.requestId = "request-3", .ioa = tag.ioa, .value = 3.0});
    require(missingWrite.status == 404 && !registry.hasDevice("missing-device"), "missing device write created a ghost device");
}

void cachedValuesReplayOnlyMatchingConfiguredTags() {
    DeviceRegistry registry;
    ConnectionConfig connection;
    const TagConfig tag{.tagId = "tag-1", .ioa = 17, .deviceDataType = "M_SP_NA_1"};
    registry.configure("device-1", {tag}, connection);
    registry.cacheValue("device-1", tag.tagId, tag.ioa, tag.deviceDataType, "value-1");
    registry.cacheValue("device-1", "removed", 18, "M_ME_NC_1", "value-removed");
    require(registry.cachedValueEvents("device-1").size() == 2, "cached values were not retained");

    registry.configure("device-1", {tag}, connection);
    const auto replay = registry.cachedValueEvents("device-1");
    require(replay.size() == 1 && replay[0] == "value-1", "cache retained a value for an unconfigured tag");
    registry.clearCachedValues("device-1");
    require(registry.cachedValueEvents("device-1").empty(), "cached values were not cleared");
}

void repeatedStartDoesNotRestartAnAlreadyRunningDevice() {
    DeviceRegistry registry;
    MockBackend backend(registry);
    ConnectionConfig connection;
    backend.configure("device-1", {}, connection);
    backend.start("device-1");
    const auto repeatedStart = backend.start("device-1");
    require(repeatedStart.status == 200 && repeatedStart.body.find("alreadyRunning") != std::string::npos, "repeated start restarted an already running device");
}

void invalidStateReportsItsPath() {
    const auto path = std::filesystem::temp_directory_path() / "vision-realtime-invalid-state-test.json";
    {
        std::ofstream output(path, std::ios::trunc);
        output << "{\"version\":1,\"devices\":[{";
    }
    bool rejected = false;
    try {
        StateStore store(path.string());
        store.load();
    } catch (const std::exception& e) {
        rejected = std::string(e.what()).find(path.string()) != std::string::npos;
    }
    std::filesystem::remove(path);
    require(rejected, "invalid state did not report a clear path-specific error");
}

} // namespace

int main() {
    try {
        parserHandlesNestedAndEscapedValues();
        sha256MatchesKnownValue();
        stateRoundTripsAndPreservesDesiredRunning();
        invalidStateReportsItsPath();
        stateVersionOneIsExplicitlyRejected();
        staleFenceIsRejected();
        desiredStateRejectsUnexpectedFields();
        removedDeviceNoLongerHasStatus();
        stoppedDeviceRemainsStoppedUntilExplicitStart();
        reconcilingDevicesRejectDataPlaneRequestsWithoutCreatingGhostDevices();
        cachedValuesReplayOnlyMatchingConfiguredTags();
        repeatedStartDoesNotRestartAnAlreadyRunningDevice();
        std::cout << "state store tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "state store test failed: " << e.what() << '\n';
        return 1;
    }
}
