#include "lib60870_backend.h"

#ifdef VISION_ONE_IEC104_WITH_LIB60870

#include "json_utils.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

extern "C" {
#include "hal_time.h"
}

namespace {

struct CallbackContext {
    Lib60870Backend* backend = nullptr;
    std::string deviceId;
    std::uint64_t connectionGeneration = 0;
};

std::string typeName(TypeID type) {
    const char* name = TypeID_toString(type);
    return name ? std::string(name) : std::to_string(static_cast<int>(type));
}

uint64_t cp56LocalTimeToUtcMs(CP56Time2a timestamp) {
    if (!timestamp) return 0;

    std::tm localTime{};
    localTime.tm_sec = CP56Time2a_getSecond(timestamp);
    localTime.tm_min = CP56Time2a_getMinute(timestamp);
    localTime.tm_hour = CP56Time2a_getHour(timestamp);
    localTime.tm_mday = CP56Time2a_getDayOfMonth(timestamp);
    localTime.tm_mon = CP56Time2a_getMonth(timestamp) - 1;
    localTime.tm_year = CP56Time2a_getYear(timestamp) + 100;
    localTime.tm_isdst = CP56Time2a_isSummerTime(timestamp) ? 1 : 0;

    const std::time_t seconds = std::mktime(&localTime);
    if (seconds < 0) return 0;
    return static_cast<uint64_t>(seconds) * 1000ULL + static_cast<uint64_t>(CP56Time2a_getMillisecond(timestamp));
}

bool decodeInformationObject(TypeID type, InformationObject io, double& value, uint8_t& quality, uint64_t& timestampMs) {
    quality = 0;
    timestampMs = 0;
    switch (type) {
        case M_SP_NA_1:
            value = SinglePointInformation_getValue((SinglePointInformation)io) ? 1.0 : 0.0;
            quality = SinglePointInformation_getQuality((SinglePointInformation)io);
            return true;
        case M_SP_TB_1:
            value = SinglePointInformation_getValue((SinglePointInformation)io) ? 1.0 : 0.0;
            quality = SinglePointInformation_getQuality((SinglePointInformation)io);
            timestampMs = cp56LocalTimeToUtcMs(SinglePointWithCP56Time2a_getTimestamp((SinglePointWithCP56Time2a)io));
            return true;
        case M_DP_NA_1:
            value = DoublePointInformation_getValue((DoublePointInformation)io);
            quality = DoublePointInformation_getQuality((DoublePointInformation)io);
            return true;
        case M_DP_TB_1:
            value = DoublePointInformation_getValue((DoublePointInformation)io);
            quality = DoublePointInformation_getQuality((DoublePointInformation)io);
            timestampMs = cp56LocalTimeToUtcMs(DoublePointWithCP56Time2a_getTimestamp((DoublePointWithCP56Time2a)io));
            return true;
        case M_ME_NA_1:
            value = MeasuredValueNormalized_getValue((MeasuredValueNormalized)io);
            quality = MeasuredValueNormalized_getQuality((MeasuredValueNormalized)io);
            return true;
        case M_ME_TD_1:
            value = MeasuredValueNormalized_getValue((MeasuredValueNormalized)io);
            quality = MeasuredValueNormalized_getQuality((MeasuredValueNormalized)io);
            timestampMs = cp56LocalTimeToUtcMs(MeasuredValueNormalizedWithCP56Time2a_getTimestamp((MeasuredValueNormalizedWithCP56Time2a)io));
            return true;
        case M_ME_NB_1:
            value = MeasuredValueScaled_getValue((MeasuredValueScaled)io);
            quality = MeasuredValueScaled_getQuality((MeasuredValueScaled)io);
            return true;
        case M_ME_TE_1:
            value = MeasuredValueScaled_getValue((MeasuredValueScaled)io);
            quality = MeasuredValueScaled_getQuality((MeasuredValueScaled)io);
            timestampMs = cp56LocalTimeToUtcMs(MeasuredValueScaledWithCP56Time2a_getTimestamp((MeasuredValueScaledWithCP56Time2a)io));
            return true;
        case M_ME_NC_1:
            value = MeasuredValueShort_getValue((MeasuredValueShort)io);
            quality = MeasuredValueShort_getQuality((MeasuredValueShort)io);
            return true;
        case M_ME_TF_1:
            value = MeasuredValueShort_getValue((MeasuredValueShort)io);
            quality = MeasuredValueShort_getQuality((MeasuredValueShort)io);
            timestampMs = cp56LocalTimeToUtcMs(MeasuredValueShortWithCP56Time2a_getTimestamp((MeasuredValueShortWithCP56Time2a)io));
            return true;
        default:
            return false;
    }
}

std::optional<TypeID> commandTypeId(const std::string& type) {
    if (type == "C_SC_NA_1") return C_SC_NA_1;
    if (type == "C_DC_NA_1") return C_DC_NA_1;
    if (type == "C_SE_NA_1") return C_SE_NA_1;
    if (type == "C_SE_NB_1") return C_SE_NB_1;
    if (type == "C_SE_NC_1") return C_SE_NC_1;
    return std::nullopt;
}

std::optional<std::string> validateCommand(const TagConfig& tag, double value) {
    if (!tag.writable) return "read-only";
    if (!commandTypeId(tag.deviceDataType)) return "unsupported-command-type";
    if (!std::isfinite(value)) return "invalid-value";
    if ((tag.deviceDataType == "C_SC_NA_1" || tag.deviceDataType == "C_DC_NA_1") && (tag.qualifier < 0 || tag.qualifier > 31)) return "invalid-qualifier";
    if (tag.deviceDataType.rfind("C_SE_", 0) == 0 && (tag.qualifier < 0 || tag.qualifier > 127)) return "invalid-qualifier";
    if (tag.deviceDataType == "C_SC_NA_1" && value != 0.0 && value != 1.0) return "invalid-value";
    if (tag.deviceDataType == "C_DC_NA_1" && value != 1.0 && value != 2.0) return "invalid-value";
    if (tag.deviceDataType == "C_SE_NA_1" && (value < -1.0 || value > 1.0)) return "invalid-value";
    if (tag.deviceDataType == "C_SE_NB_1") {
        const auto scaled = std::lround(value);
        if (scaled < -32768 || scaled > 32767) return "invalid-value";
    }
    if (tag.deviceDataType == "C_SE_NC_1" && (value < -std::numeric_limits<float>::max() || value > std::numeric_limits<float>::max())) return "invalid-value";
    return std::nullopt;
}

InformationObject createCommandObject(const TagConfig& tag, double value, bool select) {
    const std::string type = tag.deviceDataType;
    if (type == "C_SC_NA_1") return (InformationObject)SingleCommand_create(nullptr, tag.ioa, value != 0.0, select, tag.qualifier);
    if (type == "C_DC_NA_1") {
        return (InformationObject)DoubleCommand_create(nullptr, tag.ioa, static_cast<int>(std::lround(value)), select, tag.qualifier);
    }
    if (type == "C_SE_NA_1") return (InformationObject)SetpointCommandNormalized_create(nullptr, tag.ioa, static_cast<float>(value), select, tag.qualifier);
    if (type == "C_SE_NB_1") return (InformationObject)SetpointCommandScaled_create(nullptr, tag.ioa, static_cast<int>(std::lround(value)), select, tag.qualifier);
    if (type == "C_SE_NC_1") return (InformationObject)SetpointCommandShort_create(nullptr, tag.ioa, static_cast<float>(value), select, tag.qualifier);
    return nullptr;
}

bool decodeCommandObject(TypeID type, InformationObject io, int commonAddress, CommandFingerprint& fingerprint, bool& select) {
    fingerprint = {.typeId = static_cast<int>(type), .commonAddress = commonAddress, .ioa = InformationObject_getObjectAddress(io)};
    switch (type) {
        case C_SC_NA_1:
            fingerprint.qualifier = SingleCommand_getQU((SingleCommand)io);
            fingerprint.value = SingleCommand_getState((SingleCommand)io) ? 1.0 : 0.0;
            select = SingleCommand_isSelect((SingleCommand)io);
            return true;
        case C_DC_NA_1:
            fingerprint.qualifier = DoubleCommand_getQU((DoubleCommand)io);
            fingerprint.value = DoubleCommand_getState((DoubleCommand)io);
            select = DoubleCommand_isSelect((DoubleCommand)io);
            return true;
        case C_SE_NA_1:
            fingerprint.qualifier = SetpointCommandNormalized_getQL((SetpointCommandNormalized)io);
            fingerprint.value = SetpointCommandNormalized_getValue((SetpointCommandNormalized)io);
            select = SetpointCommandNormalized_isSelect((SetpointCommandNormalized)io);
            return true;
        case C_SE_NB_1:
            fingerprint.qualifier = SetpointCommandScaled_getQL((SetpointCommandScaled)io);
            fingerprint.value = SetpointCommandScaled_getValue((SetpointCommandScaled)io);
            select = SetpointCommandScaled_isSelect((SetpointCommandScaled)io);
            return true;
        case C_SE_NC_1:
            fingerprint.qualifier = SetpointCommandShort_getQL((SetpointCommandShort)io);
            fingerprint.value = SetpointCommandShort_getValue((SetpointCommandShort)io);
            select = SetpointCommandShort_isSelect((SetpointCommandShort)io);
            return true;
        default:
            return false;
    }
}

void closeStoppedConnection(const StopWorkerResult& stoppedWorker) {
    if (!stoppedWorker.connectionToClose) return;
    if (stoppedWorker.operationMutex) {
        std::lock_guard<std::mutex> operationLock(*stoppedWorker.operationMutex);
        CS104_Connection_close(stoppedWorker.connectionToClose);
        return;
    }
    CS104_Connection_close(stoppedWorker.connectionToClose);
}

bool asduReceivedHandler(void* parameter, int, CS101_ASDU asdu) {
    auto* context = static_cast<CallbackContext*>(parameter);
    if (!context || !context->backend) return true;
    TypeID type = CS101_ASDU_getTypeID(asdu);
    CS101_CauseOfTransmission cot = CS101_ASDU_getCOT(asdu);
    int count = CS101_ASDU_getNumberOfElements(asdu);
    const int commonAddress = CS101_ASDU_getCA(asdu);
    context->backend->logAsduReceived(context->deviceId, type, cot, commonAddress, count);
    for (int i = 0; i < count; ++i) {
        InformationObject io = CS101_ASDU_getElement(asdu, i);
        if (!io) continue;
        int ioa = InformationObject_getObjectAddress(io);
        double value = 0.0;
        uint8_t quality = 0;
        uint64_t timestampMs = 0;
        if (decodeInformationObject(type, io, value, quality, timestampMs)) {
            context->backend->emitValue(context->deviceId, ioa, typeName(type), value, quality, timestampMs, cot);
        } else {
            CommandFingerprint fingerprint;
            bool select = false;
            if (decodeCommandObject(type, io, commonAddress, fingerprint, select)) {
                context->backend->handleCommandConfirmation({
                    .deviceId = context->deviceId,
                    .connectionGeneration = context->connectionGeneration,
                    .fingerprint = fingerprint,
                    .select = select,
                    .causeOfTransmission = static_cast<int>(cot),
                    .negative = CS101_ASDU_isNegative(asdu),
                });
            }
        }
        InformationObject_destroy(io);
    }
    return true;
}

void connectionHandler(void* parameter, CS104_Connection, CS104_ConnectionEvent event) {
    auto* context = static_cast<CallbackContext*>(parameter);
    if (!context || !context->backend) return;
    context->backend->handleConnectionEvent(context->deviceId, context->connectionGeneration, event);
}

} // namespace

