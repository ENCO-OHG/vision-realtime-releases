#include "device_registry.h"
#include "iec104_backend.h"
#include "json_utils.h"
#ifdef VISION_ONE_IEC104_WITH_LIB60870
#include "lib60870_backend.h"
#else
#include "mock_backend.h"
#endif
#include "socket_utils.h"
#include "state_store.h"
#include "websocket_broadcaster.h"
#include "gateway_version.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <cstdlib>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#ifdef VISION_ONE_IEC104_WITH_LIB60870
bool visionOneIec104Lib60870Probe();
#endif

namespace {

std::atomic<bool> g_running{true};
volatile std::sig_atomic_t g_shutdownRequested = 0;
DeviceRegistry g_devices;
WebSocketBroadcaster g_broadcaster;
GatewayConfig g_config;
std::unique_ptr<Iec104Backend> g_backend;
std::unique_ptr<StateStore> g_stateStore;
std::mutex g_logMutex;
std::mutex g_clientsMutex;
std::vector<std::thread> g_clientThreads;
std::mutex g_deviceLifecycleMutex;
std::map<std::string, std::shared_ptr<std::mutex>> g_deviceLifecycleLocks;

constexpr std::uintmax_t LOG_MAX_BYTES = 1024 * 1024;
constexpr int LOG_MAX_FILES = 5;
constexpr const char* LOG_FILE_NAME = "vision-realtime.log";
constexpr int MAX_REQUEST_BODY_BYTES = 1024 * 1024;

std::shared_ptr<std::mutex> deviceLifecycleLock(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(g_deviceLifecycleMutex);
    auto& result = g_deviceLifecycleLocks[deviceId];
    if (!result) result = std::make_shared<std::mutex>();
    return result;
}

enum class CliCommand {
    Run,
    Version,
    Status,
    Doctor,
    Service,
    Help,
};

struct CliOptions {
    CliCommand command = CliCommand::Run;
    GatewayConfig gateway;
    std::string configPath;
    std::optional<std::string> listenAddress;
    std::optional<int> port;
    std::optional<std::string> authToken;
    std::optional<std::string> logLevel;
    std::optional<std::string> logDir;
    std::optional<std::string> stateFile;
    bool noColor = false;
    bool json = false;
    std::string serviceAction;
};

std::string backendName() {
#ifdef VISION_ONE_IEC104_WITH_LIB60870
    return "lib60870";
#else
    return "mock";
#endif
}

std::string boolJson(bool value) {
    return value ? "true" : "false";
}

std::string nowIso() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

bool colorEnabled() {
    static bool enabled = [] {
#ifdef _WIN32
        char* value = nullptr;
        std::size_t length = 0;
        errno_t err = _dupenv_s(&value, &length, "NO_COLOR");
        bool disabled = err == 0 && value != nullptr;
        if (value) free(value);
        return !disabled;
#else
        return std::getenv("NO_COLOR") == nullptr;
#endif
    }();
    return enabled && !g_config.noColor;
}

std::string logColorFor(const std::string& message) {
    if (message.find("<unmatched>") != std::string::npos) return "\033[35m";
    if (message.find("FAILED") != std::string::npos ||
        message.find("failed") != std::string::npos ||
        message.find("sent=false") != std::string::npos ||
        message.find("not-connected") != std::string::npos ||
        message.find("send-failed") != std::string::npos) return "\033[31m";
    if (message.find(" sent=true") != std::string::npos ||
        message.find(" running") != std::string::npos ||
        message.find("link probe: OK") != std::string::npos) return "\033[32m";
    if (message.find("IEC104 value") != std::string::npos) return "\033[36m";
    if (message.find("IEC104 asdu") != std::string::npos ||
        message.find("connecting") != std::string::npos ||
        message.find("config") != std::string::npos ||
        message.find("interrogation") != std::string::npos ||
        message.find("clock sync") != std::string::npos) return "\033[33m";
    return "";
}

void log(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    const std::string timestamp = nowIso();
    const std::string plainLine = timestamp + " " + message;

    if (!g_config.logDir.empty()) {
        try {
            const std::filesystem::path logDir(g_config.logDir);
            std::filesystem::create_directories(logDir);
            const auto current = logDir / LOG_FILE_NAME;
            if (std::filesystem::exists(current) && std::filesystem::file_size(current) >= LOG_MAX_BYTES) {
                for (int i = LOG_MAX_FILES - 1; i >= 1; --i) {
                    const auto from = logDir / (std::string("vision-realtime.") + std::to_string(i) + ".log");
                    const auto to = logDir / (std::string("vision-realtime.") + std::to_string(i + 1) + ".log");
                    if (std::filesystem::exists(from)) {
                        if (std::filesystem::exists(to)) std::filesystem::remove(to);
                        std::filesystem::rename(from, to);
                    }
                }
                const auto first = logDir / "vision-realtime.1.log";
                if (std::filesystem::exists(first)) std::filesystem::remove(first);
                std::filesystem::rename(current, first);
            }
            std::ofstream file(current, std::ios::app);
            file << plainLine << '\n';
        } catch (...) {
            // Logging must never break gateway operation.
        }
    }

    if (!colorEnabled()) {
        std::cout << plainLine << std::endl;
        return;
    }
    const std::string color = logColorFor(message);
    std::cout << "\033[90m" << timestamp << "\033[0m ";
    if (!color.empty()) std::cout << color;
    std::cout << message;
    if (!color.empty()) std::cout << "\033[0m";
    std::cout << std::endl;
}

std::string base64Encode(const uint8_t* data, std::size_t len) {
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);
        out.push_back(table[(n >> 18) & 63]);
        out.push_back(table[(n >> 12) & 63]);
        out.push_back(i + 1 < len ? table[(n >> 6) & 63] : '=');
        out.push_back(i + 2 < len ? table[n & 63] : '=');
    }
    return out;
}

