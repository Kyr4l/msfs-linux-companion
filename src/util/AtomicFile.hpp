// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Writing a file the user would miss if it were cut short (the config, the cached lists of an aircraft).
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace lr {

// The file at `path` is either what it was or the complete `text`: the text goes to path + ".tmp" first, is flushed and checked, and only then
// replaces the old file. A full disk or a directory that cannot be written leaves the old file alone (and no empty ".tmp" behind).
inline bool writeFileAtomically(const std::string& path, const std::string& text, std::string& err) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path target(path);
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);  // "config.json" has no directory to make
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) { err = "cannot write " + tmp; return false; }
        out << text;
        out.flush();
        if (!out) {
            out.close();
            fs::remove(tmp, ec);
            err = "cannot write " + tmp + " (disk full?)";
            return false;
        }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        err = "cannot replace " + path + ": " + ec.message();
        return false;
    }
    return true;
}

}  // namespace lr
