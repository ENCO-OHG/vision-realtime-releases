#include "state_store.h"

#include "json_utils.h"
#include "sha256.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open state file: " + path.string());
    std::ostringstream contents;
    contents << input.rdbuf();
    if (!input.good() && !input.eof()) throw std::runtime_error("cannot read state file: " + path.string());
    return contents.str();
}

JsonValue connectionJson(const ConnectionConfig& c) {
    return JsonValue(JsonValue::Object{
        {"apciK", JsonValue(static_cast<double>(c.apciK))}, {"apciT0Sec", JsonValue(static_cast<double>(c.apciT0Sec))},
        {"apciT1Sec", JsonValue(static_cast<double>(c.apciT1Sec))}, {"apciT2Sec", JsonValue(static_cast<double>(c.apciT2Sec))},
        {"apciT3Sec", JsonValue(static_cast<double>(c.apciT3Sec))}, {"apciW", JsonValue(static_cast<double>(c.apciW))},
        {"caSize", JsonValue(static_cast<double>(c.caSize))}, {"clockSyncOnConnect", JsonValue(c.clockSyncOnConnect)},
        {"commonAddress", JsonValue(static_cast<double>(c.commonAddress))}, {"cotSize", JsonValue(static_cast<double>(c.cotSize))},
        {"interrogationOnConnect", JsonValue(c.interrogationOnConnect)}, {"ioaSize", JsonValue(static_cast<double>(c.ioaSize))},
        {"originatorAddress", JsonValue(static_cast<double>(c.originatorAddress))}, {"reconnectMs", JsonValue(static_cast<double>(c.reconnectMs))},
        {"remoteAddress", JsonValue(c.remoteAddress)}, {"remotePort", JsonValue(static_cast<double>(c.remotePort))},
        {"timeoutMs", JsonValue(static_cast<double>(c.timeoutMs))},
    });
}

JsonValue tagsJson(const std::vector<TagConfig>& tags) {
    JsonValue::Array result;
    for (const auto& tag : tags) {
        result.emplace_back(JsonValue::Object{
            {"deviceDataType", JsonValue(tag.deviceDataType)}, {"ioa", JsonValue(static_cast<double>(tag.ioa))},
            {"qualifier", JsonValue(static_cast<double>(tag.qualifier))}, {"selectBeforeOperate", JsonValue(tag.selectBeforeOperate)},
            {"tagId", JsonValue(tag.tagId)}, {"visionType", JsonValue(tag.visionType)}, {"writable", JsonValue(tag.writable)},
        });
    }
    return JsonValue(std::move(result));
}

