#include "command_tracker.h"
#include "json_utils.h"

#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

PendingCommand command(bool select) {
    PendingCommand value;
    value.deviceId = "device-1";
    value.requestId = "request-1";
    value.tag = {.tagId = "tag-1", .ioa = 100, .deviceDataType = "C_SC_NA_1", .writable = true};
    value.requestedValue = 1;
    value.connectionGeneration = 3;
    value.fingerprint = {.typeId = 45, .commonAddress = 1, .ioa = 100, .qualifier = 0, .value = 1};
    value.phase = select ? CommandPhase::AwaitingSelectConfirmation : CommandPhase::AwaitingExecuteConfirmation;
    value.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    return value;
}

CommandConfirmation confirmation(bool select, bool negative = false) {
    return {
        .deviceId = "device-1",
        .connectionGeneration = 3,
        .fingerprint = {.typeId = 45, .commonAddress = 1, .ioa = 100, .qualifier = 0, .value = 1},
        .select = select,
        .causeOfTransmission = 7,
        .negative = negative,
    };
}

void directCommandCompletesOnPositiveConfirmation() {
    CommandTracker tracker;
    require(!tracker.start(command(false)), "direct command should start");
    auto result = tracker.confirm(confirmation(false));
    require(result.kind == CommandActionKind::Success, "direct command should succeed");
    require(result.command.requestId == "request-1", "direct command request ID should match");
}

void selectQueuesExecuteBeforeFinalConfirmation() {
    CommandTracker tracker;
    require(!tracker.start(command(true)), "select command should start");
    auto selected = tracker.confirm(confirmation(true));
    require(selected.kind == CommandActionKind::SendExecute, "positive select should queue execute");
    auto execute = tracker.takeQueuedExecute("device-1", 3);
    require(execute.kind == CommandActionKind::SendExecute, "queued execute should be available");
    auto completed = tracker.confirm(confirmation(false));
    require(completed.kind == CommandActionKind::Success, "positive execute should succeed");
}

void negativeSelectFails() {
    CommandTracker tracker;
    require(!tracker.start(command(true)), "negative select command should start");
    auto result = tracker.confirm(confirmation(true, true));
    require(result.kind == CommandActionKind::Failure, "negative select should fail");
    require(result.error == "select-rejected", "negative select error should match");
}

void mismatchedEchoFails() {
    CommandTracker tracker;
    require(!tracker.start(command(false)), "mismatch command should start");
    auto value = confirmation(false);
    value.fingerprint.value = 0;
    auto result = tracker.confirm(value);
    require(result.kind == CommandActionKind::Failure, "mismatched confirmation should fail");
    require(result.error == "confirmation-mismatch", "mismatch error should match");
}

void secondCommandIsRejectedWhileBusy() {
    CommandTracker tracker;
    require(!tracker.start(command(false)), "first busy command should start");
    auto second = command(false);
    second.requestId = "request-2";
    auto error = tracker.start(second);
    require(error && *error == "command-busy", "second command should be busy");
}

void expiredCommandIsRemoved() {
    CommandTracker tracker;
    auto value = command(false);
    value.deadline = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
    require(!tracker.start(value), "expired command should start");
    auto expired = tracker.expire("device-1", 3, std::chrono::steady_clock::now());
    require(expired && expired->requestId == "request-1", "expired command should be removed");
}

void terminationFromPreviousCommandIsIgnored() {
    CommandTracker tracker;
    require(!tracker.start(command(false)), "termination test command should start");
    auto termination = confirmation(false);
    termination.causeOfTransmission = 10;
    termination.fingerprint.value = 0;
    auto ignored = tracker.confirm(termination);
    require(ignored.kind == CommandActionKind::None, "activation termination should be ignored");
    auto completed = tracker.confirm(confirmation(false));
    require(completed.kind == CommandActionKind::Success, "command should remain pending after termination");
}

void confirmationAfterDeadlineCannotQueueExecute() {
    CommandTracker tracker;
    auto value = command(true);
    const auto deadline = std::chrono::steady_clock::now();
    value.deadline = deadline;
    require(!tracker.start(value), "deadline command should start");
    auto result = tracker.confirm(confirmation(true), deadline);
    require(result.kind == CommandActionKind::Failure, "late select confirmation should fail");
    require(result.error == "command-timeout", "late select should report timeout");
    require(tracker.takeQueuedExecute("device-1", 3).kind == CommandActionKind::None, "late select must not queue execute");
}

void queuedExecuteExpiresBeforeSend() {
    CommandTracker tracker;
    auto value = command(true);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(10);
    value.deadline = deadline;
    require(!tracker.start(value), "queued timeout command should start");
    auto selected = tracker.confirm(confirmation(true), deadline - std::chrono::milliseconds(1));
    require(selected.kind == CommandActionKind::SendExecute, "select before deadline should queue execute");
    auto execute = tracker.takeQueuedExecute("device-1", 3, deadline);
    require(execute.kind == CommandActionKind::Failure, "queued execute at deadline should fail");
    require(execute.error == "command-timeout", "queued execute should report timeout");
    auto next = command(false);
    next.requestId = "request-2";
    auto blocked = tracker.start(next);
    require(blocked && *blocked == "connection-recovering", "timed out connection should reject new commands");
    tracker.unblockAfterDisconnect("device-1", 3);
    require(!tracker.start(next), "new command should start after old connection is destroyed");
}

void writeJsonValuesAreParsedWithoutCoercion() {
    auto boolean = jsonWriteValueField("{\"value\":true}", "value");
    require(boolean && *boolean == 1.0, "boolean true should parse as one");
    auto exponent = jsonWriteValueField("{\"value\":1e-7}", "value");
    require(exponent && *exponent == 1e-7, "exponential number should parse completely");
    require(!jsonWriteValueField("{\"value\":\"1\"}", "value"), "string value should be rejected");
    require(!jsonWriteValueField("{\"value\":1oops}", "value"), "partial number should be rejected");
}

void timeoutAfterExecuteDequeueBlocksNewCommands() {
    CommandTracker tracker;
    auto value = command(true);
    require(!tracker.start(value), "post-dequeue timeout command should start");
    require(tracker.confirm(confirmation(true)).kind == CommandActionKind::SendExecute, "select should queue execute");
    require(tracker.takeQueuedExecute("device-1", 3).kind == CommandActionKind::SendExecute, "execute should dequeue");
    require(tracker.timeout("device-1", 3).has_value(), "timeout should remove dequeued execute");
    auto next = command(false);
    next.requestId = "request-2";
    auto blocked = tracker.start(next);
    require(blocked && *blocked == "connection-recovering", "post-dequeue timeout should quarantine connection");
}

} // namespace

int main() {
    directCommandCompletesOnPositiveConfirmation();
    selectQueuesExecuteBeforeFinalConfirmation();
    negativeSelectFails();
    mismatchedEchoFails();
    secondCommandIsRejectedWhileBusy();
    expiredCommandIsRemoved();
    terminationFromPreviousCommandIsIgnored();
    confirmationAfterDeadlineCannotQueueExecute();
    queuedExecuteExpiresBeforeSend();
    writeJsonValuesAreParsedWithoutCoercion();
    timeoutAfterExecuteDequeueBlocksNewCommands();
    std::cout << "command tracker tests passed" << std::endl;
    return 0;
}
