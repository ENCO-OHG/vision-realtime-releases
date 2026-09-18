#pragma once

#include "socket_utils.h"

#include <functional>
#include <mutex>
#include <string>
#include <vector>

class WebSocketBroadcaster {
public:
    void addClient(socket_t socket);
    bool addClientWithSnapshot(socket_t socket, const std::function<std::vector<std::string>()>& snapshotFactory);
    void removeClient(socket_t socket);
    bool sendText(socket_t socket, const std::string& text);
    void broadcast(const std::string& event);

private:
    std::mutex mutex_;
    std::vector<socket_t> clients_;
};