DesiredState parseDesiredStateDocument(const JsonValue& root, bool persisted = false) {
    if (!root.object()) throw std::runtime_error("root must be an object");
    const auto requireOnlyFields = [](const JsonValue& value, std::initializer_list<const char*> allowed, const std::string& context) {
        for (const auto& [key, unused] : *value.object()) {
            (void)unused;
            if (std::none_of(allowed.begin(), allowed.end(), [&](const char* allowedKey) { return key == allowedKey; })) throw std::runtime_error("unexpected field " + key + " in " + context);
        }
    };
    if (persisted) {
        requireOnlyFields(root, {"version", "gatewayTargetId", "controllerId", "controllerGeneration", "revision", "requestId", "devices", "hash"}, "desired state");
        const auto* version = root.find("version");
        if (!version || !version->number() || *version->number() != 2.0) throw std::runtime_error("state version must be 2");
    }
    else requireOnlyFields(root, {"gatewayTargetId", "controllerId", "controllerGeneration", "revision", "requestId", "devices"}, "desired state");
    const auto requireString = [](const JsonValue& object, const char* key) -> const std::string& {
        const auto* field = object.find(key);
        if (!field || !field->string() || field->string()->empty()) throw std::runtime_error(std::string(key) + " must be a non-empty string");
        return *field->string();
    };
    const auto requireSafeInteger = [](const JsonValue& object, const char* key) -> std::uint64_t {
        const auto* field = object.find(key);
        if (!field || !field->number() || std::floor(*field->number()) != *field->number() || *field->number() < 0 || *field->number() > 9007199254740991.0) throw std::runtime_error(std::string(key) + " must be a non-negative safe integer");
        return static_cast<std::uint64_t>(*field->number());
    };
    const JsonValue* values = root.find("devices");
    if (!values || !values->array()) throw std::runtime_error("root must contain a devices array");
    DesiredState result;
    result.gatewayTargetId = requireString(root, "gatewayTargetId");
    result.controllerId = requireString(root, "controllerId");
    result.controllerGeneration = requireSafeInteger(root, "controllerGeneration");
    result.revision = requireSafeInteger(root, "revision");
    result.requestId = requireString(root, "requestId");
    for (const auto& value : *values->array()) {
        if (!value.object()) throw std::runtime_error("device entry must be an object");
        requireOnlyFields(value, {"deviceId", "desiredState", "connection", "tags"}, "device entry");
        const auto* id = value.find("deviceId");
        const auto* desired = value.find("desiredState");
        if (!id || !id->string() || id->string()->empty()) throw std::runtime_error("deviceId must be a non-empty string");
        if (!desired || !desired->string() || (*desired->string() != "running" && *desired->string() != "stopped")) throw std::runtime_error("desiredState must be running or stopped for device " + *id->string());
        if (!value.find("connection") || !value.find("connection")->object()) throw std::runtime_error("connection must be an object for device " + *id->string());
        if (!value.find("tags") || !value.find("tags")->array()) throw std::runtime_error("tags must be an array for device " + *id->string());
        if (std::any_of(result.devices.begin(), result.devices.end(), [&](const auto& device) { return device.deviceId == *id->string(); })) throw std::runtime_error("duplicate deviceId: " + *id->string());
        const JsonValue& connectionValue = *value.find("connection");
        requireOnlyFields(connectionValue, {"remoteAddress", "remotePort", "commonAddress", "originatorAddress", "cotSize", "caSize", "ioaSize", "timeoutMs", "reconnectMs", "apciT0Sec", "apciT1Sec", "apciT2Sec", "apciT3Sec", "apciK", "apciW", "interrogationOnConnect", "clockSyncOnConnect"}, "connection for device " + *id->string());
        const auto requireDeviceString = [&](const JsonValue& object, const char* key) {
            const auto* field = object.find(key);
            if (!field || !field->string()) throw std::runtime_error(std::string(key) + " must be a string for device " + *id->string());
        };
        const auto requireInteger = [&](const JsonValue& object, const char* key) {
            const auto* field = object.find(key);
            if (!field || !field->number() || std::floor(*field->number()) != *field->number()) throw std::runtime_error(std::string(key) + " must be an integer for device " + *id->string());
        };
        const auto requireBoolean = [&](const JsonValue& object, const char* key) {
            const auto* field = object.find(key);
            if (!field || !field->boolean()) throw std::runtime_error(std::string(key) + " must be a boolean for device " + *id->string());
        };
        requireDeviceString(connectionValue, "remoteAddress");
        for (const char* key : {"remotePort", "commonAddress", "originatorAddress", "cotSize", "caSize", "ioaSize", "timeoutMs", "reconnectMs", "apciT0Sec", "apciT1Sec", "apciT2Sec", "apciT3Sec", "apciK", "apciW"}) requireInteger(connectionValue, key);
        requireBoolean(connectionValue, "interrogationOnConnect");
        requireBoolean(connectionValue, "clockSyncOnConnect");
        for (const auto& tagValue : *value.find("tags")->array()) {
            if (!tagValue.object()) throw std::runtime_error("tag entry must be an object for device " + *id->string());
            requireOnlyFields(tagValue, {"tagId", "visionType", "deviceDataType", "ioa", "qualifier", "writable", "selectBeforeOperate"}, "tag for device " + *id->string());
            requireDeviceString(tagValue, "tagId"); requireDeviceString(tagValue, "visionType"); requireDeviceString(tagValue, "deviceDataType");
            requireInteger(tagValue, "ioa"); requireInteger(tagValue, "qualifier");
            requireBoolean(tagValue, "writable"); requireBoolean(tagValue, "selectBeforeOperate");
        }
        const auto tags = parseTags(value);
        if (tags.size() != value.find("tags")->array()->size()) throw std::runtime_error("invalid tag entry for device " + *id->string());
        const auto connection = parseConnection(connectionValue);
        if (const auto error = validateConnectionConfig(connection)) throw std::runtime_error(*error + " for device " + *id->string());
        result.devices.push_back({*id->string(), tags, connection, *desired->string()});
    }
    result.hash = desiredStateHash(result);
    return result;
}

} // namespace

StateStore::StateStore(std::string path) : path_(std::move(path)) {}

