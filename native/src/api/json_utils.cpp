#include "json_utils.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {

class JsonParser {
public:
    explicit JsonParser(const std::string& input) : input_(input) {}

    JsonValue parse() {
        skipSpace();
        JsonValue value = parseValue();
        skipSpace();
        if (pos_ != input_.size()) fail("unexpected trailing content");
        return value;
    }

private:
    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error("invalid JSON at byte " + std::to_string(pos_) + ": " + message);
    }

    void skipSpace() {
        while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) ++pos_;
    }

    bool consume(char expected) {
        if (pos_ < input_.size() && input_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    void expect(char expected) {
        if (!consume(expected)) fail(std::string("expected '") + expected + "'");
    }

    JsonValue parseValue() {
        if (pos_ >= input_.size()) fail("expected value");
        const char c = input_[pos_];
        if (c == '{') return JsonValue(parseObject());
        if (c == '[') return JsonValue(parseArray());
        if (c == '"') return JsonValue(parseString());
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return JsonValue(parseNumber());
        if (input_.compare(pos_, 4, "true") == 0) { pos_ += 4; return JsonValue(true); }
        if (input_.compare(pos_, 5, "false") == 0) { pos_ += 5; return JsonValue(false); }
        if (input_.compare(pos_, 4, "null") == 0) { pos_ += 4; return JsonValue(); }
        fail("expected value");
    }

    JsonValue::Object parseObject() {
        JsonValue::Object object;
        expect('{');
        skipSpace();
        if (consume('}')) return object;
        while (true) {
            if (pos_ >= input_.size() || input_[pos_] != '"') fail("expected object key");
            std::string key = parseString();
            skipSpace();
            expect(':');
            skipSpace();
            if (!object.emplace(std::move(key), parseValue()).second) fail("duplicate object key");
            skipSpace();
            if (consume('}')) return object;
            expect(',');
            skipSpace();
        }
    }

    JsonValue::Array parseArray() {
        JsonValue::Array array;
        expect('[');
        skipSpace();
        if (consume(']')) return array;
        while (true) {
            array.push_back(parseValue());
            skipSpace();
            if (consume(']')) return array;
            expect(',');
            skipSpace();
        }
    }

    static void appendUtf8(std::string& result, unsigned codePoint) {
        if (codePoint <= 0x7f) result.push_back(static_cast<char>(codePoint));
        else if (codePoint <= 0x7ff) {
            result.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
            result.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
        } else {
            result.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
            result.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
        }
    }

    unsigned parseHex4() {
        if (pos_ + 4 > input_.size()) fail("incomplete unicode escape");
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = input_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value += static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') value += static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value += static_cast<unsigned>(c - 'A' + 10);
            else fail("invalid unicode escape");
        }
        return value;
    }

    std::string parseString() {
        expect('"');
        std::string result;
        while (pos_ < input_.size()) {
            const unsigned char c = static_cast<unsigned char>(input_[pos_++]);
            if (c == '"') return result;
            if (c < 0x20) fail("control character in string");
            if (c != '\\') { result.push_back(static_cast<char>(c)); continue; }
            if (pos_ >= input_.size()) fail("incomplete escape");
            switch (input_[pos_++]) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u': appendUtf8(result, parseHex4()); break;
                default: fail("invalid escape");
            }
        }
        fail("unterminated string");
    }

    double parseNumber() {
        const std::size_t start = pos_;
        consume('-');
        if (consume('0')) {
            if (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) fail("leading zero in number");
        } else {
            if (pos_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[pos_]))) fail("invalid number");
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
        }
        if (consume('.')) {
            if (pos_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[pos_]))) fail("invalid fraction");
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
        }
        if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) ++pos_;
            if (pos_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[pos_]))) fail("invalid exponent");
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
        }
        try {
            const double value = std::stod(input_.substr(start, pos_ - start));
            if (!std::isfinite(value)) fail("number is out of range");
            return value;
        } catch (const std::invalid_argument&) {
            fail("invalid number");
        } catch (const std::out_of_range&) {
            fail("number is out of range");
        }
    }

    const std::string& input_;
    std::size_t pos_ = 0;
};

