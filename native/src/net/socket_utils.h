#pragma once

#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
inline constexpr socket_t invalid_socket_value = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
inline constexpr socket_t invalid_socket_value = -1;
#endif

void closeSocket(socket_t socket);
bool sendAll(socket_t socket, const std::string& data);