Lib60870Backend::Lib60870Backend(DeviceRegistry& devices, std::atomic<bool>& running, EventSink eventSink, LogSink logSink)
    : devices_(devices), running_(running), eventSink_(std::move(eventSink)), logSink_(std::move(logSink)) {}

BackendResult Lib60870Backend::configure(const std::string& deviceId, const std::vector<TagConfig>& tags, const ConnectionConfig& connection) {
    logSink_("IEC104 config device=" + deviceId + " remote=" + connection.remoteAddress + ":" + std::to_string(connection.remotePort) + " tags=" + std::to_string(tags.size()));
    auto configured = devices_.configure(deviceId, tags, connection);
    closeStoppedConnection(configured.stoppedWorker);
    if (configured.stoppedWorker.worker.joinable()) configured.stoppedWorker.worker.join();
    if (configured.restartAfterConfig) {
        logSink_("IEC104 restarting device=" + deviceId + " remote=" + connection.remoteAddress + ":" + std::to_string(connection.remotePort));
        devices_.startRealWorker(deviceId, std::thread(&Lib60870Backend::realBackendLoop, this, deviceId));
    } else if (configured.tagsChanged && !configured.connectionChanged) {
        logSink_("IEC104 tags updated device=" + deviceId + " tags=" + std::to_string(configured.tagCount));
    }
    return {.status = 200, .body = "{\"ok\":true,\"deviceId\":\"" + jsonEscape(deviceId) + "\",\"tags\":" + std::to_string(configured.tagCount) + "}"};
}