uint32_t rol(uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

std::array<uint8_t, 20> sha1(const std::string& input) {
    std::vector<uint8_t> msg(input.begin(), input.end());
    uint64_t bitLen = static_cast<uint64_t>(msg.size()) * 8;
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) msg.push_back(0);
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<uint8_t>((bitLen >> (i * 8)) & 0xff));

    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xefcdab89;
    uint32_t h2 = 0x98badcfe;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xc3d2e1f0;

    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[80]{};
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(msg[chunk + i * 4]) << 24) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 3]));
        }
        for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f = 0;
            uint32_t k = 0;
            if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5a827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
            else { f = b ^ c ^ d; k = 0xca62c1d6; }
            uint32_t temp = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = temp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    std::array<uint8_t, 20> digest{};
    std::array<uint32_t, 5> h{h0, h1, h2, h3, h4};
    for (std::size_t i = 0; i < h.size(); ++i) {
        digest[i * 4] = static_cast<uint8_t>((h[i] >> 24) & 0xff);
        digest[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xff);
        digest[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xff);
        digest[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xff);
    }
    return digest;
}

std::string websocketAccept(const std::string& key) {
    auto digest = sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    return base64Encode(digest.data(), digest.size());
}

void broadcast(const std::string& event) {
    g_broadcaster.broadcast(event);
}

void valueLoop() {
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        for (const auto& event : g_backend->pollEvents()) {
            broadcast(event);
        }
    }
}

void broadcastResultEvents(const BackendResult& result) {
    for (const auto& event : result.events) broadcast(event);
}

std::string httpResponse(int status, const std::string& body) {
    std::string statusText = status == 200 ? "OK" : status == 400 ? "Bad Request" : status == 401 ? "Unauthorized" : status == 404 ? "Not Found" : status == 409 ? "Conflict" : "Internal Server Error";
    std::ostringstream out;
    out << "HTTP/1.1 " << status << " " << statusText << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    return out.str();
}

