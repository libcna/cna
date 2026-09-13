// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/KeyCode.hpp"
#include "CNA/Platform/Input/Scancode.hpp"

#include "Win32Scancodes.hpp"

#include <cstdint>
#include <string>

namespace CNA::Platform::Win32 {

    /**
     * @brief Translates a Windows virtual key into CNA's layout-dependent key.
     *
     * CNA's `KeyCode` reproduces XNA's `Keys`, whose numeric values **are** the Windows `VK_*`
     * codes -- `Back = 8`, `Escape = 27`, `A..Z = 65..90`, `OemSemicolon = 186`. The translation
     * is therefore numerically an identity, but it is still a *validated* one: the virtual-key
     * space is sparse and Windows reports keys this contract does not name (browser and launcher
     * keys on a multimedia keyboard, `VK_PACKET` from `SendInput`). Those become `KeyCode::None`
     * rather than an out-of-range enumerator.
     *
     * @param virtualKey The `wParam` of a keyboard message.
     * @return The matching key, or `KeyCode::None` when this contract does not name it.
     */
    [[nodiscard]] KeyCode ToKeyCode(std::uint32_t virtualKey);

    /**
     * @brief Resolves the sided virtual key behind an unsided one.
     *
     * `WM_KEYDOWN` reports `VK_SHIFT`, `VK_CONTROL` and `VK_MENU` without saying which side was
     * pressed, even though `GetKeyState` and CNA's `KeyCode` both distinguish them. The side is
     * recoverable from the physical key: Control and Alt differ by the extended flag, and Shift
     * has to be asked of the layout because both Shift keys are non-extended.
     *
     * @param virtualKey The `wParam` of a keyboard message.
     * @param key The physical key from the same message's `lParam`.
     * @return The sided virtual key, or @p virtualKey unchanged when it was already specific.
     */
    [[nodiscard]] std::uint32_t ResolveSidedVirtualKey(std::uint32_t virtualKey,
                                                       Win32PhysicalKey key);

    /**
     * @brief Resolves what a physical key produces on the active keyboard layout.
     *
     * @param scancode The physical key.
     * @return The key the active layout puts there, or `KeyCode::None` when the layout has none.
     */
    [[nodiscard]] KeyCode KeyCodeFromScancode(Scancode scancode);

    /**
     * @brief Gets the active layout's name for what a physical key produces.
     *
     * @param scancode The physical key.
     * @return The layout's UTF-8 name, or an empty string when the layout names nothing there.
     */
    [[nodiscard]] std::string LayoutKeyName(Scancode scancode);

    /**
     * @brief Resolves a layout key name back to a key.
     *
     * @param name A name previously produced by @ref LayoutKeyName.
     * @return The key, or `KeyCode::None` when no key on the active layout carries that name.
     */
    [[nodiscard]] KeyCode KeyCodeFromLayoutName(const std::string& name);

} // namespace CNA::Platform::Win32
