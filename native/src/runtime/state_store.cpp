#include "state_store.h"

#include "json_utils.h"

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

} // namespace

StateStore::StateStore(std::string path) : path_(std::move(path)) {}

void StateStore::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    devices_.clear();
    const std::filesystem::path path(path_);
    if (!std::filesystem::exists(path)) return;
    try {
        const JsonValue root = parseJson(readFile(path));
        const JsonValue* version = root.find("version");
        const JsonValue* values = root.find("devices");
        if (!root.object() || !version || !version->number() || *version->number() != 1.0) throw std::runtime_error("unsupported or missing state version");
        if (!values || !values->array()) throw std::runtime_error("root must contain a devices array");
        for (const auto& value : *values->array()) {
            if (!value.object()) throw std::runtime_error("device entry must be an object");
            const auto* id = value.find("deviceId");
            const auto* desired = value.find("desiredRunning");
            if (!id || !id->string() || id->string()->empty()) throw std::runtime_error("deviceId must be a non-empty string");
            if (!desired || !desired->boolean()) throw std::runtime_error("desiredRunning must be a boolean for device " + *id->string());
            if (!value.find("connection") || !value.find("connection")->object()) throw std::runtime_error("connection must be an object for device " + *id->string());
            if (!value.find("tags") || !value.find("tags")->array()) throw std::runtime_error("tags must be an array for device " + *id->string());
            if (std::any_of(devices_.begin(), devices_.end(), [&](const auto& device) { return device.deviceId == *id->string(); })) throw std::runtime_error("duplicate deviceId: " + *id->string());
            const JsonValue& connectionValue = *value.find("connection");
            const auto requireString = [&](const JsonValue& object, const char* key) {
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
            requireString(connectionValue, "remoteAddress");
            for (const char* key : {"remotePort", "commonAddress", "originatorAddress", "cotSize", "caSize", "ioaSize", "timeoutMs", "reconnectMs", "apciT0Sec", "apciT1Sec", "apciT2Sec", "apciT3Sec", "apciK", "apciW"}) {
                requireInteger(connectionValue, key);
            }
            requireBoolean(connectionValue, "interrogationOnConnect");
            requireBoolean(connectionValue, "clockSyncOnConnect");
            for (const auto& tagValue : *value.find("tags")->array()) {
                if (!tagValue.object()) throw std::runtime_error("tag entry must be an object for device " + *id->string());
                requireString(tagValue, "tagId");
                requireString(tagValue, "visionType");
                requireString(tagValue, "deviceDataType");
                requireInteger(tagValue, "ioa");
                requireInteger(tagValue, "qualifier");
                requireBoolean(tagValue, "writable");
                requireBoolean(tagValue, "selectBeforeOperate");
            }
            const auto tags = parseTags(value);
            if (tags.size() != value.find("tags")->array()->size()) throw std::runtime_error("invalid tag entry for device " + *id->string());
            const auto connection = parseConnection(connectionValue);
            if (const auto error = validateConnectionConfig(connection)) throw std::runtime_error(*error + " for device " + *id->string());
            devices_.push_back({*id->string(), tags, connection, *desired->boolean()});
        }
    } catch (const std::exception& e) {
        devices_.clear();
        throw std::runtime_error("invalid state file '" + path_ + "': " + e.what());
    }
}

void StateStore::updateConfig(const std::string& deviceId, const std::vector<TagConfig>& tags, const ConnectionConfig& connection) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto previous = devices_;
    auto it = std::find_if(devices_.begin(), devices_.end(), [&](const auto& device) { return device.deviceId == deviceId; });
    if (it == devices_.end()) devices_.push_back({deviceId, tags, connection, false});
    else { it->tags = tags; it->connection = connection; }
    try { saveLocked(); }
    catch (...) { devices_ = previous; throw; }
}

void StateStore::updateDesiredRunning(const std::string& deviceId, bool desiredRunning) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(devices_.begin(), devices_.end(), [&](const auto& device) { return device.deviceId == deviceId; });
    if (it == devices_.end()) throw std::runtime_error("device is not configured: " + deviceId);
    const bool previous = it->desiredRunning;
    it->desiredRunning = desiredRunning;
    try { saveLocked(); }
    catch (...) { it->desiredRunning = previous; throw; }
}

std::vector<PersistedDevice> StateStore::devices() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return devices_;
}

void StateStore::saveLocked() const {
    JsonValue::Array devices;
    for (const auto& device : devices_) {
        devices.emplace_back(JsonValue::Object{
            {"connection", connectionJson(device.connection)}, {"desiredRunning", JsonValue(device.desiredRunning)},
            {"deviceId", JsonValue(device.deviceId)}, {"tags", tagsJson(device.tags)},
        });
    }
    const std::string json = serializeJson(JsonValue(JsonValue::Object{{"devices", JsonValue(std::move(devices))}, {"version", JsonValue(1.0)}})) + "\n";
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
