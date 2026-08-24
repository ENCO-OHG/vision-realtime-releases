#pragma once

#include "gateway_models.h"

#include <string>
#include <vector>

struct BackendResult {
    int status = 200;
    std::string body;
    std::vector<std::string> events;
};

struct WriteRequest {
    std::string requestId;
    std::string tagId;
    int ioa = 0;
    double value = 0.0;
    bool select = false;
};

class Iec104Backend {
public:
    virtual ~Iec104Backend() = default;

    virtual BackendResult configure(const std::string& deviceId, const std::vector<TagConfig>& tags, const ConnectionConfig& connection) = 0;
    virtual BackendResult start(const std::string& deviceId) = 0;
    virtual BackendResult stop(const std::string& deviceId) = 0;
    virtual BackendResult status(const std::string& deviceId) = 0;
    virtual BackendResult write(const std::string& deviceId, const WriteRequest& request) = 0;
    virtual BackendResult interrogate(const std::string& deviceId, int qualifier) = 0;
    virtual std::vector<std::string> pollEvents() = 0;
};
