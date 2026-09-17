#pragma once

#include "gateway_models.h"
#include "json_utils.h"

#include <mutex>
#include <cstdint>
#include <string>
#include <vector>

struct PersistedDevice {
    std::string deviceId;
    std::vector<TagConfig> tags;
    ConnectionConfig connection;
    std::string desiredState;
};

struct DesiredState {
    std::string gatewayTargetId;
    std::string controllerId;
    std::uint64_t controllerGeneration = 0;
    std::uint64_t revision = 0;
    std::string hash;
    std::string requestId;
    std::vector<PersistedDevice> devices;
};

class StateStore {
public:
    explicit StateStore(std::string path);

    void load();
    bool replace(DesiredState desiredState);
    std::vector<PersistedDevice> devices() const;
    DesiredState desiredState() const;
    const std::string& path() const { return path_; }

private:
    void saveLocked() const;

    std::string path_;
    mutable std::mutex mutex_;
    std::vector<PersistedDevice> devices_;
    DesiredState desiredState_;
};

DesiredState parseDesiredState(const JsonValue& root);
std::string desiredStateHash(const DesiredState& desiredState);
