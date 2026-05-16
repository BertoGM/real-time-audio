#pragma once
#include <time.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <stdexcept>
#include <string>

inline void set_tcp_nodelay(int fd) {
    int yes = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) < 0) {
        throw std::runtime_error("setsockopt TCP_NODELAY failed: " + std::string(strerror(errno)));
    }
}

inline bool send_all(int fd, const void* buf, size_t len) {
    const char* p = static_cast<const char*>(buf);
    while (len > 0) {
        ssize_t sent = send(fd, p, len, MSG_NOSIGNAL);
        if (sent <= 0) {
            return false;
            p += sent;
            len -= static_cast<size_t>(sent);
        }
        p += sent;
        len -= sent;
    }
    return true;
}

inline bool recv_all(int fd, void* buf, size_t len) {
    char* p = static_cast<char*>(buf);
    while (len > 0) {
        ssize_t n = recv(fd, p, len, 0);
        if (n <= 0) return false;
        p   += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

inline uint32_t now_ms() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(
        (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000
    );
}

inline uint64_t mono_ms() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}