BackendResult Lib60870Backend::start(const std::string& deviceId) {
    auto stoppedWorker = devices_.stopWorkerForRestart(deviceId);
    closeStoppedConnection(stoppedWorker);
    if (stoppedWorker.worker.joinable()) stoppedWorker.worker.join();
    if (!devices_.isConfigured(deviceId)) return {.status = 409, .body = "{\"ok\":false,\"error\":\"not-configured\"}"};
    logSink_("IEC104 start device=" + deviceId);
    if (!devices_.startRealWorker(deviceId, std::thread(&Lib60870Backend::realBackendLoop, this, deviceId))) return {.status = 409, .body = "{\"ok\":false,\"error\":\"not-configured\"}"};
    return {.status = 200, .body = "{\"ok\":true,\"deviceId\":\"" + jsonEscape(deviceId) + "\"}", .events = {statusEvent(deviceId, false, "connecting")}};
}

BackendResult Lib60870Backend::stop(const std::string& deviceId) {
    auto stoppedWorker = devices_.stopRealWorker(deviceId);
    closeStoppedConnection(stoppedWorker);
    if (stoppedWorker.worker.joinable()) stoppedWorker.worker.join();
    devices_.finishRealStop(deviceId);
    return {.status = 200, .body = "{\"ok\":true,\"deviceId\":\"" + jsonEscape(deviceId) + "\"}", .events = {statusEvent(deviceId, false, "off")}};
}

