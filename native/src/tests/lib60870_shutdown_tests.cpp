#include "lib60870_backend.h"
#include "socket_utils.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class LoopbackPeer {
public:
    LoopbackPeer() {
#ifdef _WIN32
        WSADATA wsaData{};
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) throw std::runtime_error("WSAStartup failed");
#endif
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener_ == invalid_socket_value) throw std::runtime_error("cannot create loopback listener");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listener_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || listen(listener_, 1) != 0) throw std::runtime_error("cannot listen on loopback");
#ifdef _WIN32
        int length = sizeof(address);
#else
        socklen_t length = sizeof(address);
#endif
        if (getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length) != 0) throw std::runtime_error("cannot read loopback port");
        port_ = ntohs(address.sin_port);
        peer_ = std::thread([this] {
            fd_set sockets;
            FD_ZERO(&sockets);
            FD_SET(listener_, &sockets);
            timeval timeout{2, 0};
            if (select(static_cast<int>(listener_) + 1, &sockets, nullptr, nullptr, &timeout) <= 0) return;
            socket_t client = accept(listener_, nullptr, nullptr);
            closeSocket(client);
        });
    }

    ~LoopbackPeer() {
        if (peer_.joinable()) peer_.join();
        closeSocket(listener_);
#ifdef _WIN32
        WSACleanup();
#endif
    }

    int port() const { return port_; }

private:
    socket_t listener_ = invalid_socket_value;
    std::thread peer_;
    int port_ = 0;
};

void stopDuringConnectionSetupDoesNotReuseDestroyedConnection() {
    for (int attempt = 0; attempt < 25; ++attempt) {
        LoopbackPeer peer;
        DeviceRegistry registry;
        std::atomic<bool> running{true};
        Lib60870Backend backend(registry, running, [](const std::string&) {}, [](const std::string&) {});
        ConnectionConfig connection;
        connection.remoteAddress = "127.0.0.1";
        connection.remotePort = peer.port();
        connection.timeoutMs = 100;
        connection.reconnectMs = 1;

        backend.configure("device-1", {}, connection);
        backend.start("device-1");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        backend.stop("device-1");
        require(!registry.status("device-1").running, "stopped device remained running");
    }
}

} // namespace

int main() {
    try {
        stopDuringConnectionSetupDoesNotReuseDestroyedConnection();
        std::cout << "lib60870 shutdown tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "lib60870 shutdown test failed: " << e.what() << '\n';
        return 1;
    }
}
