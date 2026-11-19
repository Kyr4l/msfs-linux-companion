// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "mcdu/McduSimLink.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "util/Isolated.hpp"
#include "util/Log.hpp"

namespace lr::mcdu {

namespace {
using json = nlohmann::json;

constexpr uint16_t kPort = 8380;
constexpr size_t kMaxMessage = 512u << 10;   // a page is a few kB: anything larger is not what this link expects
constexpr size_t kMaxBuffer = 1u << 20;
constexpr auto kRetry = std::chrono::seconds(5);

struct Style { Color color; bool small; };

bool colorByName(std::string_view n, Color& c) {
    static const std::pair<const char*, Color> kNames[] = {{"amber", Color::Amber}, {"red", Color::Red}, {"green", Color::Green}, {"cyan", Color::Cyan},
                                                           {"white", Color::White}, {"magenta", Color::Magenta}, {"yellow", Color::Yellow}, {"inop", Color::Grey}};
    for (const auto& [name, color] : kNames)
        if (n == name) { c = color; return true; }
    return false;
}

std::string text(const json& o, const char* key) {
    const auto it = o.find(key);
    return it != o.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// Spaces leave what is underneath: the left, right and centre columns of a line overlap.
void place(Screen& s, int row, int col, const std::vector<Cell>& cells) {
    for (const Cell& c : cells) {
        if (col >= kCols) break;
        if (col >= 0 && c.ch != U' ') s.at(row, col) = c;
        ++col;
    }
}

void placeColumns(Screen& s, int row, const std::string& left, const std::string& right, const std::string& center, bool smallByDefault) {
    const std::vector<Cell> c = parseMarkup(center, smallByDefault), l = parseMarkup(left, smallByDefault), r = parseMarkup(right, smallByDefault);
    place(s, row, (kCols - static_cast<int>(c.size())) / 2, c);
    place(s, row, 0, l);
    place(s, row, kCols - static_cast<int>(r.size()), r);
}
}  // namespace

std::vector<Cell> parseMarkup(std::string_view input, bool smallByDefault) {
    std::vector<Cell> out;
    std::vector<Style> stack{{Color::White, smallByDefault}};
    const std::vector<char32_t> chars = decodeUtf8(input);
    for (size_t i = 0; i < chars.size(); ++i) {
        if (chars[i] == U'{') {
            size_t end = i + 1;
            std::string name;
            while (end < chars.size() && chars[end] != U'}' && chars[end] < 0x80 && name.size() < 12) name.push_back(static_cast<char>(chars[end++]));
            if (end < chars.size() && chars[end] == U'}') {
                Color c;
                Style st = stack.back();
                bool known = true;
                if (name == "end") { if (stack.size() > 1) stack.pop_back(); }
                else if (name == "sp") out.push_back(Cell{U' ', st.color, st.small});
                else if (name == "small") { st.small = true; stack.push_back(st); }
                else if (name == "big") { st.small = false; stack.push_back(st); }
                else if (name == "left" || name == "right") stack.push_back(st);
                else if (colorByName(name, c)) { st.color = c; stack.push_back(st); }
                else known = false;
                if (stack.size() > 16) stack.resize(16);
                if (known) { i = end; continue; }
            }
        }
        char32_t ch = chars[i];
        if (ch == 0xa0) ch = U' ';
        else if (ch == 0x25a1) ch = 0x2610;  // the aircraft's empty box
        out.push_back(Cell{ch, stack.back().color, stack.back().small});
    }
    return out;
}

bool parseSimBridgeUpdate(std::string_view message, Screen& screen, LampState& lamps) {
    if (message.size() > kMaxMessage) return false;
    const json doc = json::parse(message.begin(), message.end(), nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) return false;
    const auto sideIt = doc.find("left");
    if (sideIt == doc.end() || !sideIt->is_object()) return false;
    const json& side = *sideIt;

    Screen s;
    placeColumns(s, 0, text(side, "titleLeft"), text(side, "page"), text(side, "title"), false);
    if (const auto lines = side.find("lines"); lines != side.end() && lines->is_array()) {
        for (size_t i = 0; i < lines->size() && i < 12; ++i) {
            const json& cols = (*lines)[i];
            if (!cols.is_array()) continue;
            auto col = [&](size_t n) { return n < cols.size() && cols[n].is_string() ? cols[n].get<std::string>() : std::string(); };
            placeColumns(s, static_cast<int>(i) + 1, col(0), col(1), col(2), i % 2 == 0);  // labels are the even entries, in the small font
        }
    }
    place(s, 13, 0, parseMarkup(text(side, "scratchpad"), false));
    if (const auto arrows = side.find("arrows"); arrows != side.end() && arrows->is_array() && arrows->size() >= 4) {
        auto on = [&](size_t n) { return (*arrows)[n].is_boolean() && (*arrows)[n].get<bool>(); };
        if (on(1)) s.at(13, 22) = Cell{0x2193, Color::White, false};
        if (on(0)) s.at(13, 23) = Cell{0x2191, Color::White, false};
        if (on(2)) s.at(0, 21) = Cell{0x2190, Color::White, false};
        if (on(3)) s.at(0, 22) = Cell{0x2192, Color::White, false};
    }
    screen = s;
    if (const auto a = side.find("annunciators"); a != side.end() && a->is_object()) {
        auto lamp = [&](const char* key) { const auto it = a->find(key); return it != a->end() && it->is_boolean() && it->get<bool>(); };
        const bool menu = lamp("mcdu_menu");
        lamps = {lamp("fail"), lamp("fmgc"), menu, menu, lamp("fm1"), lamp("ind"), lamp("rdy"), lamp("blank"), lamp("fm2")};
    }
    return true;
}

void SimLink::setWanted(bool wanted) {
    if (wanted == wanted_) return;
    wanted_ = wanted;
    if (!wanted) {
        disconnect();
        hasScreen_ = false;
    } else {
        nextAttempt_ = Clock::time_point{};
    }
}

void SimLink::disconnect() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    state_ = State::Off;
    rx_.clear();
    message_.clear();
    rx_.shrink_to_fit();
    message_.shrink_to_fit();
}

short SimLink::pollEvents() const {
    switch (state_) {
        case State::Connecting: return POLLOUT;
        case State::Handshake: case State::Open: return POLLIN;
        default: return 0;
    }
}

void SimLink::startConnect() {
    nextAttempt_ = Clock::now() + kRetry;
    if (isolatedRun()) return;
    fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd_ < 0) return;
    const int one = 1;
    setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kPort);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0 || errno == EINPROGRESS) state_ = State::Connecting;
    else disconnect();
}