void StateStore::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    desiredState_ = {};
    const std::filesystem::path path(path_);
    if (!std::filesystem::exists(path)) return;
    try {
        const JsonValue root = parseJson(readFile(path));
        const JsonValue* version = root.find("version");
        if (version && version->number() && *version->number() == 1.0) throw std::runtime_error("state version 1 is not supported; replace the state file with a version 2 desired-state document");
        const DesiredState desired = parseDesiredStateDocument(root, true);
        const auto* persistedHash = root.find("hash");
        if (!persistedHash || !persistedHash->string() || *persistedHash->string() != desired.hash) throw std::runtime_error("state hash does not match the persisted desired state");
        desiredState_ = desired;
    } catch (const std::exception& e) {
        desiredState_ = {};
        throw std::runtime_error("invalid state file '" + path_ + "': " + e.what());
    }
}

bool StateStore::replace(DesiredState desiredState) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string hash = desiredStateHash(desiredState);
    if (!desiredState.hash.empty() && desiredState.hash != hash) throw std::runtime_error("desired-state hash does not match its payload");
    desiredState.hash = hash;
    if (desiredState.revision < desiredState_.revision) throw std::runtime_error("desired-state revision is lower than the current revision");
    if (desiredState.revision == desiredState_.revision) {
        if (desiredState.hash == desiredState_.hash) return false;
        throw std::runtime_error("desired-state revision has a different payload");
    }
    const auto previous = desiredState_;
    desiredState_ = std::move(desiredState);
    try { saveLocked(); }
    catch (...) { desiredState_ = previous; throw; }
    return true;
}

DesiredState StateStore::desiredState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return desiredState_;
}

std::vector<PersistedDevice> StateStore::devices() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return desiredState_.devices;
}

void StateStore::saveLocked() const {
    JsonValue::Array devices;
    for (const auto& device : desiredState_.devices) {
        devices.emplace_back(JsonValue::Object{
            {"connection", connectionJson(device.connection)}, {"desiredState", JsonValue(device.desiredState)},
            {"deviceId", JsonValue(device.deviceId)}, {"tags", tagsJson(device.tags)},
        });
    }
    const std::string json = serializeJson(JsonValue(JsonValue::Object{{"controllerGeneration", JsonValue(static_cast<double>(desiredState_.controllerGeneration))}, {"controllerId", JsonValue(desiredState_.controllerId)}, {"devices", JsonValue(std::move(devices))}, {"gatewayTargetId", JsonValue(desiredState_.gatewayTargetId)}, {"hash", JsonValue(desiredState_.hash)}, {"requestId", JsonValue(desiredState_.requestId)}, {"revision", JsonValue(static_cast<double>(desiredState_.revision))}, {"version", JsonValue(2.0)}})) + "\n";
    const std::filesystem::path target(path_);
    const std::filesystem::path parent = target.parent_path().empty() ? std::filesystem::current_path() : target.parent_path();
    std::filesystem::create_directories(parent);
    const std::filesystem::path temporary = target.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot write temporary state file: " + temporary.string());
        output << json;
        output.flush();
        if (!output) throw std::runtime_error("cannot flush temporary state file: " + temporary.string());
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("cannot replace state file '" + path_ + "' (Windows error " + std::to_string(GetLastError()) + ")");
    }
#else
    std::error_code error;
    std::filesystem::rename(temporary, target, error);
    if (error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("cannot replace state file '" + path_ + "': " + error.message());
    }
#endif
}

DesiredState parseDesiredState(const JsonValue& root) {
    return parseDesiredStateDocument(root);
}

std::string desiredStateHash(const DesiredState& desiredState) {
    JsonValue::Array devices;
    for (const auto& device : desiredState.devices) devices.emplace_back(JsonValue(JsonValue::Object{{"connection", connectionJson(device.connection)}, {"desiredState", JsonValue(device.desiredState)}, {"deviceId", JsonValue(device.deviceId)}, {"tags", tagsJson(device.tags)}}));
    return sha256Hex(serializeJson(JsonValue(JsonValue::Object{{"controllerGeneration", JsonValue(static_cast<double>(desiredState.controllerGeneration))}, {"controllerId", JsonValue(desiredState.controllerId)}, {"devices", JsonValue(std::move(devices))}, {"gatewayTargetId", JsonValue(desiredState.gatewayTargetId)}, {"revision", JsonValue(static_cast<double>(desiredState.revision))}})));
}
