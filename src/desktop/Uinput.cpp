// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "desktop/Uinput.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "util/Isolated.hpp"
#include "util/Log.hpp"

namespace lr::desktop {

namespace {
int createDevice(const char* name, bool pointer, std::string& err) {
    const int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        err = std::string("/dev/uinput: ") + strerror(errno);
        return -1;
    }
    bool ok = ioctl(fd, UI_SET_EVBIT, EV_KEY) == 0 && ioctl(fd, UI_SET_EVBIT, EV_SYN) == 0;
    if (pointer) {
        ok = ok && ioctl(fd, UI_SET_EVBIT, EV_REL) == 0;
        for (int rel : {REL_X, REL_Y, REL_WHEEL, REL_HWHEEL, REL_WHEEL_HI_RES, REL_HWHEEL_HI_RES}) ok = ok && ioctl(fd, UI_SET_RELBIT, rel) == 0;
        for (int b = BTN_LEFT; b <= BTN_TASK; ++b) ok = ok && ioctl(fd, UI_SET_KEYBIT, b) == 0;
        ok = ok && ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_POINTER) == 0;
    } else {
        // Keyboard keys and media keys only. No joystick button codes: this device must never look like a game controller, to this program or to a game.
        for (int k = 1; k < 0x100; ++k) ok = ok && ioctl(fd, UI_SET_KEYBIT, k) == 0;
        for (int k = 0x160; k < BTN_TRIGGER_HAPPY; ++k) ok = ok && ioctl(fd, UI_SET_KEYBIT, k) == 0;
    }
    uinput_setup setup{};
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor = 0x1209;
    setup.id.product = pointer ? 0x0002 : 0x0001;
    setup.id.version = 1;
    snprintf(setup.name, sizeof setup.name, "%s", name);
    ok = ok && ioctl(fd, UI_DEV_SETUP, &setup) == 0 && ioctl(fd, UI_DEV_CREATE) == 0;
    if (!ok) {
        err = std::string("cannot create the virtual device: ") + strerror(errno);
        ::close(fd);
        return -1;
    }
    return fd;
}

void destroyDevice(int fd) {
    ioctl(fd, UI_DEV_DESTROY);
    ::close(fd);
}
}  // namespace

bool UinputSink::open(std::string& err) {
    if (isolatedRun()) { err = "test run"; return false; }  // a test never creates real devices
    if (keyboard_.fd >= 0 && mouse_.fd >= 0) return true;
    keyboard_.fd = createDevice("MSFS Linux Companion Keyboard", false, err);
    if (keyboard_.fd < 0) return false;
    mouse_.fd = createDevice("MSFS Linux Companion Mouse", true, err);
    if (mouse_.fd < 0) {
        destroyDevice(keyboard_.fd);
        keyboard_.fd = -1;
        return false;
    }
    LOG_INFO("Desktop controls: virtual mouse and keyboard created");
    return true;
}

void UinputSink::close() {
    if (keyboard_.fd >= 0) destroyDevice(keyboard_.fd);
    if (mouse_.fd >= 0) destroyDevice(mouse_.fd);
    if (keyboard_.fd >= 0 || mouse_.fd >= 0) LOG_INFO("Desktop controls: virtual mouse and keyboard removed");
    keyboard_.fd = mouse_.fd = -1;
    keyboard_.count = mouse_.count = 0;
    notchesV_.reset();
    notchesH_.reset();
}

void UinputSink::push(Queue& q, int type, int code, int value) {
    if (q.fd < 0) return;
    if (q.count + 1 >= q.events.size()) send(q);  // the last place is kept for the end-of-frame event
    input_event& e = q.events[q.count++];
    e = input_event{};
    e.type = static_cast<__u16>(type);
    e.code = static_cast<__u16>(code);
    e.value = value;
}

void UinputSink::send(Queue& q) {
    if (q.fd < 0 || q.count == 0) return;
    input_event& syn = q.events[q.count++];
    syn = input_event{};
    syn.type = EV_SYN;
    syn.code = SYN_REPORT;
    // Non-blocking: when the kernel's queue is full the frame is dropped, the input thread never waits here.
    const ssize_t n = ::write(q.fd, q.events.data(), q.count * sizeof(input_event));
    (void)n;
    q.count = 0;
}

void UinputSink::key(int code, bool down) { push(code >= BTN_MISC && code < KEY_OK ? mouse_ : keyboard_, EV_KEY, code, down ? 1 : 0); }

void UinputSink::move(int dx, int dy) {
    if (dx) push(mouse_, EV_REL, REL_X, dx);
    if (dy) push(mouse_, EV_REL, REL_Y, dy);
}

void UinputSink::wheel(int vertical, int horizontal) {
    // both, as a real high-resolution mouse sends them: smooth for the programs that can, whole notches for the others
    if (vertical) {
        push(mouse_, EV_REL, REL_WHEEL_HI_RES, vertical);
        if (const int notches = notchesV_.feed(vertical)) push(mouse_, EV_REL, REL_WHEEL, notches);
    }
    if (horizontal) {
        push(mouse_, EV_REL, REL_HWHEEL_HI_RES, horizontal);
        if (const int notches = notchesH_.feed(horizontal)) push(mouse_, EV_REL, REL_HWHEEL, notches);
    }
}

void UinputSink::flush() {
    send(keyboard_);
    send(mouse_);
}

}  // namespace lr::desktop