BackendResult Lib60870Backend::status(const std::string& deviceId) {
    auto state = devices_.status(deviceId);
    std::string runtimeState = state.running ? (state.realConnected ? "running" : "connecting") : "off";
    return {.status = 200, .body = "{\"ok\":true,\"deviceId\":\"" + jsonEscape(deviceId) + "\",\"gatewayConnected\":true,\"iec104Connected\":" + (state.realConnected ? "true" : "false") + ",\"state\":\"" + runtimeState + "\",\"lastError\":\"" + jsonEscape(state.lastError) + "\"}"};
}

BackendResult Lib60870Backend::write(const std::string& deviceId, const WriteRequest& request) {
    auto write = devices_.prepareWrite(deviceId, request.tagId, request.ioa, request.value);
    if (!write.connected) return {.status = 409, .body = "{\"ok\":false,\"requestId\":\"" + jsonEscape(request.requestId) + "\",\"error\":\"not-connected\"}"};
    if (!write.found) return {.status = 404, .body = "{\"ok\":false,\"requestId\":\"" + jsonEscape(request.requestId) + "\",\"error\":\"unknown-ioa\"}"};
    if (request.requestId.empty()) return {.status = 400, .body = "{\"ok\":false,\"error\":\"missing-request-id\"}"};
    if (auto error = validateCommand(write.tag, request.value)) {
        return {.status = *error == "read-only" ? 409 : 400, .body = "{\"ok\":false,\"requestId\":\"" + jsonEscape(request.requestId) + "\",\"error\":\"" + *error + "\"}"};
    }
    if (request.select != write.tag.selectBeforeOperate) {
        return {.status = 400, .body = "{\"ok\":false,\"requestId\":\"" + jsonEscape(request.requestId) + "\",\"error\":\"select-mode-mismatch\"}"};
    }

    std::string commandDetails;
    if (write.tag.deviceDataType == "C_DC_NA_1") commandDetails = " state=" + std::to_string(static_cast<int>(std::lround(request.value)));
    logSink_("IEC104 write request device=" + deviceId + " requestId=" + request.requestId + " tagId=" + write.tag.tagId + " ioa=" + std::to_string(write.tag.ioa) + " type=" + write.tag.deviceDataType + " value=" + std::to_string(request.value) + commandDetails + " selectBeforeOperate=" + std::string(request.select ? "true" : "false") + " qualifier=" + std::to_string(write.tag.qualifier));

    InformationObject command = createCommandObject(write.tag, request.value, request.select);
    if (!command) return {.status = 400, .body = "{\"ok\":false,\"requestId\":\"" + jsonEscape(request.requestId) + "\",\"error\":\"unsupported-command-type\"}"};
    CommandFingerprint fingerprint;
    bool encodedSelect = false;
    const auto type = commandTypeId(write.tag.deviceDataType);
    if (!type || !decodeCommandObject(*type, command, write.commonAddress, fingerprint, encodedSelect)) {
        InformationObject_destroy(command);
        return {.status = 400, .body = "{\"ok\":false,\"requestId\":\"" + jsonEscape(request.requestId) + "\",\"error\":\"invalid-command\"}"};
    }

    const auto timeoutMs = std::max(250, write.timeoutMs - 250);
    PendingCommand pending{
        .deviceId = deviceId,
        .requestId = request.requestId,
        .tag = write.tag,
        .requestedValue = request.value,
        .connectionGeneration = write.connectionGeneration,
        .fingerprint = fingerprint,
        .phase = request.select ? CommandPhase::AwaitingSelectConfirmation : CommandPhase::AwaitingExecuteConfirmation,
        .deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs),
    };
    if (auto error = commands_.start(pending)) {
        InformationObject_destroy(command);
        return {.status = 409, .body = "{\"ok\":false,\"requestId\":\"" + jsonEscape(request.requestId) + "\",\"error\":\"" + *error + "\"}"};
    }

    bool sent = false;
    bool expiredBeforeSend = false;
    {
        std::lock_guard<std::mutex> operationLock(*write.operationMutex);
        const bool currentConnection = devices_.isCurrentConnection(deviceId, write.connection, write.connectionGeneration);
        expiredBeforeSend = std::chrono::steady_clock::now() >= pending.deadline;
        if (currentConnection && !expiredBeforeSend) {
            sent = CS104_Connection_sendProcessCommandEx(write.connection, CS101_COT_ACTIVATION, write.commonAddress, command);
        }
    }
    InformationObject_destroy(command);
    logSink_("IEC104 write " + std::string(request.select ? "select" : "execute") + " device=" + deviceId + " requestId=" + request.requestId + " sent=" + std::string(sent ? "true" : "false"));
    if (!sent) {
        commands_.fail(deviceId, pending.connectionGeneration);
        const std::string error = expiredBeforeSend ? "command-timeout" : "send-failed";
        return {.status = 409, .body = "{\"ok\":false,\"requestId\":\"" + jsonEscape(request.requestId) + "\",\"error\":\"" + error + "\"}"};
    }

    return {.status = 200, .body = "{\"ok\":true,\"requestId\":\"" + jsonEscape(request.requestId) + "\",\"cause\":\"accepted\"}"};
}

