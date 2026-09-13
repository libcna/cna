// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>

namespace CNA::Platform::Win32 {

    /**
     * @brief Reads the modifier keys currently held or latched, in CNA's own bit layout.
     *
     * Deliberately built from `GetKeyState` rather than passed through from any native mask:
     * `KeyModifier`'s bit positions are CNA's, and handing a Windows mask straight through would
     * compile, run, and mean something entirely different.
     *
     * The three lock keys are read from `GetKeyState`'s **low** bit (the toggle state), while the
     * held keys are read from its high bit. Mixing those up reports Caps Lock as held rather than
     * on, which is the classic slip here.
     *
     * @return A bitmask of `KeyModifier` values.
     */
    [[nodiscard]] std::uint16_t CurrentModifiers();

} // namespace CNA::Platform::Win32