bool isAuthorized(const HttpRequest& req) {
    if (g_config.authToken.empty()) return true;
    auto it = req.headers.find("authorization");
    return it != req.headers.end() && it->second == "Bearer " + g_config.authToken;
}

std::optional<HttpRequest> readRequest(socket_t socket) {
    std::string data;
    std::array<char, 4096> buffer{};
    while (data.find("\r\n\r\n") == std::string::npos) {
#ifdef _WIN32
        int received = recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
        ssize_t received = recv(socket, buffer.data(), buffer.size(), 0);
#endif
        if (received <= 0) return std::nullopt;
        data.append(buffer.data(), static_cast<std::size_t>(received));
        if (data.size() > 1024 * 1024) return std::nullopt;
    }

    auto headerEnd = data.find("\r\n\r\n");
    std::string header = data.substr(0, headerEnd);
    std::string body = data.substr(headerEnd + 4);
    std::istringstream in(header);
    std::string requestLine;
    std::getline(in, requestLine);
    requestLine = trim(requestLine);
    std::istringstream requestLineIn(requestLine);
    HttpRequest req;
    requestLineIn >> req.method >> req.path;

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        req.headers[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
    }

    int contentLength = 0;
    auto cl = req.headers.find("content-length");
    if (cl != req.headers.end()) contentLength = std::stoi(cl->second);
    if (contentLength < 0 || contentLength > MAX_REQUEST_BODY_BYTES) throw std::runtime_error("request body exceeds 1 MiB limit");
    while (static_cast<int>(body.size()) < contentLength) {
#ifdef _WIN32
        int received = recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
        ssize_t received = recv(socket, buffer.data(), buffer.size(), 0);
#endif
        if (received <= 0) return std::nullopt;
        body.append(buffer.data(), static_cast<std::size_t>(received));
    }
    if (contentLength > 0 && static_cast<int>(body.size()) > contentLength) body.resize(static_cast<std::size_t>(contentLength));
    req.body = body;
    return req;
}

