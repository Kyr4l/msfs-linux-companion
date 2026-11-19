// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Which local TCP listeners are worth a SimConnect handshake when the configured port does not answer.
#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace lr::sim {

struct ProbeTarget {
    std::string host;
    unsigned short port;
    unsigned long inode;
};

// Only the simulator's own (wineserver) sockets. With no Wine process nothing is probed: the simulator is not running,
// and other local services must not receive our handshake every few seconds. A wineserver whose sockets cannot be read
// is the one case where a few listeners are tried blindly.
inline std::vector<ProbeTarget> chooseProbeCandidates(const std::vector<ProbeTarget>& listeners, bool wineProcess,
                                                      const std::set<unsigned long>& wineInodes) {
    std::vector<ProbeTarget> out;
    if (!wineProcess) return out;
    for (const auto& l : listeners)
        if (wineInodes.empty() || wineInodes.count(l.inode)) out.push_back(l);
    constexpr size_t kBlindLimit = 6;
    if (wineInodes.empty() && out.size() > kBlindLimit) out.resize(kBlindLimit);
    return out;
}

// In a Flatpak the sandbox has its own process list: the wineserver and its sockets cannot be seen, but /proc/net/tcp
// (shared network) still lists every listener. Steam says when the simulator runs; then the newest sockets go first
// (the simulator opens its port long after the desktop's own services), a few per scan, and a listener that did not
// answer rests (`resting`) before it is tried again.
inline std::vector<ProbeTarget> chooseSandboxCandidates(std::vector<ProbeTarget> listeners, const std::set<unsigned long>& resting) {
    std::vector<ProbeTarget> out;
    std::sort(listeners.begin(), listeners.end(), [](const ProbeTarget& a, const ProbeTarget& b) { return a.inode > b.inode; });
    for (const auto& l : listeners) {
        if (resting.count(l.inode)) continue;
        if (std::any_of(out.begin(), out.end(), [&](const ProbeTarget& o) { return o.inode == l.inode; })) continue;  // 0.0.0.0 in tcp and tcp6
        out.push_back(l);
        if (out.size() == 6) break;
    }
    return out;
}

// Steam's logs/gameprocess_log.txt. A game starts with "AppID 2537590 adding PID 21835 as a tracked process" and ends with
// "Remove 2537590 from running list"; "Client version: ..." is a new Steam session (nothing runs yet). The PID lines
// themselves do not add up: many processes are never reported gone.
inline bool steamAppRunning(const std::string& log, const std::vector<std::string>& appIds) {
    std::set<std::string> running;
    std::istringstream in(log);
    std::string line;
    while (std::getline(in, line)) {
        if (line.find("] Client version:") != std::string::npos) {
            running.clear();
            continue;
        }
        for (const auto& app : appIds) {
            if (line.find("] AppID " + app + " adding PID ") != std::string::npos) running.insert(app);
            else if (line.find("] Remove " + app + " from running list") != std::string::npos) running.erase(app);
        }
    }
    return !running.empty();
}

}  // namespace lr::sim
