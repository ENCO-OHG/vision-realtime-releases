#pragma once

#include "gateway_models.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef VISION_ONE_IEC104_WITH_LIB60870
extern "C" {
#include "cs104_connection.h"
}
#endif

struct MockValueEvent {
    std::string deviceId;
    TagConfig tag;
    double value = 0.0;
};

struct DeviceStatusSnapshot {
    bool running = false;
    bool realConnected = false;
    std::string lastError;
};

struct WriteSnapshot {
    bool connected = false;
    bool found = false;
    TagConfig tag;
#ifdef VISION_ONE_IEC104_WITH_LIB60870
    CS104_Connection connection = nullptr;
    std::shared_ptr<std::mutex> operationMutex;
    int commonAddress = 1;
    int timeoutMs = 10000;
    std::uint64_t connectionGeneration = 0;
#endif
};

struct InterrogateSnapshot {
    bool connected = false;
#ifdef VISION_ONE_IEC104_WITH_LIB60870
    CS104_Connection connection = nullptr;
    std::shared_ptr<std::mutex> operationMutex;
    int commonAddress = 1;
    std::uint64_t connectionGeneration = 0;
#endif
};

struct StopWorkerResult {
    std::thread worker;
#ifdef VISION_ONE_IEC104_WITH_LIB60870
    CS104_Connection connectionToClose = nullptr;
    std::shared_ptr<std::mutex> operationMutex;
#endif
};

class DeviceRegistry {
public:
    struct ConfigureResult {
        StopWorkerResult stoppedWorker;
        bool restartAfterConfig = false;
        bool connectionChanged = false;
        bool tagsChanged = false;
        std::size_t tagCount = 0;
    };

    ConfigureResult configure(const std::string& deviceId, const std::vector<TagConfig>& tags, const ConnectionConfig& connection);
    StopWorkerResult stopWorkerForRestart(const std::string& deviceId);

#ifdef VISION_ONE_IEC104_WITH_LIB60870
    bool isConfigured(const std::string& deviceId);
    bool startRealWorker(const std::string& deviceId, std::thread worker);
    StopWorkerResult stopRealWorker(const std::string& deviceId);
    void finishRealStop(const std::string& deviceId);
    std::optional<ConnectionConfig> runningConnectionConfig(const std::string& deviceId);
    bool shouldContinueRunning(const std::string& deviceId);
    bool shouldReconnect(const std::string& deviceId);
    std::shared_ptr<std::mutex> setConnection(const std::string& deviceId, CS104_Connection connection, std::uint64_t connectionGeneration);
    bool isCurrentConnection(const std::string& deviceId, CS104_Connection connection, std::uint64_t connectionGeneration);
    bool clearConnectionIfMatches(const std::string& deviceId, CS104_Connection connection);
    void markConnected(const std::string& deviceId);
    void markDisconnected(const std::string& deviceId, const std::string& lastError = "");
#else
    void startMock(const std::string& deviceId);
    void stopMock(const std::string& deviceId);
#endif

    DeviceStatusSnapshot status(const std::string& deviceId);
    WriteSnapshot prepareWrite(const std::string& deviceId, const std::string& tagId, int ioa, double value);
    InterrogateSnapshot prepareInterrogate(const std::string& deviceId);
    std::string tagIdForIoa(const std::string& deviceId, int ioa);
    std::vector<MockValueEvent> collectMockValueEvents(const std::string& deviceIdFilter = "");

private:
    struct DeviceState {
        bool configured = false;
        bool running = false;
        bool realConnected = false;
        bool stopRequested = false;
        std::string lastError;
        ConnectionConfig connectionConfig;
        std::vector<TagConfig> tags;
        std::map<std::string, double> numericValues;
#ifdef VISION_ONE_IEC104_WITH_LIB60870
        CS104_Connection connection = nullptr;
        std::uint64_t connectionGeneration = 0;
        std::shared_ptr<std::mutex> operationMutex = std::make_shared<std::mutex>();
#endif
        std::thread worker;
    };

    static bool sameConnectionConfig(const ConnectionConfig& a, const ConnectionConfig& b);
    static bool sameTagConfig(const std::vector<TagConfig>& a, const std::vector<TagConfig>& b);
#ifdef VISION_ONE_IEC104_WITH_LIB60870
    static CS104_Connection requestStop(DeviceState& state, std::shared_ptr<std::mutex>& operationMutex);
#else
    static void requestStop(DeviceState& state);
#endif

    std::mutex mutex_;
    std::map<std::string, DeviceState> devices_;
};