void SimLink::process(short revents) {
    if (!wanted_) return;
    if (state_ == State::Off) {
        if (Clock::now() >= nextAttempt_) startConnect();
        return;
    }
    if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
        if (state_ == State::Open) LOG_INFO("MCDU: SimBridge closed the connection");
        disconnect();
        return;
    }
    if (state_ == State::Connecting && (revents & POLLOUT)) {
        int err = 0;
        socklen_t len = sizeof err;
        if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) { disconnect(); return; }
        static constexpr char kRequest[] =
            "GET /interfaces/v1/mcdu HTTP/1.1\r\nHost: 127.0.0.1:8380\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
        if (::send(fd_, kRequest, sizeof kRequest - 1, MSG_NOSIGNAL) != static_cast<ssize_t>(sizeof kRequest - 1)) { disconnect(); return; }
        state_ = State::Handshake;
        return;
    }
    if (revents & POLLIN) readAvailable();
}

void SimLink::readAvailable() {
    char buf[8192];
    for (int i = 0; i < 16; ++i) {
        const ssize_t n = ::recv(fd_, buf, sizeof buf, 0);
        if (n == 0) { disconnect(); return; }
        if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) disconnect();
            break;
        }
        rx_.append(buf, static_cast<size_t>(n));
        if (rx_.size() > kMaxBuffer) { LOG_WARN("MCDU: SimBridge sent too much, disconnecting"); disconnect(); return; }
        if (n < static_cast<ssize_t>(sizeof buf)) break;
    }
    if (state_ == State::Handshake && !parseHandshake()) return;
    if (state_ == State::Open) parseFrames();
}

bool SimLink::parseHandshake() {
    const size_t end = rx_.find("\r\n\r\n");
    if (end == std::string::npos) return false;
    if (rx_.compare(0, 12, "HTTP/1.1 101") != 0) { disconnect(); return false; }
    rx_.erase(0, end + 4);
    state_ = State::Open;
    LOG_INFO("MCDU: connected to SimBridge");
    sendFrame(1, "requestUpdate");
    return true;
}

