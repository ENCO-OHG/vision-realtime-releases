#include "socket_utils.h"

void closeSocket(socket_t socket) {
    if (socket == invalid_socket_value) return;
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

bool sendAll(socket_t socket, const std::string& data) {
    const char* ptr = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0) {
#ifdef _WIN32
        int sent = send(socket, ptr, static_cast<int>(remaining), 0);
#else
        ssize_t sent = send(socket, ptr, remaining, 0);
#endif
        if (sent <= 0) return false;
        ptr += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
    return true;
}
