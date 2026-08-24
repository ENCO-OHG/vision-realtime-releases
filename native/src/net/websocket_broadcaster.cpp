#include "websocket_broadcaster.h"

#include <algorithm>
#include <cstdint>

void WebSocketBroadcaster::addClient(socket_t socket) {
    std::lock_guard<std::mutex> lock(mutex_);
    clients_.push_back(socket);
}

void WebSocketBroadcaster::removeClient(socket_t socket) {
    std::lock_guard<std::mutex> lock(mutex_);
    clients_.erase(std::remove(clients_.begin(), clients_.end(), socket), clients_.end());
}

bool WebSocketBroadcaster::sendText(socket_t socket, const std::string& text) {
    std::string frame;
    frame.push_back(static_cast<char>(0x81));
    if (text.size() < 126) {
        frame.push_back(static_cast<char>(text.size()));
    } else if (text.size() <= 0xffff) {
        frame.push_back(static_cast<char>(126));
        frame.push_back(static_cast<char>((text.size() >> 8) & 0xff));
        frame.push_back(static_cast<char>(text.size() & 0xff));
    } else {
        return false;
    }
    frame += text;
    return sendAll(socket, frame);
}

void WebSocketBroadcaster::broadcast(const std::string& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<socket_t> keep;
    for (socket_t client : clients_) {
        if (sendText(client, event)) keep.push_back(client);
        else closeSocket(client);
    }
    clients_ = std::move(keep);
}
