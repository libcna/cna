//
// Created by robertvokac on 5/26/25.
//

#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "Keys.hpp"
#include "KeyState.hpp"

namespace Microsoft::Xna::Framework::Input
{
    /// Represents the state of keystrokes from a keyboard device.
    struct KeyboardState
    {
        KeyboardState();
        explicit KeyboardState(const std::unordered_set<Keys>& pressedKeys);

        /// Returns whether a key is pressed (Down) or released (Up).
        [[nodiscard]] KeyState getItem(Keys key) const;

        /// Returns true if the given key is currently held down.
        [[nodiscard]] bool IsKeyDown(Keys key) const;

        /// Returns true if the given key is not currently pressed.
        [[nodiscard]] bool IsKeyUp(Keys key) const;

        /// Returns an array of all currently pressed keys.
        [[nodiscard]] std::vector<Keys> GetPressedKeys() const;

        [[nodiscard]] bool Equals(const KeyboardState& other) const;
        [[nodiscard]] int GetHashCode() const;
        [[nodiscard]] std::string ToString() const;

        friend bool operator==(const KeyboardState& a, const KeyboardState& b);
        friend bool operator!=(const KeyboardState& a, const KeyboardState& b);

    private:
        std::unordered_set<Keys> pressedKeys_;
    };
}