bool handleWebSocket(socket_t socket, const HttpRequest& req) {
    if (req.path.rfind("/api/v1/events", 0) != 0 || !isAuthorized(req)) return false;
    auto keyIt = req.headers.find("sec-websocket-key");
    if (keyIt == req.headers.end()) return false;
    std::ostringstream response;
    response << "HTTP/1.1 101 Switching Protocols\r\n"
             << "Upgrade: websocket\r\n"
             << "Connection: Upgrade\r\n"
             << "Sec-WebSocket-Accept: " << websocketAccept(keyIt->second) << "\r\n\r\n";
    if (!sendAll(socket, response.str())) return false;

    g_broadcaster.addClient(socket);
    g_broadcaster.sendText(socket, std::string("{\"type\":\"gateway\",\"ok\":true,\"version\":\"") + jsonEscape(VISION_ONE_IEC104_GATEWAY_VERSION) + "\"}");

    std::array<char, 2> frameHeader{};
    while (g_running) {
#ifdef _WIN32
        int received = recv(socket, frameHeader.data(), static_cast<int>(frameHeader.size()), 0);
#else
        ssize_t received = recv(socket, frameHeader.data(), frameHeader.size(), 0);
#endif
        if (received <= 0) {
#ifdef _WIN32
            if (g_running && WSAGetLastError() == WSAETIMEDOUT) continue;
#else
            if (g_running && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
#endif
            break;
        }
        if ((static_cast<unsigned char>(frameHeader[0]) & 0x0f) == 0x8) break;
    }
    g_broadcaster.removeClient(socket);
    closeSocket(socket);
    return true;
}

std::string handleApi(const HttpRequest& req) {
    if (req.method == "GET" && req.path == "/api/v1/health") {
        return httpResponse(200, std::string("{\"ok\":true,\"service\":\"vision-realtime\",\"version\":\"") + jsonEscape(VISION_ONE_IEC104_GATEWAY_VERSION) + "\"}");
    }
    if (req.method == "GET" && req.path == "/api/v1/version") {
        return httpResponse(200, std::string("{\"ok\":true,\"version\":\"") + jsonEscape(VISION_ONE_IEC104_GATEWAY_VERSION) + "\",\"backend\":\""
#ifdef VISION_ONE_IEC104_WITH_LIB60870
            "lib60870"
#else
            "mock"
#endif
            "\"}");
    }
    if (!isAuthorized(req)) return httpResponse(401, "{\"ok\":false,\"error\":\"unauthorized\"}");

    std::smatch match;
    if (!std::regex_match(req.path, match, std::regex("^/api/v1/devices/([^/]+)/?(config|start|stop|status|write|interrogate)$"))) {
        return httpResponse(404, "{\"ok\":false,\"error\":\"not-found\"}");
    }
    std::string deviceId = match[1].str();
    std::string action = match[2].str();

    if (action == "config" && req.method == "POST") {
        std::lock_guard<std::mutex> lifecycleLock(*deviceLifecycleLock(deviceId));
        const JsonValue body = parseJson(req.body);
        if (!body.object()) return httpResponse(400, "{\"ok\":false,\"error\":\"invalid-config-body\"}");
        auto tags = parseTags(body);
        auto connection = parseConnection(body);
        if (const auto error = validateConnectionConfig(connection)) return httpResponse(400, "{\"ok\":false,\"error\":\"" + jsonEscape(*error) + "\"}");
        g_stateStore->updateConfig(deviceId, tags, connection);
        auto result = g_backend->configure(deviceId, tags, connection);
        broadcastResultEvents(result);
        return httpResponse(result.status, result.body);
    }

    if (action == "start" && req.method == "POST") {
        std::lock_guard<std::mutex> lifecycleLock(*deviceLifecycleLock(deviceId));
        g_stateStore->updateDesiredRunning(deviceId, true);
        auto result = g_backend->start(deviceId);
        broadcastResultEvents(result);
        return httpResponse(result.status, result.body);
    }

    if (action == "stop" && req.method == "POST") {
        std::lock_guard<std::mutex> lifecycleLock(*deviceLifecycleLock(deviceId));
        g_stateStore->updateDesiredRunning(deviceId, false);
        auto result = g_backend->stop(deviceId);
        broadcastResultEvents(result);
        return httpResponse(result.status, result.body);
    }

    if (action == "status" && req.method == "GET") {
        auto result = g_backend->status(deviceId);
        return httpResponse(result.status, result.body);
    }

    if (action == "write" && req.method == "POST") {
        std::string requestId = jsonStringField(req.body, "requestId", "");
        std::string tagId = jsonStringField(req.body, "tagId", "");
        int ioa = jsonIntField(req.body, "ioa", 0);
        auto value = jsonWriteValueField(req.body, "value");
        if (!value) return httpResponse(400, "{\"ok\":false,\"requestId\":\"" + jsonEscape(requestId) + "\",\"error\":\"invalid-value\"}");
        bool select = jsonBoolField(req.body, "select", false);
        auto result = g_backend->write(deviceId, {.requestId = requestId, .tagId = tagId, .ioa = ioa, .value = *value, .select = select});
        broadcastResultEvents(result);
        return httpResponse(result.status, result.body);
    }

    if (action == "interrogate" && req.method == "POST") {
        int qualifier = jsonIntField(req.body, "qualifier", 20);
        auto result = g_backend->interrogate(deviceId, qualifier);
        broadcastResultEvents(result);
        return httpResponse(result.status, result.body);
    }

    return httpResponse(404, "{\"ok\":false,\"error\":\"not-found\"}");
}

void handleClient(socket_t socket) {
    try {
        auto req = readRequest(socket);
        if (!req) {
            closeSocket(socket);
            return;
        }
        auto upgrade = req->headers.find("upgrade");
        if (upgrade != req->headers.end() && lower(upgrade->second) == "websocket") {
            if (handleWebSocket(socket, *req)) return;
            sendAll(socket, "HTTP/1.1 401 Unauthorized\r\nConnection: close\r\n\r\n");
            closeSocket(socket);
            return;
        }
        sendAll(socket, handleApi(*req));
    } catch (const std::exception& e) {
        const std::string message = e.what();
        const int status = message.rfind("invalid JSON", 0) == 0 ? 400 : 500;
        sendAll(socket, httpResponse(status, "{\"ok\":false,\"error\":\"" + jsonEscape(message) + "\"}"));
    }
    closeSocket(socket);
}

void setClientTimeout(socket_t socket) {
#ifdef _WIN32
    DWORD timeout = 1000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    timeval timeout{1, 0};
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
}

void requestShutdown(int) {
    g_shutdownRequested = 1;
}

#ifdef _WIN32
BOOL WINAPI consoleControlHandler(DWORD control) {
    if (control == CTRL_C_EVENT || control == CTRL_BREAK_EVENT || control == CTRL_CLOSE_EVENT || control == CTRL_SHUTDOWN_EVENT) {
        g_shutdownRequested = 1;
        return TRUE;
    }
    return FALSE;
}
#endif

void installSignalHandlers() {
    std::signal(SIGINT, requestShutdown);
    std::signal(SIGTERM, requestShutdown);
#ifdef _WIN32
    if (!SetConsoleCtrlHandler(consoleControlHandler, TRUE)) throw std::runtime_error("SetConsoleCtrlHandler failed");
#endif
}

void joinClients() {
    std::vector<std::thread> clients;
    {
        std::lock_guard<std::mutex> lock(g_clientsMutex);
        clients = std::move(g_clientThreads);
    }
    for (auto& client : clients) if (client.joinable()) client.join();
}

void stopPersistedDevices() {
    if (!g_backend || !g_stateStore) return;
    for (const auto& device : g_stateStore->devices()) g_backend->stop(device.deviceId);
}

void restorePersistedDevices() {
    for (const auto& device : g_stateStore->devices()) {
        auto configured = g_backend->configure(device.deviceId, device.tags, device.connection);
        if (configured.status < 200 || configured.status >= 300) {
            throw std::runtime_error("cannot restore device '" + device.deviceId + "': " + configured.body);
        }
        if (device.desiredRunning) {
            auto started = g_backend->start(device.deviceId);
            if (started.status < 200 || started.status >= 300) {
                throw std::runtime_error("cannot start restored device '" + device.deviceId + "': " + started.body);
            }
        }
    }
}

std::string readTextFile(const std::string& path, const std::string& purpose) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open " + purpose + " file: " + path);
    std::ostringstream contents;
    contents << input.rdbuf();
    if (!input.good() && !input.eof()) throw std::runtime_error("cannot read " + purpose + " file: " + path);
    return contents.str();
}

