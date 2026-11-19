// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "mcdu/X11Capture.hpp"

#include <dlfcn.h>
#include <xcb/composite.h>
#include <xcb/xcb.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <type_traits>

namespace lr::mcdu {

namespace {
constexpr uint32_t kMaxWindows = 1024;
constexpr int kMaxSide = 4096;
}  // namespace

struct X11Capture::Api {
    void* xcb = nullptr;
    void* composite = nullptr;
    decltype(&::xcb_connect) connect;
    decltype(&::xcb_disconnect) disconnect;
    decltype(&::xcb_connection_has_error) hasError;
    decltype(&::xcb_get_setup) getSetup;
    decltype(&::xcb_setup_roots_iterator) roots;
    decltype(&::xcb_generate_id) generateId;
    decltype(&::xcb_intern_atom) internAtom;
    decltype(&::xcb_intern_atom_reply) internAtomReply;
    decltype(&::xcb_get_property) getProperty;
    decltype(&::xcb_get_property_reply) getPropertyReply;
    decltype(&::xcb_get_property_value) propertyValue;
    decltype(&::xcb_get_property_value_length) propertyLength;
    decltype(&::xcb_get_geometry) getGeometry;
    decltype(&::xcb_get_geometry_reply) getGeometryReply;
    decltype(&::xcb_get_image) getImage;
    decltype(&::xcb_get_image_reply) getImageReply;
    decltype(&::xcb_get_image_data) imageData;
    decltype(&::xcb_get_image_data_length) imageLength;
    decltype(&::xcb_free_pixmap_checked) freePixmap;
    decltype(&::xcb_request_check) requestCheck;
    decltype(&::xcb_composite_query_version) compositeVersion;
    decltype(&::xcb_composite_query_version_reply) compositeVersionReply;
    decltype(&::xcb_composite_name_window_pixmap_checked) namePixmap;

    bool load(std::string& err) {
        xcb = dlopen("libxcb.so.1", RTLD_NOW | RTLD_LOCAL);
        composite = xcb ? dlopen("libxcb-composite.so.0", RTLD_NOW | RTLD_LOCAL) : nullptr;
        if (!xcb || !composite) { err = "libxcb or libxcb-composite is missing"; return false; }
        bool ok = true;
        auto sym = [&](void* lib, auto& fn, const char* name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(lib, name));
            ok = ok && fn != nullptr;
        };
        sym(xcb, connect, "xcb_connect");
        sym(xcb, disconnect, "xcb_disconnect");
        sym(xcb, hasError, "xcb_connection_has_error");
        sym(xcb, getSetup, "xcb_get_setup");
        sym(xcb, roots, "xcb_setup_roots_iterator");
        sym(xcb, generateId, "xcb_generate_id");
        sym(xcb, internAtom, "xcb_intern_atom");
        sym(xcb, internAtomReply, "xcb_intern_atom_reply");
        sym(xcb, getProperty, "xcb_get_property");
        sym(xcb, getPropertyReply, "xcb_get_property_reply");
        sym(xcb, propertyValue, "xcb_get_property_value");
        sym(xcb, propertyLength, "xcb_get_property_value_length");
        sym(xcb, getGeometry, "xcb_get_geometry");
        sym(xcb, getGeometryReply, "xcb_get_geometry_reply");
        sym(xcb, getImage, "xcb_get_image");
        sym(xcb, getImageReply, "xcb_get_image_reply");
        sym(xcb, imageData, "xcb_get_image_data");
        sym(xcb, imageLength, "xcb_get_image_data_length");
        sym(xcb, freePixmap, "xcb_free_pixmap_checked");
        sym(xcb, requestCheck, "xcb_request_check");
        sym(composite, compositeVersion, "xcb_composite_query_version");
        sym(composite, compositeVersionReply, "xcb_composite_query_version_reply");
        sym(composite, namePixmap, "xcb_composite_name_window_pixmap_checked");
        if (!ok) err = "libxcb is too old";
        return ok;
    }
    ~Api() {
        if (composite) dlclose(composite);
        if (xcb) dlclose(xcb);
    }
};

X11Capture::~X11Capture() {
    close();
    delete api_;
}

