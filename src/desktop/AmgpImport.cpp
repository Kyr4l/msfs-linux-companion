// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "desktop/AmgpImport.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>

#include <linux/input-event-codes.h>

namespace lr::desktop {

namespace {

constexpr size_t kMaxFileSize = 4u << 20;
constexpr int kMaxDepth = 32;
constexpr size_t kMaxNodes = 50000;

// As much XML as a profile uses: elements, attributes, text, comments. Limits keep a damaged file from costing anything.
struct Node {
    std::string name, text;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<std::unique_ptr<Node>> children;

    const Node* child(const char* n) const {
        for (const auto& c : children)
            if (c->name == n) return c.get();
        return nullptr;
    }
    std::string childText(const char* n) const {
        const Node* c = child(n);
        return c ? c->text : std::string();
    }
    std::string attribute(const char* n) const {
        for (const auto& [k, v] : attributes)
            if (k == n) return v;
        return {};
    }
    int index() const { return std::atoi(attribute("index").c_str()); }
};

class XmlReader {
public:
    explicit XmlReader(const std::string& s) : s_(s) {}

    std::unique_ptr<Node> root(std::string& err) {
        skipMisc();
        auto n = element(0);
        if (!n) err = error_.empty() ? "not an XML file" : error_;
        return n;
    }

private:
    void skipSpace() { while (p_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[p_]))) ++p_; }
    bool starts(const char* t) const { return s_.compare(p_, std::char_traits<char>::length(t), t) == 0; }
    void skipUntil(const char* t) {
        const size_t e = s_.find(t, p_);
        p_ = e == std::string::npos ? s_.size() : e + std::char_traits<char>::length(t);
    }
    void skipMisc() {
        for (;;) {
            skipSpace();
            if (starts("<?")) skipUntil("?>");
            else if (starts("<!--")) skipUntil("-->");
            else if (starts("<!")) skipUntil(">");
            else return;
        }
    }
    std::string name() {
        const size_t b = p_;
        while (p_ < s_.size() && !std::isspace(static_cast<unsigned char>(s_[p_])) && s_[p_] != '>' && s_[p_] != '/' && s_[p_] != '=') ++p_;
        return s_.substr(b, p_ - b);
    }
    static std::string unescape(std::string t) {
        static const std::pair<const char*, const char*> kEntities[] = {{"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}, {"&amp;", "&"}};
        for (const auto& [from, to] : kEntities)
            for (size_t i = t.find(from); i != std::string::npos; i = t.find(from, i + 1)) t.replace(i, std::char_traits<char>::length(from), to);
        return t;
    }
    std::unique_ptr<Node> element(int depth) {
        if (depth > kMaxDepth || ++nodes_ > kMaxNodes) { error_ = "the file is nested too deeply"; return nullptr; }
        if (p_ >= s_.size() || s_[p_] != '<') return nullptr;
        ++p_;
        auto n = std::make_unique<Node>();
        n->name = name();
        if (n->name.empty()) return nullptr;
        for (;;) {
            skipSpace();
            if (p_ >= s_.size()) return nullptr;
            if (s_[p_] == '/') { skipUntil(">"); return n; }
            if (s_[p_] == '>') { ++p_; break; }
            std::string key = name();
            if (key.empty()) return nullptr;
            skipSpace();
            if (p_ >= s_.size() || s_[p_] != '=') return nullptr;
            ++p_;
            skipSpace();
            if (p_ >= s_.size() || (s_[p_] != '"' && s_[p_] != '\'')) return nullptr;
            const char quote = s_[p_++];
            const size_t e = s_.find(quote, p_);
            if (e == std::string::npos) return nullptr;
            n->attributes.emplace_back(std::move(key), unescape(s_.substr(p_, e - p_)));
            p_ = e + 1;
        }
        for (;;) {
            const size_t lt = s_.find('<', p_);
            if (lt == std::string::npos) return nullptr;
            n->text += s_.substr(p_, lt - p_);
            p_ = lt;
            if (starts("</")) { skipUntil(">"); break; }
            if (starts("<!--")) { skipUntil("-->"); continue; }
            auto c = element(depth + 1);
            if (!c) return nullptr;
            n->children.push_back(std::move(c));
        }
        n->text = unescape(n->text);
        const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        n->text.erase(n->text.begin(), std::find_if(n->text.begin(), n->text.end(), notSpace));
        n->text.erase(std::find_if(n->text.rbegin(), n->text.rend(), notSpace).base(), n->text.end());
        return n;
    }

    const std::string& s_;
    size_t p_ = 0;
    size_t nodes_ = 0;
    std::string error_;
};

// The vendor and product inside an SDL joystick GUID ("0300d09f 9840 0000 70b9 ..."): 16 bit little endian at bytes 4 and 8.
bool guidIds(const std::string& guid, uint16_t& vendor, uint16_t& product) {
    if (guid.size() < 24) return false;
    auto byteAt = [&](size_t i) { return static_cast<unsigned>(std::strtoul(guid.substr(i * 2, 2).c_str(), nullptr, 16)); };
    vendor = static_cast<uint16_t>(byteAt(4) | (byteAt(5) << 8));
    product = static_cast<uint16_t>(byteAt(8) | (byteAt(9) << 8));
    return vendor != 0 || product != 0;
}

const input::DeviceSnapshot* findDevice(const Node& root, const std::vector<input::DeviceSnapshot>& devices) {
    uint16_t vendor = 0, product = 0;
    std::string guid = root.childText("uniqueID");
    if (guid.empty()) guid = root.childText("guid");
    if (guidIds(guid, vendor, product))
        for (const auto& d : devices)
            if (d.vendor == vendor && d.product == product) return &d;
    const std::string sdl = root.childText("sdlname");
    if (!sdl.empty())
        for (const auto& d : devices)
            if (d.name.find(sdl) != std::string::npos) return &d;
    return nullptr;
}

struct Slots {
    std::vector<Output> outputs;
    bool any = false;
};

Slots readSlots(const Node& owner, const std::string& where, std::vector<std::string>& notes) {
    Slots out;
    const Node* list = owner.child("slots");
    if (!list) return out;
    for (const auto& slot : list->children) {
        if (slot->name != "slot") continue;
        out.any = true;
        const std::string mode = slot->childText("mode");
        const unsigned code = static_cast<unsigned>(std::strtoul(slot->childText("code").c_str(), nullptr, 0));
        if (mode == "keyboard") {
            const int key = qtKeyToLinux(code);
            if (key) out.outputs.push_back({OutputKind::Key, key});
            else notes.push_back(where + ": unknown key code " + slot->childText("code"));
        } else if (mode == "mousebutton") {
            switch (code) {
                case 1: out.outputs.push_back({OutputKind::MouseButton, BTN_LEFT}); break;
                case 2: out.outputs.push_back({OutputKind::MouseButton, BTN_MIDDLE}); break;
                case 3: out.outputs.push_back({OutputKind::MouseButton, BTN_RIGHT}); break;
                case 4: out.outputs.push_back({OutputKind::Wheel, static_cast<int>(Direction::Up)}); break;
                case 5: out.outputs.push_back({OutputKind::Wheel, static_cast<int>(Direction::Down)}); break;
                case 6: out.outputs.push_back({OutputKind::Wheel, static_cast<int>(Direction::Left)}); break;
                case 7: out.outputs.push_back({OutputKind::Wheel, static_cast<int>(Direction::Right)}); break;
                case 8: out.outputs.push_back({OutputKind::MouseButton, BTN_SIDE}); break;
                case 9: out.outputs.push_back({OutputKind::MouseButton, BTN_EXTRA}); break;
                default: notes.push_back(where + ": unknown mouse button " + std::to_string(code));
            }
        } else if (mode == "mousemovement") {
            if (code >= 1 && code <= 4) out.outputs.push_back({OutputKind::MouseMove, static_cast<int>(code - 1)});  // 1 up, 2 down, 3 left, 4 right
            else notes.push_back(where + ": unknown mouse direction " + std::to_string(code));
        } else {
            notes.push_back(where + ": \"" + mode + "\" is not supported");
        }
    }
    return out;
}

// The settings a button of the profile can carry.
void readButtonSettings(const Node& owner, Binding& b, const std::string& where, std::vector<std::string>& notes) {
    if (const Node* n = owner.child("mousespeedx")) b.mouseSpeed = std::clamp(std::atoi(n->text.c_str()), 1, 300);
    if (const Node* n = owner.child("mousespeedy")) b.mouseSpeed = std::clamp(std::atoi(n->text.c_str()), 1, 300);
    if (const Node* n = owner.child("wheelspeedy")) b.wheelSpeed = std::clamp(std::atoi(n->text.c_str()), 1, 100);
    if (const Node* n = owner.child("wheelspeedx")) b.wheelSpeed = std::clamp(std::atoi(n->text.c_str()), 1, 100);
    if (owner.childText("toggle") == "true") notes.push_back(where + ": toggle is not supported");
    if (owner.childText("turbo") == "true") notes.push_back(where + ": turbo is not supported");
    if (owner.child("setselect")) notes.push_back(where + ": set switching is not supported");
    const std::string curve = owner.childText("mouseacceleration");
    if (curve == "linear") b.curve = MouseCurve::Linear;
    else if (curve == "quadratic") b.curve = MouseCurve::Quadratic;
    else if (curve == "cubic") b.curve = MouseCurve::Cubic;
    else if (curve == "quadratic-extreme") b.curve = MouseCurve::QuadraticExtreme;
    else if (curve == "power") b.curve = MouseCurve::Power;
    else if (!curve.empty() && curve != "precision") notes.push_back(where + ": mouse curve \"" + curve + "\" is used as enhanced precision");
    if (const Node* n = owner.child("mousesensitivity")) b.sensitivity = std::clamp(std::atof(n->text.c_str()), 0.001, 1000.0);
}

std::string outputsText(const Node& set) {
    size_t n = 0;
    for (const auto& c : set.children) n += c->child("slots") != nullptr;
    return std::to_string(n);
}

}  // namespace

int qtKeyToLinux(unsigned k) {
    static const int kLetters[26] = {KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
                                     KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z};
    static const int kDigits[10] = {KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9};
    static const int kFunction[24] = {KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
                                      KEY_F13, KEY_F14, KEY_F15, KEY_F16, KEY_F17, KEY_F18, KEY_F19, KEY_F20, KEY_F21, KEY_F22, KEY_F23, KEY_F24};
    if (k >= 0x41 && k <= 0x5a) return kLetters[k - 0x41];
    if (k >= 0x61 && k <= 0x7a) return kLetters[k - 0x61];
    if (k >= 0x30 && k <= 0x39) return kDigits[k - 0x30];
    if (k >= 0x01000030 && k < 0x01000030 + 24) return kFunction[k - 0x01000030];
    switch (k) {
        case 0x20: return KEY_SPACE;
        case 0x27: return KEY_APOSTROPHE;
        case 0x2c: return KEY_COMMA;
        case 0x2d: return KEY_MINUS;
        case 0x2e: return KEY_DOT;
        case 0x2f: return KEY_SLASH;
        case 0x3b: return KEY_SEMICOLON;
        case 0x3d: return KEY_EQUAL;
        case 0x5b: return KEY_LEFTBRACE;
        case 0x5c: return KEY_BACKSLASH;
        case 0x5d: return KEY_RIGHTBRACE;
        case 0x60: return KEY_GRAVE;
        case 0x01000000: return KEY_ESC;
        case 0x01000001: return KEY_TAB;
        case 0x01000003: return KEY_BACKSPACE;
        case 0x01000004: return KEY_ENTER;
        case 0x01000005: return KEY_KPENTER;
        case 0x01000006: return KEY_INSERT;
        case 0x01000007: return KEY_DELETE;
        case 0x01000008: return KEY_PAUSE;
        case 0x01000009: return KEY_SYSRQ;
        case 0x01000010: return KEY_HOME;
        case 0x01000011: return KEY_END;
        case 0x01000012: return KEY_LEFT;
        case 0x01000013: return KEY_UP;
        case 0x01000014: return KEY_RIGHT;
        case 0x01000015: return KEY_DOWN;
        case 0x01000016: return KEY_PAGEUP;
        case 0x01000017: return KEY_PAGEDOWN;
        case 0x01000020: return KEY_LEFTSHIFT;
        case 0x01000021: return KEY_LEFTCTRL;
        case 0x01000022: return KEY_LEFTMETA;
        case 0x01000023: return KEY_LEFTALT;
        case 0x01000024: return KEY_CAPSLOCK;
        case 0x01000025: return KEY_NUMLOCK;
        case 0x01000026: return KEY_SCROLLLOCK;
        case 0x01000055: return KEY_COMPOSE;
        case 0x01001103: return KEY_RIGHTALT;
        case 0x01000070: return KEY_VOLUMEDOWN;
        case 0x01000071: return KEY_MUTE;
        case 0x01000072: return KEY_VOLUMEUP;
        case 0x01000080: return KEY_PLAYCD;
        case 0x01000081: return KEY_STOPCD;
        case 0x01000082: return KEY_PREVIOUSSONG;
        case 0x01000083: return KEY_NEXTSONG;
        case 0x01000085: return KEY_PAUSECD;
        case 0x01000086: return KEY_PLAYPAUSE;
        case 0x010000b2: return KEY_BRIGHTNESSUP;
        case 0x010000b3: return KEY_BRIGHTNESSDOWN;
        default: return 0;
    }
}

ImportResult importAmgp(const std::string& xml, const std::vector<input::DeviceSnapshot>& devices) {
    ImportResult r;
    if (xml.size() > kMaxFileSize) { r.error = "The file is too large"; return r; }
    XmlReader reader(xml);
    std::string err;
    const auto root = reader.root(err);
    if (!root || (root->name != "joystick" && root->name != "gamecontroller")) {
        r.error = err.empty() ? "Not an antiMicroX profile" : err;
        return r;
    }
    if (root->name == "gamecontroller") {
        r.error = "Game controller profiles are not supported";
        return r;
    }
    const input::DeviceSnapshot* dev = findDevice(*root, devices);
    r.deviceName = root->childText("sdlname");
    if (!dev) {
        r.error = "Connect " + (r.deviceName.empty() ? std::string("the joystick") : r.deviceName) + " first";
        return r;
    }
    const Node* sets = root->child("sets");
    if (!sets) { r.error = "The profile has no bindings"; return r; }

    for (const auto& set : sets->children) {
        if (set->name != "set") continue;
        if (set->index() != 1) {
            if (outputsText(*set) != "0") r.notes.push_back("Set " + std::to_string(set->index()) + " skipped: only set 1 is imported");
            continue;
        }
        for (const auto& item : set->children) {
            const int index = item->index();
            if (item->name == "button") {
                const std::string where = "Button " + std::to_string(index);
                const Slots slots = readSlots(*item, where, r.notes);
                if (slots.outputs.empty()) continue;
                if (index < 1 || static_cast<size_t>(index) > dev->buttonCodes.size()) {
                    r.notes.push_back(where + ": the device has no such button");
                    continue;
                }
                Binding b;
                b.label = where;
                b.input = {dev->key, input::InputKind::Button, dev->buttonCodes[static_cast<size_t>(index - 1)], 0};
                b.outputs = slots.outputs;
                readButtonSettings(*item, b, where, r.notes);
                r.bindings.push_back(std::move(b));
            } else if (item->name == "axis") {
                const std::string axisName = "Axis " + std::to_string(index);
                const std::string throttle = item->childText("throttle");
                bool usable = true;
                if (index < 1 || static_cast<size_t>(index) > dev->axisCodes.size()) usable = false;
                for (const auto& half : item->children) {
                    if (half->name != "axisbutton") continue;
                    const bool negative = half->index() == 1;
                    const std::string where = axisName + (negative ? " down" : " up");
                    const Slots slots = readSlots(*half, where, r.notes);
                    if (slots.outputs.empty()) continue;
                    if (!usable) { r.notes.push_back(where + ": the device has no such axis"); continue; }
                    Binding b;
                    b.label = where;
                    b.input = {dev->key, input::InputKind::Axis, dev->axisCodes[static_cast<size_t>(index - 1)], 0};
                    b.side = negative ? -1 : 1;
                    b.mode = throttle == "negative" ? AxisMode::Negative : throttle == "positive" ? AxisMode::Positive : throttle == "negativehalf" ? AxisMode::NegativeHalf
                             : throttle == "positivehalf" ? AxisMode::PositiveHalf : AxisMode::Normal;
                    b.outputs = slots.outputs;
                    if (const Node* n = item->child("deadZone")) b.deadZone = std::clamp(std::atof(n->text.c_str()) / kAxisUnits, 0.0, 0.99);
                    if (const Node* n = item->child("maxZone")) b.maxZone = std::clamp(std::atof(n->text.c_str()) / kAxisUnits, 0.01, 1.0);
                    readButtonSettings(*half, b, where, r.notes);
                    r.bindings.push_back(std::move(b));
                }
            } else if (item->name == "dpad") {
                for (const auto& dir : item->children) {
                    if (dir->name != "dpadbutton") continue;
                    const std::string where = "Hat " + std::to_string(index) + " direction " + std::to_string(dir->index());
                    const Slots slots = readSlots(*dir, where, r.notes);
                    if (slots.outputs.empty()) continue;
                    int hatDir = -1;
                    switch (dir->index()) {
                        case 1: hatDir = 0; break;  // up
                        case 2: hatDir = 1; break;  // right
                        case 4: hatDir = 2; break;  // down
                        case 8: hatDir = 3; break;  // left
                        default: break;
                    }
                    if (hatDir < 0) { r.notes.push_back(where + ": diagonals are not supported"); continue; }
                    if (index < 1 || index > dev->hatCount) { r.notes.push_back(where + ": the device has no such hat"); continue; }
                    Binding b;
                    b.label = "Hat " + std::to_string(index) + " " + input::hatDirName(hatDir);
                    b.input = {dev->key, input::InputKind::Hat, index - 1, hatDir};
                    b.outputs = slots.outputs;
                    readButtonSettings(*dir, b, where, r.notes);
                    r.bindings.push_back(std::move(b));
                }
            } else if ((item->name == "stick" || item->name == "vdpad") && !item->children.empty()) {
                r.notes.push_back(item->name + " " + std::to_string(index) + " skipped: not supported");
            }
        }
    }
    if (r.bindings.size() > kMaxBindings) {
        r.notes.push_back("Only the first " + std::to_string(kMaxBindings) + " bindings were kept");
        r.bindings.resize(kMaxBindings);
    }
    r.deviceName = dev->name;
    r.ok = true;
    return r;
}

ImportResult importAmgpFile(const std::string& path, const std::vector<input::DeviceSnapshot>& devices) {
    ImportResult r;
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) { r.error = "Cannot read " + path; return r; }
    const std::streamoff size = in.tellg();
    if (size < 0 || static_cast<size_t>(size) > kMaxFileSize) { r.error = "The file is too large"; return r; }
    in.seekg(0);
    std::string text(static_cast<size_t>(size), '\0');
    in.read(text.data(), size);
    if (!in) { r.error = "Cannot read " + path; return r; }
    return importAmgp(text, devices);
}

}  // namespace lr::desktop
