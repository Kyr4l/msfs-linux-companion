// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "desktop/SdBus.hpp"

#include <dlfcn.h>

#include "util/Log.hpp"

namespace lr::desktop::sdbus {

namespace {
template <typename F>
bool bind(void* lib, F& target, const char* name) {
    target = reinterpret_cast<F>(dlsym(lib, name));
    if (!target) LOG_WARN("libsystemd: %s is missing", name);
    return target != nullptr;
}

bool load(Api& a) {
    void* lib = dlopen("libsystemd.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        LOG_INFO("libsystemd not found: no tray icon or media control");
        return false;
    }
    bool ok = true;
    ok &= bind(lib, a.open_user, "sd_bus_open_user");
    ok &= bind(lib, a.bus_new, "sd_bus_new");
    ok &= bind(lib, a.set_address, "sd_bus_set_address");
    ok &= bind(lib, a.set_monitor, "sd_bus_set_monitor");
    ok &= bind(lib, a.set_bus_client, "sd_bus_set_bus_client");
    ok &= bind(lib, a.start, "sd_bus_start");
    ok &= bind(lib, a.flush_close_unref, "sd_bus_flush_close_unref");
    ok &= bind(lib, a.get_fd, "sd_bus_get_fd");
    ok &= bind(lib, a.get_events, "sd_bus_get_events");
    ok &= bind(lib, a.get_timeout, "sd_bus_get_timeout");
    ok &= bind(lib, a.process, "sd_bus_process");
    ok &= bind(lib, a.flush, "sd_bus_flush");
    ok &= bind(lib, a.get_unique_name, "sd_bus_get_unique_name");
    ok &= bind(lib, a.add_object, "sd_bus_add_object");
    ok &= bind(lib, a.add_match, "sd_bus_add_match");
    ok &= bind(lib, a.send, "sd_bus_send");
    ok &= bind(lib, a.call, "sd_bus_call");
    ok &= bind(lib, a.call_async, "sd_bus_call_async");
    ok &= bind(lib, a.slot_unref, "sd_bus_slot_unref");
    ok &= bind(lib, a.message_new_method_call, "sd_bus_message_new_method_call");
    ok &= bind(lib, a.message_new_method_return, "sd_bus_message_new_method_return");
    ok &= bind(lib, a.message_new_signal, "sd_bus_message_new_signal");
    ok &= bind(lib, a.message_unref, "sd_bus_message_unref");
    ok &= bind(lib, a.message_append, "sd_bus_message_append");
    ok &= bind(lib, a.message_append_array, "sd_bus_message_append_array");
    ok &= bind(lib, a.message_open_container, "sd_bus_message_open_container");
    ok &= bind(lib, a.message_close_container, "sd_bus_message_close_container");
    ok &= bind(lib, a.message_read, "sd_bus_message_read");
    ok &= bind(lib, a.message_enter_container, "sd_bus_message_enter_container");
    ok &= bind(lib, a.message_exit_container, "sd_bus_message_exit_container");
    ok &= bind(lib, a.message_skip, "sd_bus_message_skip");
    ok &= bind(lib, a.message_peek_type, "sd_bus_message_peek_type");
    ok &= bind(lib, a.message_at_end, "sd_bus_message_at_end");
    ok &= bind(lib, a.message_get_member, "sd_bus_message_get_member");
    ok &= bind(lib, a.message_get_interface, "sd_bus_message_get_interface");
    ok &= bind(lib, a.message_is_method_call, "sd_bus_message_is_method_call");
    ok &= bind(lib, a.message_is_signal, "sd_bus_message_is_signal");
    ok &= bind(lib, a.message_get_sender, "sd_bus_message_get_sender");
    ok &= bind(lib, a.message_get_path, "sd_bus_message_get_path");
    ok &= bind(lib, a.message_get_errno, "sd_bus_message_get_errno");
    ok &= bind(lib, a.error_free, "sd_bus_error_free");
    return ok;
}
}  // namespace

const Api* api() {
    static Api a{};
    static const bool ok = load(a);
    return ok ? &a : nullptr;
}

}  // namespace lr::desktop::sdbus