BackendResult Lib60870Backend::interrogate(const std::string& deviceId, int qualifier) {
    auto interrogation = devices_.prepareInterrogate(deviceId);
    if (!interrogation.connected) return {.status = 409, .body = "{\"ok\":false,\"error\":\"not-connected\"}"};
    bool sent = false;
    logSink_("IEC104 manual interrogation device=" + deviceId + " ca=" + std::to_string(interrogation.commonAddress) + " qoi=" + std::to_string(qualifier) + " sending");
    {
        std::lock_guard<std::mutex> operationLock(*interrogation.operationMutex);
        if (devices_.isCurrentConnection(deviceId, interrogation.connection, interrogation.connectionGeneration)) {
            sent = CS104_Connection_sendInterrogationCommand(interrogation.connection, CS101_COT_ACTIVATION, interrogation.commonAddress, static_cast<QualifierOfInterrogation>(qualifier));
        }
    }
    logSink_("IEC104 manual interrogation device=" + deviceId + " sent=" + std::string(sent ? "true" : "false"));
    if (!sent) return {.status = 409, .body = "{\"ok\":false,\"error\":\"send-failed\"}"};
    return {.status = 200, .body = "{\"ok\":true}"};
}

std::vector<std::string> Lib60870Backend::pollEvents() {
    std::lock_guard<std::mutex> lock(eventMutex_);
    std::vector<std::string> events(queuedEvents_.begin(), queuedEvents_.end());
    queuedEvents_.clear();
    return events;
}

