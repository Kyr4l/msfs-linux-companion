// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Direct hidraw access to Winwing (vendor 0x4098) devices for the vibration motors.
// Frame format (14 bytes, report ID 0x02): [0x02, device, family, 0, 0, len, opcode, args...].
// One USB device carries several logical controllers (e.g. left grip, right grip, base), each
// addressed by a (device, family) pair. Motor/LED level: opcode 0x49 (SET_LEDX) with [index, value];
// index 0 is the motor on a grip and the backlight on a base. Accepted frames are echoed on the
// vendor input report with 0x10 added to the family byte; a request sent to the broadcast
// address (device 0x01, family 0x00) makes every controller answer with its own id.
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace lr::haptics {

constexpr uint16_t WINWING_VID = 0x4098;

struct MotorAddress {
    int device = -1;  // -1 = unknown
    int index = 0;    // SET_LEDX index
    bool known() const { return device >= 0 && index >= 0; }
};

struct WinwingIdentity {
    int family = -1;
    MotorAddress left, right;  // right == left on single-motor sticks
    bool known() const { return family >= 0 && left.known() && right.known(); }
    bool singleMotor() const { return left.device == right.device && left.index == right.index; }
};

struct ControllerInfo {
    int device = 0, family = 0;
    std::string name;      // product name read from flash (offset 0x5c), may be empty
    uint32_t typeId = 0;   // flash 0x08
};

struct WinwingDeviceInfo {
    std::string key;    // "4098:b970"
    std::string path;   // /dev/hidrawN
    std::string name;
    uint16_t pid = 0;
    bool open = false;
    std::string error;
    WinwingIdentity identity;
    bool identityIsDefault = true;  // from the built-in table / auto discovery, not set by the user
    std::vector<ControllerInfo> controllers;  // from the last discovery
    uint8_t lastLeft = 0, lastRight = 0;
    bool motorsDriven = false;  // a non-zero level was sent at least once
    uint64_t framesWritten = 0;
};

// Built-in defaults by product id (community reverse engineering + discovery on real hardware).
WinwingIdentity defaultIdentityForPid(uint16_t pid);

class WinwingHidManager {
public:
    WinwingHidManager();
    ~WinwingHidManager();

    void rescan();
    int scanCount() const { return scans_.load(); }  // 0 until the first rescan() finished
    std::vector<WinwingDeviceInfo> devices() const;
    // The MCDU found by the last scan: its hidraw path and product id ("" when none). It is never opened here: it has its own protocol (mcdu/McduService.hpp).
    std::pair<std::string, uint16_t> mcduDevice() const;
    void setIdentity(const std::string& key, const WinwingIdentity& id);

    // Writes motor levels (0..255). Only sends frames when a value changed unless force is set.
    bool setMotors(const std::string& key, uint8_t left, uint8_t right, bool force = false);
    void allOff();  // sends level 0 to the motors of devices that were actually driven
    // Read-only dump of flash config words [first, last] of every controller (for a baseline / diff).
    std::vector<std::string> dumpConfig(const std::string& key, uint32_t first = 0, uint32_t last = 0x110);
    // Reads `length` bytes of a controller's stored settings (opcode 0x05, read only), e.g. its lighting. False when a part did not answer.
    bool readConfigBlock(const std::string& key, int device, int family, uint32_t offset, size_t length, std::vector<uint8_t>& out);
    // Saves one byte of a controller's stored settings (opcode 0x06 WRITE_CFG_DATA, one 4-byte word, as SimAppPro does). The only
    // write to stored settings in this program, and protected: the words around it are read first and handed to `keepBackup`
    // (with the offset of the first one) before anything is written; the word is written with that byte changed, the device must
    // acknowledge it, and the same words are read again: only that byte may differ, otherwise the original word is written back and
    // false returned.
    bool saveConfigByte(const std::string& key, int device, int family, uint32_t offset, int byte, uint8_t value,
                        const std::function<void(const std::vector<uint8_t>& words, uint32_t from)>& keepBackup, std::string& error);
    // Broadcast identity request; returns every controller behind the USB device with its name.
    std::vector<ControllerInfo> discover(const std::string& key);
    // SET_LEDX level on an explicit address (raw motor test / sweep).
    bool setLevel(const std::string& key, int device, int family, int index, int value);
    bool sendRaw(const std::string& key, const std::vector<uint8_t>& frame);

private:
    struct Dev {
        int fd = -1;
        WinwingDeviceInfo info;
    };
    bool writeFrame(Dev& d, const uint8_t* frame14);
    // Collects vendor replies (report 0x02) with the given opcode for up to ms, or until `done`
    // returns true for a reply (early exit).
    std::vector<std::vector<uint8_t>> collect(Dev& d, int opcode, int ms,
                                              const std::function<bool(const std::vector<uint8_t>&)>& done = {});
    bool readCfg(Dev& d, int device, int family, uint32_t offset, uint8_t out[4]);
    bool writeCfg(Dev& d, int device, int family, uint32_t offset, const uint8_t word[4]);  // true when the device acknowledged it
    std::vector<ControllerInfo> discoverLocked(Dev& d);
    void autoAssign(Dev& d);
    Dev* find(const std::string& key);

    mutable std::mutex m_;
    std::vector<Dev> devs_;
    std::pair<std::string, uint16_t> mcdu_{"", 0};
    std::vector<std::pair<std::string, WinwingIdentity>> overrides_;
    std::atomic<int> scans_{0};
    std::map<std::string, std::string> openErrorReported_;  // path -> the last open error that was logged (guarded by m_)
};

}  // namespace lr::haptics
