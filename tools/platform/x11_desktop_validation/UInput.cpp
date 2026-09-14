// SPDX-License-Identifier: MS-PL

#include "UInput.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#if defined(__linux__)
#  include <fcntl.h>
#  include <linux/uinput.h>
#  include <sys/ioctl.h>
#  include <unistd.h>
#endif

namespace CnaX11Validation {

#if defined(__linux__)

    std::unique_ptr<VirtualInput> VirtualInput::Create(const bool keyboard, const bool mouse,
                                                       std::string& error)
    {
        const int descriptor = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (descriptor < 0)
        {
            error = std::string("cannot open /dev/uinput: ") + std::strerror(errno);
            return nullptr;
        }

        bool ok = true;
        const auto enable = [&ok, descriptor](const unsigned long request, const int value) {
            ok = ok && ::ioctl(descriptor, request, value) >= 0;
        };
        if (keyboard)
        {
            enable(UI_SET_EVBIT, EV_KEY);
            enable(UI_SET_EVBIT, EV_REP);
            // Every key a standard 105-key keyboard and its keypad have. KEY_RESERVED is not one.
            for (int code = KEY_ESC; code <= KEY_F24; ++code)
            {
                enable(UI_SET_KEYBIT, code);
            }
        }
        if (mouse)
        {
            enable(UI_SET_EVBIT, EV_KEY);
            enable(UI_SET_EVBIT, EV_REL);
            for (const int button : {BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_SIDE, BTN_EXTRA})
            {
                enable(UI_SET_KEYBIT, button);
            }
            for (const int axis : {REL_X, REL_Y, REL_WHEEL, REL_HWHEEL})
            {
                enable(UI_SET_RELBIT, axis);
            }
        }
        enable(UI_SET_EVBIT, EV_SYN);

        uinput_setup setup{};
        setup.id.bustype = BUS_VIRTUAL;
        setup.id.vendor = 0x1d6b;  // "Linux Foundation" -- a virtual device, and says so
        setup.id.product = keyboard ? (mouse ? 0x0103 : 0x0101) : 0x0102;
        std::snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "CNA X11 validation %s",
                      keyboard ? (mouse ? "keyboard+mouse" : "keyboard") : "mouse");
        ok = ok && ::ioctl(descriptor, UI_DEV_SETUP, &setup) >= 0 &&
             ::ioctl(descriptor, UI_DEV_CREATE) >= 0;
        if (!ok)
        {
            error = std::string("uinput setup failed: ") + std::strerror(errno);
            ::close(descriptor);
            return nullptr;
        }

        // udev announces the device, libinput opens it and the compositor adds it to the seat.
        // None of that is synchronous with UI_DEV_CREATE, and events emitted before the
        // compositor listens are simply lost.
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        return std::unique_ptr<VirtualInput>(new VirtualInput(descriptor));
    }

    VirtualInput::~VirtualInput()
    {
        if (descriptor_ >= 0)
        {
            ::ioctl(descriptor_, UI_DEV_DESTROY);
            ::close(descriptor_);
        }
    }

    void VirtualInput::Emit(const int type, const int code, const int value)
    {
        input_event event{};
        event.type = static_cast<unsigned short>(type);
        event.code = static_cast<unsigned short>(code);
        event.value = value;
        (void) !::write(descriptor_, &event, sizeof(event));
    }

    void VirtualInput::Sync()
    {
        Emit(EV_SYN, SYN_REPORT, 0);
    }

    void VirtualInput::Key(const int code, const bool down)
    {
        Emit(EV_KEY, code, down ? 1 : 0);
        Sync();
    }

    void VirtualInput::Tap(const int code, const int holdMilliseconds)
    {
        Key(code, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(holdMilliseconds));
        Key(code, false);
    }

    void VirtualInput::Repeat(const int code)
    {
        Emit(EV_KEY, code, 2);
        Sync();
    }

    void VirtualInput::Move(const int deltaX, const int deltaY)
    {
        if (deltaX != 0) { Emit(EV_REL, REL_X, deltaX); }
        if (deltaY != 0) { Emit(EV_REL, REL_Y, deltaY); }
        Sync();
    }

    void VirtualInput::Wheel(const int notches)
    {
        Emit(EV_REL, REL_WHEEL, notches);
        Sync();
    }

    void VirtualInput::HorizontalWheel(const int notches)
    {
        Emit(EV_REL, REL_HWHEEL, notches);
        Sync();
    }

#else

    std::unique_ptr<VirtualInput> VirtualInput::Create(bool, bool, std::string& error)
    {
        error = "uinput exists only on Linux";
        return nullptr;
    }
    VirtualInput::~VirtualInput() = default;
    void VirtualInput::Emit(int, int, int) {}
    void VirtualInput::Sync() {}
    void VirtualInput::Key(int, bool) {}
    void VirtualInput::Tap(int, int) {}
    void VirtualInput::Repeat(int) {}
    void VirtualInput::Move(int, int) {}
    void VirtualInput::Wheel(int) {}
    void VirtualInput::HorizontalWheel(int) {}

#endif

} // namespace CnaX11Validation
