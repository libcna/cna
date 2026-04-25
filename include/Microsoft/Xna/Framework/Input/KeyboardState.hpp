//
// Created by robertvokac on 5/26/25.
//

#pragma once

#include <unordered_set>
#include <vector>

#include "Keys.hpp"

namespace Microsoft::Xna::Framework::Input {

/**
 * @brief Snapshot of currently pressed keyboard keys.
 *
 * @note Status: PARTIAL
 */
struct KeyboardState {
public:
    KeyboardState();
    explicit KeyboardState(const std::unordered_set<Keys>& pressedKeys);

    /**
     * @brief Checks whether the specified key is currently pressed.
     *
     * @note Status: IMPLEMENTED
     */
    bool IsKeyDown(Keys key) const;

    /**
     * @brief Checks whether the specified key is currently released.
     *
     * @note Status: IMPLEMENTED
     */
    bool IsKeyUp(Keys key) const;

    /**
     * @brief Returns all currently pressed keys in this snapshot.
     *
     * @note Status: IMPLEMENTED
     */
    std::vector<Keys> GetPressedKeys() const;

private:
    std::unordered_set<Keys> pressedKeys_;
};

}

