// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "mcdu/SystemInfo.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>

#include <dirent.h>
#include <dlfcn.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "util/Isolated.hpp"

namespace lr::mcdu {

namespace {
constexpr int kDiskScanSeconds = 30;
constexpr int kNvmlRetrySeconds = 30;
constexpr size_t kMaxDisks = 16;
constexpr size_t kMaxGpus = 8;
constexpr size_t kGpuNameWidth = 14;

int64_t monotonicSeconds() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
}

bool readLine(const std::string& path, char* buf, size_t size) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    const bool ok = fgets(buf, static_cast<int>(size), f) != nullptr;
    fclose(f);
    return ok;
}

long long readNumber(const std::string& path, long long fallback) {
    char buf[64];
    if (!readLine(path, buf, sizeof buf)) return fallback;
    long long v = fallback;
    return sscanf(buf, "%lld", &v) == 1 ? v : fallback;
}

std::string readWord(const std::string& path) {
    char buf[64] = {0};
    if (!readLine(path, buf, sizeof buf)) return {};
    buf[strcspn(buf, " \n")] = 0;
    return buf;
}

// The CPU's temperature by the name of its sensor chip.
int readCpuTemp() {
    DIR* dir = opendir("/sys/class/hwmon");
    if (!dir) return -1;
    int temp = -1;
    while (dirent* e = readdir(dir)) {
        if (std::strncmp(e->d_name, "hwmon", 5) != 0) continue;
        const std::string base = std::string("/sys/class/hwmon/") + e->d_name;
        const std::string name = readWord(base + "/name");
        if (name != "k10temp" && name != "coretemp" && name != "zenpower") continue;
        const long long milli = readNumber(base + "/temp1_input", -1);
        if (milli >= 0) temp = static_cast<int>(milli / 1000);
    }
    closedir(dir);
    return temp;
}

double monotonicTime() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

