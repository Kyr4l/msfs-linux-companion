// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "desktop/Tray.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "ui/AppIcon.hpp"
#include "util/Isolated.hpp"
#include "util/Log.hpp"

namespace lr::desktop {

namespace {
constexpr const char* kItemPath = "/StatusNotifierItem";
constexpr const char* kMenuPath = "/MenuBar";
constexpr const char* kWatcher = "org.kde.StatusNotifierWatcher";
constexpr const char* kItemInterface = "org.kde.StatusNotifierItem";
constexpr const char* kMenuInterface = "com.canonical.dbusmenu";
constexpr const char* kProperties = "org.freedesktop.DBus.Properties";
constexpr const char* kIntrospectable = "org.freedesktop.DBus.Introspectable";

enum MenuId { kRoot = 0, kOpen = 1, kSep1 = 2, kDesktop = 3, kSep2 = 4, kQuit = 5 };
constexpr int kMenuItems[] = {kOpen, kSep1, kDesktop, kSep2, kQuit};

constexpr const char* kItemXml =
    "<node><interface name=\"org.kde.StatusNotifierItem\">"
    "<property name=\"Category\" type=\"s\" access=\"read\"/><property name=\"Id\" type=\"s\" access=\"read\"/>"
    "<property name=\"Title\" type=\"s\" access=\"read\"/><property name=\"Status\" type=\"s\" access=\"read\"/>"
    "<property name=\"WindowId\" type=\"i\" access=\"read\"/><property name=\"IconName\" type=\"s\" access=\"read\"/>"
    "<property name=\"IconPixmap\" type=\"a(iiay)\" access=\"read\"/><property name=\"OverlayIconName\" type=\"s\" access=\"read\"/>"
    "<property name=\"AttentionIconName\" type=\"s\" access=\"read\"/><property name=\"ToolTip\" type=\"(sa(iiay)ss)\" access=\"read\"/>"
    "<property name=\"ItemIsMenu\" type=\"b\" access=\"read\"/><property name=\"Menu\" type=\"o\" access=\"read\"/>"
    "<method name=\"Activate\"><arg type=\"i\" direction=\"in\"/><arg type=\"i\" direction=\"in\"/></method>"
    "<method name=\"SecondaryActivate\"><arg type=\"i\" direction=\"in\"/><arg type=\"i\" direction=\"in\"/></method>"
    "<method name=\"ContextMenu\"><arg type=\"i\" direction=\"in\"/><arg type=\"i\" direction=\"in\"/></method>"
    "<method name=\"Scroll\"><arg type=\"i\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/></method>"
    "<signal name=\"NewToolTip\"/><signal name=\"NewIcon\"/><signal name=\"NewStatus\"><arg type=\"s\"/></signal>"
    "</interface></node>";

constexpr const char* kMenuXml =
    "<node><interface name=\"com.canonical.dbusmenu\">"
    "<property name=\"Version\" type=\"u\" access=\"read\"/><property name=\"TextDirection\" type=\"s\" access=\"read\"/>"
    "<property name=\"Status\" type=\"s\" access=\"read\"/><property name=\"IconThemePath\" type=\"as\" access=\"read\"/>"
    "<method name=\"GetLayout\"><arg type=\"i\" direction=\"in\"/><arg type=\"i\" direction=\"in\"/><arg type=\"as\" direction=\"in\"/>"
    "<arg type=\"u\" direction=\"out\"/><arg type=\"(ia{sv}av)\" direction=\"out\"/></method>"
    "<method name=\"GetGroupProperties\"><arg type=\"ai\" direction=\"in\"/><arg type=\"as\" direction=\"in\"/><arg type=\"a(ia{sv})\" direction=\"out\"/></method>"
    "<method name=\"GetProperty\"><arg type=\"i\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/><arg type=\"v\" direction=\"out\"/></method>"
    "<method name=\"Event\"><arg type=\"i\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/><arg type=\"v\" direction=\"in\"/><arg type=\"u\" direction=\"in\"/></method>"
    "<method name=\"EventGroup\"><arg type=\"a(isvu)\" direction=\"in\"/><arg type=\"ai\" direction=\"out\"/></method>"
    "<method name=\"AboutToShow\"><arg type=\"i\" direction=\"in\"/><arg type=\"b\" direction=\"out\"/></method>"
    "<method name=\"AboutToShowGroup\"><arg type=\"ai\" direction=\"in\"/><arg type=\"ai\" direction=\"out\"/><arg type=\"ai\" direction=\"out\"/></method>"
    "<signal name=\"LayoutUpdated\"><arg type=\"u\"/><arg type=\"i\"/></signal>"
    "</interface></node>";

constexpr const char* kItemProps[] = {"Category", "Id", "Title", "Status", "WindowId", "IconName", "IconPixmap", "OverlayIconName", "AttentionIconName", "ToolTip", "ItemIsMenu", "Menu"};
}  // namespace

Tray::~Tray() {
    if (!api_) return;
    if (pendingRegister_) api_->slot_unref(pendingRegister_);
    for (sdbus::Slot* s : slots_) api_->slot_unref(s);
    if (bus_) api_->flush_close_unref(bus_);
}

bool Tray::start(Callbacks callbacks, const std::string& iconName) {
    // a test run has no tray, unless a test brings its own private session bus
    if (isolatedRun() && !std::getenv("MSFS_LINUX_COMPANION_TEST_TRAY")) return false;
    api_ = sdbus::api();
    if (!api_) return false;
    cb_ = std::move(callbacks);
    iconName_ = iconName;
    status_ = "MSFS Linux Companion";
    buildPixmaps();
    int r = api_->open_user(&bus_);
    if (r < 0) {
        LOG_WARN("Tray: no session bus (%s)", strerror(-r));
        bus_ = nullptr;
        return false;
    }
    sdbus::Slot* slot = nullptr;
    if (api_->add_object(bus_, &slot, kItemPath, &Tray::onItem, this) >= 0) slots_.push_back(slot);
    if (api_->add_object(bus_, &slot, kMenuPath, &Tray::onMenu, this) >= 0) slots_.push_back(slot);
    // The desktop may start after this program (at login it usually does): the icon is offered again each time a tray host appears.
    const std::string match = "type='signal',sender='org.freedesktop.DBus',member='NameOwnerChanged',arg0='" + std::string(kWatcher) + "'";
    if (api_->add_match(bus_, &slot, match.c_str(), &Tray::onWatcherChanged, this) >= 0) slots_.push_back(slot);
    registerWithWatcher();
    return true;
}

void Tray::buildPixmaps() {
    // the icon as the protocol wants it: ARGB, most significant byte first
    auto add = [this](int size, const unsigned char* rgba) {
        Pixmap p{size, std::vector<uint8_t>(static_cast<size_t>(size) * size * 4)};
        for (size_t i = 0; i + 3 < p.argb.size(); i += 4) {
            p.argb[i] = rgba[i + 3];
            p.argb[i + 1] = rgba[i];
            p.argb[i + 2] = rgba[i + 1];
            p.argb[i + 3] = rgba[i + 2];
        }
        pixmaps_.push_back(std::move(p));
    };
    pixmaps_.clear();
    if (icon_ == TrayIcon::Colour) add(ui::kAppIconSize, ui::kAppIconRgba);
    else
        for (const ui::TrayIconImage& img : icon_ == TrayIcon::Light ? ui::kTrayLight : ui::kTrayDark) add(img.size, img.rgba);
}

void Tray::setIcon(TrayIcon icon) {
    if (icon == icon_) return;
    icon_ = icon;
    buildPixmaps();
    if (!bus_) return;
    sdbus::Message* s = nullptr;
    if (api_->message_new_signal(bus_, &s, kItemPath, kItemInterface, "NewIcon") < 0) return;
    api_->send(bus_, s, nullptr);
    api_->message_unref(s);
}

void Tray::registerWithWatcher() {
    if (!bus_) return;
    if (pendingRegister_) { api_->slot_unref(pendingRegister_); pendingRegister_ = nullptr; }
    sdbus::Message* m = nullptr;
    if (api_->message_new_method_call(bus_, &m, kWatcher, "/StatusNotifierWatcher", kWatcher, "RegisterStatusNotifierItem") < 0) return;
    // The object path instead of a well-known name: the host takes the sender's own bus name, and a Flatpak needs no permission to own a name.
    api_->message_append(m, "s", kItemPath);
    // Asynchronous: the host calls back into this object while it registers it.
    if (api_->call_async(bus_, &pendingRegister_, m, &Tray::onRegistered, this, 5'000'000) < 0) registered_ = false;
    api_->message_unref(m);
}

int Tray::onRegistered(sdbus::Message* m, void* self, sdbus::Error*) {
    auto* t = static_cast<Tray*>(self);
    t->registered_ = t->api_->message_get_errno(m) == 0;
    if (t->pendingRegister_) { t->api_->slot_unref(t->pendingRegister_); t->pendingRegister_ = nullptr; }
    LOG_INFO("Tray: %s", t->registered_ ? "icon registered" : "no tray host");
    return 1;
}

int Tray::onWatcherChanged(sdbus::Message* m, void* self, sdbus::Error*) {
    auto* t = static_cast<Tray*>(self);
    const char* name = nullptr, *oldOwner = nullptr, *newOwner = nullptr;
    if (t->api_->message_read(m, "sss", &name, &oldOwner, &newOwner) < 0) return 0;
    if (newOwner && *newOwner) t->registerWithWatcher();
    else t->registered_ = false;
    return 0;
}

int Tray::fd() const { return bus_ ? api_->get_fd(bus_) : -1; }

short Tray::events() const {
    if (!bus_) return 0;
    const int e = api_->get_events(bus_);
    return e < 0 ? 0 : static_cast<short>(e);
}

int Tray::timeoutMs() const {
    if (!bus_) return -1;
    uint64_t usec = 0;
    if (api_->get_timeout(bus_, &usec) <= 0 || usec == UINT64_MAX) return -1;
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const uint64_t now = static_cast<uint64_t>(ts.tv_sec) * 1000000u + static_cast<uint64_t>(ts.tv_nsec) / 1000u;
    return usec <= now ? 0 : static_cast<int>(std::min<uint64_t>((usec - now) / 1000 + 1, 60000));
}

void Tray::process() {
    if (!bus_) return;
    for (int i = 0; i < 64; ++i) {  // a flood from the bus cannot hold the main thread
        const int r = api_->process(bus_, nullptr);
        if (r < 0) {
            LOG_WARN("Tray: the session bus went away (%s)", strerror(-r));
            for (sdbus::Slot* s : slots_) api_->slot_unref(s);
            slots_.clear();
            if (pendingRegister_) { api_->slot_unref(pendingRegister_); pendingRegister_ = nullptr; }
            api_->flush_close_unref(bus_);
            bus_ = nullptr;
            registered_ = false;
            return;
        }
        if (r == 0) break;
    }
    api_->flush(bus_);
}

void Tray::appendItemProperty(sdbus::Message* r, const std::string& name) {
    const auto& a = *api_;
    if (name == "Category") a.message_append(r, "v", "s", "ApplicationStatus");
    else if (name == "Id") a.message_append(r, "v", "s", "msfs-linux-companion");
    else if (name == "Title") a.message_append(r, "v", "s", "MSFS Linux Companion");
    else if (name == "Status") a.message_append(r, "v", "s", "Active");
    else if (name == "WindowId") a.message_append(r, "v", "i", 0);
    // by name the host would take the app's icon from the icon theme: the one-colour icons go as pictures only
    else if (name == "IconName") a.message_append(r, "v", "s", icon_ == TrayIcon::Colour ? iconName_.c_str() : "");
    else if (name == "OverlayIconName" || name == "AttentionIconName") a.message_append(r, "v", "s", "");
    else if (name == "ItemIsMenu") a.message_append(r, "v", "b", 0);
    else if (name == "Menu") a.message_append(r, "v", "o", kMenuPath);
    else if (name == "IconPixmap") {
        a.message_open_container(r, 'v', "a(iiay)");
        a.message_open_container(r, 'a', "(iiay)");
        for (const Pixmap& p : pixmaps_) {
            a.message_open_container(r, 'r', "iiay");
            a.message_append(r, "ii", p.size, p.size);
            a.message_append_array(r, 'y', p.argb.data(), p.argb.size());
            a.message_close_container(r);
        }
        a.message_close_container(r);
        a.message_close_container(r);
    } else if (name == "ToolTip") {
        a.message_open_container(r, 'v', "(sa(iiay)ss)");
        a.message_open_container(r, 'r', "sa(iiay)ss");
        a.message_append(r, "s", iconName_.c_str());
        a.message_open_container(r, 'a', "(iiay)");
        a.message_close_container(r);
        a.message_append(r, "ss", "MSFS Linux Companion", status_.c_str());
        a.message_close_container(r);
        a.message_close_container(r);
    }
}

int Tray::onItem(sdbus::Message* m, void* self, sdbus::Error*) {
    auto* t = static_cast<Tray*>(self);
    const auto& a = *t->api_;
    sdbus::Message* reply = nullptr;
    auto finish = [&] {
        if (reply) { a.send(t->bus_, reply, nullptr); a.message_unref(reply); }
        return 1;
    };
    if (a.message_is_method_call(m, kIntrospectable, "Introspect") > 0) {
        a.message_new_method_return(m, &reply);
        a.message_append(reply, "s", kItemXml);
        return finish();
    }
    if (a.message_is_method_call(m, kProperties, "Get") > 0) {
        const char* iface = nullptr, *name = nullptr;
        if (a.message_read(m, "ss", &iface, &name) < 0) return 0;
        a.message_new_method_return(m, &reply);
        t->appendItemProperty(reply, name);
        return finish();
    }
    if (a.message_is_method_call(m, kProperties, "GetAll") > 0) {
        a.message_new_method_return(m, &reply);
        a.message_open_container(reply, 'a', "{sv}");
        for (const char* name : kItemProps) {
            a.message_open_container(reply, 'e', "sv");
            a.message_append(reply, "s", name);
            t->appendItemProperty(reply, name);
            a.message_close_container(reply);
        }
        a.message_close_container(reply);
        return finish();
    }
    if (a.message_is_method_call(m, kItemInterface, "Activate") > 0 || a.message_is_method_call(m, kItemInterface, "SecondaryActivate") > 0) {
        if (t->cb_.open) t->cb_.open();
        a.message_new_method_return(m, &reply);
        return finish();
    }
    if (a.message_is_method_call(m, kItemInterface, "ContextMenu") > 0 || a.message_is_method_call(m, kItemInterface, "Scroll") > 0) {
        a.message_new_method_return(m, &reply);
        return finish();
    }
    return 0;
}

void Tray::appendMenuProps(sdbus::Message* r, int id) {
    const auto& a = *api_;
    a.message_open_container(r, 'a', "{sv}");
    auto text = [&](const char* key, const char* value) {
        a.message_open_container(r, 'e', "sv");
        a.message_append(r, "s", key);
        a.message_append(r, "v", "s", value);
        a.message_close_container(r);
    };
    auto number = [&](const char* key, int value) {
        a.message_open_container(r, 'e', "sv");
        a.message_append(r, "s", key);
        a.message_append(r, "v", "i", value);
        a.message_close_container(r);
    };
    switch (id) {
        case kRoot: text("children-display", "submenu"); break;
        case kOpen: text("label", "Open"); break;
        case kSep1: case kSep2: text("type", "separator"); break;
        case kDesktop:
            text("label", "Desktop controls");
            text("toggle-type", "checkmark");
            number("toggle-state", desktopControls_ ? 1 : 0);
            break;
        case kQuit: text("label", "Quit"); break;
        default: break;
    }
    a.message_close_container(r);
}

// One entry of the layout: (id, properties, children as variants).
void Tray::appendMenuItem(sdbus::Message* r, int id) {
    const auto& a = *api_;
    a.message_open_container(r, 'r', "ia{sv}av");
    a.message_append(r, "i", id);
    appendMenuProps(r, id);
    a.message_open_container(r, 'a', "v");
    if (id == kRoot)
        for (int child : kMenuItems) {
            a.message_open_container(r, 'v', "(ia{sv}av)");
            appendMenuItem(r, child);
            a.message_close_container(r);
        }
    a.message_close_container(r);
    a.message_close_container(r);
}

void Tray::menuClicked(int id) {
    if (id == kOpen && cb_.open) cb_.open();
    else if (id == kQuit && cb_.quit) cb_.quit();
    else if (id == kDesktop && cb_.desktopControls) cb_.desktopControls(!desktopControls_);
}

int Tray::onMenu(sdbus::Message* m, void* self, sdbus::Error*) {
    auto* t = static_cast<Tray*>(self);
    const auto& a = *t->api_;
    sdbus::Message* reply = nullptr;
    auto finish = [&] {
        if (reply) { a.send(t->bus_, reply, nullptr); a.message_unref(reply); }
        return 1;
    };
    if (a.message_is_method_call(m, kIntrospectable, "Introspect") > 0) {
        a.message_new_method_return(m, &reply);
        a.message_append(reply, "s", kMenuXml);
        return finish();
    }
    auto menuProperty = [&](const std::string& name) {
        if (name == "Version") a.message_append(reply, "v", "u", 3u);
        else if (name == "TextDirection") a.message_append(reply, "v", "s", "ltr");
        else if (name == "Status") a.message_append(reply, "v", "s", "normal");
        else if (name == "IconThemePath") {
            a.message_open_container(reply, 'v', "as");
            a.message_open_container(reply, 'a', "s");
            a.message_close_container(reply);
            a.message_close_container(reply);
        }
    };
    if (a.message_is_method_call(m, kProperties, "Get") > 0) {
        const char* iface = nullptr, *name = nullptr;
        if (a.message_read(m, "ss", &iface, &name) < 0) return 0;
        a.message_new_method_return(m, &reply);
        menuProperty(name);
        return finish();
    }
    if (a.message_is_method_call(m, kProperties, "GetAll") > 0) {
        a.message_new_method_return(m, &reply);
        a.message_open_container(reply, 'a', "{sv}");
        for (const char* name : {"Version", "TextDirection", "Status", "IconThemePath"}) {
            a.message_open_container(reply, 'e', "sv");
            a.message_append(reply, "s", name);
            menuProperty(name);
            a.message_close_container(reply);
        }
        a.message_close_container(reply);
        return finish();
    }
    if (a.message_is_method_call(m, kMenuInterface, "GetLayout") > 0) {
        int parent = 0, depth = 0;
        if (a.message_read(m, "ii", &parent, &depth) < 0) return 0;
        a.message_new_method_return(m, &reply);
        a.message_append(reply, "u", t->revision_);
        t->appendMenuItem(reply, parent);
        return finish();
    }
    if (a.message_is_method_call(m, kMenuInterface, "GetGroupProperties") > 0) {
        a.message_new_method_return(m, &reply);
        a.message_open_container(reply, 'a', "(ia{sv})");
        if (a.message_enter_container(m, 'a', "i") > 0) {
            int id = 0;
            while (a.message_read(m, "i", &id) > 0) {
                a.message_open_container(reply, 'r', "ia{sv}");
                a.message_append(reply, "i", id);
                t->appendMenuProps(reply, id);
                a.message_close_container(reply);
            }
            a.message_exit_container(m);
        }
        a.message_close_container(reply);
        return finish();
    }
    if (a.message_is_method_call(m, kMenuInterface, "GetProperty") > 0) {
        a.message_new_method_return(m, &reply);
        a.message_append(reply, "v", "s", "");
        return finish();
    }
    if (a.message_is_method_call(m, kMenuInterface, "Event") > 0) {
        int id = 0;
        const char* event = nullptr;
        if (a.message_read(m, "is", &id, &event) < 0) return 0;
        const bool clicked = event && std::strcmp(event, "clicked") == 0;
        a.message_new_method_return(m, &reply);
        finish();
        if (clicked) t->menuClicked(id);
        return 1;
    }
    if (a.message_is_method_call(m, kMenuInterface, "EventGroup") > 0) {
        std::vector<int> clicked;
        if (a.message_enter_container(m, 'a', "(isvu)") > 0) {
            while (a.message_enter_container(m, 'r', "isvu") > 0) {
                int id = 0;
                const char* event = nullptr;
                if (a.message_read(m, "is", &id, &event) >= 0 && event && std::strcmp(event, "clicked") == 0) clicked.push_back(id);
                a.message_skip(m, "vu");
                a.message_exit_container(m);
            }
            a.message_exit_container(m);
        }
        a.message_new_method_return(m, &reply);
        a.message_open_container(reply, 'a', "i");
        a.message_close_container(reply);
        finish();
        for (int id : clicked) t->menuClicked(id);
        return 1;
    }
    if (a.message_is_method_call(m, kMenuInterface, "AboutToShow") > 0) {
        a.message_new_method_return(m, &reply);
        a.message_append(reply, "b", 0);
        return finish();
    }
    if (a.message_is_method_call(m, kMenuInterface, "AboutToShowGroup") > 0) {
        a.message_new_method_return(m, &reply);
        a.message_open_container(reply, 'a', "i");
        a.message_close_container(reply);
        a.message_open_container(reply, 'a', "i");
        a.message_close_container(reply);
        return finish();
    }
    return 0;
}

void Tray::signalLayout() {
    if (!bus_) return;
    ++revision_;
    sdbus::Message* s = nullptr;
    if (api_->message_new_signal(bus_, &s, kMenuPath, kMenuInterface, "LayoutUpdated") < 0) return;
    api_->message_append(s, "ui", revision_, 0);
    api_->send(bus_, s, nullptr);
    api_->message_unref(s);
}

void Tray::setDesktopControls(bool on) {
    if (on == desktopControls_) return;
    desktopControls_ = on;
    signalLayout();
}

void Tray::setStatus(const std::string& text) {
    if (text == status_) return;
    status_ = text;
    if (!bus_) return;
    sdbus::Message* s = nullptr;
    if (api_->message_new_signal(bus_, &s, kItemPath, kItemInterface, "NewToolTip") < 0) return;
    api_->send(bus_, s, nullptr);
    api_->message_unref(s);
}

}  // namespace lr::desktop
