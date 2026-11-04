// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "app/SimPackages.hpp"
#include "sim/AircraftId.hpp"
#include "sim/FsArchive.hpp"
#include "util/Log.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

#include <nlohmann/json.hpp>

namespace lr::app {

namespace fs = std::filesystem;

namespace {

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The child of `dir` whose name is `name` ignoring the case (Windows paths do not keep the case of the files on disk).
std::optional<fs::path> childNoCase(const fs::path& dir, const std::string& name) {
    std::error_code ec;
    const fs::path exact = dir / name;
    if (fs::exists(exact, ec)) return exact;
    const std::string want = sim::lowerAscii(name);
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (sim::lowerAscii(e.path().filename().string()) == want) return e.path();
    return std::nullopt;
}

std::vector<std::string> steamLibraries() {
    std::vector<std::string> out;
    const char* home = std::getenv("HOME");
    if (!home) return out;
    const std::string h = home;
    for (const std::string& root : {h + "/.local/share/Steam", h + "/.steam/steam", h + "/.var/app/com.valvesoftware.Steam/.local/share/Steam"}) {
        std::error_code ec;
        if (!fs::is_directory(root + "/steamapps", ec)) continue;
        out.push_back(fs::weakly_canonical(root, ec).string());
        // libraryfolders.vdf: "path"		"/mnt/games/Steam" lines, one per library
        std::istringstream vdf(readFile(root + "/steamapps/libraryfolders.vdf"));
        std::string line;
        while (std::getline(vdf, line)) {
            const size_t k = line.find("\"path\"");
            if (k == std::string::npos) continue;
            const size_t a = line.find('"', k + 6);
            const size_t b = a == std::string::npos ? a : line.find('"', a + 1);
            if (b == std::string::npos) continue;
            std::string p;
            for (size_t i = a + 1; i < b; ++i) {
                if (line[i] == '\\' && i + 1 < b) ++i;  // "\\" in the file is one backslash
                p += line[i];
            }
            out.push_back(fs::weakly_canonical(p, ec).string());
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

}  // namespace

std::string packagesPathFromUserCfg(const std::string& text, const std::string& pfx) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const size_t k = line.find("InstalledPackagesPath");
        if (k == std::string::npos) continue;
        const size_t a = line.find('"', k);
        const size_t b = a == std::string::npos ? a : line.find('"', a + 1);
        if (b == std::string::npos || b - a < 4) continue;
        std::string win = line.substr(a + 1, b - a - 1);
        if (win.size() < 3 || win[1] != ':') continue;
        // drive letters are links in the prefix (dosdevices/c: -> ../drive_c, z: -> /)
        fs::path p = fs::path(pfx) / "dosdevices" / (std::string(1, static_cast<char>(std::tolower(static_cast<unsigned char>(win[0])))) + ":");
        std::string rest = win.substr(2);
        std::replace(rest.begin(), rest.end(), '\\', '/');
        std::istringstream parts(rest);
        std::string part;
        while (std::getline(parts, part, '/')) {
            if (part.empty()) continue;
            const auto c = childNoCase(p, part);
            p = c ? *c : p / part;
        }
        std::error_code ec;
        if (fs::is_directory(p, ec)) return fs::weakly_canonical(p, ec).string();
    }
    return {};
}

std::vector<std::string> simPackageRoots() {
    std::vector<std::string> out;
    if (const char* env = std::getenv("MSFS_PACKAGES_DIRS"); env && *env) {
        std::istringstream in(env);
        std::string d;
        while (std::getline(in, d, ':'))
            if (!d.empty()) out.push_back(d);
        return out;
    }
    // Steam app id and the folder name of its settings in AppData/Roaming
    const std::pair<const char*, const char*> sims[] = {{"2537590", "Microsoft Flight Simulator 2024"}, {"1250410", "Microsoft Flight Simulator"}};
    for (const auto& lib : steamLibraries()) {
        for (const auto& [app, folder] : sims) {
            const std::string pfx = lib + "/steamapps/compatdata/" + app + "/pfx";
            const std::string cfg = readFile(pfx + "/drive_c/users/steamuser/AppData/Roaming/" + folder + "/UserCfg.opt");
            if (cfg.empty()) continue;
            const std::string dir = packagesPathFromUserCfg(cfg, pfx);
            if (!dir.empty() && std::find(out.begin(), out.end(), dir) == out.end()) out.push_back(dir);
        }
    }
    return out;
}

std::optional<AircraftPackage> findAircraftPackage(const std::vector<std::string>& roots, const std::string& aircraftId) {
    // aircraft key -> (priority, package); built once per set of roots, a package folder has a few hundred packages
    struct Found { int priority; AircraftPackage pkg; };
    static std::mutex mu;
    static std::vector<std::string> indexedRoots;
    static std::map<std::string, Found> index;
    std::lock_guard<std::mutex> lock(mu);
    if (indexedRoots != roots) {
        index.clear();
        indexedRoots = roots;
        for (const auto& root : roots) {
            std::error_code ec;
            // packages sit one to three levels down (Community/<p>, Official2024/Steam/<p>); a package is a folder with simobjects/
            std::vector<std::pair<fs::path, int>> todo{{fs::path(root), 0}};
            while (!todo.empty()) {
                const auto [dir, depth] = todo.back();
                todo.pop_back();
                const auto simobjects = depth > 0 ? childNoCase(dir, "simobjects") : std::nullopt;
                if (simobjects && fs::is_directory(*simobjects, ec)) {
                    const std::string rel = sim::lowerAscii(fs::relative(dir, root, ec).string());
                    const int priority = rel.rfind("community", 0) == 0 ? 0 : rel.rfind("official", 0) == 0 ? 1 : 2;
                    for (const auto& kind : fs::directory_iterator(*simobjects, ec)) {
                        if (!kind.is_directory(ec)) continue;
                        for (const auto& ac : fs::directory_iterator(kind.path(), ec)) {
                            if (!ac.is_directory(ec)) continue;
                            const std::string key = sim::aircraftKey(ac.path().filename().string());
                            auto it = index.find(key);
                            if (it != index.end() && it->second.priority <= priority) continue;
                            index[key] = {priority, {dir.string(), dir.filename().string(), ac.path().filename().string()}};
                        }
                    }
                    continue;
                }
                if (depth >= 3) continue;
                for (const auto& e : fs::directory_iterator(dir, ec))
                    if (e.is_directory(ec)) todo.emplace_back(e.path(), depth + 1);
            }
        }
        LOG_INFO("Sim packages: %zu aircraft folders in %zu package folder(s)", index.size(), roots.size());
    }
    const auto it = index.find(sim::aircraftKey(aircraftId));
    if (it == index.end()) return std::nullopt;
    return it->second.pkg;
}

bool parseLocpak(const std::string& text, std::map<std::string, std::string>& out) {
    try {
        const auto j = nlohmann::json::parse(text);
        const auto& s = j.at("LocalisationPackage").at("Strings");
        for (auto it = s.begin(); it != s.end(); ++it)
            if (it.value().is_string()) out[it.key()] = it.value().get<std::string>();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

PackageText readPackageText(const AircraftPackage& pkg) {
    PackageText t;
    const fs::path dir(pkg.dir);
    std::string err;
    // tooltips: inside minimal.fsarchive (2024), else a loose en-US.locPak (2020, add-ons)
    if (const auto m = childNoCase(dir, "minimal.fsarchive")) {
        sim::FsArchive a;
        std::string text;
        if (!a.open(m->string(), err)) {
            LOG_INFO("%s", err.c_str());
        } else if (const auto* f = a.find("en-us.locpak"); f && a.read(*f, text, err) && parseLocpak(text, t.strings)) {
            t.sources.push_back(pkg.name + "/minimal.fsarchive, en-us.locpak: the aircraft's English tooltips");
        }
    }
    if (t.strings.empty())
        if (const auto l = childNoCase(dir, "en-US.locPak"); l && parseLocpak(readFile(*l), t.strings))
            t.sources.push_back(pkg.name + "/" + l->filename().string() + ": the aircraft's English tooltips");
    // the sim's own checklist, in the unencrypted html_ui archive of some 2024 packages
    if (const auto h = childNoCase(dir, "html_ui")) {
        if (const auto ha = childNoCase(*h, "html_ui.fsarchive")) {
            sim::FsArchive a;
            if (a.open(ha->string(), err)) {
                const std::string want = sim::lowerAscii(pkg.folder);
                const sim::FsArchiveFile* best = nullptr;
                for (const auto& f : a.files()) {
                    const std::string p = sim::lowerAscii(f.path);
                    if (p.find("\\checklist\\") == std::string::npos || p.size() < 4 || p.compare(p.size() - 4, 4, ".xml") != 0 || !f.stored()) continue;
                    if (!best || (p.find(want) != std::string::npos && sim::lowerAscii(best->path).find(want) == std::string::npos)) best = &f;
                }
                std::string xml;
                if (best && a.read(*best, xml, err) && xml.find("<Checklist") != std::string::npos && xml.find("<Group") != std::string::npos) {
                    t.checklistXml = std::move(xml);
                    t.checklistSource = pkg.name + "/html_ui/html_ui.fsarchive, " + best->path;
                    t.sources.push_back(t.checklistSource + ": the sim's own checklist");
                }
            }
        }
    }
    const auto get = [&](const char* k) { auto it = t.strings.find(k); return it == t.strings.end() ? std::string() : it->second; };
    const std::string maker = get("AIRCRAFT.UI_MANUFACTURER"), model = get("AIRCRAFT.UI_MODEL");
    std::string name = model;
    if (name.empty())  // some packages name the model only per variant: the package's own title then ("PC12-NGX")
        if (const auto m = childNoCase(dir, "manifest.json")) {
            try {
                name = nlohmann::json::parse(readFile(*m)).value("title", "");
            } catch (const std::exception&) {
            }
        }
    t.title = maker.empty() ? name : name.empty() ? maker : maker + " " + name;
    return t;
}

}  // namespace lr::app