void SimLink::parseFrames() {
    size_t p = 0;
    while (state_ == State::Open && rx_.size() - p >= 2) {
        const uint8_t b0 = static_cast<uint8_t>(rx_[p]), b1 = static_cast<uint8_t>(rx_[p + 1]);
        const uint8_t opcode = b0 & 0x0f;
        const bool fin = b0 & 0x80, masked = b1 & 0x80;
        uint64_t len = b1 & 0x7f;
        size_t head = 2;
        if (len == 126) {
            if (rx_.size() - p < 4) break;
            len = (static_cast<uint64_t>(static_cast<uint8_t>(rx_[p + 2])) << 8) | static_cast<uint8_t>(rx_[p + 3]);
            head = 4;
        } else if (len == 127) {
            if (rx_.size() - p < 10) break;
            len = 0;
            for (size_t i = 0; i < 8; ++i) len = (len << 8) | static_cast<uint8_t>(rx_[p + 2 + i]);
            head = 10;
        }
        if (len > kMaxMessage) { LOG_WARN("MCDU: oversized message from SimBridge"); disconnect(); return; }
        const size_t maskBytes = masked ? 4 : 0;
        if (rx_.size() - p < head + maskBytes + len) break;
        std::string payload = rx_.substr(p + head + maskBytes, static_cast<size_t>(len));
        if (masked)
            for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<char>(payload[i] ^ rx_[p + head + (i % 4)]);
        p += head + maskBytes + static_cast<size_t>(len);
        if (opcode == 0x8) { disconnect(); return; }
        if (opcode == 0x9) { sendFrame(0xa, payload); continue; }
        if (opcode == 0xa) continue;
        if (opcode == 0x1 || opcode == 0x0) {
            if (message_.size() + payload.size() > kMaxMessage) { disconnect(); return; }
            message_ += payload;
            if (fin) {
                handleMessage(message_);
                if (state_ != State::Open) return;
                message_.clear();
            }
        }
    }
    if (state_ == State::Open) rx_.erase(0, p);
}

void SimLink::handleMessage(std::string_view m) {
    if (m.rfind("update:", 0) == 0) {
        if (parseSimBridgeUpdate(m.substr(7), screen_, lamps_)) {
            hasScreen_ = true;
            changed_ = true;
        }
    } else if (m.rfind("mcduConnected", 0) == 0) {  // the aircraft came (back): it does not send its page by itself
        sendFrame(1, "requestUpdate");
    }
}

bool SimLink::takeChanged() {
    const bool c = changed_;
    changed_ = false;
    return c;
}

bool SimLink::sendKey(const char* keyId) {
    if (state_ != State::Open || !keyId || !*keyId) return false;
    return sendFrame(1, std::string("event:left:") + keyId);
}

bool SimLink::sendFrame(uint8_t opcode, std::string_view payload) {
    if (fd_ < 0 || payload.size() > 0xffff) return false;
    std::string f;
    f.reserve(payload.size() + 8);
    f.push_back(static_cast<char>(0x80 | opcode));
    if (payload.size() < 126) f.push_back(static_cast<char>(0x80 | payload.size()));
    else {
        f.push_back(static_cast<char>(0x80 | 126));
        f.push_back(static_cast<char>(payload.size() >> 8));
        f.push_back(static_cast<char>(payload.size() & 0xff));
    }
    // A client has to mask what it sends; on the loopback this needs no strength.
    maskCounter_ = maskCounter_ * 2654435761u + 7u;
    const char mask[4] = {static_cast<char>(maskCounter_), static_cast<char>(maskCounter_ >> 8), static_cast<char>(maskCounter_ >> 16), static_cast<char>(maskCounter_ >> 24)};
    f.append(mask, 4);
    for (size_t i = 0; i < payload.size(); ++i) f.push_back(static_cast<char>(payload[i] ^ mask[i % 4]));
    const ssize_t n = ::send(fd_, f.data(), f.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
    if (n != static_cast<ssize_t>(f.size())) {  // a socket this full means the other end stopped reading
        disconnect();
        return false;
    }
    return true;
}

}  // namespace lr::mcdu
