// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Which local TCP listeners are worth a SimConnect handshake when the configured port does not answer.
#include <set>
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

}  // namespace lr::sim
