#pragma once

#include "device_registry.h"
#include "iec104_backend.h"

class MockBackend final : public Iec104Backend {
public:
    explicit MockBackend(DeviceRegistry& devices);

    BackendResult configure(const std::string& deviceId, const std::vector<TagConfig>& tags, const ConnectionConfig& connection) override;
    BackendResult start(const std::string& deviceId) override;
    BackendResult stop(const std::string& deviceId) override;
    BackendResult status(const std::string& deviceId) override;
    BackendResult write(const std::string& deviceId, const WriteRequest& request) override;
    BackendResult interrogate(const std::string& deviceId, int qualifier) override;
    std::vector<std::string> pollEvents() override;

private:
    DeviceRegistry& devices_;
};
