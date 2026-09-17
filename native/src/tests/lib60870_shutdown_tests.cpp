#include "lib60870_backend.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void stopDuringConnectionSetupDoesNotReuseDestroyedConnection() {
    for (int attempt = 0; attempt < 25; ++attempt) {
        DeviceRegistry registry;
        std::atomic<bool> running{true};
        Lib60870Backend backend(registry, running, [](const std::string&) {}, [](const std::string&) {});
        ConnectionConfig connection;
        connection.remoteAddress = "127.0.0.1";
        connection.remotePort = 1;
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
