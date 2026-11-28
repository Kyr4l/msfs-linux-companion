// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// The parts of the desktop controls and the MCDU that need no hardware: the USB packets, the antiMicroX import, the mouse maths, the pages.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>

#include <linux/input-event-codes.h>

#include "desktop/AmgpImport.hpp"
#include "desktop/DesktopEngine.hpp"
#include "desktop/DesktopLauncher.hpp"
#include "desktop/ProgressTracker.hpp"
#include "mcdu/AvionicsPages.hpp"
#include "mcdu/McduPages.hpp"
#include "mcdu/McduProtocol.hpp"
#include "mcdu/McduSimLink.hpp"
#include "mcdu/ScreenReader.hpp"
#include "app/Lights.hpp"
#include "mcdu/McduFont.hpp"
#include <nlohmann/json.hpp>

using namespace lr;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

// ---------------------------------------------------------------- MCDU protocol

static void testScreenEncoding() {
    using namespace mcdu;
    Screen s;
    const auto reports = encodeScreen(s);
    CHECK(reports.size() == 16);  // 336 cells of 3 bytes in reports with 63 bytes of payload
    for (const auto& r : reports) CHECK(r[0] == 0xf2);
    // white is 2 * 0x21; the first cell is marked with +1, the last with +2
    CHECK(reports[0][1] == 0x43 && reports[0][2] == 0x00 && reports[0][3] == ' ');
    CHECK(reports[0][4] == 0x42 && reports[0][5] == 0x00 && reports[0][6] == ' ');
    CHECK(reports[15][1 + (1008 - 15 * 63) - 3] == 0x44);

    Screen t;
    t.put(0, 1, "A", Color::Amber, true);  // amber small: 0x21 + 0x16b = 0x18c
    const auto r2 = encodeScreen(t);
    CHECK(r2[0][4] == 0x8c && r2[0][5] == 0x01 && r2[0][6] == 'A');

    Screen u;
    u.put(0, 1, "\xe2\x86\x90");  // a symbol takes three bytes
    const auto r3 = encodeScreen(u);
    CHECK(r3[0][6] == 0xe2 && r3[0][7] == 0x86 && r3[0][8] == 0x90);
    CHECK(glyphBytes(U'€') == "?");  // not in the panel's font
    CHECK(glyphBytes(U'Δ') == "Δ" && glyphBytes(U'□') == "☐" && glyphBytes(U'▲') == "?");  // as the unit shows them (firmware 1.09)
}

static void testInitAndLights() {
    using namespace mcdu;
    const auto init = initReports();
    CHECK(init.size() == 17);
    for (size_t i = 0; i < init.size(); ++i) {
        CHECK(init[i][0] == 0xf0 && init[i][1] == 0x00);
        CHECK(init[i][2] == i + 1);  // numbered in order
    }
    CHECK(init[0][4] == 0x32 && init[0][5] == 0xbb);
    const auto l = encodeLight(Light::Screen, 200);
    const uint8_t expected[14] = {0x02, 0x32, 0xbb, 0, 0, 0x03, 0x49, 0x01, 200, 0, 0, 0, 0, 0};
    CHECK(std::memcmp(l.data(), expected, 14) == 0);
    CHECK(static_cast<int>(Light::Fm2) == 0x10);
}

static void testKeys() {
    using namespace mcdu;
    // the positions two independent sources give
    CHECK(static_cast<int>(Key::L1) == 0);
    CHECK(static_cast<int>(Key::R1) == 6);
    CHECK(static_cast<int>(Key::Dir) == 12);
    CHECK(static_cast<int>(Key::Menu) == 24);
    CHECK(static_cast<int>(Key::D1) == 32);
    CHECK(static_cast<int>(Key::D0) == 42);
    CHECK(static_cast<int>(Key::A) == 44);
    CHECK(static_cast<int>(Key::Slash) == 70);
    CHECK(static_cast<int>(Key::Clr) == 73);
    CHECK(std::strcmp(keyId(Key::Fpln), "FPLN") == 0);
    CHECK(std::strcmp(keyId(Key::Space), "SP") == 0);
    CHECK(keyId(Key::Brt)[0] == 0);

    uint8_t report[25] = {1};
    report[1 + 73 / 8] |= 1 << (73 % 8);  // CLR
    report[17] = 0x34; report[18] = 0x02; report[19] = 0x78; report[20] = 0x01;
    InputState st;
    CHECK(parseInputReport(report, sizeof report, st));
    CHECK(st.keys.test(static_cast<size_t>(Key::Clr)) && st.keys.count() == 1);
    CHECK(st.lightLeft == 0x234 && st.lightRight == 0x178);
    report[0] = 2;
    CHECK(!parseInputReport(report, sizeof report, st));
    report[0] = 1;
    CHECK(!parseInputReport(report, 14, st));
    CHECK(isMcduProduct(0xbb36) && isMcduProduct(0xbb3e) && isMcduProduct(0xbb3a) && !isMcduProduct(0xb970));
}

// ---------------------------------------------------------------- the aircraft's page

static void testIniBuildsKeys() {
    using namespace mcdu;
    CHECK(iniBuildsKeyCode(Key::Data, 0xbb36) == "1 (>L:INI_MCDU1_DATA)");     // pressed in the A320neo V2 and the A330
    CHECK(iniBuildsKeyCode(Key::L1, 0xbb36) == "1 (>L:INI_MCDU1_LSK1L)");
    CHECK(iniBuildsKeyCode(Key::R6, 0xbb36) == "1 (>L:INI_MCDU1_LSK6R)");
    CHECK(iniBuildsKeyCode(Key::T, 0xbb36) == "1 (>L:INI_MCDU1_T)");
    CHECK(iniBuildsKeyCode(Key::D1, 0xbb36) == "1 (>L:INI_MCDU1_1)");
    CHECK(iniBuildsKeyCode(Key::Clr, 0xbb36) == "1 (>L:INI_MCDU1_CLR)");
    CHECK(iniBuildsKeyCode(Key::Fpln, 0xbb3a) == "1 (>L:INI_MCDU1_FPLAN)");    // the observer's unit works the captain's side
    CHECK(iniBuildsKeyCode(Key::Data, 0xbb3e) == "1 (>L:INI_MCDU2_DATA)");     // the first officer's the other one
    for (Key k : {Key::Brt, Key::Dim, Key::Blank1, Key::Blank2}) CHECK(iniBuildsKeyCode(k, 0xbb36).empty());
    // the CJ4 FMC (pressed in the aircraft 2026-11-24: letters, digits, page keys, CLR)
    CHECK(wt21KeyEvent(Key::A, 0xbb36) == "WT21_FMC_1_BTN_A" && wt21KeyEvent(Key::D7, 0xbb36) == "WT21_FMC_1_BTN_7");
    CHECK(wt21KeyEvent(Key::Init, 0xbb36) == "WT21_FMC_1_BTN_IDX" && wt21KeyEvent(Key::Prog, 0xbb36) == "WT21_FMC_1_BTN_LEGS");
    CHECK(wt21KeyEvent(Key::R6, 0xbb3e) == "WT21_FMC_2_BTN_R6" && wt21KeyEvent(Key::Ovfy, 0xbb36) == "WT21_FMC_1_BTN_EXEC");
    CHECK(wt21KeyEvent(Key::DownArrow, 0xbb36) == "WT21_FMC_1_BTN_NEXTPAGE" && wt21KeyEvent(Key::Slash, 0xbb36) == "WT21_FMC_1_BTN_DIV");
    for (Key k : {Key::Brt, Key::Dim, Key::Blank1, Key::Blank2}) CHECK(wt21KeyEvent(k, 0xbb36).empty());
    std::map<std::string, int> seen;                                            // every other key has a name of its own
    for (int i = 0; i < kKeyCount; ++i) {
        const std::string code = iniBuildsKeyCode(static_cast<Key>(i), 0xbb36);
        if (!code.empty()) ++seen[code];
    }
    CHECK(seen.size() == static_cast<size_t>(kKeyCount - 4));
}

// A made-up "font" of three shapes, drawn on the grid of the pop-out, read back.
static void testScreenReader() {
    using namespace mcdu;
    struct Shape { char32_t ch; int w, h; bool hollow; };
    const Shape shapes[] = {{U'A', 10, 16, false}, {U'B', 10, 16, true}, {U'C', 3, 16, false}};
    auto on = [](const Shape& s, int x, int y) { return !s.hollow || x < 2 || y < 2 || x >= s.w - 2 || y >= s.h - 2; };
    GlyphBank bank;
    for (const Shape& sh : shapes) {
        GlyphSample g;
        g.ch = sh.ch;
        g.geo = {static_cast<float>(sh.h), static_cast<float>(sh.w), -8.f};
        const int oy = (kGlyphH - sh.h) / 2, ox = (kGlyphW - sh.w) / 2;
        for (int y = 0; y < sh.h; ++y)
            for (int x = 0; x < sh.w; ++x) g.pix[static_cast<size_t>((oy + y) * kGlyphW + ox + x)] = on(sh, x, y) ? 255 : 0;
        bank.add(g);
    }
    char path[] = "/tmp/mcdu-glyphs-XXXXXX";
    const int fd = mkstemp(path);
    CHECK(fd >= 0);
    if (fd >= 0) ::close(fd);
    std::string err;
    CHECK(bank.save(path, err));
    GlyphBank loaded;
    CHECK(loaded.load(path, err) && loaded.samples().size() == 3 && loaded.samples()[1].ch == U'B' && loaded.samples()[1].pix == bank.samples()[1].pix);
    std::remove(path);
    GlyphBank missing;
    CHECK(!missing.load("/nonexistent/glyphs.bin", err) && missing.empty());

    const ScreenLayout& AL = airbusLayout();
    const int W = AL.refW, H = AL.refH;
    std::vector<uint8_t> img(static_cast<size_t>(W) * H * 4, 0);  // as the simulator draws it: a dark screen inside a black border
    for (int y = 4; y <= 361; ++y)
        for (int x = 20; x <= 420; ++x) std::memset(&img[(static_cast<size_t>(y) * W + x) * 4], 8, 3);
    auto draw = [&](const Shape& sh, int r, int c, uint8_t red, uint8_t green, uint8_t blue) {
        const int cx = static_cast<int>(std::lround(AL.x0 + c * AL.pitchX)), cy = static_cast<int>(std::lround(AL.rowY[static_cast<size_t>(r)]));
        for (int y = 0; y < sh.h; ++y)
            for (int x = 0; x < sh.w; ++x)
                if (on(sh, x, y)) {
                    uint8_t* px = &img[(static_cast<size_t>(cy - 8 + y) * W + cx - sh.w / 2 + x) * 4];
                    px[0] = blue, px[1] = green, px[2] = red;
                }
    };
    draw(shapes[0], 0, 0, 240, 240, 240);
    draw(shapes[1], 2, 5, 80, 200, 240);
    draw(shapes[2], 13, 24, 200, 160, 80);  // the 25th column of the scratchpad: the row moves left by one
    draw(shapes[0], 13, 0, 80, 160, 80);
    ScreenReader reader(loaded);
    ReadResult r = reader.read(img.data(), W, H, W * 4);
    CHECK(r.glyphs == 4 && r.unsure == 0);
    CHECK(r.screen.at(0, 0).ch == U'A' && r.screen.at(0, 0).color == Color::White);
    CHECK(r.screen.at(2, 5).ch == U'B' && r.screen.at(2, 5).color == Color::Cyan);
    CHECK(r.screen.at(13, 23).ch == U'C' && r.screen.at(13, 23).color == Color::Amber);
    CHECK(r.screen.at(1, 1).ch == U' ');
    const Screen first = r.screen;
    CHECK(reader.read(img.data(), W, H, W * 4).screen == first);  // read again from what it remembers
    // a pop-out opened twice as big
    std::vector<uint8_t> big(static_cast<size_t>(W) * H * 16);
    for (int y = 0; y < H * 2; ++y)
        for (int x = 0; x < W * 2; ++x) std::memcpy(&big[(static_cast<size_t>(y) * W * 2 + x) * 4], &img[(static_cast<size_t>(y / 2) * W + x / 2) * 4], 4);
    ScreenReader reader2(loaded);
    r = reader2.read(big.data(), W * 2, H * 2, W * 8);
    CHECK(r.screen.at(0, 0).ch == U'A' && r.screen.at(2, 5).ch == U'B' && r.screen.at(13, 23).ch == U'C');
    CHECK(reader2.read(nullptr, W, H, W * 4).glyphs == 0);

    // The CJ4 FMC: 15 rows, the message line under the scratchpad goes on the unit's last row while the scratchpad is empty
    {
        const ScreenLayout& WL = wt21Layout();
        const int w = WL.refW, h = WL.refH;
        std::vector<uint8_t> pic(static_cast<size_t>(w) * h * 4, 0);
        for (int y = 0; y < h; ++y)
            for (int x = 59; x <= 465; ++x) std::memset(&pic[(static_cast<size_t>(y) * w + x) * 4], 14, 3);
        auto put = [&](const Shape& sh, int r, int c) {
            const int cx = static_cast<int>(std::lround(WL.x0 + c * WL.pitchX)), cy = static_cast<int>(std::lround(WL.rowY[static_cast<size_t>(r)]));
            for (int y = 0; y < sh.h; ++y)
                for (int x = 0; x < sh.w; ++x)
                    if (on(sh, x, y)) std::memset(&pic[(static_cast<size_t>(cy - 8 + y) * w + cx - sh.w / 2 + x) * 4], 230, 3);
        };
        put(shapes[0], 0, 3);
        put(shapes[2], 13, 0), put(shapes[2], 13, 23);  // the scratchpad's brackets
        put(shapes[1], 14, 0);  // a message
        ScreenReader wr(loaded, WL);
        ReadResult m = wr.read(pic.data(), w, h, w * 4);
        CHECK(m.screen.at(0, 3).ch == U'A' && m.screen.at(13, 0).ch == U'B' && m.screen.at(13, 23).ch == U' ');
        put(shapes[0], 13, 5);  // typed: the scratchpad wins
        ScreenReader wr2(loaded, WL);
        m = wr2.read(pic.data(), w, h, w * 4);
        CHECK(m.screen.at(13, 0).ch == U'C' && m.screen.at(13, 5).ch == U'A' && m.screen.at(13, 23).ch == U'C');
    }
    CHECK(classifyColor(80, 80, 80) == Color::Grey && classifyColor(200, 200, 200) == Color::White && classifyColor(80, 160, 80) == Color::Green &&
          classifyColor(240, 120, 0) == Color::Amber && classifyColor(80, 200, 200) == Color::Cyan);
}

