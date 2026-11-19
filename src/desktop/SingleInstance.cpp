// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "desktop/SingleInstance.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace lr::desktop {

namespace {
socklen_t socketAddress(sockaddr_un& addr) {
    std::memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    // sun_path[0] stays 0: the abstract namespace
    const int n = snprintf(addr.sun_path + 1, sizeof addr.sun_path - 1, "msfs-linux-companion-%u", static_cast<unsigned>(getuid()));
    return static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + static_cast<size_t>(n));
}
}  // namespace

SingleInstance::~SingleInstance() {
    if (fd_ >= 0) ::close(fd_);
}

bool SingleInstance::acquire() {
    sockaddr_un addr;
    const socklen_t len = socketAddress(addr);
    fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd_ < 0) return true;  // no socket: better one copy too many than none at all
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), len) == 0) {
        ::listen(fd_, 4);
        return true;
    }
    ::close(fd_);
    fd_ = -1;
    if (errno != EADDRINUSE) return true;
    const int c = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (c >= 0) {
        if (::connect(c, reinterpret_cast<sockaddr*>(&addr), len) == 0) {
            const ssize_t n = ::send(c, "show", 4, MSG_NOSIGNAL);
            (void)n;
        }
        ::close(c);
    }
    return false;
}

bool SingleInstance::showRequested() {
    if (fd_ < 0) return false;
    bool show = false;
    for (int i = 0; i < 8; ++i) {  // never more than a few at a time, whatever connects
        const int c = ::accept4(fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (c < 0) break;
        ucred peer{};
        socklen_t size = sizeof peer;
        const bool sameUser = getsockopt(c, SOL_SOCKET, SO_PEERCRED, &peer, &size) == 0 && peer.uid == getuid();
        pollfd p{c, POLLIN, 0};
        char buf[8] = {0};
        if (sameUser && ::poll(&p, 1, 50) > 0 && ::recv(c, buf, sizeof buf, 0) >= 4 && std::memcmp(buf, "show", 4) == 0) show = true;
        ::close(c);
    }
    return show;
}

}  // namespace lr::desktop
