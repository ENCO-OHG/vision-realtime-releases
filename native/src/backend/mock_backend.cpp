#include "mock_backend.h"

#include "json_utils.h"

MockBackend::MockBackend(DeviceRegistry& devices) : devices_(devices) {}

BackendResult MockBackend::configure(const std::string& deviceId, const std::vector<TagConfig>& tags, const ConnectionConfig& connection) {
    auto configured = devices_.configure(deviceId, tags, connection);
    if (configured.stoppedWorker.worker.joinable()) configured.stoppedWorker.worker.join();
    return {.status = 200, .body = "{\"ok\":true,\"deviceId\":\"" + jsonEscape(deviceId) + "\",\"tags\":" + std::to_string(configured.tagCount) + "}"};
}

BackendResult MockBackend::start(const std::string& deviceId) {
    devices_.startMock(deviceId);
    return {.status = 200, .body = "{\"ok\":true,\"deviceId\":\"" + jsonEscape(deviceId) + "\"}", .events = {statusEvent(deviceId, true)}};
}

BackendResult MockBackend::stop(const std::string& deviceId) {
    devices_.stopMock(deviceId);
    return {.status = 200, .body = "{\"ok\":true,\"deviceId\":\"" + jsonEscape(deviceId) + "\"}", .events = {statusEvent(deviceId, false)}};
}

BackendResult MockBackend::status(const std::string& deviceId) {
    auto state = devices_.status(deviceId);
    return {.status = 200, .body = "{\"ok\":true,\"deviceId\":\"" + jsonEscape(deviceId) + "\",\"gatewayConnected\":true,\"iec104Connected\":" + (state.running ? "true" : "false") + ",\"state\":\"" + (state.running ? "running" : "off") + "\"}"};
}

BackendResult MockBackend::write(const std::string& deviceId, const WriteRequest& request) {
    if (request.requestId.empty()) return {.status = 400, .body = "{\"ok\":false,\"error\":\"missing-request-id\"}"};
    auto write = devices_.prepareWrite(deviceId, request.tagId, request.ioa, request.value);
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
    if (!devices_.prepareInterrogate(deviceId).connected) return {.status = 409, .body = "{\"ok\":false,\"error\":\"not-connected\"}"};
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