// Every temperature but the GPUs': those come with their cards. In a fixed order, so that the page does not shuffle.
std::vector<SensorSample> readSensors() {
    std::vector<std::string> dirs;
    if (DIR* dir = opendir("/sys/class/hwmon")) {
        while (dirent* e = readdir(dir))
            if (std::strncmp(e->d_name, "hwmon", 5) == 0) dirs.push_back(std::string("/sys/class/hwmon/") + e->d_name);
        closedir(dir);
    }
    std::sort(dirs.begin(), dirs.end());
    std::vector<SensorSample> out;
    int drives = 0;
    for (const auto& base : dirs) {
        const std::string chip = readWord(base + "/name");
        if (chip == "amdgpu" || chip == "nouveau" || chip == "i915" || chip == "xe") continue;
        const long long milli = readNumber(base + "/temp1_input", -1);
        if (milli < 0) continue;
        std::string name;
        if (chip == "k10temp" || chip == "coretemp" || chip == "zenpower") name = "CPU";
        else if (chip == "nvme") name = "NVME" + std::to_string(++drives);
        else if (chip == "acpitz") name = "BOARD";
        else if (chip.rfind("mt7", 0) == 0 || chip.rfind("iwl", 0) == 0) name = "WIFI";
        else {
            name = chip.substr(0, 6);
            for (char& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        out.push_back({name, static_cast<int>(milli / 1000)});
    }
    return out;
}

// The name of a PCI device from the system's list of ids; "" when it is not there.
std::string pciName(unsigned vendor, unsigned device) {
    for (const char* path : {"/usr/share/hwdata/pci.ids", "/run/host/usr/share/hwdata/pci.ids", "/usr/share/misc/pci.ids"}) {
        FILE* f = fopen(path, "r");
        if (!f) continue;
        char line[512], wantVendor[8], wantDevice[8];
        snprintf(wantVendor, sizeof wantVendor, "%04x  ", vendor);
        snprintf(wantDevice, sizeof wantDevice, "\t%04x ", device);
        bool inVendor = false;
        std::string found;
        while (fgets(line, sizeof line, f)) {
            if (line[0] == '#' || line[0] == '\n') continue;
            if (line[0] != '\t') {
                if (inVendor) break;  // past this vendor's devices
                inVendor = std::strncmp(line, wantVendor, 6) == 0;
            } else if (inVendor && std::strncmp(line, wantDevice, 6) == 0) {
                found = line + 7;
                found.erase(found.find_last_not_of(" \r\n") + 1);
                found.erase(0, found.find_first_not_of(' '));
                break;
            }
        }
        fclose(f);
        if (!found.empty()) return found;
    }
    return {};
}

std::string unescapeMount(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 3 < s.size()) {
            out.push_back(static_cast<char>(std::strtol(s.substr(i + 1, 3).c_str(), nullptr, 8)));
            i += 3;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

bool startsWith(const std::string& s, const std::string& prefix) { return s.compare(0, prefix.size(), prefix) == 0; }

std::string diskName(const std::string& path) {
    std::string name = path.substr(path.find_last_of('/') + 1);
    for (char& c : name) c = c == '-' || c == '_' ? ' ' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return name.substr(0, 14);
}
}  // namespace

std::vector<DiskMount> parseMounts(const std::string& mountinfo, const std::string& home, bool sandboxed) {
    static const char* const kRealFs[] = {"ext2", "ext3", "ext4", "btrfs", "xfs", "f2fs", "ntfs", "ntfs3", "fuseblk", "exfat", "vfat", "zfs", "bcachefs", "jfs", "nilfs2"};
    static const char* const kSystemDirs[] = {"/boot", "/efi", "/usr", "/app", "/etc", "/var", "/opt", "/srv", "/root", "/tmp", "/.snapshots", "/run/host", "/proc", "/sys", "/dev"};
    struct Best { int rank; DiskMount mount; };
    std::map<std::string, Best> bySource;
    std::istringstream in(mountinfo);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        std::string id, parent, majmin, root, mountPoint, word;
        if (!(fields >> id >> parent >> majmin >> root >> mountPoint)) continue;
        while (fields >> word && word != "-") {}
        std::string fsType, source;
        if (!(fields >> fsType >> source)) continue;
        if (std::none_of(std::begin(kRealFs), std::end(kRealFs), [&](const char* f) { return fsType == f; })) continue;
        const std::string path = unescapeMount(mountPoint);

        int rank;
        std::string name;
        if ((!sandboxed && path == "/") || (sandboxed && path == "/etc/machine-id")) { rank = 0; name = "SYSTEM"; }
        else if (path == "/home" || (!home.empty() && (path == home || startsWith(path, home + "/")))) { rank = 1; name = "HOME"; }
        else if (startsWith(path, "/mnt/") || startsWith(path, "/media/") || startsWith(path, "/run/media/")) { rank = 2; name = diskName(path); }
        else if (sandboxed || path == "/mnt" || path == "/media") continue;  // the runtime's own files, and folders of the system disk
        else if (std::any_of(std::begin(kSystemDirs), std::end(kSystemDirs), [&](const char* d) { return path == d || startsWith(path, std::string(d) + "/"); })) continue;
        else { rank = 3; name = diskName(path); }

        auto it = bySource.find(source);
        if (it == bySource.end() || rank < it->second.rank) bySource[source] = Best{rank, DiskMount{name, path}};
    }
    std::vector<Best> sorted;
    for (auto& [source, best] : bySource) sorted.push_back(best);
    std::stable_sort(sorted.begin(), sorted.end(), [](const Best& a, const Best& b) { return a.rank != b.rank ? a.rank < b.rank : a.mount.name < b.mount.name; });
    std::vector<DiskMount> out;
    for (const Best& b : sorted)
        if (out.size() < kMaxDisks) out.push_back(b.mount);
    return out;
}

std::string shortGpuName(const std::string& pci) {
    std::string s = pci;
    const size_t open = s.find('['), close = s.find(']', open == std::string::npos ? 0 : open);
    if (open != std::string::npos && close != std::string::npos) s = s.substr(open + 1, close - open - 1);
    if (const size_t slash = s.find('/'); slash != std::string::npos) s.resize(slash);
    for (const char* brand : {"Radeon ", "GeForce ", "Intel "})
        if (startsWith(s, brand)) s.erase(0, std::strlen(brand));
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s.substr(0, kGpuNameWidth);
}

struct SystemInfo::Gpu {
    std::string dir, name;
    unsigned vendor = 0;
};

// The NVIDIA driver's own library, loaded while the pages are open. Without the driver the card is simply listed without numbers.
struct SystemInfo::Nvml {
    void* lib = nullptr;
    int (*shutdown)() = nullptr;
    int (*handle)(unsigned, void**) = nullptr;
    int (*utilization)(void*, unsigned*) = nullptr;
    int (*temperature)(void*, int, unsigned*) = nullptr;
    int (*memory)(void*, unsigned long long*) = nullptr;
    int (*power)(void*, unsigned*) = nullptr;   // optional: not every card reports it
    int (*fan)(void*, unsigned*) = nullptr;
    ~Nvml() {
        if (shutdown) shutdown();
        if (lib) dlclose(lib);
    }
};

SystemInfo::SystemInfo() = default;
SystemInfo::~SystemInfo() = default;

void SystemInfo::release() {
    nvml_.reset();
    nextNvmlTry_ = 0;
}

void SystemInfo::scanGpus() {
    gpusScanned_ = true;
    DIR* dir = opendir("/sys/bus/pci/devices");
    if (!dir) return;
    std::vector<std::string> addresses;
    while (dirent* e = readdir(dir))
        if (e->d_name[0] != '.') addresses.emplace_back(e->d_name);
    closedir(dir);
    std::sort(addresses.begin(), addresses.end());
    for (const auto& address : addresses) {
        const std::string base = "/sys/bus/pci/devices/" + address;
        if (readWord(base + "/class").rfind("0x03", 0) != 0 || gpus_.size() >= kMaxGpus) continue;
        Gpu g;
        g.dir = base;
        g.vendor = static_cast<unsigned>(std::strtoul(readWord(base + "/vendor").c_str(), nullptr, 16));
        const unsigned device = static_cast<unsigned>(std::strtoul(readWord(base + "/device").c_str(), nullptr, 16));
        g.name = shortGpuName(pciName(g.vendor, device));
        if (g.name.empty()) g.name = g.vendor == 0x1002 ? "AMD GPU" : g.vendor == 0x10de ? "NVIDIA GPU" : g.vendor == 0x8086 ? "INTEL GPU" : "GPU";
        gpus_.push_back(std::move(g));
    }
}

void SystemInfo::scanDisks() {
    std::ifstream in("/proc/self/mountinfo");
    std::stringstream text;
    text << in.rdbuf();
    const char* home = std::getenv("HOME");
    disks_ = parseMounts(text.str(), home ? home : "", access("/.flatpak-info", F_OK) == 0);
}

void SystemInfo::readNvidia(Gpu&, unsigned index, GpuSample& out) {
    if (isolatedRun()) return;
    const int64_t now = monotonicSeconds();
    if (!nvml_ && now >= nextNvmlTry_) {
        nextNvmlTry_ = now + kNvmlRetrySeconds;
        if (void* lib = dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL)) {
            auto n = std::make_unique<Nvml>();
            n->lib = lib;
            auto init = reinterpret_cast<int (*)()>(dlsym(lib, "nvmlInit_v2"));
            n->handle = reinterpret_cast<int (*)(unsigned, void**)>(dlsym(lib, "nvmlDeviceGetHandleByIndex_v2"));
            n->utilization = reinterpret_cast<int (*)(void*, unsigned*)>(dlsym(lib, "nvmlDeviceGetUtilizationRates"));
            n->temperature = reinterpret_cast<int (*)(void*, int, unsigned*)>(dlsym(lib, "nvmlDeviceGetTemperature"));
            n->memory = reinterpret_cast<int (*)(void*, unsigned long long*)>(dlsym(lib, "nvmlDeviceGetMemoryInfo"));
            if (init && n->handle && n->utilization && n->temperature && n->memory && init() == 0) {
                n->shutdown = reinterpret_cast<int (*)()>(dlsym(lib, "nvmlShutdown"));
                n->power = reinterpret_cast<int (*)(void*, unsigned*)>(dlsym(lib, "nvmlDeviceGetPowerUsage"));
                n->fan = reinterpret_cast<int (*)(void*, unsigned*)>(dlsym(lib, "nvmlDeviceGetFanSpeed"));
                nvml_ = std::move(n);
            }
        }
    }
    if (!nvml_) return;
    void* device = nullptr;
    if (nvml_->handle(index, &device) != 0) return;
    unsigned rates[2] = {0, 0}, temp = 0;
    unsigned long long memory[3] = {0, 0, 0};  // total, free, used
    if (nvml_->utilization(device, rates) == 0) out.loadPercent = static_cast<int>(rates[0]);
    if (nvml_->temperature(device, 0, &temp) == 0) out.tempC = static_cast<int>(temp);
    if (nvml_->memory(device, memory) == 0) {
        out.memTotalGb = static_cast<double>(memory[0]) / (1024.0 * 1024.0 * 1024.0);
        out.memUsedGb = static_cast<double>(memory[2]) / (1024.0 * 1024.0 * 1024.0);
    }
    unsigned milliwatts = 0, fanPercent = 0;
    if (nvml_->power && nvml_->power(device, &milliwatts) == 0) out.powerWatts = milliwatts / 1000.0;
    if (nvml_->fan && nvml_->fan(device, &fanPercent) == 0) out.fanPercent = static_cast<int>(fanPercent);
}