void Lib60870Backend::realBackendLoop(std::string deviceId) {
    while (running_) {
        auto cfgSnapshot = devices_.runningConnectionConfig(deviceId);
        if (!cfgSnapshot) break;
        ConnectionConfig cfg = *cfgSnapshot;

        logSink_("IEC104 connecting device=" + deviceId + " remote=" + cfg.remoteAddress + ":" + std::to_string(cfg.remotePort));
        CS104_Connection connection = CS104_Connection_create(cfg.remoteAddress.c_str(), static_cast<uint16_t>(cfg.remotePort));
        const std::uint64_t connectionGeneration = nextConnectionGeneration_.fetch_add(1);
        CallbackContext callbackContext{this, deviceId, connectionGeneration};
        CS101_AppLayerParameters params = CS104_Connection_getAppLayerParameters(connection);
        params->originatorAddress = cfg.originatorAddress;
        params->sizeOfCOT = cfg.cotSize;
        params->sizeOfCA = cfg.caSize;
        params->sizeOfIOA = cfg.ioaSize;
        CS104_APCIParameters apci = CS104_Connection_getAPCIParameters(connection);
        apci->t0 = cfg.apciT0Sec;
        apci->t1 = cfg.apciT1Sec;
        apci->t2 = cfg.apciT2Sec;
        apci->t3 = cfg.apciT3Sec;
        apci->k = cfg.apciK;
        apci->w = cfg.apciW;
        CS104_Connection_setAPCIParameters(connection, apci);
        CS104_Connection_setConnectionHandler(connection, connectionHandler, &callbackContext);
        CS104_Connection_setASDUReceivedHandler(connection, asduReceivedHandler, &callbackContext);
        CS104_Connection_setConnectTimeout(connection, cfg.timeoutMs);
        auto operationMutex = devices_.setConnection(deviceId, connection, connectionGeneration);

        bool connected = false;
        {
            std::lock_guard<std::mutex> operationLock(*operationMutex);
            connected = CS104_Connection_connect(connection);
        }
        if (connected) {
            {
                std::lock_guard<std::mutex> operationLock(*operationMutex);
                CS104_Connection_sendStartDT(connection);
            }
            const int startTimeoutMs = std::max(1000, std::min(cfg.timeoutMs, 5000));
            for (int waitedMs = 0; waitedMs < startTimeoutMs; waitedMs += 100) {
                if (!devices_.shouldContinueRunning(deviceId)) break;
                if (devices_.status(deviceId).realConnected) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (cfg.interrogationOnConnect) {
                bool sent = false;
                if (devices_.status(deviceId).realConnected) {
                    std::lock_guard<std::mutex> operationLock(*operationMutex);
                    sent = CS104_Connection_sendInterrogationCommand(connection, CS101_COT_ACTIVATION, cfg.commonAddress, IEC60870_QOI_STATION);
                }
                logSink_("IEC104 initial interrogation device=" + deviceId + " sent=" + std::string(sent ? "true" : "false"));
            }
            if (cfg.clockSyncOnConnect) {
                struct sCP56Time2a now{};
                CP56Time2a_createFromMsTimestamp(&now, Hal_getTimeInMs());
                bool sent = false;
                {
                    std::lock_guard<std::mutex> operationLock(*operationMutex);
                    sent = CS104_Connection_sendClockSyncCommand(connection, cfg.commonAddress, &now);
                }
                logSink_("IEC104 clock sync device=" + deviceId + " sent=" + std::string(sent ? "true" : "false"));
            }
        } else {
            devices_.markDisconnected(deviceId, "connect failed");
            eventSink_(statusEvent(deviceId, false, "disconnected", "connect failed"));
        }

        bool commandTimedOut = false;
        while (running_ && connected) {
            if (!devices_.shouldContinueRunning(deviceId)) break;
            if (processCommandWork(deviceId, connectionGeneration, connection, operationMutex)) {
                commandTimedOut = true;
                break;
            }
            bool stillConnected = false;
            {
                std::lock_guard<std::mutex> operationLock(*operationMutex);
                stillConnected = CS104_Connection_isConnected(connection);
            }
            if (!stillConnected) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }

        failPendingCommand(deviceId, connectionGeneration, commandTimedOut ? "command-timeout" : "disconnected");
        flushQueuedEvents();

        {
            std::lock_guard<std::mutex> operationLock(*operationMutex);
            if (devices_.clearConnectionIfMatches(deviceId, connection)) CS104_Connection_close(connection);
            CS104_Connection_destroy(connection);
        }
        commands_.unblockAfterDisconnect(deviceId, connectionGeneration);
        eventSink_(statusEvent(deviceId, false, "disconnected"));

        if (!devices_.shouldReconnect(deviceId)) break;
        const int reconnectDelayMs = std::max(250, cfg.reconnectMs);
        for (int waitedMs = 0; waitedMs < reconnectDelayMs && devices_.shouldReconnect(deviceId); waitedMs += 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min(100, reconnectDelayMs - waitedMs)));
        }
    }
}

