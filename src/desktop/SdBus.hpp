// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// The D-Bus library of systemd, loaded when the program needs it. Where it is missing the tray and the media page are simply not there;
// the program itself never depends on it (and the build needs no development package).
#include <cstddef>
#include <cstdint>

namespace lr::desktop::sdbus {

struct Bus;
struct Message;
struct Slot;
struct Error {
    const char* name = nullptr;
    const char* message = nullptr;
    int needFree = 0;
};

using Handler = int (*)(Message* m, void* userdata, Error* err);

struct Api {
    int (*open_user)(Bus** bus);
    int (*bus_new)(Bus** bus);
    int (*set_address)(Bus* bus, const char* address);
    int (*set_monitor)(Bus* bus, int on);
    int (*set_bus_client)(Bus* bus, int on);
    int (*start)(Bus* bus);
    Bus* (*flush_close_unref)(Bus* bus);
    int (*get_fd)(Bus* bus);
    int (*get_events)(Bus* bus);
    int (*get_timeout)(Bus* bus, uint64_t* usec);
    int (*process)(Bus* bus, Message** reply);
    int (*flush)(Bus* bus);
    int (*get_unique_name)(Bus* bus, const char** name);
    int (*add_object)(Bus* bus, Slot** slot, const char* path, Handler callback, void* userdata);
    int (*add_match)(Bus* bus, Slot** slot, const char* match, Handler callback, void* userdata);
    int (*send)(Bus* bus, Message* m, uint64_t* cookie);
    int (*call)(Bus* bus, Message* m, uint64_t usec, Error* err, Message** reply);
    int (*call_async)(Bus* bus, Slot** slot, Message* m, Handler callback, void* userdata, uint64_t usec);
    Slot* (*slot_unref)(Slot* slot);

    int (*message_new_method_call)(Bus* bus, Message** m, const char* destination, const char* path, const char* interface, const char* member);
    int (*message_new_method_return)(Message* call, Message** m);
    int (*message_new_signal)(Bus* bus, Message** m, const char* path, const char* interface, const char* member);
    Message* (*message_unref)(Message* m);
    int (*message_append)(Message* m, const char* types, ...);
    int (*message_append_array)(Message* m, char type, const void* ptr, size_t size);
    int (*message_open_container)(Message* m, char type, const char* contents);
    int (*message_close_container)(Message* m);
    int (*message_read)(Message* m, const char* types, ...);
    int (*message_enter_container)(Message* m, char type, const char* contents);
    int (*message_exit_container)(Message* m);
    int (*message_skip)(Message* m, const char* types);
    int (*message_peek_type)(Message* m, char* type, const char** contents);
    int (*message_at_end)(Message* m, int complete);
    const char* (*message_get_member)(Message* m);
    const char* (*message_get_interface)(Message* m);
    int (*message_is_method_call)(Message* m, const char* interface, const char* member);
    int (*message_is_signal)(Message* m, const char* interface, const char* member);
    const char* (*message_get_sender)(Message* m);
    const char* (*message_get_path)(Message* m);
    int (*message_get_errno)(Message* m);  // 0: not an error reply
    void (*error_free)(Error* e);
};

// Null when the library cannot be loaded.
const Api* api();

}  // namespace lr::desktop::sdbus
