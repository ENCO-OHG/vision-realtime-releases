#include "mock_backend.h"

#include "json_utils.h"

MockBackend::MockBackend(DeviceRegistry& devices) : devices_(devices) {}

BackendResult MockBackend::configure(const std::string& deviceId, const std::vector<TagConfig>& tags, const ConnectionConfig& connection) {
    auto configured = devices_.configure(deviceId, tags, connection);
    if (configured.stoppedWorker.worker.joinable()) configured.stoppedWorker.worker.join();
    return {.status = 200, .body = "{\"ok\":true,\"deviceId\":\"" + jsonEscape(deviceId) + "\",\"tags\":" + std::to_string(configured.tagCount) + "}"};
}

BackendResult MockBackend::start(const std::string& deviceId) {
    const auto status = devices_.status(deviceId);
    if (status.lookupState == DeviceLookupState::Available && status.running) {
        return {.status = 200, .body = "{\"ok\":true,\"deviceId\":\"" + jsonEscape(deviceId) + "\",\"alreadyRunning\":true}"};
    }
    devices_.startMock(deviceId);
    return {.status = 200, .body = "{\"ok\":true,\"deviceId\":\"" + jsonEscape(deviceId) + "\"}", .events = {statusEvent(deviceId, true)}};
}

BackendResult MockBackend::stop(const std::string& deviceId) {
    devices_.stopMock(deviceId);
    devices_.clearCachedValues(deviceId);
    return {.status = 200, .body = "{\"ok\":true,\"deviceId\":\"" + jsonEscape(deviceId) + "\"}", .events = {statusEvent(deviceId, false)}};
}

BackendResult MockBackend::remove(const std::string& deviceId) {
    devices_.remove(deviceId);
    return {.status = 200, .body = "{\"ok\":true,\"deviceId\":\"" + jsonEscape(deviceId) + "\"}", .events = {statusEvent(deviceId, false)}};
}

BackendResult MockBackend::status(const std::string& deviceId) {
    auto state = devices_.status(deviceId);
    if (state.lookupState == DeviceLookupState::Missing) return {.status = 404, .body = "{\"ok\":false,\"error\":\"unknown-device\"}"};
    if (state.lookupState == DeviceLookupState::Reconciling) return {.status = 200, .body = "{\"ok\":true,\"deviceId\":\"" + jsonEscape(deviceId) + "\",\"gatewayConnected\":true,\"iec104Connected\":false,\"state\":\"reconciling\"}"};
    return {.status = 200, .body = "{\"ok\":true,\"deviceId\":\"" + jsonEscape(deviceId) + "\",\"gatewayConnected\":true,\"iec104Connected\":" + (state.running ? "true" : "false") + ",\"state\":\"" + (state.running ? "running" : "off") + "\"}"};
}

BackendResult MockBackend::write(const std::string& deviceId, const WriteRequest& request) {
    auto write = devices_.prepareWrite(deviceId, request.tagId, request.ioa, request.value);
    if (write.lookupState == DeviceLookupState::Missing) return {.status = 404, .body = "{\"ok\":false,\"error\":\"unknown-device\"}"};
    if (write.lookupState == DeviceLookupState::Reconciling) return {.status = 409, .body = "{\"ok\":false,\"requestId\":\"" + jsonEscape(request.requestId) + "\",\"error\":\"device_reconciling\"}"};
    if (request.requestId.empty()) return {.status = 400, .body = "{\"ok\":false,\"error\":\"missing-request-id\"}"};
    if (!write.connected) return {.status = 409, .body = "{\"ok\":false,\"requestId\":\"" + jsonEscape(request.requestId) + "\",\"error\":\"not-connected\"}"};
    if (!write.found) return {.status = 404, .body = "{\"ok\":false,\"requestId\":\"" + jsonEscape(request.requestId) + "\",\"error\":\"unknown-ioa\"}"};
    return {
        .status = 200,
        .body = "{\"ok\":true,\"requestId\":\"" + jsonEscape(request.requestId) + "\",\"cause\":\"accepted\"}",
        .events = {writeResultEvent(deviceId, request.requestId, write.tag), valueEvent(deviceId, write.tag, request.value)},
    };
}

BackendResult MockBackend::interrogate(const std::string& deviceId, int qualifier) {
    (void)qualifier;
    const auto interrogation = devices_.prepareInterrogate(deviceId);
    if (interrogation.lookupState == DeviceLookupState::Missing) return {.status = 404, .body = "{\"ok\":false,\"error\":\"unknown-device\"}"};
    if (interrogation.lookupState == DeviceLookupState::Reconciling) return {.status = 409, .body = "{\"ok\":false,\"error\":\"device_reconciling\"}"};
    if (!interrogation.connected) return {.status = 409, .body = "{\"ok\":false,\"error\":\"not-connected\"}"};
    BackendResult result{.status = 200, .body = "{\"ok\":true}"};
    for (const auto& event : devices_.collectMockValueEvents(deviceId)) {
        if (event.tag.deviceDataType.starts_with("C_")) continue;
        result.events.push_back(valueEvent(event.deviceId, event.tag.ioa, event.tag.tagId, event.tag.deviceDataType, event.value, 0, 0, 20));
    }
    return result;
}

std::vector<std::string> MockBackend::pollEvents() {
    std::vector<std::string> events;
    for (const auto& event : devices_.collectMockValueEvents()) {
        if (event.tag.deviceDataType.starts_with("C_")) continue;
        events.push_back(valueEvent(event.deviceId, event.tag, event.value));
    }
    return events;
}