void applyConfigFile(GatewayConfig& config, const std::string& path) {
    if (path.empty()) return;
    JsonValue root;
    try {
        root = parseJson(readTextFile(path, "config"));
    } catch (const std::exception& e) {
        throw std::runtime_error("invalid config file '" + path + "': " + e.what());
    }
    if (!root.object()) throw std::runtime_error("invalid config file '" + path + "': root must be an object");
    auto stringSetting = [&](const char* key, std::string& target) {
        const JsonValue* value = root.find(key);
        if (!value) return;
        if (!value->string()) throw std::runtime_error("invalid config file '" + path + "': " + key + " must be a string");
        target = *value->string();
    };
    stringSetting("listenAddress", config.listenAddress);
    stringSetting("authToken", config.authToken);
    stringSetting("logLevel", config.logLevel);
    stringSetting("logDir", config.logDir);
    stringSetting("stateFile", config.stateFile);
    if (const JsonValue* port = root.find("port")) {
        if (!port->number() || std::floor(*port->number()) != *port->number() || *port->number() < 1 || *port->number() > 65535) throw std::runtime_error("invalid config file '" + path + "': port must be an integer between 1 and 65535");
        config.port = static_cast<int>(*port->number());
    }
}

void validateConfig(const GatewayConfig& config) {
    if (config.listenAddress.empty()) throw std::runtime_error("listenAddress must not be empty");
    if (config.port < 1 || config.port > 65535) throw std::runtime_error("port must be between 1 and 65535");
    if (config.authToken == "__GENERATE_SECURE_TOKEN__") throw std::runtime_error("authToken placeholder must be replaced with a secure token");
    if (config.stateFile.empty()) throw std::runtime_error("stateFile must not be empty");
    const std::string level = lower(config.logLevel);
    if (level != "trace" && level != "debug" && level != "info" && level != "warn" && level != "error") {
        throw std::runtime_error("logLevel must be one of trace, debug, info, warn, error");
    }
}

