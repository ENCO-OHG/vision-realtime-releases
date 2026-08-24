#include "command_tracker.h"

#include <utility>

namespace {

constexpr int COT_ACTIVATION_CONFIRMATION = 7;
constexpr int COT_UNKNOWN_TYPE_ID = 44;
constexpr int COT_UNKNOWN_COT = 45;
constexpr int COT_UNKNOWN_CA = 46;
constexpr int COT_UNKNOWN_IOA = 47;

CommandAction failure(PendingCommand command, std::string error, std::string phase, int cot) {
    return {
        .kind = CommandActionKind::Failure,
        .command = std::move(command),
        .error = std::move(error),
        .phase = std::move(phase),
        .causeOfTransmission = cot,
    };
}

} // namespace

std::optional<std::string> CommandTracker::start(PendingCommand command) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (blockedByDevice_.contains(command.deviceId)) return "connection-recovering";
    for (const auto& [_, pending] : pendingByDevice_) {
        if (pending.requestId == command.requestId) return "duplicate-request-id";
    }
    if (pendingByDevice_.contains(command.deviceId)) return "command-busy";
    pendingByDevice_.emplace(command.deviceId, std::move(command));
    return std::nullopt;
}

CommandAction CommandTracker::confirm(const CommandConfirmation& confirmation, std::chrono::steady_clock::time_point now) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pendingByDevice_.find(confirmation.deviceId);
    if (it == pendingByDevice_.end()) return {};

    PendingCommand& pending = it->second;
    if (pending.connectionGeneration != confirmation.connectionGeneration) return {};
    if (pending.fingerprint.typeId != confirmation.fingerprint.typeId ||
        pending.fingerprint.commonAddress != confirmation.fingerprint.commonAddress ||
        pending.fingerprint.ioa != confirmation.fingerprint.ioa) return {};

    const std::string phase = pending.phase == CommandPhase::AwaitingSelectConfirmation ? "select" : "execute";
    const bool isRejectionCause = confirmation.causeOfTransmission == COT_UNKNOWN_TYPE_ID ||
        confirmation.causeOfTransmission == COT_UNKNOWN_COT ||
        confirmation.causeOfTransmission == COT_UNKNOWN_CA ||
        confirmation.causeOfTransmission == COT_UNKNOWN_IOA;
    if (confirmation.causeOfTransmission != COT_ACTIVATION_CONFIRMATION && !isRejectionCause) return {};
    if (now >= pending.deadline) {
        blockedByDevice_[pending.deviceId] = pending.connectionGeneration;
        PendingCommand completed = std::move(pending);
        pendingByDevice_.erase(it);
        return failure(std::move(completed), "command-timeout", phase, confirmation.causeOfTransmission);
    }
    if (!sameFingerprint(pending.fingerprint, confirmation.fingerprint)) {
        PendingCommand completed = std::move(pending);
        pendingByDevice_.erase(it);
        return failure(std::move(completed), "confirmation-mismatch", phase, confirmation.causeOfTransmission);
    }

    if (isRejectionCause) {
        PendingCommand completed = std::move(pending);
        pendingByDevice_.erase(it);
        const std::string error = rejectionError(confirmation.causeOfTransmission);
        return failure(std::move(completed), error, phase, confirmation.causeOfTransmission);
    }

    const bool expectsSelect = pending.phase == CommandPhase::AwaitingSelectConfirmation;
    if (confirmation.select != expectsSelect) {
        PendingCommand completed = std::move(pending);
        pendingByDevice_.erase(it);
        return failure(std::move(completed), "confirmation-phase-mismatch", phase, confirmation.causeOfTransmission);
    }
    if (confirmation.negative) {
        PendingCommand completed = std::move(pending);
        pendingByDevice_.erase(it);
        return failure(std::move(completed), expectsSelect ? "select-rejected" : "execute-rejected", phase, confirmation.causeOfTransmission);
    }

    if (expectsSelect) {
        pending.phase = CommandPhase::ExecuteQueued;
        return {
            .kind = CommandActionKind::SendExecute,
            .command = pending,
            .phase = "select",
            .causeOfTransmission = confirmation.causeOfTransmission,
        };
    }

    PendingCommand completed = std::move(pending);
    pendingByDevice_.erase(it);
    return {
        .kind = CommandActionKind::Success,
        .command = std::move(completed),
        .phase = "execute",
        .causeOfTransmission = confirmation.causeOfTransmission,
    };
}

