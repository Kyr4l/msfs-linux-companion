// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// What the System pages of the MCDU show. Read from /proc, /sys and the NVIDIA driver only while one of those pages is on screen.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lr::mcdu {

struct GpuSample {
    std::string name;
    int loadPercent = -1, tempC = -1;          // -1: not known (no driver, or the card sleeps and is left asleep)
    double memUsedGb = -1, memTotalGb = -1;
    double powerWatts = -1;
    int fanRpm = -1, fanPercent = -1;          // AMD reports turns a minute, NVIDIA a percentage
};

struct SensorSample {
    std::string name;
    int tempC = 0;
};

struct DiskSample {
    std::string name, mount;
    double freeGb = 0, totalGb = 0;
    int usedPercent = 0;
};

struct SystemSample {
    double cpuPercent = -1;
    std::vector<int> corePercent;             // every logical core, -1 until measured twice
    double cpuGhz = -1;                       // the fastest core right now
    double cpuWatts = -1;                     // the CPU package (RAPL); -1: not readable
    bool cpuPowerDenied = false;              // RAPL is there but root-only (the default since 2020)
    int cpuTempC = -1;
    double load1 = 0, load5 = 0, load15 = 0;
    double memUsedGb = 0, memAvailableGb = 0, memTotalGb = 0, swapUsedGb = 0, swapTotalGb = 0;
    bool zram = false;                        // swap lives in compressed memory
    double zramStoredGb = 0, zramMemoryGb = 0;  // what was swapped out, and the memory it takes once compressed
    long uptimeSeconds = 0;
    std::vector<GpuSample> gpus;
    std::vector<DiskSample> disks;
    std::vector<SensorSample> sensors;         // every temperature the computer reports
    double downBytesPerSecond = -1, upBytesPerSecond = -1;  // all network interfaces; -1: not measured yet
};

struct DiskMount {
    std::string name, path;
};
// The disks worth showing in a /proc/self/mountinfo text, one per device. In a Flatpak `/` is the sandbox's own: the host's system disk is found through a
// file the sandbox shares from it, the home disk through the folders it shares from there.
std::vector<DiskMount> parseMounts(const std::string& mountinfo, const std::string& home, bool sandboxed);
// "Navi 48 [Radeon RX 9070/9070 XT/9070 GRE]" -> "RX 9070", "TU116 [GeForce GTX 1660 SUPER]" -> "GTX 1660 SUPER"
std::string shortGpuName(const std::string& pciName);

class SystemInfo {
public:
    SystemInfo();
    ~SystemInfo();
    SystemSample read();
    void release();  // the pages are gone: lets go of the NVIDIA driver, which would keep the card awake

private:
    struct Gpu;
    struct Nvml;
    void scanGpus();
    void scanDisks();
    void readNvidia(Gpu& gpu, unsigned index, GpuSample& out);

    uint64_t prevBusy_ = 0, prevTotal_ = 0;
    std::vector<std::pair<uint64_t, uint64_t>> prevCores_;  // busy, total
    uint64_t prevEnergy_ = 0;
    double prevEnergyTime_ = 0;
    uint64_t prevDown_ = 0, prevUp_ = 0;
    double prevNetTime_ = 0;
    bool gpusScanned_ = false;
    std::vector<Gpu> gpus_;
    std::vector<DiskMount> disks_;
    int64_t nextDiskScan_ = 0;
    std::unique_ptr<Nvml> nvml_;
    int64_t nextNvmlTry_ = 0;
};

}  // namespace lr::mcdu