SystemSample SystemInfo::read() {
    SystemSample s;
    char line[256];
    if (readLine("/proc/stat", line, sizeof line)) {
        unsigned long long user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
        if (sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) >= 4) {
            const uint64_t busy = user + nice + system + irq + softirq + steal, total = busy + idle + iowait;
            if (prevTotal_ && total > prevTotal_) s.cpuPercent = 100.0 * static_cast<double>(busy - prevBusy_) / static_cast<double>(total - prevTotal_);
            prevBusy_ = busy;
            prevTotal_ = total;
        }
    }
    s.cpuTempC = readCpuTemp();
    if (readLine("/proc/loadavg", line, sizeof line)) sscanf(line, "%lf %lf %lf", &s.load1, &s.load5, &s.load15);
    if (FILE* f = fopen("/proc/meminfo", "r")) {
        unsigned long long total = 0, available = 0, swapTotal = 0, swapFree = 0, value = 0;
        char key[64];
        while (fgets(line, sizeof line, f)) {
            if (sscanf(line, "%63s %llu", key, &value) != 2) continue;
            if (!std::strcmp(key, "MemTotal:")) total = value;
            else if (!std::strcmp(key, "MemAvailable:")) available = value;
            else if (!std::strcmp(key, "SwapTotal:")) swapTotal = value;
            else if (!std::strcmp(key, "SwapFree:")) swapFree = value;
        }
        fclose(f);
        constexpr double kKbPerGb = 1024.0 * 1024.0;
        s.memTotalGb = static_cast<double>(total) / kKbPerGb;
        s.memAvailableGb = static_cast<double>(available) / kKbPerGb;
        s.memUsedGb = static_cast<double>(total - available) / kKbPerGb;
        s.swapTotalGb = static_cast<double>(swapTotal) / kKbPerGb;
        s.swapUsedGb = static_cast<double>(swapTotal - swapFree) / kKbPerGb;
    }
    if (readLine("/proc/uptime", line, sizeof line)) {
        double up = 0;
        if (sscanf(line, "%lf", &up) == 1) s.uptimeSeconds = static_cast<long>(up);
    }

    if (!gpusScanned_) scanGpus();
    unsigned nvidiaIndex = 0;
    for (Gpu& g : gpus_) {
        GpuSample out;
        out.name = g.name;
        // A card that sleeps stays asleep: asking it for numbers would wake it up.
        const bool asleep = readWord(g.dir + "/power/runtime_status") == "suspended";
        if (g.vendor == 0x10de) {
            if (!asleep) readNvidia(g, nvidiaIndex, out);
            ++nvidiaIndex;
        } else if (!asleep) {
            out.loadPercent = static_cast<int>(readNumber(g.dir + "/gpu_busy_percent", -1));
            const long long used = readNumber(g.dir + "/mem_info_vram_used", -1), total = readNumber(g.dir + "/mem_info_vram_total", -1);
            if (used >= 0 && total > 0) {
                out.memUsedGb = static_cast<double>(used) / (1024.0 * 1024.0 * 1024.0);
                out.memTotalGb = static_cast<double>(total) / (1024.0 * 1024.0 * 1024.0);
            }
            if (DIR* dir = opendir((g.dir + "/hwmon").c_str())) {
                while (dirent* e = readdir(dir)) {
                    if (std::strncmp(e->d_name, "hwmon", 5) != 0) continue;
                    const std::string hw = g.dir + "/hwmon/" + e->d_name;
                    const long long milli = readNumber(hw + "/temp1_input", -1);
                    if (milli >= 0) out.tempC = static_cast<int>(milli / 1000);
                    long long micro = readNumber(hw + "/power1_average", -1);
                    if (micro < 0) micro = readNumber(hw + "/power1_input", -1);
                    if (micro >= 0) out.powerWatts = static_cast<double>(micro) / 1e6;
                    out.fanRpm = static_cast<int>(readNumber(hw + "/fan1_input", -1));
                }
                closedir(dir);
            }
        }
        s.gpus.push_back(std::move(out));
    }
    s.sensors = readSensors();
    for (size_t i = 0; i < s.gpus.size(); ++i)
        if (s.gpus[i].tempC >= 0) s.sensors.push_back({"GPU " + std::to_string(i + 1), s.gpus[i].tempC});

    if (FILE* f = fopen("/proc/net/dev", "r")) {
        uint64_t down = 0, up = 0;
        char text[512];
        while (fgets(text, sizeof text, f)) {
            const char* colon = std::strchr(text, ':');
            if (!colon) continue;
            const char* name = text;
            while (*name == ' ') ++name;
            if (std::strncmp(name, "lo:", 3) == 0) continue;
            unsigned long long received = 0, sent = 0, skip = 0;
            if (sscanf(colon + 1, "%llu %llu %llu %llu %llu %llu %llu %llu %llu", &received, &skip, &skip, &skip, &skip, &skip, &skip, &skip, &sent) == 9) {
                down += received;
                up += sent;
            }
        }
        fclose(f);
        const double t = monotonicTime();
        if (prevNetTime_ > 0 && t > prevNetTime_ && down >= prevDown_ && up >= prevUp_) {
            s.downBytesPerSecond = static_cast<double>(down - prevDown_) / (t - prevNetTime_);
            s.upBytesPerSecond = static_cast<double>(up - prevUp_) / (t - prevNetTime_);
        }
        prevDown_ = down;
        prevUp_ = up;
        prevNetTime_ = t;
    }

    const int64_t now = monotonicSeconds();
    if (now >= nextDiskScan_) {
        nextDiskScan_ = now + kDiskScanSeconds;
        scanDisks();
    }
    for (const DiskMount& d : disks_) {
        struct statvfs fs{};
        if (statvfs(d.path.c_str(), &fs) != 0 || fs.f_blocks == 0) continue;
        DiskSample out;
        out.name = d.name;
        out.mount = d.path;
        const double unit = static_cast<double>(fs.f_frsize);
        const double used = static_cast<double>(fs.f_blocks - fs.f_bfree) * unit, available = static_cast<double>(fs.f_bavail) * unit;
        constexpr double kBytesPerGb = 1024.0 * 1024.0 * 1024.0;  // the same numbers as df and the file manager
        out.freeGb = available / kBytesPerGb;
        out.totalGb = static_cast<double>(fs.f_blocks) * unit / kBytesPerGb;
        out.usedPercent = used + available > 0 ? static_cast<int>(used * 100.0 / (used + available) + 0.5) : 0;
        s.disks.push_back(std::move(out));
    }
    return s;
}

}  // namespace lr::mcdu
