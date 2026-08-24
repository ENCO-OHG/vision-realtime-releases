#pragma once

#include "device_registry.h"
#include "iec104_backend.h"
#include "command_tracker.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <set>
#include <string>

#ifdef VISION_ONE_IEC104_WITH_LIB60870
class Lib60870Backend final : public Iec104Backend {
public:
    using EventSink = std::function<void(const std::string&)>;
    using LogSink = std::function<void(const std::string&)>;

    Lib60870Backend(DeviceRegistry& devices, std::atomic<bool>& running, EventSink eventSink, LogSink logSink);

    BackendResult configure(const std::string& deviceId, const std::vector<TagConfig>& tags, const ConnectionConfig& connection) override;
    BackendResult start(const std::string& deviceId) override;
    BackendResult stop(const std::string& deviceId) override;
    BackendResult status(const std::string& deviceId) override;
    BackendResult write(const std::string& deviceId, const WriteRequest& request) override;
    BackendResult interrogate(const std::string& deviceId, int qualifier) override;
    std::vector<std::string> pollEvents() override;

    void logAsduReceived(const std::string& deviceId, TypeID type, CS101_CauseOfTransmission cot, int ca, int count);
    void emitValue(const std::string& deviceId, int ioa, const std::string& asduType, double value, uint8_t quality, uint64_t timestampMs, CS101_CauseOfTransmission cot);
    void handleCommandConfirmation(const CommandConfirmation& confirmation);
    void handleConnectionEvent(const std::string& deviceId, std::uint64_t connectionGeneration, CS104_ConnectionEvent event);

private:
    void realBackendLoop(std::string deviceId);
    bool processCommandWork(const std::string& deviceId, std::uint64_t connectionGeneration, CS104_Connection connection, const std::shared_ptr<std::mutex>& operationMutex);
    void failPendingCommand(const std::string& deviceId, std::uint64_t connectionGeneration, const std::string& error);
    void queueCommandResult(const PendingCommand& command, bool ok, const std::string& cause, const std::string& error, const std::string& phase, int cot);
    void queueEvent(std::string event);
    void flushQueuedEvents();
    void requestReconnect(std::uint64_t connectionGeneration);
    bool takeReconnectRequest(std::uint64_t connectionGeneration);

    DeviceRegistry& devices_;
    std::atomic<bool>& running_;
    EventSink eventSink_;
    LogSink logSink_;
    CommandTracker commands_;
    std::atomic<std::uint64_t> nextConnectionGeneration_{1};
    std::mutex eventMutex_;
    std::deque<std::string> queuedEvents_;
    std::mutex reconnectMutex_;
    std::set<std::uint64_t> reconnectGenerations_;
};
#endif
