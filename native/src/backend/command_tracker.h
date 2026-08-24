#pragma once

#include "gateway_models.h"

#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <string>

enum class CommandPhase {
    AwaitingSelectConfirmation,
    ExecuteQueued,
    AwaitingExecuteConfirmation,
};

struct CommandFingerprint {
    int typeId = 0;
    int commonAddress = 0;
    int ioa = 0;
    int qualifier = 0;
    double value = 0.0;
};

struct PendingCommand {
    std::string deviceId;
    std::string requestId;
    TagConfig tag;
    double requestedValue = 0.0;
    std::uint64_t connectionGeneration = 0;
    CommandFingerprint fingerprint;
    CommandPhase phase = CommandPhase::AwaitingExecuteConfirmation;
    std::chrono::steady_clock::time_point deadline;
};

struct CommandConfirmation {
    std::string deviceId;
    std::uint64_t connectionGeneration = 0;
    CommandFingerprint fingerprint;
    bool select = false;
    int causeOfTransmission = 0;
    bool negative = false;
};

enum class CommandActionKind {
    None,
    SendExecute,
    Success,
    Failure,
};

struct CommandAction {
    CommandActionKind kind = CommandActionKind::None;
    PendingCommand command;
    std::string error;
    std::string phase;
    int causeOfTransmission = 0;
};

class CommandTracker {
public:
    std::optional<std::string> start(PendingCommand command);
    CommandAction confirm(const CommandConfirmation& confirmation, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    CommandAction takeQueuedExecute(const std::string& deviceId, std::uint64_t connectionGeneration, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    std::optional<PendingCommand> fail(const std::string& deviceId, std::uint64_t connectionGeneration);
    std::optional<PendingCommand> timeout(const std::string& deviceId, std::uint64_t connectionGeneration);
    std::optional<PendingCommand> expire(const std::string& deviceId, std::uint64_t connectionGeneration, std::chrono::steady_clock::time_point now);
    void unblockAfterDisconnect(const std::string& deviceId, std::uint64_t connectionGeneration);

private:
    static bool sameFingerprint(const CommandFingerprint& expected, const CommandFingerprint& actual);
    static std::string rejectionError(int causeOfTransmission);

    std::mutex mutex_;
    std::map<std::string, PendingCommand> pendingByDevice_;
    std::map<std::string, std::uint64_t> blockedByDevice_;
};