void serialize(std::ostringstream& out, const JsonValue& value) {
    if (const auto* object = value.object()) {
        out << '{';
        bool first = true;
        for (const auto& [key, child] : *object) {
            if (!first) out << ',';
            first = false;
            out << '"' << jsonEscape(key) << "\":";
            serialize(out, child);
        }
        out << '}';
    } else if (const auto* array = value.array()) {
        out << '[';
        for (std::size_t i = 0; i < array->size(); ++i) {
            if (i) out << ',';
            serialize(out, (*array)[i]);
        }
        out << ']';
    } else if (const auto* string = value.string()) out << '"' << jsonEscape(*string) << '"';
    else if (const auto* number = value.number()) out << std::setprecision(17) << *number;
    else if (const auto* boolean = value.boolean()) out << (*boolean ? "true" : "false");
    else out << "null";
}

const JsonValue* rootField(const std::string& json, const std::string& key) {
    static thread_local JsonValue root;
    try {
        root = parseJson(json);
        return root.find(key);
    } catch (...) {
        return nullptr;
    }
}

int integer(const JsonValue* value, int fallback) {
    if (!value || !value->number() || std::floor(*value->number()) != *value->number() ||
        *value->number() < std::numeric_limits<int>::min() || *value->number() > std::numeric_limits<int>::max()) return fallback;
    return static_cast<int>(*value->number());
}

std::string stringValue(const JsonValue* value, const std::string& fallback) {
    return value && value->string() ? *value->string() : fallback;
}

bool boolValue(const JsonValue* value, bool fallback) {
    return value && value->boolean() ? *value->boolean() : fallback;
}

} // namespace

const JsonValue::Object* JsonValue::object() const { return std::get_if<Object>(&value_); }
const JsonValue::Array* JsonValue::array() const { return std::get_if<Array>(&value_); }
const std::string* JsonValue::string() const { return std::get_if<std::string>(&value_); }
const double* JsonValue::number() const { return std::get_if<double>(&value_); }
const bool* JsonValue::boolean() const { return std::get_if<bool>(&value_); }
const JsonValue* JsonValue::find(const std::string& key) const {
    const auto* values = object();
    if (!values) return nullptr;
    const auto it = values->find(key);
    return it == values->end() ? nullptr : &it->second;
}

JsonValue parseJson(const std::string& json) { return JsonParser(json).parse(); }
std::string serializeJson(const JsonValue& value) { std::ostringstream out; serialize(out, value); return out.str(); }

std::string trim(std::string value) {
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char c) { return !isSpace(static_cast<unsigned char>(c)); }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](char c) { return !isSpace(static_cast<unsigned char>(c)); }).base(), value.end());
    return value;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string jsonEscape(const std::string& value) {
    std::ostringstream out;
    for (unsigned char c : value) {
        switch (c) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
                else out << static_cast<char>(c);
        }
    }
    return out.str();
}

std::string jsonStringField(const std::string& json, const std::string& key, const std::string& fallback) {
    return stringValue(rootField(json, key), fallback);
}

int jsonIntField(const std::string& json, const std::string& key, int fallback) { return integer(rootField(json, key), fallback); }
double jsonNumberField(const std::string& json, const std::string& key, double fallback) {
    const auto* value = rootField(json, key);
    return value && value->number() ? *value->number() : fallback;
}
std::optional<double> jsonWriteValueField(const std::string& json, const std::string& key) {
    const auto* value = rootField(json, key);
    if (value && value->number()) return *value->number();
    if (value && value->boolean()) return *value->boolean() ? 1.0 : 0.0;
    return std::nullopt;
}
bool jsonBoolField(const std::string& json, const std::string& key, bool fallback) { return boolValue(rootField(json, key), fallback); }

