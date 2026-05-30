//
// Created by robertvokac on 5/26/25.
//

#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"

namespace Microsoft::Xna::Framework::Input
{
    KeyboardState::KeyboardState() = default;

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
        return {pressedKeys_.begin(), pressedKeys_.end()};
    }
}