bool X11Capture::open(std::string& err) {
    if (conn_) return true;
    if (!std::getenv("DISPLAY")) { err = "no X11 display"; return false; }
    if (!api_) {
        api_ = new Api();
        if (!api_->load(err)) {
            delete api_;
            api_ = nullptr;
            return false;
        }
    }
    int screen = 0;
    auto* c = api_->connect(nullptr, &screen);
    if (!c || api_->hasError(c)) {
        if (c) api_->disconnect(c);
        err = "cannot reach the X server";
        return false;
    }
    auto it = api_->roots(api_->getSetup(c));
    for (int i = 0; i < screen && it.rem > 0; ++i) {
        --it.rem;
        ++it.data;
    }
    if (it.rem <= 0 || !it.data) {
        api_->disconnect(c);
        err = "no X screen";
        return false;
    }
    root_ = it.data->root;
    xcb_generic_error_t* e = nullptr;
    auto* v = api_->compositeVersionReply(c, api_->compositeVersion(c, 0, 2), &e);  // the extension must be asked for once
    std::free(e);
    if (!v) {
        api_->disconnect(c);
        err = "the X server has no Composite extension";
        return false;
    }
    std::free(v);
    const char name[] = "_NET_CLIENT_LIST";
    auto* a = api_->internAtomReply(c, api_->internAtom(c, 1, sizeof name - 1, name), nullptr);
    clientList_ = a ? a->atom : 0;
    std::free(a);
    conn_ = c;
    return true;
}

void X11Capture::close() {
    if (conn_ && api_) api_->disconnect(static_cast<xcb_connection_t*>(conn_));
    conn_ = nullptr;
}

std::vector<uint32_t> X11Capture::popOuts() {
    std::vector<uint32_t> out;
    auto* c = static_cast<xcb_connection_t*>(conn_);
    if (!c || !clientList_) return out;
    if (api_->hasError(c)) {
        close();
        return out;
    }
    auto property = [&](uint32_t window, uint32_t atom, uint32_t type, uint32_t longs) {
        return api_->getPropertyReply(c, api_->getProperty(c, 0, window, atom, type, 0, longs), nullptr);
    };
    auto* list = property(root_, clientList_, XCB_ATOM_WINDOW, kMaxWindows);
    if (!list) return out;
    const auto* windows = static_cast<const uint32_t*>(api_->propertyValue(list));
    const int count = api_->propertyLength(list) / 4;
    for (int i = 0; i < count && i < static_cast<int>(kMaxWindows); ++i) {
        const uint32_t w = windows[i];
        auto* cls = property(w, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 32);
        bool sim = false;
        if (cls) {
            const std::string v(static_cast<const char*>(api_->propertyValue(cls)), static_cast<size_t>(std::max(api_->propertyLength(cls), 0)));
            sim = v.find("steam_app_2537590") != std::string::npos || v.find("steam_app_1250410") != std::string::npos;  // MSFS 2024, MSFS 2020
            std::free(cls);
        }
        if (!sim) continue;
        auto* nm = property(w, XCB_ATOM_WM_NAME, XCB_GET_PROPERTY_TYPE_ANY, 16);
        if (nm) {
            const std::string v(static_cast<const char*>(api_->propertyValue(nm)), static_cast<size_t>(std::max(api_->propertyLength(nm), 0)));
            if (v == "WASMINSTRUMENT") out.push_back(w);
            std::free(nm);
        }
    }
    std::free(list);
    return out;
}

bool X11Capture::grab(uint32_t window, std::vector<uint8_t>& out, int& width, int& height) {
    auto* c = static_cast<xcb_connection_t*>(conn_);
    if (!c) return false;
    if (api_->hasError(c)) {
        close();
        return false;
    }
    xcb_generic_error_t* e = nullptr;
    auto* g = api_->getGeometryReply(c, api_->getGeometry(c, window), &e);
    std::free(e);
    if (!g) return false;
    width = g->width, height = g->height;
    const int depth = g->depth;
    std::free(g);
    if (width < 64 || height < 64 || width > kMaxSide || height > kMaxSide || (depth != 24 && depth != 32)) return false;
    // The window's own pixmap, named again for every picture: a plain read of the window can lag seconds behind under XWayland.
    const uint32_t pixmap = api_->generateId(c);
    if (auto* err = api_->requestCheck(c, api_->namePixmap(c, window, pixmap))) {
        std::free(err);
        return false;
    }
    auto* img = api_->getImageReply(c, api_->getImage(c, XCB_IMAGE_FORMAT_Z_PIXMAP, pixmap, 0, 0, static_cast<uint16_t>(width), static_cast<uint16_t>(height), ~0u), &e);
    std::free(e);
    std::free(api_->requestCheck(c, api_->freePixmap(c, pixmap)));
    if (!img) return false;
    const size_t need = static_cast<size_t>(width) * height * 4;
    const bool ok = api_->imageLength(img) >= static_cast<int>(need);
    if (ok) out.assign(api_->imageData(img), api_->imageData(img) + need);
    std::free(img);
    return ok;
}

}  // namespace lr::mcdu