bool Lib60870Backend::processCommandWork(const std::string& deviceId, std::uint64_t connectionGeneration, CS104_Connection connection, const std::shared_ptr<std::mutex>& operationMutex) {
    if (takeReconnectRequest(connectionGeneration)) {
        flushQueuedEvents();
        return true;
    }
    auto executeAction = commands_.takeQueuedExecute(deviceId, connectionGeneration);
    if (executeAction.kind == CommandActionKind::Failure) {
        queueCommandResult(executeAction.command, false, "", executeAction.error, executeAction.phase, 0);
        flushQueuedEvents();
        return true;
    }
    if (executeAction.kind == CommandActionKind::SendExecute) {
        const PendingCommand& command = executeAction.command;
        InformationObject execute = createCommandObject(command.tag, command.requestedValue, false);
        bool sent = false;
        bool expiredBeforeSend = false;
        if (execute) {
            std::lock_guard<std::mutex> operationLock(*operationMutex);
            const bool currentConnection = devices_.isCurrentConnection(deviceId, connection, connectionGeneration);
            expiredBeforeSend = std::chrono::steady_clock::now() >= command.deadline;
            if (currentConnection && !expiredBeforeSend) {
                sent = CS104_Connection_sendProcessCommandEx(connection, CS101_COT_ACTIVATION, command.fingerprint.commonAddress, execute);
            }
            InformationObject_destroy(execute);
        }
        logSink_("IEC104 write execute device=" + deviceId + " requestId=" + command.requestId + " sent=" + std::string(sent ? "true" : "false"));
        if (!sent) {
            failPendingCommand(deviceId, connectionGeneration, expiredBeforeSend ? "command-timeout" : "send-failed");
            if (expiredBeforeSend) {
                flushQueuedEvents();
                return true;
            }
        }
    }

    flushQueuedEvents();
    auto expired = commands_.expire(deviceId, connectionGeneration, std::chrono::steady_clock::now());
    if (!expired) return false;
    queueCommandResult(*expired, false, "", "command-timeout", expired->phase == CommandPhase::AwaitingSelectConfirmation ? "select" : "execute", 0);
    flushQueuedEvents();
    return true;
}

void Lib60870Backend::logAsduReceived(const std::string& deviceId, TypeID type, CS101_CauseOfTransmission cot, int ca, int count) {
    logSink_("IEC104 asdu device=" + deviceId + " type=" + typeName(type) + " cot=" + std::to_string(static_cast<int>(cot)) + " ca=" + std::to_string(ca) + " count=" + std::to_string(count));
}