ConnectionConfig parseConnection(const JsonValue& value) {
    const JsonValue* source = value.find("connection");
    if (!source) source = &value;
    ConnectionConfig connection;
    connection.remoteAddress = stringValue(source->find("remoteAddress"), connection.remoteAddress);
    connection.remotePort = integer(source->find("remotePort"), connection.remotePort);
    connection.commonAddress = integer(source->find("commonAddress"), connection.commonAddress);
    connection.originatorAddress = integer(source->find("originatorAddress"), connection.originatorAddress);
    connection.cotSize = integer(source->find("cotSize"), connection.cotSize);
    connection.caSize = integer(source->find("caSize"), connection.caSize);
    connection.ioaSize = integer(source->find("ioaSize"), connection.ioaSize);
    connection.timeoutMs = integer(source->find("timeoutMs"), connection.timeoutMs);
    connection.reconnectMs = integer(source->find("reconnectMs"), connection.reconnectMs);
    connection.apciT0Sec = std::max(1, integer(source->find("apciT0Sec"), connection.apciT0Sec));
    connection.apciT1Sec = std::max(1, integer(source->find("apciT1Sec"), connection.apciT1Sec));
    connection.apciT2Sec = std::max(1, integer(source->find("apciT2Sec"), connection.apciT2Sec));
    connection.apciT3Sec = std::max(1, integer(source->find("apciT3Sec"), connection.apciT3Sec));
    connection.apciK = std::max(1, integer(source->find("apciK"), connection.apciK));
    connection.apciW = std::max(1, integer(source->find("apciW"), connection.apciW));
    connection.interrogationOnConnect = boolValue(source->find("interrogationOnConnect"), connection.interrogationOnConnect);
    connection.clockSyncOnConnect = boolValue(source->find("clockSyncOnConnect"), connection.clockSyncOnConnect);
    return connection;
}

std::optional<std::string> validateConnectionConfig(const ConnectionConfig& connection) {
    if (connection.remoteAddress.empty()) return "remoteAddress must not be empty";
    if (connection.remotePort < 1 || connection.remotePort > 65535) return "remotePort must be between 1 and 65535";
    if (connection.commonAddress < 0 || connection.commonAddress > 65535) return "commonAddress must be between 0 and 65535";
    if (connection.originatorAddress < 0 || connection.originatorAddress > 255) return "originatorAddress must be between 0 and 255";
    if (connection.cotSize < 1 || connection.cotSize > 2) return "cotSize must be 1 or 2";
    if (connection.caSize < 1 || connection.caSize > 2) return "caSize must be 1 or 2";
    if (connection.ioaSize < 1 || connection.ioaSize > 3) return "ioaSize must be between 1 and 3";
    if (connection.timeoutMs < 250 || connection.timeoutMs > 20000) return "timeoutMs must be between 250 and 20000";
    if (connection.reconnectMs < 250 || connection.reconnectMs > 3600000) return "reconnectMs must be between 250 and 3600000";
    if (connection.apciT0Sec < 1 || connection.apciT1Sec < 1 || connection.apciT2Sec < 1 || connection.apciT3Sec < 1) return "APCI timers must be positive";
    if (connection.apciK < 1 || connection.apciW < 1 || connection.apciW > connection.apciK) return "APCI windows must satisfy 1 <= w <= k";
    return std::nullopt;
}

std::vector<TagConfig> parseTags(const JsonValue& value) {
    std::vector<TagConfig> tags;
    const JsonValue* tagsValue = value.find("tags");
    if (!tagsValue || !tagsValue->array()) return tags;
    for (const JsonValue& item : *tagsValue->array()) {
        if (!item.object()) continue;
        TagConfig tag;
        tag.tagId = stringValue(item.find("tagId"), "");
        tag.ioa = integer(item.find("ioa"), 0);
        tag.visionType = stringValue(item.find("visionType"), "Number");
        tag.deviceDataType = stringValue(item.find("deviceDataType"), "M_ME_NC_1");
        tag.writable = boolValue(item.find("writable"), false);
        tag.selectBeforeOperate = boolValue(item.find("selectBeforeOperate"), false);
        tag.qualifier = integer(item.find("qualifier"), 0);
        if (!tag.tagId.empty() && tag.ioa >= 0) tags.push_back(tag);
    }
    return tags;
}