void resolveConfig(CliOptions& options) {
    applyConfigFile(options.gateway, options.configPath);
    if (options.listenAddress) options.gateway.listenAddress = *options.listenAddress;
    if (options.port) options.gateway.port = *options.port;
    if (options.authToken) options.gateway.authToken = *options.authToken;
    if (options.logLevel) options.gateway.logLevel = *options.logLevel;
    if (options.logDir) options.gateway.logDir = *options.logDir;
    if (options.stateFile) options.gateway.stateFile = *options.stateFile;
    if (options.noColor) options.gateway.noColor = true;
    validateConfig(options.gateway);
}

void printHelp() {
    std::cout
        << "Vision Realtime\n"
        << "\n"
        << "Usage:\n"
        << "  vision-realtime [run] [--config <json>] [options]\n"
        << "  vision-realtime version|status|doctor [--config <json>] [--json]\n"
        << "  vision-realtime service install|start|stop|restart|status|uninstall\n"
        << "\nOptions:\n"
        << "  --listen <address> --port <port> --token <token>\n"
        << "  --log-level <level> --log-dir <path> --state-file <path> --no-color\n";
}

bool isCommandToken(const std::string& value) {
    return value == "run" || value == "version" || value == "status" || value == "doctor" || value == "service" || value == "help";
}

CliOptions parseArgs(int argc, char** argv) {
    CliOptions options;
    int start = 1;
    if (argc > 1) {
        const std::string first = argv[1];
        if (first == "run") {
            options.command = CliCommand::Run;
            start = 2;
        } else if (first == "version") {
            options.command = CliCommand::Version;
            start = 2;
        } else if (first == "status") {
            options.command = CliCommand::Status;
            start = 2;
        } else if (first == "doctor") {
            options.command = CliCommand::Doctor;
            start = 2;
        } else if (first == "service") {
            options.command = CliCommand::Service;
            if (argc < 3) throw std::runtime_error("service action is required: install, start, stop, restart, status, or uninstall");
            options.serviceAction = argv[2];
            const std::vector<std::string> actions{"install", "start", "stop", "restart", "status", "uninstall"};
            if (std::find(actions.begin(), actions.end(), options.serviceAction) == actions.end()) throw std::runtime_error("unknown service action: " + options.serviceAction);
            start = 3;
        } else if (first == "help") {
            options.command = CliCommand::Help;
            start = 2;
        }
    }

    for (int i = start; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("missing value for " + arg);
            return argv[++i];
        };
        if (arg == "--config") options.configPath = next();
        else if (arg == "--listen") options.listenAddress = next();
        else if (arg == "--port") options.port = std::stoi(next());
        else if (arg == "--token") options.authToken = next();
        else if (arg == "--log-level") options.logLevel = next();
        else if (arg == "--log-dir") options.logDir = next();
        else if (arg == "--state-file") options.stateFile = next();
        else if (arg == "--no-color") options.noColor = true;
        else if (arg == "--json") options.json = true;
        else if (arg == "--help" || arg == "-h") options.command = CliCommand::Help;
        else if (isCommandToken(arg)) throw std::runtime_error("command must be the first argument: " + arg);
        else throw std::runtime_error("unknown argument: " + arg);
    }
    return options;
}

