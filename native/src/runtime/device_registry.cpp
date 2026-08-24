#include "device_registry.h"

#ifdef VISION_ONE_IEC104_WITH_LIB60870
CS104_Connection DeviceRegistry::requestStop(DeviceState& state, std::shared_ptr<std::mutex>& operationMutex) {
    state.stopRequested = true;
    state.running = false;
    state.realConnected = false;
    CS104_Connection connection = state.connection;
    operationMutex = state.operationMutex;
    state.connection = nullptr;
    return connection;
}
#else
void DeviceRegistry::requestStop(DeviceState& state) {
    state.stopRequested = true;
    state.running = false;
}
#endif

bool DeviceRegistry::sameConnectionConfig(const ConnectionConfig& a, const ConnectionConfig& b) {
    return a.remoteAddress == b.remoteAddress &&
           a.remotePort == b.remotePort &&
           a.commonAddress == b.commonAddress &&
           a.originatorAddress == b.originatorAddress &&
           a.cotSize == b.cotSize &&
           a.caSize == b.caSize &&
           a.ioaSize == b.ioaSize &&
           a.timeoutMs == b.timeoutMs &&
           a.reconnectMs == b.reconnectMs &&
           a.apciT0Sec == b.apciT0Sec &&
           a.apciT1Sec == b.apciT1Sec &&
           a.apciT2Sec == b.apciT2Sec &&
           a.apciT3Sec == b.apciT3Sec &&
           a.apciK == b.apciK &&
           a.apciW == b.apciW &&
           a.interrogationOnConnect == b.interrogationOnConnect &&
           a.clockSyncOnConnect == b.clockSyncOnConnect;
}

bool DeviceRegistry::sameTagConfig(const std::vector<TagConfig>& a, const std::vector<TagConfig>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].tagId != b[i].tagId ||
            a[i].ioa != b[i].ioa ||
            a[i].visionType != b[i].visionType ||
            a[i].deviceDataType != b[i].deviceDataType ||
            a[i].writable != b[i].writable ||
            a[i].selectBeforeOperate != b[i].selectBeforeOperate ||
            a[i].qualifier != b[i].qualifier) {
            return false;
        }
    }
    return true;
}

DeviceRegistry::ConfigureResult DeviceRegistry::configure(const std::string& deviceId, const std::vector<TagConfig>& tags, const ConnectionConfig& connection) {
    ConfigureResult result;
    result.tagCount = tags.size();

    std::lock_guard<std::mutex> lock(mutex_);
    auto& state = devices_[deviceId];
    result.connectionChanged = !state.configured || !sameConnectionConfig(state.connectionConfig, connection);
    result.tagsChanged = !sameTagConfig(state.tags, tags);
#ifdef VISION_ONE_IEC104_WITH_LIB60870
    if (state.worker.joinable() && result.connectionChanged) {
        result.restartAfterConfig = state.running && !state.stopRequested;
        result.stoppedWorker.connectionToClose = requestStop(state, result.stoppedWorker.operationMutex);
        result.stoppedWorker.worker = std::move(state.worker);
    }
#endif
    state.tags = tags;
    state.connectionConfig = connection;
    state.configured = true;
    state.stopRequested = false;
    state.lastError.clear();
    for (const auto& tag : tags) {
        if (!state.numericValues.contains(tag.tagId)) state.numericValues[tag.tagId] = 0.0;
    }
    return result;
}

StopWorkerResult DeviceRegistry::stopWorkerForRestart(const std::string& deviceId) {
    StopWorkerResult result;
    std::lock_guard<std::mutex> lock(mutex_);
    auto& state = devices_[deviceId];
#ifdef VISION_ONE_IEC104_WITH_LIB60870
    result.connectionToClose = requestStop(state, result.operationMutex);
#else
    requestStop(state);
#endif
    if (state.worker.joinable()) result.worker = std::move(state.worker);
    return result;
}

#ifdef VISION_ONE_IEC104_WITH_LIB60870
bool DeviceRegistry::isConfigured(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = devices_.find(deviceId);
    return it != devices_.end() && it->second.configured;
}

bool DeviceRegistry::startRealWorker(const std::string& deviceId, std::thread worker) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto& state = devices_[deviceId];
    if (!state.configured) {
        lock.unlock();
        if (worker.joinable()) worker.join();
        return false;
    }
    state.running = true;
    state.stopRequested = false;
    state.realConnected = false;
    state.operationMutex = std::make_shared<std::mutex>();
    state.worker = std::move(worker);
    return true;
}

StopWorkerResult DeviceRegistry::stopRealWorker(const std::string& deviceId) {
    return stopWorkerForRestart(deviceId);
}

void DeviceRegistry::finishRealStop(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& state = devices_[deviceId];
    state.stopRequested = false;
    state.realConnected = false;
}

std::optional<ConnectionConfig> DeviceRegistry::runningConnectionConfig(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = devices_.find(deviceId);
    if (it == devices_.end() || it->second.stopRequested || !it->second.running) return std::nullopt;
    it->second.lastError.clear();
    return it->second.connectionConfig;
}