ConnectionConfig parseConnection(const std::string& body) { return parseConnection(parseJson(body)); }
std::vector<TagConfig> parseTags(const std::string& body) { return parseTags(parseJson(body)); }

std::string statusEvent(const std::string& deviceId, bool running) {
    return statusEvent(deviceId, running, running ? "running" : "off");
}

std::string statusEvent(const std::string& deviceId, bool connected, const std::string& state, const std::string& lastError) {
    std::ostringstream out;
    out << "{\"type\":\"status\",\"deviceId\":\"" << jsonEscape(deviceId)
        << "\",\"gatewayConnected\":true,\"iec104Connected\":" << (connected ? "true" : "false")
        << ",\"state\":\"" << jsonEscape(state) << "\",\"lastError\":\"" << jsonEscape(lastError) << "\"}";
    return out.str();
}

std::string qualityJson(uint8_t quality) {
    std::ostringstream out;
    out << "{\"invalid\":" << ((quality & 0x80) ? "true" : "false")
        << ",\"notTopical\":" << ((quality & 0x40) ? "true" : "false")
        << ",\"substituted\":" << ((quality & 0x20) ? "true" : "false")
        << ",\"blocked\":" << ((quality & 0x10) ? "true" : "false")
        << ",\"overflow\":" << ((quality & 0x01) ? "true" : "false") << "}";
    return out.str();
}

std::string valueEvent(const std::string& deviceId, const TagConfig& tag, double value) {
    return valueEvent(deviceId, tag.ioa, tag.tagId, tag.deviceDataType, value, 0, 0, 3);
}

std::string valueEvent(const std::string& deviceId, int ioa, const std::string& tagId, const std::string& asduType, double value, uint8_t quality, uint64_t timestampMs, int cot) {
    auto nowMillis = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    uint64_t millis = timestampMs > 0 ? timestampMs : static_cast<uint64_t>(nowMillis);
    std::ostringstream out;
    out << "{\"type\":\"value\",\"deviceId\":\"" << jsonEscape(deviceId)
        << "\",\"tagId\":\"" << jsonEscape(tagId) << "\",\"ioa\":" << ioa
        << ",\"asduType\":\"" << jsonEscape(asduType) << "\",\"value\":" << value
        << ",\"quality\":" << qualityJson(quality) << ",\"cot\":" << cot << ",\"timestamp\":" << millis << "}";
    return out.str();
}

std::string writeResultEvent(const std::string& deviceId, const std::string& requestId, const TagConfig& tag) {
    return writeResultEvent(deviceId, requestId, tag, true, "activation-confirmed", "", "execute", 7);
}

std::string writeResultEvent(const std::string& deviceId, const std::string& requestId, const TagConfig& tag, bool ok, const std::string& cause, const std::string& error, const std::string& phase, int cot) {
    std::ostringstream out;
    out << "{\"type\":\"write-result\",\"deviceId\":\"" << jsonEscape(deviceId)
        << "\",\"requestId\":\"" << jsonEscape(requestId) << "\",\"ioa\":" << tag.ioa
        << ",\"deviceDataType\":\"" << jsonEscape(tag.deviceDataType) << "\",\"ok\":" << (ok ? "true" : "false");
    if (!cause.empty()) out << ",\"cause\":\"" << jsonEscape(cause) << "\"";
    if (!error.empty()) out << ",\"error\":\"" << jsonEscape(error) << "\"";
    if (!phase.empty()) out << ",\"phase\":\"" << jsonEscape(phase) << "\"";
    if (cot > 0) out << ",\"cot\":" << cot;
    out << "}";
    return out.str();
}
