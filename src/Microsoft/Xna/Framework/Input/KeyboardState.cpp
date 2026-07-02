// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/26/25.
//

#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"

#include <algorithm>

namespace Microsoft::Xna::Framework::Input
{
    KeyboardState::KeyboardState() = default;

    KeyboardState::KeyboardState(std::initializer_list<Keys> keys)
        : pressedKeys_(keys)
    {
    }

    KeyboardState::KeyboardState(const std::unordered_set<Keys>& pressedKeys)
        : pressedKeys_(pressedKeys)
    {
    }

    bool KeyboardState::IsKeyDown(const Keys key) const
    {
        return pressedKeys_.contains(key);
    }

    bool KeyboardState::IsKeyUp(const Keys key) const
    {
        return !IsKeyDown(key);
    }

    std::vector<Keys> KeyboardState::GetPressedKeys() const
    {
        std::vector<Keys> keys(pressedKeys_.begin(), pressedKeys_.end());
        // FNA returns keys in ascending numeric order (KeyboardState.cs's GetPressedKeys()
        // walks its keys0..keys7 bitfields from bit 0 upward); pressedKeys_ is an
        // unordered_set, so iteration order is otherwise unspecified.
        std::sort(keys.begin(), keys.end(), [](const Keys a, const Keys b)
        {
            return static_cast<int>(a) < static_cast<int>(b);
        });
        return keys;
    }

    KeyState KeyboardState::getItem(const Keys key) const
    {
        return IsKeyDown(key) ? KeyState::Down : KeyState::Up;
    }

    bool KeyboardState::Equals(const KeyboardState& other) const
    {
        return pressedKeys_ == other.pressedKeys_;
    }

    int KeyboardState::GetHashCode() const
    {
        int hash = 0;
        for (const Keys k : pressedKeys_)
            hash ^= static_cast<int>(k) * 31;
        return hash;
    }

    std::string KeyboardState::ToString() const
    {
        return "[KeyboardState]";
    }

    bool operator==(const KeyboardState& a, const KeyboardState& b)
    {
        return a.Equals(b);
    }

    bool operator!=(const KeyboardState& a, const KeyboardState& b)
    {
        return !(a == b);
    }
}
