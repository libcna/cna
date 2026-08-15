// SPDX-License-Identifier: MS-PL
#pragma once

// Shared test scaffolding: a whole-keyboard snapshot supplied by a test.
//
// The pending/current split is deliberate. It makes the once-per-frame boundary observable:
// changing the host-side state does nothing until Update(), after which keys and modifiers move
// together. The old SystemKeyboardBackend fake could inject only a live modifier query and could
// therefore never prove that Keyboard::GetState() and GetModStateEXT() used the same clock.

#include "CNA/Platform/Input/IPlatformKeyboard.hpp"
#include "CNA/Platform/Input/KeyCode.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"

#include <initializer_list>

namespace CNA::Platform::Testing {

    /** @brief A keyboard service whose next snapshot is controlled by a test. */
    class CannedKeyboard final : public IPlatformKeyboard
    {
    public:
        /**
         * @brief Sets the state that the next Update() will publish.
         * @param keys Virtual keys to report as held.
         * @param modifiers Mask of `KeyModifier` values to report.
         */
        void SetPending(const std::initializer_list<KeyCode> keys,
                        const std::uint16_t modifiers = 0)
        {
            pending_.pressedKeys.clear();
            pending_.pressedKeys.reserve(keys.size());
            for (const KeyCode key : keys)
            {
                if (key != KeyCode::None)
                {
                    pending_.pressedKeys.push_back(key);
                }
            }
            pending_.modifiers = modifiers;
        }

        /** @brief Publishes the pending whole-keyboard snapshot. */
        void Update() override
        {
            snapshot_ = pending_;
            ++updateCount_;
        }

        /** @brief Gets the last published snapshot. @return The current scripted snapshot. */
        [[nodiscard]] const KeyboardSnapshot& GetSnapshot() const override { return snapshot_; }

        /** @brief Gets whether the scripted keyboard is present. @return The configured value. */
        [[nodiscard]] bool HasKeyboard() const override { return present_; }

        /** @brief Sets whether HasKeyboard reports a device. @param present The new answer. */
        void SetPresent(const bool present) { present_ = present; }

        /** @brief Gets the number of published frames. @return The Update() call count. */
        [[nodiscard]] int UpdateCount() const { return updateCount_; }

    private:
        KeyboardSnapshot pending_;
        KeyboardSnapshot snapshot_;
        bool present_ = true;
        int updateCount_ = 0;
    };

    /** @brief A platform that is real in every respect except its keyboard snapshot. */
    class CannedKeyboardPlatform final : public PlatformTestDecorator
    {
    public:
        /** @brief Gets the scripted keyboard service. @return The service, or null when hidden. */
        [[nodiscard]] IPlatformKeyboard* GetKeyboard() override
        {
            return keyboardAvailable_ ? &keyboard_ : nullptr;
        }

        /** @brief Gets the scripted service for writing. @return The scripted keyboard. */
        [[nodiscard]] CannedKeyboard& Canned() { return keyboard_; }

        /** @brief Controls whether the platform exposes a keyboard service. @param available New state. */
        void SetKeyboardAvailable(const bool available) { keyboardAvailable_ = available; }

    private:
        CannedKeyboard keyboard_;
        bool keyboardAvailable_ = true;
    };

} // namespace CNA::Platform::Testing