CommandAction CommandTracker::takeQueuedExecute(const std::string& deviceId, std::uint64_t connectionGeneration, std::chrono::steady_clock::time_point now) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pendingByDevice_.find(deviceId);
    if (it == pendingByDevice_.end() || it->second.connectionGeneration != connectionGeneration || it->second.phase != CommandPhase::ExecuteQueued) return {};
    if (now >= it->second.deadline) {
        blockedByDevice_[it->second.deviceId] = it->second.connectionGeneration;
        PendingCommand completed = std::move(it->second);
        pendingByDevice_.erase(it);
        return failure(std::move(completed), "command-timeout", "execute", 0);
    }
    it->second.phase = CommandPhase::AwaitingExecuteConfirmation;
    return {.kind = CommandActionKind::SendExecute, .command = it->second, .phase = "execute"};
}

std::optional<PendingCommand> CommandTracker::fail(const std::string& deviceId, std::uint64_t connectionGeneration) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pendingByDevice_.find(deviceId);
    if (it == pendingByDevice_.end() || it->second.connectionGeneration != connectionGeneration) return std::nullopt;
    PendingCommand command = std::move(it->second);
    pendingByDevice_.erase(it);
    return command;
}

std::optional<PendingCommand> CommandTracker::timeout(const std::string& deviceId, std::uint64_t connectionGeneration) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pendingByDevice_.find(deviceId);
    if (it == pendingByDevice_.end() || it->second.connectionGeneration != connectionGeneration) return std::nullopt;
    blockedByDevice_[it->second.deviceId] = it->second.connectionGeneration;
    PendingCommand command = std::move(it->second);
    pendingByDevice_.erase(it);
    return command;
}

std::optional<PendingCommand> CommandTracker::expire(const std::string& deviceId, std::uint64_t connectionGeneration, std::chrono::steady_clock::time_point now) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pendingByDevice_.find(deviceId);
    if (it == pendingByDevice_.end() || it->second.connectionGeneration != connectionGeneration || now < it->second.deadline) return std::nullopt;
    blockedByDevice_[it->second.deviceId] = it->second.connectionGeneration;
    PendingCommand command = std::move(it->second);
    pendingByDevice_.erase(it);
    return command;
}

void CommandTracker::unblockAfterDisconnect(const std::string& deviceId, std::uint64_t connectionGeneration) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = blockedByDevice_.find(deviceId);
    if (it != blockedByDevice_.end() && it->second == connectionGeneration) blockedByDevice_.erase(it);
}

bool CommandTracker::sameFingerprint(const CommandFingerprint& expected, const CommandFingerprint& actual) {
    return expected.typeId == actual.typeId &&
        expected.commonAddress == actual.commonAddress &&
        expected.ioa == actual.ioa &&
        expected.qualifier == actual.qualifier &&
        expected.value == actual.value;
}

std::string CommandTracker::rejectionError(int causeOfTransmission) {
    if (causeOfTransmission == COT_UNKNOWN_TYPE_ID) return "unknown-type-id";
    if (causeOfTransmission == COT_UNKNOWN_COT) return "unknown-cause-of-transmission";
    if (causeOfTransmission == COT_UNKNOWN_CA) return "unknown-common-address";
    if (causeOfTransmission == COT_UNKNOWN_IOA) return "unknown-ioa";
    return "command-rejected";
}
