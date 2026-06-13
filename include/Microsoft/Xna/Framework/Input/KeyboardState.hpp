// SPDX-License-Identifier: MS-PL
#pragma once

#include <initializer_list>
#include <string>
#include <unordered_set>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Keys.hpp"
#include "KeyState.hpp"

namespace Microsoft::Xna::Framework::Input
{
    /**
     * @brief Holds the state of keystrokes by a keyboard.
     */
    struct KeyboardState
    {
        /** @brief Constructs a default KeyboardState with no keys pressed. */
        NOXNA KeyboardState();

        /**
         * @brief Constructs a KeyboardState with the given keys flagged as pressed.
         * @param keys List of keys to be flagged as pressed.
         */
        KeyboardState(std::initializer_list<Keys> keys);

        /**
         * @brief Constructs a KeyboardState from a set of pressed keys.
         * @param pressedKeys The set of keys currently pressed.
         */
        NOXNA explicit KeyboardState(const std::unordered_set<Keys>& pressedKeys);

        /**
         * @brief Returns the state of a specified key.
         * @param key The key to query.
         * @return KeyState::Down if pressed; KeyState::Up otherwise.
         */
        [[nodiscard]] KeyState getItem(Keys key) const;

        /**
         * @brief Gets whether the given key is currently being pressed.
         * @param key The key to query.
         * @return True if the key is pressed; false otherwise.
         */
        [[nodiscard]] bool IsKeyDown(Keys key) const;

        /**
         * @brief Gets whether the given key is currently not pressed.
         * @param key The key to query.
         * @return True if the key is not pressed; false otherwise.
         */
        [[nodiscard]] bool IsKeyUp(Keys key) const;

        /**
         * @brief Returns an array of all currently pressed keys.
         * @return A vector of keys that are currently pressed.
         */
        [[nodiscard]] std::vector<Keys> GetPressedKeys() const;

        /**
         * @brief Compares this instance with another for equality.
         * @param other The other KeyboardState to compare.
         * @return True if equal; false otherwise.
         */
        [[nodiscard]] bool Equals(const KeyboardState& other) const;

        /**
         * @brief Gets the hash code for this KeyboardState instance.
         * @return Hash code of the object.
         */
        [[nodiscard]] int GetHashCode() const;

        /**
         * @brief Returns a string representation of this KeyboardState.
         * @return The string representation.
         */
        NOXNA [[nodiscard]] std::string ToString() const;

        /**
         * @brief Compares two KeyboardState instances for equality.
         * @param a The left-hand operand.
         * @param b The right-hand operand.
         * @return True if the instances are equal; false otherwise.
         */
        friend bool operator==(const KeyboardState& a, const KeyboardState& b);

        /**
         * @brief Compares two KeyboardState instances for inequality.
         * @param a The left-hand operand.
         * @param b The right-hand operand.
         * @return True if the instances differ; false otherwise.
         */
        friend bool operator!=(const KeyboardState& a, const KeyboardState& b);

    private:
        std::unordered_set<Keys> pressedKeys_;
    };
}
