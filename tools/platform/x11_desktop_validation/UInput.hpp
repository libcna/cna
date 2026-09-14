// SPDX-License-Identifier: MS-PL
//
// Kernel-level virtual input devices for the real-desktop harness.
//
// XTest injects events into the X server's own input queue, after the kernel, libinput and the
// compositor. On a Wayland desktop that skips the entire path a physical device takes to reach an
// Xwayland client:
//
//     evdev -> libinput -> compositor (keymap, focus, pointer constraints) -> wl_seat ->
//     Xwayland (XKB, XI2 raw events from zwp_relative_pointer) -> X client
//
// A uinput device enters at the very top of that path. It is still not a person pressing a key,
// and the harness says so in every line it reports -- but it exercises every layer a person's key
// press passes through, including the compositor's focus decision and Xwayland's translation of
// relative pointer motion into XI2 raw events, which XTest cannot.
//
// Linux-only by nature (the backend under test is not; the harness is a Linux desktop tool).
#pragma once

#include <memory>
#include <string>

namespace CnaX11Validation {

    /** @brief One uinput device: a keyboard, a mouse, or both. */
    class VirtualInput
    {
    public:
        /**
         * @brief Creates a device and waits for the desktop to adopt it.
         *
         * @param keyboard Advertise keyboard keys.
         * @param mouse Advertise relative axes, a wheel and five buttons.
         * @param error Receives why creation failed.
         * @return The device, or null (no /dev/uinput access, not Linux, ...).
         */
        static std::unique_ptr<VirtualInput> Create(bool keyboard, bool mouse, std::string& error);

        ~VirtualInput();
        VirtualInput(const VirtualInput&) = delete;
        VirtualInput& operator=(const VirtualInput&) = delete;

        /** @brief Presses or releases an evdev key (`KEY_*` / `BTN_*`), then syncs. */
        void Key(int code, bool down);

        /** @brief Presses and releases a key. */
        void Tap(int code, int holdMilliseconds = 15);

        /** @brief Emits one relative motion report, then syncs. */
        void Move(int deltaX, int deltaY);

        /** @brief Emits wheel notches (positive = away from the user / up), then syncs. */
        void Wheel(int notches);

        /** @brief Emits horizontal wheel notches (positive = right), then syncs. */
        void HorizontalWheel(int notches);

        /** @brief Emits an auto-repeat report the way the kernel does for a held key. */
        void Repeat(int code);

    private:
        explicit VirtualInput(int descriptor) : descriptor_(descriptor) {}
        void Emit(int type, int code, int value);
        void Sync();

        int descriptor_ = -1;
    };

} // namespace CnaX11Validation