// The throttle's light file as shipped, and how stored bytes become colours.
static void testLights() {
    using namespace app;
    std::ifstream f(std::string(COMPANION_TEST_FIXTURES) + "/../../data/winctrl_ursa_minor_combat_throttle_lights.json");
    std::stringstream ss;
    ss << f.rdbuf();
    LightDevice d;
    std::string err;
    CHECK(parseLightDevice(ss.str(), d, err));
    CHECK(d.vendor == 0x4098 && d.product == 0xb970 && d.driver == "winwing" && d.device == 0x40 && d.family == 0xb9);
    CHECK(d.states.size() == 3 && d.states[0].offset == 0x210 && d.states[2].offset == 0x270 && d.leds.size() == 12);
    for (const auto& p : d.parts) CHECK(p.x >= 0 && p.y >= 0 && p.x + p.w <= 1.001f && p.y + p.h <= 1.001f && p.textLed <= 12 && p.barLed <= 12);
    // the user's Normal state as read from the throttle (0x240): white, orange x 9, green, white
    std::vector<uint8_t> bytes = {0xff, 0xff, 0xff, 0xb3, 0xd6, 0x7e, 0x2c, 0xcc};
    for (int i = 0; i < 8; ++i) bytes.insert(bytes.end(), {0xd6, 0x7e, 0x2c, 0xe6});
    bytes.insert(bytes.end(), {0x73, 0xff, 0x00, 0xe6, 0xff, 0xff, 0xff, 0x99});
    const auto c = decodeLightState(d, bytes);
    CHECK(c.size() == 12 && c[0].r == 0xff && c[0].level == 0xb3 && c[2].g == 0x7e && c[10].g == 0xff && c[11].level == 0x99);
    CHECK(decodeLightState(d, {1, 2, 3}).empty());
    CHECK(!parseLightDevice("{\"name\":\"x\"}", d, err) && !parseLightDevice("not json", d, err));
    // the throttle's key / axis / both inputs (SimAppPro: 1 key, 2 axis, 3 both), as read from the user's throttle
    CHECK(d.settings.size() == 3 && d.settings[0].offset == 0xEC && d.settings[1].byte == 1 && d.settings[2].device == 0x08 && d.levels.empty());
    CHECK(describeLightSetting(d.settings[0], 3) == "Both" && describeLightSetting(d.settings[2], 2) == "Axis" && describeLightSetting(d.settings[0], 9) == "?");

    // the stick: no stored colours, two stored settings and one live light (SimAppPro2's definition of the Fighter Stick R)
    std::ifstream g(std::string(COMPANION_TEST_FIXTURES) + "/../../data/winctrl_ursa_minor_fighter_stick_r_lights.json");
    std::stringstream gs;
    gs << g.rdbuf();
    LightDevice stick;
    CHECK(parseLightDevice(gs.str(), stick, err));
    CHECK(stick.product == 0xbc2a && stick.states.empty() && stick.settings.size() == 2 && stick.levels.size() == 1);
    CHECK(stick.settings[0].device == 0x20 && stick.settings[0].family == 0xbb && stick.settings[0].offset == 0xEC && stick.levels[0].index == 0);
    CHECK(describeLightSetting(stick.settings[0], 128) == "50%" && describeLightSetting(stick.settings[0], 255) == "100%");
    CHECK(describeLightSetting(stick.settings[1], 0) == "On" && describeLightSetting(stick.settings[1], 1) == "Off");  // inverted, as SimAppPro writes it
    // a setting outside the stored words, or of an unknown kind, is refused
    const std::string bad = R"({"name":"x","usb":"4098:1","driver":"winwing","settings":[{"name":"a","kind":"percent","device":"0x20","family":"0xbb","offset":"0x2000"}]})";
    CHECK(!parseLightDevice(bad, stick, err));
    const std::string odd = R"({"name":"x","usb":"4098:1","driver":"winwing","settings":[{"name":"a","kind":"write","device":"0x20","family":"0xbb","offset":"0xEC"}]})";
    CHECK(!parseLightDevice(odd, stick, err));
    // saving: the stick's two light settings may be saved; key / axis modes may not be marked so
    CHECK(parseLightDevice(gs.str(), stick, err) && stick.settings[0].save && stick.settings[1].save && !d.settings[0].save);
    const std::string mode = R"({"name":"x","usb":"4098:1","driver":"winwing","settings":[{"name":"a","kind":"key_axis","device":"0x40","family":"0xb9","offset":"0xEC","save":true}]})";
    CHECK(!parseLightDevice(mode, stick, err));
}

// B612 for the unit: the shipped font file and packet map make the series SimAppPro sends, with the glyphs in their places.
static void testCurves() {
    const desktop::McduConfig d;
    CHECK(desktop::curveAt(d.autoScreen, 0) == 35);
    CHECK(desktop::curveAt(d.autoScreen, 100) == 100);
    CHECK(desktop::curveAt(d.autoKeys, 200) == 100);
    CHECK(desktop::curveAt(d.autoKeys, -5) == 70);
    CHECK(desktop::curveAt({0, 30, 60, 90}, 50) == 45);  // halfway between the dim and lit points
}