bool DeviceRegistry::shouldContinueRunning(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = devices_.find(deviceId);
    return it != devices_.end() && !it->second.stopRequested && it->second.running;
}

bool DeviceRegistry::shouldReconnect(const std::string& deviceId) {
    return shouldContinueRunning(deviceId);
}

std::shared_ptr<std::mutex> DeviceRegistry::setConnection(const std::string& deviceId, CS104_Connection connection, std::uint64_t connectionGeneration) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = devices_.find(deviceId);
    if (it == devices_.end()) return std::make_shared<std::mutex>();
    it->second.connection = connection;
    it->second.connectionGeneration = connectionGeneration;
    return it->second.operationMutex;
}

bool DeviceRegistry::isCurrentConnection(const std::string& deviceId, CS104_Connection connection, std::uint64_t connectionGeneration) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = devices_.find(deviceId);
    return it != devices_.end() && it->second.running && it->second.realConnected && it->second.connection == connection && it->second.connectionGeneration == connectionGeneration;
}

bool DeviceRegistry::clearConnectionIfMatches(const std::string& deviceId, CS104_Connection connection) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = devices_.find(deviceId);
    if (it == devices_.end()) return false;
    bool matched = it->second.connection == connection;
    if (matched) it->second.connection = nullptr;
    it->second.realConnected = false;
    return matched;
}

void DeviceRegistry::markConnected(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = devices_.find(deviceId);
    if (it == devices_.end()) return;
    it->second.realConnected = true;
    it->second.lastError.clear();
}

void DeviceRegistry::markDisconnected(const std::string& deviceId, const std::string& lastError) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = devices_.find(deviceId);
    if (it == devices_.end()) return;
    it->second.realConnected = false;
    it->second.lastError = lastError;
}
#else
void DeviceRegistry::startMock(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(mutex_);
    devices_[deviceId].running = true;
}

void DeviceRegistry::stopMock(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(mutex_);
    devices_[deviceId].running = false;
}
#endif

DeviceStatusSnapshot DeviceRegistry::status(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& state = devices_[deviceId];
    return {.running = state.running, .realConnected = state.realConnected, .lastError = state.lastError};
}

WriteSnapshot DeviceRegistry::prepareWrite(const std::string& deviceId, const std::string& tagId, int ioa, double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& state = devices_[deviceId];
    WriteSnapshot snapshot;
#ifdef VISION_ONE_IEC104_WITH_LIB60870
    snapshot.connected = state.running && state.realConnected && state.connection != nullptr;
    snapshot.connection = state.connection;
    snapshot.operationMutex = state.operationMutex;
    snapshot.commonAddress = state.connectionConfig.commonAddress;
    snapshot.timeoutMs = state.connectionConfig.timeoutMs;
    snapshot.connectionGeneration = state.connectionGeneration;
#else
    snapshot.connected = state.running;
#endif
    if (!snapshot.connected) return snapshot;

    for (const auto& tag : state.tags) {
        if (!tagId.empty() && tag.tagId == tagId) {
            snapshot.tag = tag;
            snapshot.found = true;
            break;
        }
    }
    if (!snapshot.found) {
        for (const auto& tag : state.tags) {
            if (tag.ioa == ioa) {
                snapshot.tag = tag;
                snapshot.found = true;
                break;
            }
        }
    }
    if (snapshot.found) state.numericValues[snapshot.tag.tagId] = value;
    return snapshot;
}

InterrogateSnapshot DeviceRegistry::prepareInterrogate(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& state = devices_[deviceId];
    InterrogateSnapshot snapshot;
#ifdef VISION_ONE_IEC104_WITH_LIB60870
    snapshot.connected = state.running && state.realConnected && state.connection != nullptr;
    snapshot.connection = state.connection;
    snapshot.operationMutex = state.operationMutex;
    snapshot.commonAddress = state.connectionConfig.commonAddress;
    snapshot.connectionGeneration = state.connectionGeneration;
#else
    snapshot.connected = state.running;
#endif
    return snapshot;
}

std::string DeviceRegistry::tagIdForIoa(const std::string& deviceId, int ioa) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto dev = devices_.find(deviceId);
    if (dev == devices_.end()) return "";
    for (const auto& tag : dev->second.tags) {
        if (tag.ioa == ioa) return tag.tagId;
    }
    return "";
}

std::vector<MockValueEvent> DeviceRegistry::collectMockValueEvents(const std::string& deviceIdFilter) {
    std::vector<MockValueEvent> events;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [deviceId, state] : devices_) {
        if (!deviceIdFilter.empty() && deviceId != deviceIdFilter) continue;
        if (!state.running || !state.configured) continue;
        for (const auto& tag : state.tags) {
            double next = state.numericValues[tag.tagId] + 0.1;
            state.numericValues[tag.tagId] = next;
            events.push_back({.deviceId = deviceId, .tag = tag, .value = next});
        }
    }
    return events;
}
