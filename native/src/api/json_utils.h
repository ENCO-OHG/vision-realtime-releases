#pragma once

#include "gateway_models.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

class JsonValue {
public:
    using Object = std::map<std::string, JsonValue>;
    using Array = std::vector<JsonValue>;
    using Value = std::variant<std::nullptr_t, bool, double, std::string, Object, Array>;

    JsonValue() = default;
    explicit JsonValue(Value value) : value_(std::move(value)) {}

    const Object* object() const;
    const Array* array() const;
    const std::string* string() const;
    const double* number() const;
    const bool* boolean() const;
    const JsonValue* find(const std::string& key) const;

private:
    Value value_ = nullptr;
};

JsonValue parseJson(const std::string& json);
std::string serializeJson(const JsonValue& value);

std::string trim(std::string value);
std::string lower(std::string value);
std::string jsonEscape(const std::string& value);
std::string jsonStringField(const std::string& json, const std::string& key, const std::string& fallback = "");
int jsonIntField(const std::string& json, const std::string& key, int fallback = 0);
double jsonNumberField(const std::string& json, const std::string& key, double fallback = 0.0);
std::optional<double> jsonWriteValueField(const std::string& json, const std::string& key);
bool jsonBoolField(const std::string& json, const std::string& key, bool fallback = false);
ConnectionConfig parseConnection(const std::string& body);
std::vector<TagConfig> parseTags(const std::string& body);
ConnectionConfig parseConnection(const JsonValue& value);
std::optional<std::string> validateConnectionConfig(const ConnectionConfig& connection);
std::vector<TagConfig> parseTags(const JsonValue& value);
std::string statusEvent(const std::string& deviceId, bool running);
std::string statusEvent(const std::string& deviceId, bool connected, const std::string& state, const std::string& lastError = "");
std::string qualityJson(uint8_t quality);
std::string valueEvent(const std::string& deviceId, const TagConfig& tag, double value);
std::string valueEvent(const std::string& deviceId, int ioa, const std::string& tagId, const std::string& asduType, double value, uint8_t quality, uint64_t timestampMs = 0, int cot = 0);
std::string writeResultEvent(const std::string& deviceId, const std::string& requestId, const TagConfig& tag);
std::string writeResultEvent(const std::string& deviceId, const std::string& requestId, const TagConfig& tag, bool ok, const std::string& cause, const std::string& error, const std::string& phase, int cot);