int runServiceCommand(const std::string& action) {
#ifdef _WIN32
    std::vector<wchar_t> modulePath(32768);
    const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
    if (length == 0 || length >= modulePath.size()) throw std::runtime_error("cannot resolve gateway executable path");
    const std::filesystem::path wrapper = std::filesystem::path(modulePath.data()).parent_path().parent_path() / L"VisionRealtime.exe";
    if (!std::filesystem::exists(wrapper)) throw std::runtime_error("WinSW service wrapper not found: " + wrapper.string());

    std::wstring commandLine = L"\"" + wrapper.wstring() + L"\" " + std::wstring(action.begin(), action.end());
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(wrapper.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr, wrapper.parent_path().c_str(), &startup, &process)) {
        throw std::runtime_error("cannot execute WinSW service command (Windows error " + std::to_string(GetLastError()) + ")");
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(exitCode);
#else
    (void)action;
    throw std::runtime_error("service management is only available on Windows");
#endif
}

void initSockets() {
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) throw std::runtime_error("WSAStartup failed");
#endif
}

void cleanupSockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}

int runGateway(const GatewayConfig& config) {
    g_config = config;
    g_running = true;
    g_shutdownRequested = 0;
    installSignalHandlers();
    initSockets();
    g_stateStore = std::make_unique<StateStore>(g_config.stateFile);
    g_stateStore->load();

    socket_t server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == invalid_socket_value) throw std::runtime_error("socket failed");

    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(g_config.port));
    if (inet_pton(AF_INET, g_config.listenAddress.c_str(), &addr.sin_addr) != 1) throw std::runtime_error("invalid listen address");

    if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) throw std::runtime_error("bind failed");
    if (listen(server, 32) != 0) throw std::runtime_error("listen failed");

    std::thread values;
#ifdef VISION_ONE_IEC104_WITH_LIB60870
    g_backend = std::make_unique<Lib60870Backend>(
        g_devices,
        g_running,
        [](const std::string& event) { broadcast(event); },
        [](const std::string& message) { log(message); }
    );
#else
    g_backend = std::make_unique<MockBackend>(g_devices);
    values = std::thread(valueLoop);
#endif
    restorePersistedDevices();
    log("IEC104 gateway listening on http://" + g_config.listenAddress + ":" + std::to_string(g_config.port));
    if (!g_config.logDir.empty()) log("IEC104 gateway file logging dir=" + g_config.logDir);
#ifdef VISION_ONE_IEC104_WITH_LIB60870
    log(std::string("lib60870-C link probe: ") + (visionOneIec104Lib60870Probe() ? "OK" : "FAILED"));
#else
    log("lib60870-C support: disabled (mock backend)");
#endif

    while (g_running && !g_shutdownRequested) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(server, &readable);
        timeval timeout{0, 250000};
#ifdef _WIN32
        const int ready = select(0, &readable, nullptr, nullptr, &timeout);
#else
        const int ready = select(server + 1, &readable, nullptr, nullptr, &timeout);
#endif
        if (!g_running || g_shutdownRequested) break;
        if (ready <= 0) continue;
        sockaddr_in clientAddr{};
#ifdef _WIN32
        int len = sizeof(clientAddr);
#else
        socklen_t len = sizeof(clientAddr);
#endif
        socket_t client = accept(server, reinterpret_cast<sockaddr*>(&clientAddr), &len);
        if (client == invalid_socket_value) continue;
        setClientTimeout(client);
        std::lock_guard<std::mutex> lock(g_clientsMutex);
        g_clientThreads.emplace_back(handleClient, client);
    }

    g_running = false;
    closeSocket(server);
    joinClients();
    stopPersistedDevices();
    if (values.joinable()) values.join();
    g_backend.reset();
    cleanupSockets();
    return 0;
}