static void testFont() {
    using namespace mcdu;
    auto slurp = [](const std::string& rel) {
        std::ifstream f(std::string(COMPANION_TEST_FIXTURES) + "/../../data/mcdu_" + rel, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    };
    const std::string font = slurp("b612-font-21x31.json"), map = slurp("WinWingFontPacketMap-3x31.json");
    FontUpload up;
    std::string err;
    CHECK(buildFontUpload(font, map, 0xcc, up, err));
    CHECK(up.name == "B612" && up.glyphWidth == 23 && up.glyphHeight == 31 && up.packets.size() == 604);
    bool sizes = true, prefix = true;
    for (const auto& p : up.packets) {
        sizes = sizes && (p.size() == 14 || p.size() == 64);
        prefix = prefix && (p[0] == 0xf0 || p[0] == 0x02 || p.size() == 14);
    }
    CHECK(sizes);
    // the large "A": its first lit row must be in the series where the map puts that glyph
    const auto fj = nlohmann::json::parse(font), mj = nlohmann::json::parse(map);
    std::vector<uint8_t> blob;
    for (const auto& p : up.packets) blob.insert(blob.end(), p.begin(), p.end());
    CHECK(blob.size() > static_cast<size_t>(mj.at("DisplayBrightnessOffset").get<int>()) && blob[mj.at("DisplayBrightnessOffset").get<size_t>()] == 0xcc);
    CHECK(blob[mj.at("XOffsetOffset").get<size_t>()] == 0x24 + 16 && blob[mj.at("YOffsetOffset").get<size_t>()] == 0x14);
    int lit = 0;
    for (const auto& g : fj.at("LargeGlyphs"))
        if (g.at("Character") == "A")
            for (const auto& row : g.at("BitArray")) {
                const std::string bits = row.get<std::string>();
                lit += static_cast<int>(std::count(bits.begin(), bits.end(), 'X') + std::count(bits.begin(), bits.end(), '1'));
            }
    CHECK(lit > 20);  // the bitmap is not empty
    CHECK(!buildFontUpload(font, "{\"GlyphHeight\": 29, \"GlyphWidth\": 23, \"Packets\": []}", 0, up, err));
    CHECK(!buildFontUpload("not json", map, 0, up, err));
}

static void testMarkup() {
    using namespace mcdu;
    auto cells = parseMarkup("{green}FL{small}350{end}{end}X", false);
    CHECK(cells.size() == 6);
    CHECK(cells[0].ch == U'F' && cells[0].color == Color::Green && !cells[0].small);
    CHECK(cells[2].ch == U'3' && cells[2].color == Color::Green && cells[2].small);
    CHECK(cells[5].ch == U'X' && cells[5].color == Color::White);
    CHECK(parseMarkup("{sp}{sp}A", false).size() == 3);
    CHECK(parseMarkup("{unknown}", false).size() == 9);  // not markup: shown as it is

    const char* update =
        R"({"left":{"title":"{cyan}INIT{end}","titleLeft":"","page":"{small}1/2{end}","scratchpad":"{amber}HELLO{end}",)"
        R"("arrows":[true,false,true,false],"annunciators":{"fail":true,"fmgc":false,"mcdu_menu":true,"fm1":false,"ind":false,"rdy":true,"blank":false,"fm2":false},)"
        R"("lines":[["{small}CO RTE{end}","FROM/TO",""],["{amber}__{end}","{cyan}ABCD/EFGH{end}",""]]}})";
    Screen s;
    LampState lamps{};
    CHECK(parseSimBridgeUpdate(update, s, lamps));
    CHECK(s.at(0, 10).ch == U'I' && s.at(0, 10).color == Color::Cyan);
    CHECK(s.at(0, 23).ch == U'2' && s.at(0, 23).small);
    CHECK(s.at(1, 0).ch == U'C' && s.at(1, 0).small);
    CHECK(s.at(1, 23).ch == U'O' && s.at(1, 23).small);      // labels default to the small font
    CHECK(s.at(2, 23).ch == U'H' && s.at(2, 23).color == Color::Cyan && !s.at(2, 23).small);
    CHECK(s.at(13, 0).ch == U'H' && s.at(13, 0).color == Color::Amber);
    CHECK(s.at(13, 23).ch == 0x2191);
    CHECK(lamps[0] && lamps[2] && lamps[3] && lamps[6] && !lamps[1]);
    CHECK(!parseSimBridgeUpdate("not json", s, lamps));
    CHECK(!parseSimBridgeUpdate("{\"right\":{}}", s, lamps));
}

// ---------------------------------------------------------------- antiMicroX import

static std::vector<input::DeviceSnapshot> throttle() {
    input::DeviceSnapshot d;
    d.key = "4098:b970:WINCTRL WINCTRL URSA MINOR Combat Throttle Metal.EX";
    d.name = "WINCTRL WINCTRL URSA MINOR Combat Throttle Metal.EX";
    d.vendor = 0x4098;
    d.product = 0xb970;
    for (int c = BTN_JOYSTICK; c < BTN_JOYSTICK + 16; ++c) d.buttonCodes.push_back(c);      // as the real device reports them
    for (int c = BTN_TRIGGER_HAPPY; c < BTN_TRIGGER_HAPPY + 64; ++c) d.buttonCodes.push_back(c);
    for (int a = 0; a < 8; ++a) d.axisCodes.push_back(a);
    d.axisValue.assign(8, 0.5f);
    d.buttonState.assign(d.buttonCodes.size(), false);
    return {d};
}

static const desktop::Binding* find(const desktop::ImportResult& r, input::InputKind kind, int code, int side) {
    for (const auto& b : r.bindings)
        if (b.input.kind == kind && b.input.code == code && b.side == side) return &b;
    return nullptr;
}

static void testImport() {
    using namespace desktop;
    std::ifstream in(std::string(COMPANION_TEST_FIXTURES) + "/desktop.joystick.amgp");
    std::stringstream text;
    text << in.rdbuf();
    CHECK(!text.str().empty());
    const ImportResult r = importAmgp(text.str(), throttle());
    CHECK(r.ok);
    CHECK(r.notes.empty());
    CHECK(r.bindings.size() == 9);

    // button 27 is the 11th of the second block of the device: left click
    const Binding* click = find(r, input::InputKind::Button, BTN_TRIGGER_HAPPY + 10, 0);
    CHECK(click && click->outputs.size() == 1 && click->outputs[0] == (Output{OutputKind::MouseButton, BTN_LEFT}));
    const Binding* up = find(r, input::InputKind::Button, BTN_TRIGGER_HAPPY + 60, 0);    // button 77
    CHECK(up && up->outputs[0] == (Output{OutputKind::Key, KEY_VOLUMEUP}));
    const Binding* down = find(r, input::InputKind::Button, BTN_TRIGGER_HAPPY + 58, 0);  // button 75
    CHECK(down && down->outputs[0] == (Output{OutputKind::Key, KEY_VOLUMEDOWN}));

    // axis 3: the wheel, with a dead zone of 1000
    const Binding* wheelDown = find(r, input::InputKind::Axis, 2, -1);
    CHECK(wheelDown && wheelDown->outputs[0] == (Output{OutputKind::Wheel, static_cast<int>(Direction::Down)}));
    CHECK(wheelDown && std::fabs(wheelDown->deadZone - 1000.0 / 32767.0) < 1e-9);
    const Binding* wheelUp = find(r, input::InputKind::Axis, 2, 1);
    CHECK(wheelUp && wheelUp->outputs[0] == (Output{OutputKind::Wheel, static_cast<int>(Direction::Up)}));

    // axes 8 and 7: the pointer, dead zone 1200
    const Binding* mouseUp = find(r, input::InputKind::Axis, 7, -1);
    CHECK(mouseUp && mouseUp->outputs[0] == (Output{OutputKind::MouseMove, static_cast<int>(Direction::Up)}));
    CHECK(mouseUp && std::fabs(mouseUp->deadZone - 1200.0 / 32767.0) < 1e-9 && mouseUp->mouseSpeed == 50);
    const Binding* mouseRight = find(r, input::InputKind::Axis, 6, 1);
    CHECK(mouseRight && mouseRight->outputs[0] == (Output{OutputKind::MouseMove, static_cast<int>(Direction::Right)}));

    CHECK(!importAmgp(text.str(), {}).ok);                  // the device has to be there
    CHECK(!importAmgp("<notaprofile/>", throttle()).ok);
    CHECK(!importAmgp("<joystick><sets", throttle()).ok);
    std::string deep;
    for (int i = 0; i < 5000; ++i) deep += "<a>";
    CHECK(!importAmgp(deep, throttle()).ok);                // a damaged file costs nothing

    const std::string turbo =
        "<joystick><uniqueID>0300d09f9840000070b90000110100001653647472</uniqueID><sets><set index=\"1\"><button index=\"1\"><turbo>true</turbo>"
        "<slots><slot><code>0x41</code><mode>keyboard</mode></slot><slot><code>5</code><mode>pause</mode></slot></slots></button></set></sets></joystick>";
    const ImportResult t = importAmgp(turbo, throttle());
    CHECK(t.ok && t.bindings.size() == 1 && t.bindings[0].outputs[0].code == KEY_A);
    CHECK(t.notes.size() == 2);  // what could not be taken over is said
    CHECK(qtKeyToLinux(0x01000072) == KEY_VOLUMEUP && qtKeyToLinux(0x01000030) == KEY_F1 && qtKeyToLinux(0x12345678) == 0);
}

// ---------------------------------------------------------------- desktop engine

namespace {
struct FakeSink final : desktop::OutputSink {
    bool opened = false;
    std::map<int, bool> keys;
    int presses = 0, x = 0, y = 0, wheelV = 0, wheelH = 0, wheelEvents = 0, largestStep = 0;
    bool open(std::string&) override { opened = true; return true; }
    void close() override { opened = false; }
    void key(int code, bool down) override { keys[code] = down; presses += down; }
    void move(int dx, int dy) override { x += dx; y += dy; }
    void wheel(int v, int h) override {
        wheelV += v;
        wheelH += h;
        ++wheelEvents;
        largestStep = std::max({largestStep, std::abs(v), std::abs(h)});
    }
    void flush() override {}
};

input::InputEvent axisEvent(const input::InputId& id, float v) {
    input::InputEvent e;
    e.id = id;
    e.axis = v;
    return e;
}
input::InputEvent buttonEvent(const input::InputId& id, bool down) {
    input::InputEvent e;
    e.id = id;
    e.pressed = down;
    return e;
}
}  // namespace

static void testEngine() {
    using namespace desktop;
    CHECK(std::fabs(enhancedPrecision(0.4) - 0.148) < 1e-9);
    CHECK(std::fabs(enhancedPrecision(0.75) - 0.498) < 1e-9);
    CHECK(std::fabs(enhancedPrecision(1.0) - 1.0) < 1e-9);

    FakeSink sink;
    DesktopEngine e(sink);
    const input::InputId btn{"dev", input::InputKind::Button, 300, 0}, ax{"dev", input::InputKind::Axis, 7, 0}, wh{"dev", input::InputKind::Axis, 2, 0};
    Binding click; click.input = btn; click.outputs = {{OutputKind::Key, KEY_VOLUMEUP}};
    Binding same; same.input = btn; same.outputs = {{OutputKind::Key, KEY_VOLUMEUP}};
    Binding mouse; mouse.input = ax; mouse.side = 1; mouse.deadZone = 0.1; mouse.outputs = {{OutputKind::MouseMove, static_cast<int>(Direction::Down)}};
    Binding wheel; wheel.input = wh; wheel.side = 1; wheel.deadZone = 0.1; wheel.outputs = {{OutputKind::Wheel, static_cast<int>(Direction::Up)}};
    e.setBindings({click, same, mouse, wheel});

    e.onInput(buttonEvent(btn, true));       // inactive: nothing happens
    CHECK(sink.presses == 0);
    CHECK(e.tick() == -1);
    CHECK(e.setActive(true) && sink.opened);
    CHECK(e.tick() == -1);                   // at rest it asks for no wake-up at all

    e.onInput(buttonEvent(btn, true));
    CHECK(sink.keys[KEY_VOLUMEUP] && sink.presses == 1);  // two bindings on one key press it once
    e.onInput(buttonEvent(btn, false));
    CHECK(!sink.keys[KEY_VOLUMEUP]);

    // inside the dead zone nothing moves
    e.onInput(axisEvent(ax, 0.52f));
    CHECK(e.tick() == -1);
    // full deflection: 50 * 20 = 1000 pixels a second
    e.onInput(axisEvent(ax, 1.0f));
    CHECK(e.tick() > 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    e.tick();
    CHECK(sink.y >= 30 && sink.y <= 80);
    CHECK(sink.x == 0);
    e.onInput(axisEvent(ax, 0.5f));
    const int stopped = sink.y;
    CHECK(e.tick() == -1);
    CHECK(sink.y == stopped);

    // an axis on the wheel starts from rest and stops with the axis
    e.onInput(axisEvent(wh, 1.0f));
    CHECK(sink.wheelV == 0);
    CHECK(e.tick() == 8);
    e.onInput(axisEvent(wh, 0.5f));
    CHECK(e.tick() == -1);

    // whatever is held is let go when the simulator takes over
    e.onInput(buttonEvent(btn, true));
    CHECK(sink.keys[KEY_VOLUMEUP]);
    e.setActive(false);
    CHECK(!sink.keys[KEY_VOLUMEUP] && !sink.opened);
    e.onInput(buttonEvent(btn, true));
    CHECK(!sink.keys[KEY_VOLUMEUP]);

    // an axis already pushed when the controls start counts at once
    FakeSink sink2;
    DesktopEngine e2(sink2);
    e2.setAxisProbe([](const std::string&, int) { return 1.0f; });
    e2.setBindings({wheel});
    e2.setActive(true);
    CHECK(sink2.wheelV == 0 && e2.tick() == 8);
}

static void testHighResolutionWheel() {
    using namespace desktop;
    WheelNotches notches;
    CHECK(notches.feed(40) == 0 && notches.feed(40) == 0 && notches.feed(40) == 1);  // three thirds of a notch
    CHECK(notches.feed(100) == 0 && notches.feed(-20) == 0 && notches.feed(-100) == -1);  // turning back starts the count again
    notches.reset();
    CHECK(notches.feed(250) == 2 && notches.feed(-1) == 0);

    const input::InputId ax{"dev", input::InputKind::Axis, 2, 0}, btn{"dev", input::InputKind::Button, 300, 0};
    Binding up;
    up.input = ax; up.side = 1; up.deadZone = 0; up.maxZone = 1.0; up.curve = MouseCurve::Linear; up.wheelSpeed = 10;
    up.outputs = {{OutputKind::Wheel, static_cast<int>(Direction::Up)}};
    auto scroll = [&](const Binding& binding, float position) {
        FakeSink sink;
        DesktopEngine e(sink);
        e.setBindings({binding});
        e.setActive(true);
        e.onInput(axisEvent(binding.input, position));
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(320);
        while (std::chrono::steady_clock::now() < end) {
            e.tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        return std::array<int, 4>{sink.wheelV, sink.wheelH, sink.wheelEvents, sink.largestStep};
    };
    // 10 notches a second at full travel: 1200 units a second, in many small steps
    const auto full = scroll(up, 1.0f);
    CHECK(full[0] > 200 && full[0] < 480);
    CHECK(full[2] >= 10 && full[3] < kWheelNotch);
    // the speed follows the axis
    const auto half = scroll(up, 0.75f);
    CHECK(half[0] > full[0] * 0.3 && half[0] < full[0] * 0.7);
    const auto slow = scroll(up, 0.55f);
    CHECK(slow[0] > 0 && slow[0] < half[0] * 0.5);
    // the other directions
    Binding down = up, right = up;
    down.outputs = {{OutputKind::Wheel, static_cast<int>(Direction::Down)}};
    right.outputs = {{OutputKind::Wheel, static_cast<int>(Direction::Right)}};
    CHECK(scroll(down, 1.0f)[0] < -200);
    const auto sideways = scroll(right, 1.0f);
    CHECK(sideways[1] > 200 && sideways[0] == 0);

    // a button still turns the wheel notch by notch
    Binding button;
    button.input = btn; button.wheelSpeed = 20;
    button.outputs = {{OutputKind::Wheel, static_cast<int>(Direction::Up)}};
    FakeSink sink;
    DesktopEngine e(sink);
    e.setBindings({button});
    e.setActive(true);
    e.onInput(buttonEvent(btn, true));
    CHECK(sink.wheelV == kWheelNotch);
    const int wait = e.tick();
    CHECK(wait > 8 && wait <= 51);
}

// ---------------------------------------------------------------- pages

namespace {
struct FakeActions final : mcdu::PageActions {
    std::vector<std::vector<int>> taps;
    std::vector<desktop::MediaCommand> commands;
    int slept = 0;
    void tapKeys(const std::vector<int>& keys) override { taps.push_back(keys); }
    void media(desktop::MediaCommand c) override { commands.push_back(c); }
    void sleepScreen() override { ++slept; }
    std::vector<desktop::AudioCommand> audioCommands;
    void audio(desktop::AudioCommand c) override { audioCommands.push_back(c); }
    std::vector<std::string> queries, urls, dismissed;
    bool runnerWorks = true;
    bool runnerQuery(const std::string& text) override { queries.push_back(text); return runnerWorks; }
    bool openUrl(const std::string& url) override { urls.push_back(url); return true; }
    void dismissTask(const std::string& key) override { dismissed.push_back(key); }
};
std::string rowText(const mcdu::Screen& s, int row) {
    std::string out;
    for (int c = 0; c < mcdu::kCols; ++c) out += s.at(row, c).ch < 0x80 ? static_cast<char>(s.at(row, c).ch) : '?';
    return out;
}
mcdu::SystemSample sampleSystem() {
    mcdu::SystemSample v;
    v.cpuPercent = 34; v.cpuTempC = 68; v.memUsedGb = 15.2; v.memAvailableGb = 15.8; v.memTotalGb = 31; v.uptimeSeconds = 7 * 3600 + 48 * 60;
    mcdu::GpuSample a, b;
    a.name = "RX 9070"; a.loadPercent = 32; a.tempC = 53; a.memUsedGb = 3.1; a.memTotalGb = 16;
    b.name = "GTX 1660 SUPER";  // no driver: no numbers
    v.gpus = {a, b};
    for (const char* n : {"SYSTEM", "HOME", "SSD DATA 0"}) {
        mcdu::DiskSample d;
        d.name = n; d.mount = std::string("/") + n; d.freeGb = 324; d.totalGb = 492; d.usedPercent = 31;
        v.disks.push_back(d);
    }
    return v;
}
}  // namespace

static void testAvionics() {
    using namespace mcdu;
    using A = AvionicsPages;
    CHECK(A::frequencyHz("11830", false) == 118300000);
    CHECK(A::frequencyHz("118.3", false) == 118300000);
    CHECK(A::frequencyHz("118.305", false) == 118305000);
    CHECK(A::frequencyHz("121.5", false) == 121500000);
    CHECK(!A::frequencyHz("108.00", false));  // a NAV frequency for a COM
    CHECK(A::frequencyHz("1135", true) == 113500000);
    CHECK(A::frequencyHz("110.55", true) == 110550000);
    CHECK(!A::frequencyHz("12A", false));
    CHECK(A::squawkBco16("7000") == 0x7000);
    CHECK(A::squawkBco16("1200") == 0x1200);
    CHECK(!A::squawkBco16("1280"));
    CHECK(!A::squawkBco16("700"));
    CHECK(std::fabs(*A::baroMillibars("2992") - 1013.2) < 0.1);
    CHECK(std::fabs(*A::baroMillibars("29.92") - 1013.2) < 0.1);
    CHECK(A::baroMillibars("1013") == 1013.0);
    CHECK(!A::baroMillibars("500"));

    std::map<std::string, double> sim = {{"(A:COM ACTIVE FREQUENCY:1, MHz)", 118.3}, {"(A:COM STANDBY FREQUENCY:1, MHz)", 121.5},
                                         {"(A:TRANSPONDER CODE:1, Bco16)", 0x1200}, {"(A:TRANSPONDER STATE:1, Number)", 4},
                                         {"(A:AUTOPILOT HEADING LOCK DIR, Degrees)", 270}, {"(A:AUTOPILOT HEADING LOCK, Bool)", 1}};
    const A::Read read = [&](const std::string& c) -> std::optional<double> {
        const auto it = sim.find(c);
        if (it == sim.end()) return std::nullopt;
        return it->second;
    };
    for (const auto& [code, v] : sim) CHECK(std::find(A::codes().begin(), A::codes().end(), code) != A::codes().end());
    A pages;
    Screen s;
    pages.render(s, read);
    CHECK(rowText(s, 0).find("MCDU MENU") != std::string::npos && rowText(s, 2).find("<AVIONICS") != std::string::npos);
    // RAD NAV: RADIO COMM, then RADIO NAV
    CHECK(pages.onKey(Key::Rad, read).empty());
    CHECK(pages.page() == AvionicsPage::Comm);
    pages.render(s, read);
    CHECK(rowText(s, 2).find("118.300") != std::string::npos && rowText(s, 4).find("121.500") != std::string::npos);
    CHECK(rowText(s, 2).find("---") != std::string::npos);  // COM2 not read yet
    // an empty scratchpad: the line key copies the field there
    pages.onKey(Key::L1, read);
    pages.render(s, read);
    CHECK(rowText(s, 13).find("118.300") != std::string::npos);
    for (int i = 0; i < 7; ++i) pages.onKey(Key::Clr, read);
    // type 122.80 into COM1 standby, then swap
    for (Key k : {Key::D1, Key::D2, Key::D2, Key::D8, Key::D0}) pages.onKey(k, read);
    auto out = pages.onKey(Key::L2, read);
    CHECK(out.size() == 1 && out[0] == "122800000 (>K:COM_STBY_RADIO_SET_HZ)");
    out = pages.onKey(Key::L3, read);
    CHECK(out.size() == 1 && out[0] == "(>K:COM_STBY_RADIO_SWAP)");
    for (Key k : {Key::D9, Key::D9}) pages.onKey(k, read);
    CHECK(pages.onKey(Key::L2, read).empty());  // 99: not a frequency, kept with a message
    pages.render(s, read);
    CHECK(rowText(s, 13).find("ENTRY OUT OF RANGE") != std::string::npos);
    pages.onKey(Key::Clr, read);  // CLR takes the message away first
    pages.render(s, read);
    CHECK(rowText(s, 13).find("99") != std::string::npos);
    pages.onKey(Key::Clr, read);
    pages.onKey(Key::Clr, read);
    pages.onKey(Key::RightArrow, read);
    CHECK(pages.page() == AvionicsPage::Nav);
    // ATC COMM: the transponder
    pages.onKey(Key::Atc, read);
    CHECK(pages.page() == AvionicsPage::Xpdr);
    pages.render(s, read);
    CHECK(rowText(s, 2).find("1200") != std::string::npos && rowText(s, 6).find("ALT") != std::string::npos && rowText(s, 2).find("STBY>") != std::string::npos);
    for (Key k : {Key::D7, Key::D0, Key::D0, Key::D0}) pages.onKey(k, read);
    out = pages.onKey(Key::L1, read);
    CHECK(out.size() == 1 && out[0] == "28672 (>K:XPNDR_SET)");
    out = pages.onKey(Key::R1, read);  // STBY
    CHECK(out.size() == 1 && out[0] == "1 (>A:TRANSPONDER STATE:1, Number)");
    for (Key k : {Key::D2, Key::D9, Key::D9, Key::D2}) pages.onKey(k, read);
    out = pages.onKey(Key::L4, read);
    CHECK(out.size() == 1 && out[0] == "16211 (>K:KOHLSMAN_SET)");
    // PERF: the FCU; a target with a value, the mode key, V/S below zero
    pages.onKey(Key::Perf, read);
    CHECK(pages.page() == AvionicsPage::Fcu);
    pages.render(s, read);
    CHECK(rowText(s, 2).find("270") != std::string::npos);
    CHECK(rowText(s, 11).find("HDG") != std::string::npos);  // the mode line
    for (Key k : {Key::D5, Key::D0, Key::D0, Key::PlusMinus}) pages.onKey(k, read);
    out = pages.onKey(Key::R2, read);
    CHECK(out.size() == 1 && out[0] == "500 neg (>K:AP_VS_VAR_SET_ENGLISH)");
    out = pages.onKey(Key::L3, read);
    CHECK(out.size() == 1 && out[0] == "(>K:AP_PANEL_HEADING_HOLD)");
    // MCDU MENU: AVIONICS goes back to the last page; the G1000 PFD and MFD
    pages.onKey(Key::Menu, read);
    pages.onKey(Key::L1, read);
    CHECK(pages.page() == AvionicsPage::Fcu);
    pages.onKey(Key::Menu, read);
    pages.onKey(Key::L2, read);
    CHECK(pages.page() == AvionicsPage::Pfd);
    out = pages.onKey(Key::L1, read);  // the functions by name
    CHECK(out.size() == 1 && out[0] == "(>H:AS1000_PFD_DIRECTTO)");
    pages.render(s, read);
    CHECK(rowText(s, 2).find("<DIRECT TO") == 0 && rowText(s, 13).find("SENT DIRECT TO") == 0);
    out = pages.onKey(Key::L4, read);
    CHECK(out.size() == 1 && out[0] == "(>H:AS1000_PFD_FMS_Upper_INC)");
    pages.onKey(Key::PlusMinus, read);  // +/- then the knob: one click back
    out = pages.onKey(Key::L4, read);
    CHECK(out.size() == 1 && out[0] == "(>H:AS1000_PFD_FMS_Upper_DEC)");
    out = pages.onKey(Key::L6, read);
    CHECK(out.size() == 1 && out[0] == "(>H:AS1000_PFD_FMS_Upper_PUSH)");
    CHECK(pages.onKey(Key::R6, read).empty() && pages.page() == AvionicsPage::PfdKeys);  // SOFTKEYS>
    out = pages.onKey(Key::R6, read);
    CHECK(out.size() == 1 && out[0] == "(>H:AS1000_PFD_SOFTKEYS_12)");
    pages.onKey(Key::RightArrow, read);  // PFD SOFTKEYS -> MFD -> MFD SOFTKEYS -> PFD
    CHECK(pages.page() == AvionicsPage::Mfd);
    pages.onKey(Key::RightArrow, read);
    out = pages.onKey(Key::L1, read);
    CHECK(pages.page() == AvionicsPage::MfdKeys && out.size() == 1 && out[0] == "(>H:AS1000_MFD_SOFTKEYS_1)");
    pages.onKey(Key::RightArrow, read);
    CHECK(pages.page() == AvionicsPage::Pfd);
    pages.onKey(Key::Atc, read);  // the other function keys still open their pages
    CHECK(pages.page() == AvionicsPage::Xpdr);
    pages.onKey(Key::Menu, read);
    pages.onKey(Key::L3, read);
    out = pages.onKey(Key::Ovfy, read);
    CHECK(out.size() == 1 && out[0] == "(>H:AS1000_MFD_ENT_Push)");
    out = pages.onKey(Key::UpArrow, read);
    CHECK(out.size() == 1 && out[0] == "(>H:AS1000_MFD_FMS_Lower_INC)");
    out = pages.onKey(Key::Perf, read);
    CHECK(out.size() == 1 && out[0] == "(>H:AS1000_MFD_PROC_Push)");
    pages.render(s, read);
    CHECK(rowText(s, 0).find("G1000 MFD") != std::string::npos);
    pages.onKey(Key::Menu, read);
    CHECK(pages.page() == AvionicsPage::Menu);
}

// The Airbus style: same function keys, a scratchpad, the slew keys, and the load colours of the settings.
static void testAirbusPages() {
    using namespace mcdu;
    FakeActions a;
    PageHost p(a);
    desktop::McduConfig cfg;
    cfg.pageStyle = "airbus";
    p.configure(cfg);
    p.setSystem(sampleSystem());
    std::tm now{};
    now.tm_hour = 9; now.tm_min = 5; now.tm_sec = 7; now.tm_mday = 18; now.tm_mon = 10; now.tm_year = 126; now.tm_wday = 3;
    Screen s;
    p.render(s, now);
    CHECK(rowText(s, 0).find("MCDU MENU") != std::string::npos && rowText(s, 2).find("<DESKTOP") == 0);
    CHECK(rowText(s, 13).find("SELECT DESIRED SYSTEM") == 0);

    p.onKey(Key::Data);
    p.render(s, now);
    CHECK(p.page() == PageId::DataIndex && rowText(s, 0).find("DATA INDEX") != std::string::npos);
    p.onKey(Key::L1);  // PC status
    p.render(s, now);
    CHECK(p.page() == PageId::System && rowText(s, 0).find("PC STATUS") != std::string::npos);
    CHECK(rowText(s, 2).find("34%") != std::string::npos && rowText(s, 12).find("<RETURN") == 0);
    CHECK(s.at(2, 2).color == Color::Green);  // 34 %: below the warning stage
    p.onKey(Key::RightArrow);
    CHECK(p.page() == PageId::Cpu);
    p.onKey(Key::RightArrow);
    p.render(s, now);
    CHECK(p.page() == PageId::Cpu && rowText(s, 0).find("CPU CORES") != std::string::npos);
    p.onKey(Key::LeftArrow);
    p.onKey(Key::LeftArrow);
    CHECK(p.page() == PageId::System);
    p.onKey(Key::LeftArrow);  // round to the last one
    CHECK(p.page() == PageId::Power);
    {  // POWER with two GPUs (fans) and many sensors: nothing may run into the REFRESH line (rows 11 and 12)
        SystemSample full = sampleSystem();
        full.gpus[0].fanRpm = 1200;
        full.gpus[1].fanPercent = 40;
        for (const char* n : {"nvme", "acpitz", "ram", "wifi", "board", "vrm", "chipset", "ssd"}) full.sensors.push_back({n, 45});
        p.setSystem(full);
        p.render(s, now);
        if (std::getenv("SHOW_POWER")) for (int r = 0; r < 14; ++r) std::printf("%2d|%s|\n", r, rowText(s, r).c_str());
        const size_t refresh = rowText(s, 11).find("REFRESH");
        CHECK(refresh != std::string::npos);
        for (size_t c = 0; c < refresh; ++c) CHECK(s.at(11, static_cast<int>(c)).ch == U' ');  // the sensors stop before the REFRESH label row
        CHECK(rowText(s, 4).find("RX 9070") == 0 && rowText(s, 6).find("GTX 1660") == 0);  // both GPU lines intact
        p.setSystem(sampleSystem());
    }

    // the load colours follow the settings
    cfg.loadColors.warn.above = 30;
    p.configure(cfg);
    p.onKey(Key::Data);
    p.onKey(Key::L1);
    p.render(s, now);
    CHECK(s.at(2, 2).color == Color::Amber);  // 34 % is above 30
    cfg.loadColors.on = false;
    p.configure(cfg);
    p.render(s, now);
    CHECK(s.at(2, 2).color == Color::Green);

    // the scratchpad and a field: the home airport on INIT
    p.onKey(Key::Init);
    p.render(s, now);
    CHECK(rowText(s, 0).find("INIT") != std::string::npos && s.at(10, 0).color == Color::Amber);  // boxes until set
    for (Key k : {Key::L, Key::F}) p.onKey(k);
    p.onKey(Key::L5);
    p.render(s, now);
    CHECK(rowText(s, 13).find("FORMAT ERROR") == 0);
    p.onKey(Key::Clr);  // the message goes first
    for (Key k : {Key::P, Key::G}) p.onKey(k);
    p.onKey(Key::L5);
    p.render(s, now);
    CHECK(rowText(s, 10).find("LFPG") == 0 && p.homeAirport() == "LFPG" && p.takeWeatherRequest());
    p.onKey(Key::L5);  // empty scratchpad: the field is copied into it
    p.render(s, now);
    CHECK(rowText(s, 13).find("LFPG") == 0);

    // F-PLN: each shortcut with its keys above it
    p.onKey(Key::Fpln);
    p.render(s, now);
    CHECK(rowText(s, 0).find("KEYS") != std::string::npos && rowText(s, 2).find("<VOL UP") == 0 && rowText(s, 1).find("VOL+") != std::string::npos);
    CHECK(rowText(s, 9).find("CTRL+C") != std::string::npos);
    a.taps.clear();
    p.onKey(Key::R5);
    CHECK(a.taps.size() == 1 && a.taps[0] == std::vector<int>({KEY_LEFTCTRL, KEY_C}));

    // DIR TO: the scratchpad runs, OVFY too
    p.onKey(Key::Dir);
    p.onKey(Key::Clr);
    for (int i = 0; i < 4; ++i) p.onKey(Key::Clr);
    for (Key k : {Key::K, Key::A, Key::T, Key::E}) p.onKey(k);
    p.onKey(Key::Ovfy);
    CHECK(!a.queries.empty() && a.queries.back() == "KATE");

    // PERF: one page per player (← →), ↑ ↓ on the player's program or the PC (R1), MUTE on the same
    {
        using C = desktop::MediaCommand;
        p.onKey(Key::Perf);
        desktop::MediaInfo m;
        m.present = true; m.playing = true; m.players = 3; m.page = 1; m.player = "Firefox"; m.title = "Plane snaps";
        m.appVolume = 68; m.pcVolume = 45;
        p.setMedia(m);
        p.render(s, now);
        CHECK(rowText(s, 0).find("1/3") == 18 && rowText(s, 1).find("VOLUME 68%") == 14 && rowText(s, 2).find("FIREFOX") == 0 && rowText(s, 2).find("APP/ALL") == 17);
        CHECK(s.at(2, 17).color == Color::Green && s.at(2, 21).small);  // APP chosen
        a.taps.clear();
        p.onKey(Key::UpArrow);
        CHECK(a.commands.back() == C::AppVolumeUp && a.taps.empty());
        p.onKey(Key::RightArrow);
        CHECK(a.commands.back() == C::NextPlayer);
        p.onKey(Key::LeftArrow);
        CHECK(a.commands.back() == C::PrevPlayer);
        p.onKey(Key::R5);
        CHECK(a.commands.back() == C::Mute);
        p.onKey(Key::R1);  // ALL
        CHECK(p.takePrefsChanged());
        desktop::McduConfig saved;
        p.exportPrefs(saved);
        CHECK(saved.mediaVolumeAll);
        p.render(s, now);
        CHECK(rowText(s, 1).find("VOLUME 45%") == 14 && s.at(2, 21).color == Color::Green && !s.at(2, 21).small);
        p.onKey(Key::DownArrow);
        CHECK(a.commands.back() == C::PcVolumeDown);
        p.onKey(Key::R5);
        CHECK(a.commands.back() == C::PcMute);
        m.pcMuted = true; m.players = 1;
        p.setMedia(m);
        p.render(s, now);
        CHECK(rowText(s, 10).find("UNMUTE>") != std::string::npos && rowText(s, 0).find("1/1") == std::string::npos);  // one player: no page number
        p.onKey(Key::R1);  // back to APP
    }

    // back to the classic pages
    cfg.pageStyle = "classic";
    p.configure(cfg);
    p.onKey(Key::Data);
    CHECK(p.page() == PageId::System);
}

// the Vision Jet's G3000: MCDU MENU offers the touch controllers, whose softkeys and knobs are the aircraft's input events
// STD on a G1000 that does not raise the sim's STD flag (the C400): the page remembers it set STD, and one knob click leaves it
// F-PLN: the leg the GPS flies, and on a G1000 with a control pad (the Corvalis) the MCDU's keys type into it
static void testFpln() {
    using namespace mcdu;
    AvionicsPages p;
    const AvionicsPages::Read read = [](const std::string& code) -> std::optional<double> {
        if (code.find("GPS IS ACTIVE FLIGHT PLAN") != std::string::npos) return 1.0;
        if (code.find("WP COUNT") != std::string::npos) return 9.0;
        if (code.find("WP INDEX") != std::string::npos) return 4.0;
        if (code.find("GPS WP DISTANCE") != std::string::npos) return 12.3;
        if (code.find("GPS WP BEARING") != std::string::npos) return 87.0;
        return std::nullopt;
    };
    p.onKey(Key::Fpln, read);
    CHECK(p.page() == AvionicsPage::Fpln);
    p.setWaypoints("KSEA", "SUMMA");
    Screen s;
    p.render(s, read);
    CHECK(rowText(s, 2).find("KSEA") == 0 && rowText(s, 4).find("SUMMA") == 0 && rowText(s, 4).find("087?/12.3NM") != std::string::npos);
    CHECK(rowText(s, 0).find("4/9") != std::string::npos && rowText(s, 12).find("ON") == std::string::npos);  // no control pad
    auto out = p.onKey(Key::L5, read);
    CHECK(out.size() == 1 && out[0] == "(>H:AS1000_MFD_FPL_Push)");
    CHECK(p.onKey(Key::K, read).empty());  // without a pad the letters stay on the MCDU
    p.setControlPad(true);
    p.onKey(Key::L6, read);  // KEYS TO G1000 on
    CHECK(p.keysToG1000());
    out = p.onKey(Key::K, read);
    CHECK(out.size() == 1 && out[0] == "1 (>B:INSTRUMENT_AS1000_CONTROLPAD_K_Set)");
    out = p.onKey(Key::Ovfy, read);
    CHECK(out.size() == 1 && out[0] == "1 (>B:INSTRUMENT_AS1000_CONTROLPAD_PUSH_ENTER_Set)");
    out = p.onKey(Key::UpArrow, read);
    CHECK(out.size() == 1 && out[0] == "(>H:AS1000_MFD_FMS_Lower_INC)");
    p.onKey(Key::Prog, read);  // the function keys still open their pages
    CHECK(p.page() == AvionicsPage::Prog);
}

static void testFuelUnits() {
    using namespace mcdu;
    AvionicsPages p;
    const AvionicsPages::Read read = [](const std::string& code) -> std::optional<double> {
        if (code.find("FUEL TOTAL QUANTITY") != std::string::npos) return 50.0;  // gallons
        if (code.find("FUEL WEIGHT PER GALLON") != std::string::npos) return 6.0;  // avgas, pounds
        if (code.find("FUEL FLOW GPH") != std::string::npos) return 10.0;
        return std::nullopt;
    };
    p.onKey(Key::Fuel, read);
    Screen s;
    p.render(s, read);
    CHECK(rowText(s, 4).find("50.0GAL") == 0 && rowText(s, 4).find("10.0GPH") != std::string::npos);
    p.setFuelUnit("l");
    p.render(s, read);
    CHECK(rowText(s, 4).find("189L") == 0 && rowText(s, 4).find("37.9L/H") != std::string::npos);
    p.setFuelUnit("kg");
    p.render(s, read);
    CHECK(rowText(s, 4).find("136KG") == 0 && rowText(s, 4).find("27KG/H") != std::string::npos);  // 50 gal x 6 lb x 0.4536
    p.setFuelUnit("gal_kg");
    p.render(s, read);
    CHECK(rowText(s, 4).find("50.0GAL") == 0 && rowText(s, 3).find("FOB 136KG") != std::string::npos);
}

static void testBaroStd() {
    using namespace mcdu;
    AvionicsPages p;
    double mb = 1009.0;
    const AvionicsPages::Read read = [&mb](const std::string& code) -> std::optional<double> {
        if (code.find("KOHLSMAN SETTING STD") != std::string::npos) return 0.0;  // never raised
        if (code.find("KOHLSMAN SETTING MB") != std::string::npos) return mb;
        if (code.find("KOHLSMAN SETTING HG") != std::string::npos) return mb / 33.8639;
        return std::nullopt;
    };
    Screen s;
    p.onKey(Key::Atc, read);
    p.render(s, read);
    CHECK(rowText(s, 8).find("STD>") != std::string::npos);
    auto out = p.onKey(Key::R4, read);
    CHECK(out.size() == 1 && out[0] == "(>K:BAROMETRIC_STD_PRESSURE)");
    mb = 1013.19;  // the G1000 went to STD
    p.render(s, read);
    CHECK(rowText(s, 8).find("STD") == 0 && rowText(s, 8).find("QNH>") != std::string::npos);
    for (Key k : {Key::D1, Key::D0, Key::D1, Key::D0}) p.onKey(k, read);
    CHECK(p.onKey(Key::L4, read).empty());  // a typed value in STD: QNH> first
    p.render(s, read);
    CHECK(rowText(s, 13).find("NOT ALLOWED") == 0);
    for (int i = 0; i < 4; ++i) p.onKey(Key::Clr, read);
    out = p.onKey(Key::R4, read);
    CHECK(out.size() == 1 && out[0] == "(>H:AS1000_PFD_BARO_INC)");  // one click: leaves STD, back to the local setting
}

static void testGtc() {
    using namespace mcdu;
    AvionicsPages p;
    p.setG3000(true);
    const AvionicsPages::Read read = [](const std::string&) -> std::optional<double> { return std::nullopt; };
    Screen s;
    p.render(s, read);
    CHECK(rowText(s, 4).find("<GTC") == 0 && rowText(s, 6).find("G1000") == std::string::npos);
    p.onKey(Key::L2, read);
    CHECK(p.page() == AvionicsPage::Gtc);
    auto out = p.onKey(Key::L1, read);
    CHECK(out.size() == 1 && out[0] == "1 (>B:AS3000_TSC_1_SOFTKEY_1_Set)");
    p.onKey(Key::RightArrow, read);  // GTC 2
    CHECK(p.gtc() == 2);
    out = p.onKey(Key::R1, read);
    CHECK(out.size() == 1 && out[0] == "1 (>B:AS3000_TSC_2_FREQUENCY_KNOB_MHZ_Set)");
    p.onKey(Key::PlusMinus, read);  // then one click back
    out = p.onKey(Key::R1, read);
    CHECK(out.size() == 1 && out[0] == "1 neg (>B:AS3000_TSC_2_FREQUENCY_KNOB_MHZ_Set)");
    p.onKey(Key::LeftArrow, read);
    p.onKey(Key::LeftArrow, read);  // round to GTC 3
    CHECK(p.gtc() == 3);
    p.render(s, read);
    CHECK(rowText(s, 0).find("GTC 3") != std::string::npos && rowText(s, 13).find("SENT INNER") == std::string::npos);
    CHECK(p.onKey(Key::A, read).empty());  // no scratchpad here
    p.onKey(Key::Rad, read);  // the function keys still open their pages
    CHECK(p.page() == AvionicsPage::Comm);
}

// The key that gives the unit to the desktop pages in flight
static void testSwitchKey() {
    using namespace mcdu;
    using A = SwitchKey::Action;
    const auto t0 = SwitchKey::Clock::now();
    SwitchKey k;
    k.configure(Key::Blank1, true);
    CHECK(k.is(Key::Blank1) && !k.is(Key::Menu));
    CHECK(k.down(t0) == A::None && k.due() && k.tick(t0 + std::chrono::milliseconds(500)) == A::None);
    CHECK(k.tick(t0 + SwitchKey::kHold) == A::Toggle && !k.due());       // held: switches once, while still down
    CHECK(k.tick(t0 + std::chrono::seconds(5)) == A::None && k.up() == A::None);  // and letting go does nothing more
    CHECK(k.down(t0) == A::None && k.up() == A::Press);                    // let go early: an ordinary press
    k.configure(Key::Blank2, false);                                       // a short press
    CHECK(k.down(t0) == A::Toggle && k.up() == A::None);
    k.configure(Key::Menu, false);                                         // MCDU MENU is always held
    CHECK(k.down(t0) == A::None && k.up() == A::Press);
    k.configure(Key::L1, false);                                           // any key with a job too
    CHECK(k.down(t0) == A::None && k.up() == A::Press);
    k.configure(std::nullopt, true);
    CHECK(!k.is(Key::Blank1) && !k.is(Key::Menu));
    CHECK(keyNumberName(18) == "Key 18" && keyNumberName(25) == "Key 25 (MCDU MENU)" && keyNumberName(1) == "Key 1 (L1)" && keyNumberName(0) == "None");
    CHECK(keyFromNumber(28) == Key::Blank2 && !keyFromNumber(kKeyCount + 1) && keyIsFree(Key::Blank1) && !keyIsFree(Key::Brt) && !keyIsFree(Key::A));
}

// The ES-30: two vertical touch controllers, three knobs each, and its batteries on FUEL PRED
static void testGtcVertical() {
    using namespace mcdu;
    AvionicsPages p;
    p.setG3000(false, true);
    p.setElectric(true);
    const AvionicsPages::Read read = [](const std::string& code) -> std::optional<double> {
        if (code.find("ESTIMATED CAPACITY PCT:3") != std::string::npos) return 99.5;
        if (code.find("BATTERY VOLTAGE:1") != std::string::npos) return 28.04;
        return std::nullopt;
    };
    p.onKey(Key::L2, read);
    CHECK(p.page() == AvionicsPage::Gtc);
    Screen s;
    p.render(s, read);
    CHECK(rowText(s, 0).find("LEFT GTC") != std::string::npos);
    auto out = p.onKey(Key::R1, read);  // checked: COM1 standby +1 MHz
    CHECK(out.size() == 1 && out[0] == "1 (>B:INSTRUMENT_G5000_CONTROL_3_G5000_SECONDARY_SCREEN_LEFT_Set)");
    p.onKey(Key::PlusMinus, read);
    out = p.onKey(Key::R2, read);
    CHECK(out.size() == 1 && out[0] == "1 neg (>B:INSTRUMENT_G5000_CONTROL_3_INNER_G5000_SECONDARY_SCREEN_LEFT_Set)");
    p.onKey(Key::RightArrow, read);
    CHECK(p.gtc() == 2);
    out = p.onKey(Key::R3, read);  // the name as the sim lists it, cut at 63 characters
    CHECK(out.size() == 1 && out[0] == "1 (>B:INSTRUMENT_G5000_CONTROL_3_INNER_PUSH_G5000_SECONDARY_SCREEN_RI_Set)");
    out = p.onKey(Key::L4, read);
    CHECK(out.size() == 1 && out[0] == "1 (>B:INSTRUMENT_G5000_CONTROL_2_PUSH_G5000_SECONDARY_SCREEN_RIGHT_Set)");
    p.onKey(Key::RightArrow, read);  // two controllers: back to the left one
    CHECK(p.gtc() == 1);
    p.onKey(Key::Fuel, read);
    p.render(s, read);
    CHECK(rowText(s, 9).find("HV BATT") != std::string::npos && rowText(s, 10).find("100%") == 0 && rowText(s, 10).find("28.0V") != std::string::npos);
}

static void testAvionicsTemperatures() {
    using namespace mcdu;
    AvionicsPages p;
    const AvionicsPages::Read read = [](const std::string& code) -> std::optional<double> {
        if (code.find("OIL TEMPERATURE") != std::string::npos) return 212.0;  // read in °F
        if (code.find("ENGINE TYPE") != std::string::npos) return 0.0;      // piston
        return std::nullopt;
    };
    p.onKey(Key::Data, read);  // ENGINE
    Screen s;
    p.render(s, read);
    CHECK(rowText(s, 4).find("212?F") == 0);  // as the aircraft: engine in °F
    p.setTemperatures("c");
    p.render(s, read);
    CHECK(rowText(s, 4).find("100?C") == 0);
    p.setTemperatures("both");
    p.render(s, read);
    CHECK(rowText(s, 4).find("100?C") == 0 && rowText(s, 3).find("OIL T 212?F") != std::string::npos);
}

static void testPages() {
    using namespace mcdu;
    FakeActions a;
    PageHost p(a);
    std::tm now{};
    now.tm_hour = 9; now.tm_min = 5; now.tm_sec = 7; now.tm_mday = 18; now.tm_mon = 10; now.tm_year = 126; now.tm_wday = 3;
    Screen s;
    p.render(s, now);
    CHECK(rowText(s, 0).find("MCDU MENU") != std::string::npos);
    CHECK(p.refreshMs() == 0);  // a page that does not change costs nothing
    for (size_t i : {0u, 1u, 2u, 3u, 4u, 6u, 7u, 8u, 9u, 10u}) CHECK(!p.sideLabels()[i].empty());  // the side keys all do something here

    p.onKey(Key::L1);
    CHECK(p.page() == PageId::Clock && p.refreshMs() == 1000);
    p.render(s, now);
    CHECK(rowText(s, 2).find("09:05:07") != std::string::npos);
    CHECK(rowText(s, 4).find("WED 18 NOV 2026") != std::string::npos);
    p.onKey(Key::R1);  // 12 hours
    p.render(s, now);
    CHECK(rowText(s, 2).find("09:05:07 AM") != std::string::npos);
    CHECK(p.takePrefsChanged());
    p.onKey(Key::L4);
    p.render(s, now);
    CHECK(rowText(s, 8).find("<STOP") == 0);
    p.onKey(Key::R4);
    p.render(s, now);
    CHECK(rowText(s, 8).find("<START") == 0);

    p.onKey(Key::Perf);
    CHECK(p.page() == PageId::Media);
    p.onKey(Key::L2);
    CHECK(a.commands.size() == 1 && a.commands[0] == desktop::MediaCommand::PlayPause);
    p.onKey(Key::R1);
    CHECK(a.taps.back() == std::vector<int>{KEY_VOLUMEUP});
    {
        desktop::MediaInfo list;  // shuffle shows only for a player that has it
        list.present = true; list.canNext = list.hasShuffle = true;
        p.setMedia(list);
    }
    p.onKey(Key::R4);
    CHECK(a.commands.back() == desktop::MediaCommand::Shuffle);
    p.onKey(Key::L4);
    CHECK(a.commands.back() == desktop::MediaCommand::NextPlayer);
    desktop::MediaInfo m;
    m.present = true; m.playing = true; m.player = "spotify"; m.title = "A very long track title that does not fit"; m.artist = "Someone";
    p.setMedia(m);
    p.render(s, now);
    CHECK(rowText(s, 3).find(" A VERY LONG TRACK TITLE") == 0 || rowText(s, 3).find(" A VERY LONG TRACK TITL") == 0);
    CHECK(rowText(s, 4).find("<PAUSE") == 0);
    CHECK(rowText(s, 2).find("<PREV") == std::string::npos && rowText(s, 8).find("SHUFFLE") == std::string::npos);  // a player that cannot skip
    m.canPrevious = m.canNext = m.hasShuffle = m.hasRepeat = true;  // a playlist
    p.setMedia(m);
    p.render(s, now);
    CHECK(rowText(s, 2).find("<PREV") == 0 && rowText(s, 8).find("SHUFFLE OFF>") != std::string::npos);
    // Mute: the player's program only (the sound server), not the PC's mute key
    const size_t tapsBefore = a.taps.size();
    p.onKey(Key::R3);
    CHECK(a.commands.back() == desktop::MediaCommand::Mute && a.taps.size() == tapsBefore);
    m.muted = true;
    m.appVolume = 60;  // the player makes sound: its own mute state shows
    p.setMedia(m);
    p.render(s, now);
    CHECK(rowText(s, 6).find("UNMUTE>") != std::string::npos);
    m.appVolume = -1;  // no sound from the player: MUTE falls back to the PC, and so does the label
    p.setMedia(m);
    p.render(s, now);
    CHECK(rowText(s, 6).find("MUTE>") != std::string::npos && rowText(s, 6).find("UNMUTE>") == std::string::npos);

    p.onKey(Key::Fpln);
    CHECK(p.page() == PageId::Keys);
    p.onKey(Key::R1);  // the seventh slot
    CHECK(a.taps.back() == (std::vector<int>{KEY_LEFTALT, KEY_TAB}));
    p.render(s, now);
    CHECK(rowText(s, 2).find("<VOL UP") == 0);

    p.onKey(Key::Rad);
    CHECK(p.page() == PageId::Type);
    p.onKey(Key::A);
    CHECK(a.taps.back() == std::vector<int>{KEY_A});
    p.onKey(Key::D5);
    p.onKey(Key::Clr);
    CHECK(a.taps.back() == std::vector<int>{KEY_BACKSPACE});
    p.render(s, now);
    CHECK(rowText(s, 13).find("A ") == 0);
    p.onKey(Key::L1);  // shift
    p.onKey(Key::B);
    CHECK(a.taps.back() == (std::vector<int>{KEY_LEFTSHIFT, KEY_B}));
    p.onKey(Key::L1);
    p.onKey(Key::R1);
    CHECK(a.taps.back() == (std::vector<int>{KEY_LEFTCTRL, KEY_C}));

    desktop::McduConfig cfg;
    cfg.keyboardLayout = "azerty";
    p.configure(cfg);
    p.onKey(Key::A);
    CHECK(a.taps.back() == std::vector<int>{KEY_Q});
    p.onKey(Key::D5);
    CHECK(a.taps.back() == (std::vector<int>{KEY_LEFTSHIFT, KEY_5}));
    CHECK(defaultShortcuts().size() == static_cast<size_t>(desktop::kShortcutSlots));

    p.onKey(Key::Menu);
    p.onKey(Key::R5);
    CHECK(a.slept == 1);
}

static void testSystemPage() {
    using namespace mcdu;
    FakeActions a;
    PageHost p(a);
    std::tm now{};
    Screen s;
    desktop::McduConfig cfg;
    p.configure(cfg);
    p.setSystem(sampleSystem());
    p.onKey(Key::Data);
    CHECK(p.wantsSystem() && p.refreshMs() == 2000);

    // both GPUs on the page; a card without a driver shows no numbers instead of made-up ones
    p.render(s, now);
    CHECK(rowText(s, 5).find("RX 9070") == 0);
    CHECK(rowText(s, 6).find("<32 %") == 0);
    CHECK(rowText(s, 7).find("GTX 1660 SUPER") == 0);
    CHECK(rowText(s, 8).find("<--") == 0);
    CHECK(rowText(s, 0).find("7H48") != std::string::npos);

    // the disk changes with the side key next to it, and is remembered
    CHECK(rowText(s, 9).find("DISK 1/3 SYSTEM") == 0);
    p.onKey(Key::L5);
    p.render(s, now);
    CHECK(rowText(s, 9).find("DISK 2/3 HOME") == 0);
    CHECK(p.takePrefsChanged());
    desktop::McduConfig saved;
    p.exportPrefs(saved);
    CHECK(saved.diskMount == "/HOME");
    p.onKey(Key::R6);
    CHECK(p.refreshMs() == 5000);

    // bars
    cfg.systemLook = "bars"; cfg.diskMount = "/HOME";
    p.configure(cfg);
    p.render(s, now);
    CHECK(rowText(s, 2).find("<||||........") == 0);

    // one GPU at a time, changed with the key
    cfg.gpuView = "switch";
    p.configure(cfg);
    p.render(s, now);
    CHECK(rowText(s, 5).find("GPU 1/2 RX 9070") == 0);
    p.onKey(Key::L3);
    p.render(s, now);
    CHECK(rowText(s, 5).find("GPU 2/2 GTX 1660 SUPER") == 0);

    // the GPUs on their own page
    cfg.gpuView = "page"; cfg.gpuIndex = 0;
    p.configure(cfg);
    p.onKey(Key::L3);
    CHECK(p.page() == PageId::Gpus);
    p.render(s, now);
    CHECK(rowText(s, 2).find("<32 %") == 0);
    CHECK(rowText(s, 3).find(" VRAM 3.1/16 GB") == 0);
    p.onKey(Key::L6);
    CHECK(p.page() == PageId::System);

    // the disk picked from a list goes back to the page
    cfg.diskView = "pick";
    p.configure(cfg);
    p.onKey(Key::L5);
    CHECK(p.page() == PageId::Disks);
    p.onKey(Key::L3);
    CHECK(p.page() == PageId::System);
    p.render(s, now);
    CHECK(rowText(s, 9).find("DISK SSD DATA 0") == 0);

    // all disks on a page of their own
    cfg.diskView = "page";
    p.configure(cfg);
    p.render(s, now);
    CHECK(rowText(s, 10).find("<3 MOUNTED") == 0);
}

static void testMetar() {
    using namespace mcdu;
    MetarReport m = parseMetar("METAR LFPG 182330Z 18013KT 9999 SCT013TCU BKN013 BKN020 11/10 Q0990 NOSIG\n");
    CHECK(m.station == "LFPG" && m.time == "2330Z" && m.wind == "180/13KT" && m.visibility == "10 KM+");
    CHECK(m.temperature == "11" && m.dewpoint == "10" && m.pressure == "990 HPA");
    CHECK(m.raw == "METAR LFPG 182330Z 18013KT 9999 SCT013TCU BKN013 BKN020 11/10 Q0990 NOSIG");
    m = parseMetar("KJFK 182351Z 31008G16KT 10SM FEW250 M02/M10 A3012 RMK AO2 SLP199");
    CHECK(m.wind == "310/08G16KT" && m.visibility == "10SM" && m.temperature == "-2" && m.dewpoint == "-10" && m.pressure == "30.12 IN");
    m = parseMetar("EGLL 010000Z 00000KT CAVOK 05/03 Q1030 TEMPO 4000 BR");
    CHECK(m.wind == "CALM" && m.visibility == "CAVOK" && m.temperature == "5" && m.dewpoint == "3");
    CHECK(parseMetar("").station.empty());
    CHECK(validAirportCode("LFPG") && validAirportCode("K1G4") && !validAirportCode("LFP") && !validAirportCode("lfpg") && !validAirportCode("LF&G"));
}

static void testNewPages() {
    using namespace mcdu;
    FakeActions a;
    PageHost p(a);
    std::tm now{};
    Screen s;
    SystemSample v = sampleSystem();
    v.gpus[0].powerWatts = 64; v.gpus[0].fanRpm = 1450;
    v.sensors = {{"CPU", 68}, {"NVME1", 31}, {"GPU 1", 90}};
    v.downBytesPerSecond = 2.5e6; v.upBytesPerSecond = 120e3;
    p.setSystem(v);

    p.onKey(Key::Fuel);
    CHECK(p.page() == PageId::Power && p.wantsSystem());
    p.render(s, now);
    CHECK(rowText(s, 2).find("64 W") == 0 && rowText(s, 2).find("1450 RPM") != std::string::npos);
    CHECK(rowText(s, 4).find("--") == 0);  // the card without a driver
    CHECK(rowText(s, 6).find("CPU") == 0 && rowText(s, 6).find("NVME1") == 13);
    CHECK(s.at(7, 7).color == Color::Red);  // 90 degrees

    p.onKey(Key::Sec);
    CHECK(p.page() == PageId::Keys2);
    p.onKey(Key::L1);
    CHECK(a.taps.back() == (std::vector<int>{KEY_LEFTCTRL, KEY_X}));
    p.onKey(Key::L2);
    CHECK(a.taps.back() == (std::vector<int>{KEY_LEFTCTRL, KEY_Z}));
    desktop::McduConfig fr;
    fr.keyboardLayout = "azerty";
    p.configure(fr);
    p.onKey(Key::L2);  // Ctrl+Z is printed on another key of a French keyboard
    CHECK(a.taps.back() == (std::vector<int>{KEY_LEFTCTRL, KEY_W}));

    p.configure(desktop::McduConfig{});
    p.onKey(Key::Atc);
    CHECK(p.page() == PageId::Comms && p.wantsSystem());
    p.render(s, now);
    CHECK(rowText(s, 2).find("<MIC") != 0);  // nothing is offered before the sound server has answered
    desktop::AudioState sound;
    sound.known = true;
    p.setAudio(sound);
    p.render(s, now);
    CHECK(rowText(s, 9).find("SOUND SERVER UNAVAILABLE") == 0);
    sound.available = true;  // outside a call
    p.setAudio(sound);
    p.render(s, now);
    CHECK(rowText(s, 2).find("<MIC LIVE") == 0 && rowText(s, 4).find("<VOICE --") == 0 && rowText(s, 6).find("<SOUND --") == 0);
    p.onKey(Key::L2);
    p.render(s, now);
    CHECK(a.audioCommands.empty() && rowText(s, 13).find("NOT IN A CALL") == 0);
    sound.voiceStreams = 1; sound.soundStreams = 1; sound.soundMuted = true; sound.voiceMuted = true; sound.micMuted = true;
    p.setAudio(sound);
    p.onKey(Key::L1);
    p.onKey(Key::L2);
    p.onKey(Key::L3);
    CHECK(a.audioCommands == (std::vector<desktop::AudioCommand>{desktop::AudioCommand::ToggleMic, desktop::AudioCommand::ToggleVoice, desktop::AudioCommand::ToggleDeafen}));
    p.render(s, now);
    CHECK(rowText(s, 2).find("<MIC MUTED") == 0 && rowText(s, 4).find("<VOICE MUTED") == 0 && rowText(s, 6).find("<DEAFENED") == 0);
    CHECK(s.at(4, 1).color == Color::Amber);
    p.render(s, now);
    CHECK(rowText(s, 2).find("2.5 MB/S>") != std::string::npos && rowText(s, 4).find("120 KB/S>") != std::string::npos);
    p.onKey(Key::R1);
    p.render(s, now);
    CHECK(rowText(s, 2).find("20.0 MBIT>") != std::string::npos);
    CHECK(p.takePrefsChanged());

    // the airport goes through the scratchpad, as on the real unit
    p.onKey(Key::Airport);
    CHECK(p.page() == PageId::Weather && p.wantsWeather() && p.refreshMs() == 0);
    p.render(s, now);
    CHECK(rowText(s, 7).find("TYPE AIRPORT THEN 1L") != std::string::npos);
    p.onKey(Key::L1);  // nothing typed yet
    CHECK(!p.takeWeatherRequest() && p.homeAirport().empty());
    for (Key k : {Key::L, Key::F, Key::P, Key::Q, Key::Clr, Key::G, Key::A}) p.onKey(k);  // the fifth letter does not fit
    p.render(s, now);
    CHECK(rowText(s, 13).find("LFPG") == 0);
    p.onKey(Key::L1);
    CHECK(p.homeAirport() == "LFPG" && p.takeWeatherRequest() && p.takePrefsChanged());
    desktop::McduConfig saved;
    p.exportPrefs(saved);
    CHECK(saved.homeAirport == "LFPG");
    WeatherResult w;
    w.status = WeatherStatus::Ok;
    w.report = parseMetar("METAR LFPG 182330Z 18013KT 9999 SCT013TCU BKN013 BKN020 11/10 Q0990 NOSIG");
    p.setWeather(w);
    p.render(s, now);
    CHECK(rowText(s, 2).find("<LFPG") == 0);
    CHECK(rowText(s, 4).find("180/13KT") == 0 && rowText(s, 4).find("10 KM+") == 13);
    CHECK(rowText(s, 6).find("11/10") == 0 && rowText(s, 6).find("990 HPA") == 13);
    CHECK(rowText(s, 8).find("METAR LFPG 182330Z") == 0);
    CHECK(rowText(s, 10).find("BKN013 BKN020 11/10") == 0 && rowText(s, 11).find("Q0990 NOSIG") == 0);  // wrapped between words
    p.onKey(Key::R1);
    CHECK(p.takeWeatherRequest());
    w.status = WeatherStatus::Offline;
    p.setWeather(w);
    p.render(s, now);
    CHECK(rowText(s, 7).find("NO CONNECTION") != std::string::npos);
}

static void testProgressTracker() {
    using namespace desktop;
    using Clock = ProgressTracker::Clock;
    using std::chrono::seconds;
    CHECK(applicationName("application://org.kde.dolphin.desktop") == "DOLPHIN");
    CHECK(applicationName("firefox.desktop") == "FIREFOX");
    CHECK(applicationName("io.github.milkshiift.GoofCord") == "GOOFCORD");
    CHECK(applicationName("") == "TASK");

    ProgressTracker t;
    const auto now = Clock::now();
    CHECK(t.tasks(now).empty());
    t.jobRequested(":1.5", "org.kde.dolphin");
    t.jobUpdate(":1.5", "/JobView/1", "", 10, now);
    auto list = t.tasks(now);
    CHECK(list.size() == 1 && list[0].name == "DOLPHIN" && list[0].percent == 10);
    t.jobUpdate(":1.5", "/JobView/1", "Copying", -1, now);       // an update without a percentage keeps the last one
    t.jobUpdate(":1.5", "/JobView/1", "", 40, now);              // and one without a title keeps the title
    list = t.tasks(now);
    CHECK(list.size() == 1 && list[0].name == "COPYING" && list[0].percent == 40);

    t.notification(":1.9", "Updater", 55, now);
    t.notification(":1.9", "Updater", 60, now);                  // the same task, not a second one
    t.launcher(":1.7", "application://firefox.desktop", 0.25, true, now);
    list = t.tasks(now);
    CHECK(list.size() == 3 && list[1].name == "UPDATER" && list[1].percent == 60 && list[2].name == "FIREFOX" && list[2].percent == 25);

    t.launcher(":1.7", "application://firefox.desktop", -1, false, now);  // the icon's bar went away
    t.jobEnded(":1.5", "/JobView/1");
    list = t.tasks(now);
    CHECK(list.size() == 1 && list[0].name == "UPDATER");

    t.dismiss(list[0].key);                                       // hidden, and it stays hidden while it goes on
    t.notification(":1.9", "Updater", 70, now);
    CHECK(t.tasks(now).empty());

    t.notification(":2.0", "Backup", 100, now);                   // finished: shown for a moment, then gone
    CHECK(t.tasks(now + seconds(3)).size() == 1);
    CHECK(t.tasks(now + seconds(7)).empty());
    t.notification(":2.1", "Stuck", 20, now);                     // a program that stopped reporting
    CHECK(t.tasks(now + seconds(60)).size() == 1);
    CHECK(t.tasks(now + seconds(120)).empty());

    // one program, one piece of work, reported in several ways: one bar
    ProgressTracker once;
    once.jobRequested(":4.1", "org.kde.dolphin");
    once.jobUpdate(":4.1", "/JobView/7", "Copying", 30, now);
    once.launcher(":4.1", "application://org.kde.dolphin.desktop", 0.30, true, now);
    once.notification(":4.1", "Dolphin", 30, now);
    list = once.tasks(now);
    CHECK(list.size() == 1 && list[0].name == "COPYING");
    once.jobEnded(":4.1", "/JobView/7");                          // the other reports take over when the job view is gone
    list = once.tasks(now);
    CHECK(list.size() == 1 && list[0].name == "DOLPHIN");
    ProgressTracker portal;                                       // through the Flatpak portal the sender differs, the program does not
    portal.notification(":5.1", "Firefox", 40, now);
    portal.launcher(":5.2", "application://org.mozilla.firefox.desktop", 0.40, true, now);
    CHECK(portal.tasks(now).size() == 1);
    ProgressTracker two;                                          // two real copies stay two bars
    two.jobRequested(":6.1", "org.kde.dolphin");
    two.jobUpdate(":6.1", "/JobView/1", "Copying", 10, now);
    two.jobUpdate(":6.1", "/JobView/2", "Moving", 20, now);
    CHECK(two.tasks(now).size() == 2);
    ProgressTracker others;                                       // different programs are never merged
    others.notification(":7.1", "Updater", 10, now);
    others.launcher(":7.2", "application://firefox.desktop", 0.5, true, now);
    CHECK(others.tasks(now).size() == 2);

    ProgressTracker many;                                         // whatever a program sends, the list stays small
    for (int i = 0; i < 500; ++i) many.notification(":3." + std::to_string(i), "App" + std::to_string(i), 5, now + seconds(i % 3));
    CHECK(many.tasks(now + seconds(3)).size() <= 32);
}

static void testDirectAndProgressPages() {
    using namespace mcdu;
    FakeActions a;
    PageHost p(a);
    std::tm now{};
    Screen s;
    p.onKey(Key::Dir);
    CHECK(p.page() == PageId::Direct);
    p.render(s, now);
    CHECK(rowText(s, 2).find("<FIREFOX") == 0 && rowText(s, 2).find("GOOGLE>") != std::string::npos);
    CHECK(rowText(s, 12).find("<KONSOLE") == 0 && rowText(s, 12).find("RUN>") != std::string::npos);

    // the first press shows it, only the second one presses Enter
    p.onKey(Key::L1);
    CHECK(a.queries == std::vector<std::string>{"Firefox"} && a.taps.empty());
    p.render(s, now);
    CHECK(rowText(s, 13).find("PRESS AGAIN TO OPEN") == 0);
    p.onKey(Key::L1);
    CHECK(a.queries.size() == 1 && a.taps.size() == 1 && a.taps.back() == std::vector<int>{KEY_ENTER});
    p.onKey(Key::L2);                                  // another app starts over
    p.onKey(Key::L1);
    CHECK(a.taps.size() == 1 && a.queries.size() == 3);
    p.onKey(Key::Menu);                                // leaving the page forgets the first press
    p.onKey(Key::Dir);
    p.onKey(Key::L1);
    CHECK(a.taps.size() == 1 && a.queries.size() == 4);
    a.runnerWorks = false;                             // without KRunner nothing is ever pressed
    p.onKey(Key::L3);
    p.onKey(Key::L3);
    CHECK(a.taps.size() == 1);
    p.render(s, now);
    CHECK(rowText(s, 13).find("KRUNNER UNAVAILABLE") == 0);
    a.runnerWorks = true;

    // searches use what was typed
    p.onKey(Key::R1);
    CHECK(a.urls.empty());
    for (Key k : {Key::L, Key::F, Key::P, Key::G}) p.onKey(k);
    p.render(s, now);
    CHECK(rowText(s, 13).find("LFPG") == 0);
    p.onKey(Key::R4);
    CHECK(a.urls.back() == "https://skyvector.com/airport/LFPG");
    p.onKey(Key::Space);
    p.onKey(Key::A);
    p.onKey(Key::R4);                                  // not an airport code
    CHECK(a.urls.size() == 1);
    p.onKey(Key::R1);
    CHECK(a.urls.back() == "https://www.google.com/search?q=lfpg%20a");
    p.onKey(Key::Ovfy);
    CHECK(a.queries.back() == "LFPG A");
    CHECK(desktop::searchAddress(1, "A&B") == "https://www.youtube.com/results?search_query=a%26b");

    p.onKey(Key::Prog);
    CHECK(p.page() == PageId::Progress && p.wantsTasks() && p.refreshMs() == 1000);
    p.render(s, now);
    CHECK(rowText(s, 6).find("NOT AVAILABLE") != std::string::npos);
    p.setTasks({}, true);
    p.render(s, now);
    CHECK(rowText(s, 6).find("NO ACTIVE TASKS") != std::string::npos);
    p.setTasks({{"k1", "COPYING", 50}, {"k2", "BACKUP", 100}}, true);
    p.render(s, now);
    CHECK(rowText(s, 1).find("COPYING") == 0 && rowText(s, 2).find("<|||||||||") == 0 && rowText(s, 2).find("50%") == 21);
    CHECK(rowText(s, 4).find("100%") == 20 && s.at(4, 1).color == Color::Green);
    p.onKey(Key::L2);
    CHECK(a.dismissed == std::vector<std::string>{"k2"});
}

static void testAudioStreams() {
    using namespace desktop;
    const std::string list =
        R"([{"index":6656,"mute":true,"properties":{"application.name":"goofcord","application.process.binary":"goofcord"}},)"
        R"({"index":7000,"mute":false,"properties":{"application.name":"Firefox"}},)"
        R"({"index":7100,"mute":true,"properties":{"application.name":"WEBRTC VoiceEngine","pipewire.access.portal.app_id":"io.github.milkshiift.GoofCord"}}])";
    StreamSummary s = summarizeStreams(list, "GoofCord");
    CHECK(s.indexes == (std::vector<int>{6656, 7100}) && s.allMuted);     // whatever name the stream gives itself, the Flatpak's id tells whose it is
    s = summarizeStreams(list, "firefox");
    CHECK(s.indexes == std::vector<int>{7000} && !s.allMuted);
    CHECK(summarizeStreams(list, "discord").indexes.empty());
    CHECK(summarizeStreams("not json", "goofcord").indexes.empty());
    CHECK(!summarizeStreams("[]", "goofcord").allMuted);
    const std::string withVolume = R"([{"index":12173,"mute":false,"volume":{"front-left":{"value":44564,"value_percent":"68%"},"front-right":{"value":45875,"value_percent":"70%"}},)"
                                   R"("properties":{"application.name":"Firefox","application.process.binary":"firefox"}}])";
    CHECK(summarizeStreams(withVolume, "firefox").volume == 70 && summarizeStreams(list, "firefox").volume == -1);
    CHECK(parseVolumePercent("Volume: front-left: 45875 /  70% / -9.29 dB,   front-right: 45875 /  70% / -9.29 dB") == 70);
    CHECK(parseVolumePercent("Mute: no") == -1 && parseVolumePercent("100%") == 100);
    setenv("PULSE_SERVER", "unix:/nonexistent/pulse", 1);  // a test run never reaches the real sound server
    AudioControl control;
    const AudioState st = control.read("goofcord");
    unsetenv("PULSE_SERVER");
    CHECK(st.known && !st.available);
}

static void testMounts() {
    using namespace mcdu;
    // what the Flatpak really sees on this machine: its own root is no disk at all
    const std::string sandbox =
        "1 0 0:50 / / rw - tmpfs tmpfs rw\n"
        "2 1 259:3 /x /usr ro - ext4 /dev/nvme0n1p3 ro\n"
        "3 1 259:2 /etc/machine-id /etc/machine-id ro - btrfs /dev/nvme0n1p2 ro\n"
        "4 1 259:3 /pilot/.local/share/Steam /home/pilot/.local/share/Steam ro - ext4 /dev/nvme0n1p3 ro\n"
        "5 1 259:3 /pilot/Documents /home/pilot/Documents ro - ext4 /dev/nvme0n1p3 ro\n"
        "6 1 259:2 /mnt /mnt ro - btrfs /dev/nvme0n1p2 ro\n"
        "7 6 8:1 / /mnt/ssd-datastore0 ro - ext4 /dev/sda1 ro\n"
        "8 6 259:5 / /mnt/ssd-datastore1 ro - ext4 /dev/nvme1n1p1 ro\n"
        "9 6 8:18 / /mnt/hdd-datastore0 ro - ext4 /dev/sdb2 ro\n";
    auto d = parseMounts(sandbox, "/home/pilot", true);
    CHECK(d.size() == 5);
    CHECK(d.size() == 5 && d[0].name == "SYSTEM" && d[0].path == "/etc/machine-id");
    CHECK(d.size() == 5 && d[1].name == "HOME" && d[1].path.rfind("/home/pilot", 0) == 0);
    CHECK(d.size() == 5 && d[2].name == "HDD DATASTORE0" && d[3].name == "SSD DATASTORE0" && d[4].name == "SSD DATASTORE1");

    const std::string host =
        "1 0 0:30 /@ / rw - btrfs /dev/nvme0n1p2 rw\n"
        "2 1 0:30 /@/var /var rw - btrfs /dev/nvme0n1p2 rw\n"
        "3 1 259:1 / /boot/efi rw - vfat /dev/nvme0n1p1 rw\n"
        "4 1 259:3 / /home rw - ext4 /dev/nvme0n1p3 rw\n"
        "5 1 0:40 / /tmp rw - tmpfs tmpfs rw\n"
        "6 1 8:1 / /mnt/my\\040disk rw - ext4 /dev/sda1 rw\n";
    d = parseMounts(host, "/home/pilot", false);
    CHECK(d.size() == 3);
    CHECK(d.size() == 3 && d[0].path == "/" && d[1].path == "/home" && d[2].name == "MY DISK" && d[2].path == "/mnt/my disk");

    CHECK(shortGpuName("Navi 48 [Radeon RX 9070/9070 XT/9070 GRE]") == "RX 9070");
    CHECK(shortGpuName("TU116 [GeForce GTX 1660 SUPER]") == "GTX 1660 SUPER");
}

static void testAxisModes() {
    using namespace desktop;
    CHECK(axisReading(AxisMode::Normal, 1.0) == 1.0 && axisReading(AxisMode::Normal, 0.0) == -1.0);
    CHECK(axisReading(AxisMode::Positive, 0.0) == 0.0 && axisReading(AxisMode::Positive, 1.0) == 1.0);
    CHECK(axisReading(AxisMode::Negative, 1.0) == 0.0 && axisReading(AxisMode::Negative, 0.0) == -1.0);
    CHECK(axisReading(AxisMode::PositiveHalf, 0.0) == 1.0 && axisReading(AxisMode::NegativeHalf, 1.0) == -1.0);
    CHECK(applyCurve(MouseCurve::Linear, 0.5, 1) == 0.5);
    CHECK(applyCurve(MouseCurve::Cubic, 0.5, 1) == 0.125);
    CHECK(applyCurve(MouseCurve::QuadraticExtreme, 1.0, 1) == 1.5);
    CHECK(std::fabs(applyCurve(MouseCurve::Power, 0.25, 2.0) - 0.5) < 1e-9);

    const std::string amgp =
        "<joystick><uniqueID>0300d09f9840000070b90000110100001653647472</uniqueID><sets><set index=\"1\"><axis index=\"1\"><throttle>positivehalf</throttle>"
        "<axisbutton index=\"2\"><mouseacceleration>cubic</mouseacceleration><slots><slot><code>4</code><mode>mousemovement</mode></slot></slots></axisbutton></axis></set></sets></joystick>";
    const ImportResult r = importAmgp(amgp, throttle());
    CHECK(r.ok && r.notes.empty() && r.bindings.size() == 1);
    CHECK(r.bindings.size() == 1 && r.bindings[0].mode == AxisMode::PositiveHalf && r.bindings[0].curve == MouseCurve::Cubic);
}

int main() {
    testScreenEncoding();
    testInitAndLights();
    testKeys();
    testIniBuildsKeys();
    testScreenReader();
    testLights();
    testCurves();
    testAvionics();
    testFont();
    testMarkup();
    testImport();
    testEngine();
    testHighResolutionWheel();
    testPages();
    testAirbusPages();
    testAvionicsTemperatures();
    testGtc();
    testGtcVertical();
    testSwitchKey();
    testBaroStd();
    testFuelUnits();
    testFpln();
    testSystemPage();
    testMetar();
    testNewPages();
    testProgressTracker();
    testDirectAndProgressPages();
    testAudioStreams();
    testMounts();
    testAxisModes();
    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::puts("desktop_mcdu: all checks passed");
    return 0;
}
