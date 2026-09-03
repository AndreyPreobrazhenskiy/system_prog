#pragma once
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <cstring>
#include <cerrno>
#include <cstdint>

constexpr unsigned short SERVER_PORT = 60002;
constexpr int SOCKET_TYPE = SOCK_STREAM;

constexpr int32_t STATUS_CORRECT = 0;  // Угадал
constexpr int32_t STATUS_TOO_LOW = 1;  // Мало
constexpr int32_t STATUS_TOO_HIGH = 2; // Много
constexpr int32_t STATUS_INVALID = -1; // Ошибка ввода

template<typename T>
T check(T result, const std::string& msg = "Socket error") {
    if (result < 0) {
        std::cerr << msg << ": " << strerror(errno) << std::endl;
        throw std::runtime_error(msg);
    }
    return result;
}

inline std::ostream& operator<<(std::ostream& s, const sockaddr_in& addr) {
    union { in_addr_t x; char c[sizeof(in_addr)]; } t{};
    t.x = addr.sin_addr.s_addr;
    return s << std::to_string(int(t.c[0])) << "."
             << std::to_string(int(t.c[1])) << "."
             << std::to_string(int(t.c[2])) << "."
             << std::to_string(int(t.c[3]))
             << ":" << std::to_string(ntohs(addr.sin_port));
}

inline int make_socket(int type) {
    switch(type) {
        case SOCK_STREAM: return socket(AF_INET, SOCK_STREAM, 0);
        default: errno = EINVAL; return -1;
    }
}

inline sockaddr_in local_addr(unsigned short port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return addr;
}

inline bool send_int32(int sock, int32_t value) {
    int32_t net_val = htonl(value); // Сетевой порядок байтов
    return send(sock, &net_val, sizeof(net_val), 0) == sizeof(net_val);
}

inline bool recv_int32(int sock, int32_t& out_value) {
    int32_t net_val = 0;
    ssize_t n = recv(sock, &net_val, sizeof(net_val), MSG_WAITALL);
    if (n != sizeof(net_val)) return false; // EOF или обрыв
    out_value = ntohl(net_val);
    return true;
}