int printVersion(bool json) {
    if (json) {
        std::cout << "{\"ok\":true,\"version\":\"" << jsonEscape(VISION_ONE_IEC104_GATEWAY_VERSION) << "\",\"apiVersion\":\"1\",\"backend\":\"" << backendName() << "\"}" << std::endl;
        return 0;
    }
    std::cout << "Vision Realtime " << VISION_ONE_IEC104_GATEWAY_VERSION << " (backend=" << backendName() << ", api=1)" << std::endl;
    return 0;
}

int printOfflineStatus(const CliOptions& options) {
    StateStore state(options.gateway.stateFile);
    state.load();
    const auto deviceCount = state.devices().size();
    if (options.json) {
        std::cout << "{\"ok\":true,\"mode\":\"offline\",\"gateway\":{\"version\":\"" << jsonEscape(VISION_ONE_IEC104_GATEWAY_VERSION)
                   << "\",\"apiVersion\":\"1\",\"backend\":\"" << backendName() << "\",\"listenAddress\":\""
                   << jsonEscape(options.gateway.listenAddress) << "\",\"port\":" << options.gateway.port << ",\"stateFile\":\""
                   << jsonEscape(options.gateway.stateFile) << "\",\"configuredDevices\":" << deviceCount << "}}" << std::endl;
        return 0;
    }
    std::cout << "Gateway status: offline configuration inspection" << std::endl;
    std::cout << "Version: " << VISION_ONE_IEC104_GATEWAY_VERSION << std::endl;
    std::cout << "API: 1" << std::endl;
    std::cout << "Backend: " << backendName() << std::endl;
    std::cout << "Listen: " << options.gateway.listenAddress << ":" << options.gateway.port << std::endl;
    std::cout << "State: " << options.gateway.stateFile << " (" << deviceCount << " configured devices)" << std::endl;
    return 0;
}

int printDoctor(const CliOptions& options) {
    const bool productionBackend = backendName() == "lib60870";
    StateStore state(options.gateway.stateFile);
    state.load();
    bool backendOk = true;
#ifdef VISION_ONE_IEC104_WITH_LIB60870
    backendOk = visionOneIec104Lib60870Probe();
#endif
    const bool ok = backendOk;
    if (options.json) {
        std::cout << "{\"ok\":" << boolJson(ok) << ",\"checks\":["
                  << "{\"name\":\"config\",\"ok\":true,\"value\":\"valid\"},"
                  << "{\"name\":\"state\",\"ok\":true,\"value\":\"" << jsonEscape(options.gateway.stateFile) << "\"},"
                  << "{\"name\":\"backend\",\"ok\":" << boolJson(backendOk) << ",\"value\":\"" << backendName() << "\"}]}" << std::endl;
        return ok ? 0 : 7;
    }
    std::cout << "config: OK" << std::endl;
    std::cout << "state: OK (" << options.gateway.stateFile << ")" << std::endl;
    std::cout << "backend: " << backendName() << (backendOk ? " OK" : " FAILED") << (productionBackend ? "" : " (development backend)") << std::endl;
    return ok ? 0 : 7;
}

} // namespace

int main(int argc, char** argv) {
    try {
        CliOptions options = parseArgs(argc, argv);
        if (options.command == CliCommand::Help) {
            printHelp();
            return 0;
        }
        if (options.command == CliCommand::Service) return runServiceCommand(options.serviceAction);
        resolveConfig(options);
        if (options.command == CliCommand::Version) return printVersion(options.json);
        if (options.command == CliCommand::Status) return printOfflineStatus(options);
        if (options.command == CliCommand::Doctor) return printDoctor(options);
        return runGateway(options.gateway);
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << std::endl;
        cleanupSockets();
        return 1;
    }
}
