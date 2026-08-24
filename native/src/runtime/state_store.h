#pragma once

#include "gateway_models.h"

#include <mutex>
#include <string>
#include <vector>

struct PersistedDevice {
    std::string deviceId;
    std::vector<TagConfig> tags;
    ConnectionConfig connection;
    bool desiredRunning = false;
};

class StateStore {
public:
    explicit StateStore(std::string path);

    void load();
    void updateConfig(const std::string& deviceId, const std::vector<TagConfig>& tags, const ConnectionConfig& connection);
    void updateDesiredRunning(const std::string& deviceId, bool desiredRunning);
    std::vector<PersistedDevice> devices() const;
    const std::string& path() const { return path_; }

private:
    void saveLocked() const;

    std::string path_;
    mutable std::mutex mutex_;
    std::vector<PersistedDevice> devices_;
};
