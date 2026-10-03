// SPDX-License-Identifier: MS-PL
#include "CNA/Platform/Input/KeyboardGamepad.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>

namespace CNA::Platform
{
    namespace
    {
        struct SoftwareGamepad
        {
            std::atomic<bool> enabled{false};
            std::mutex mutex;
            GamepadSnapshot keyboard;
            GamepadSnapshot merged;
            std::uint32_t heldButtons = 0;
        };

        SoftwareGamepad& Source()
        {
            static SoftwareGamepad source;
            return source;
        }

        bool SameState(const GamepadSnapshot& a, const GamepadSnapshot& b)
        {
            return a.connected == b.connected && a.buttons == b.buttons && a.axes == b.axes;
        }

        void Publish(GamepadSnapshot& destination, GamepadSnapshot value)
        {
            value.packetNumber = destination.packetNumber + (SameState(destination, value) ? 0u : 1u);
            destination = value;
        }

        constexpr std::uint32_t mappedButtons =
            static_cast<std::uint32_t>(GamepadButton::A) |
            static_cast<std::uint32_t>(GamepadButton::B) |
            static_cast<std::uint32_t>(GamepadButton::X) |
            static_cast<std::uint32_t>(GamepadButton::Y) |
            static_cast<std::uint32_t>(GamepadButton::Back) |
            static_cast<std::uint32_t>(GamepadButton::Start) |
            static_cast<std::uint32_t>(GamepadButton::LeftShoulder) |
            static_cast<std::uint32_t>(GamepadButton::RightShoulder) |
            static_cast<std::uint32_t>(GamepadButton::LeftStick) |
            static_cast<std::uint32_t>(GamepadButton::RightStick) |
            static_cast<std::uint32_t>(GamepadButton::DPadUp) |
            static_cast<std::uint32_t>(GamepadButton::DPadDown) |
            static_cast<std::uint32_t>(GamepadButton::DPadLeft) |
            static_cast<std::uint32_t>(GamepadButton::DPadRight);
    }

    bool KeyboardGamepad::IsEnabled() { return Source().enabled.load(); }

    void KeyboardGamepad::SetEnabled(bool enabled)
    {
        auto& source = Source();
        std::lock_guard lock(source.mutex);
        if (source.enabled.load() == enabled) return;
        GamepadSnapshot neutral;
        neutral.connected = enabled;
        Publish(source.keyboard, neutral);
        source.heldButtons = 0;
        source.enabled.store(enabled);
    }

    void KeyboardGamepad::Update(const KeyboardSnapshot& keyboard, bool focused)
    {
        auto& source = Source();
        if (!source.enabled.load()) return;
        const auto held = [&](KeyCode key) {
            return std::find(keyboard.pressedKeys.begin(), keyboard.pressedKeys.end(), key)
                != keyboard.pressedKeys.end();
        };
        GamepadSnapshot value;
        value.connected = true;
        std::uint32_t heldButtons = 0;
        const auto button = [&](KeyCode key, GamepadButton mapped) {
            if (held(key)) heldButtons |= static_cast<std::uint32_t>(mapped);
        };
        button(KeyCode::K, GamepadButton::A);
        button(KeyCode::L, GamepadButton::B);
        button(KeyCode::J, GamepadButton::X);
        button(KeyCode::I, GamepadButton::Y);
        button(KeyCode::Q, GamepadButton::LeftShoulder);
        button(KeyCode::E, GamepadButton::RightShoulder);
        button(KeyCode::LeftShift, GamepadButton::LeftStick);
        button(KeyCode::RightShift, GamepadButton::RightStick);
        button(KeyCode::Enter, GamepadButton::Start);
        button(KeyCode::Escape, GamepadButton::Back);
        button(KeyCode::T, GamepadButton::DPadUp);
        button(KeyCode::F, GamepadButton::DPadLeft);
        button(KeyCode::G, GamepadButton::DPadDown);
        button(KeyCode::H, GamepadButton::DPadRight);
        const auto stick = [&](std::size_t offset, KeyCode left, KeyCode right, KeyCode down, KeyCode up) {
            float x = static_cast<float>(held(right)) - static_cast<float>(held(left));
            float y = static_cast<float>(held(up)) - static_cast<float>(held(down));
            const float length = std::sqrt(x * x + y * y);
            if (length > 1.0f) { x /= length; y /= length; }
            value.axes[offset] = x;
            value.axes[offset + 1] = y;
        };
        stick(0, KeyCode::A, KeyCode::D, KeyCode::S, KeyCode::W);
        stick(2, KeyCode::Left, KeyCode::Right, KeyCode::Down, KeyCode::Up);
        value.axes[4] = held(KeyCode::Z) ? 1.0f : 0.0f;
        value.axes[5] = held(KeyCode::C) ? 1.0f : 0.0f;
        if (focused) value.buttons = heldButtons;
        else value.axes.fill(0.0f);
        std::lock_guard lock(source.mutex);
        if (source.enabled.load())
        {
            source.heldButtons = heldButtons;
            Publish(source.keyboard, value);
        }
    }

    GamepadSnapshot KeyboardGamepad::GetSnapshot()
    {
        auto& source = Source();
        std::lock_guard lock(source.mutex);
        return source.keyboard;
    }

    GamepadSnapshot KeyboardGamepad::MergeSnapshot(const GamepadSnapshot& physical)
    {
        auto& source = Source();
        std::lock_guard lock(source.mutex);
        if (!source.enabled.load()) return physical;
        GamepadSnapshot value = physical.connected ? physical : GamepadSnapshot{};
        value.connected = true;
        value.buttons |= source.keyboard.buttons;
        for (std::size_t i = 0; i < value.axes.size(); ++i)
            if (source.keyboard.axes[i] != 0.0f) value.axes[i] = source.keyboard.axes[i];
        // A sum of physical/software packet counters can change for an overridden input. Track
        // the effective state instead, so repeated reads and hidden physical changes are stable.
        Publish(source.merged, value);
        return source.merged;
    }

    std::uint32_t KeyboardGamepad::GetHeldButtons()
    {
        auto& source = Source();
        std::lock_guard lock(source.mutex);
        return source.heldButtons;
    }

    GamepadCapabilities KeyboardGamepad::MergeCapabilities(const GamepadCapabilities& physical)
    {
        if (!IsEnabled()) return physical;
        GamepadCapabilities value = physical.connected ? physical : GamepadCapabilities{};
        value.connected = true;
        value.kind = GamepadKind::Gamepad;
        value.buttons |= mappedButtons;
        value.axes |= static_cast<std::uint8_t>((1u << GamepadAxisCount) - 1u);
        return value;
    }
}
