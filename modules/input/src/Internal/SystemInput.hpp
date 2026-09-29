// SPDX-License-Identifier: MS-PL
// The filters the game-facing Keyboard, GamePad and Mouse reads apply (see
// CNA/Internal/Input/SystemInput.hpp).
#pragma once
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include <cstdint>
#include <unordered_set>

namespace CNA::Internal::Input
{
    /** @brief Removes what the game may not see from a keyboard read. @param pressed Keys down. */
    void filterGameKeys(std::unordered_set<Microsoft::Xna::Framework::Input::Keys>& pressed);
    /** @brief The pad buttons the game may see. @param slot Pad 0-3. @param buttons Raw flags.
     * @return Flags. */
    std::uint32_t filterGameButtons(int slot, std::uint32_t buttons);
    /** @brief The mouse buttons the game may see. @param buttons Raw button bits. @return Bits. */
    std::uint32_t filterGameMouseButtons(std::uint32_t buttons);
}