void Lib60870Backend::emitValue(const std::string& deviceId, int ioa, const std::string& asduType, double value, uint8_t quality, uint64_t timestampMs, CS101_CauseOfTransmission cot) {
    std::string tagId = devices_.tagIdForIoa(deviceId, ioa);
    logSink_("IEC104 value device=" + deviceId + " ioa=" + std::to_string(ioa) + " type=" + asduType + " value=" + std::to_string(value) + " quality=" + std::to_string(quality) + " tagId=" + (tagId.empty() ? "<unmatched>" : tagId));
    eventSink_(valueEvent(deviceId, ioa, tagId, asduType, value, quality, timestampMs, static_cast<int>(cot)));
}

void Lib60870Backend::handleCommandConfirmation(const CommandConfirmation& confirmation) {
    auto action = commands_.confirm(confirmation);
    if (action.kind == CommandActionKind::None || action.kind == CommandActionKind::SendExecute) return;
    if (action.kind == CommandActionKind::Success) {
        logSink_("IEC104 write confirmed device=" + confirmation.deviceId + " requestId=" + action.command.requestId + " phase=" + action.phase + " cot=" + std::to_string(action.causeOfTransmission));
        queueCommandResult(action.command, true, "activation-confirmed", "", action.phase, action.causeOfTransmission);
        return;
    }
    logSink_("IEC104 write rejected device=" + confirmation.deviceId + " requestId=" + action.command.requestId + " phase=" + action.phase + " cot=" + std::to_string(action.causeOfTransmission) + " error=" + action.error);
    queueCommandResult(action.command, false, "activation-confirmation", action.error, action.phase, action.causeOfTransmission);
    if (action.error == "command-timeout") requestReconnect(confirmation.connectionGeneration);
}

void Lib60870Backend::handleConnectionEvent(const std::string& deviceId, std::uint64_t connectionGeneration, CS104_ConnectionEvent event) {
    if (event == CS104_CONNECTION_STARTDT_CON_RECEIVED) {
        devices_.markConnected(deviceId);
        queueEvent(statusEvent(deviceId, true, "running"));
        return;
    }
    if (event == CS104_CONNECTION_OPENED) {
        queueEvent(statusEvent(deviceId, false, "connected"));
        return;
    }
    if (event == CS104_CONNECTION_CLOSED || event == CS104_CONNECTION_FAILED) {
        failPendingCommand(deviceId, connectionGeneration, event == CS104_CONNECTION_FAILED ? "connect-failed" : "disconnected");
        devices_.markDisconnected(deviceId, event == CS104_CONNECTION_FAILED ? "connect failed" : "");
        queueEvent(statusEvent(deviceId, false, "disconnected", event == CS104_CONNECTION_FAILED ? "connect failed" : ""));
    }
}

void Lib60870Backend::failPendingCommand(const std::string& deviceId, std::uint64_t connectionGeneration, const std::string& error) {
    auto command = error == "command-timeout" ? commands_.timeout(deviceId, connectionGeneration) : commands_.fail(deviceId, connectionGeneration);
    if (!command) return;
    queueCommandResult(*command, false, "", error, command->phase == CommandPhase::AwaitingSelectConfirmation ? "select" : "execute", 0);
}

void Lib60870Backend::queueCommandResult(const PendingCommand& command, bool ok, const std::string& cause, const std::string& error, const std::string& phase, int cot) {
    queueEvent(writeResultEvent(command.deviceId, command.requestId, command.tag, ok, cause, error, phase, cot));
}

void Lib60870Backend::queueEvent(std::string event) {
    std::lock_guard<std::mutex> lock(eventMutex_);
    queuedEvents_.push_back(std::move(event));
}

void Lib60870Backend::flushQueuedEvents() {
    for (const auto& event : pollEvents()) eventSink_(event);
}

void Lib60870Backend::requestReconnect(std::uint64_t connectionGeneration) {
    std::lock_guard<std::mutex> lock(reconnectMutex_);
    reconnectGenerations_.insert(connectionGeneration);
}

bool Lib60870Backend::takeReconnectRequest(std::uint64_t connectionGeneration) {
    std::lock_guard<std::mutex> lock(reconnectMutex_);
    return reconnectGenerations_.erase(connectionGeneration) > 0;
}

#endif
