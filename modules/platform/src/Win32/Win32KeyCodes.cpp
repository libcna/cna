// SPDX-License-Identifier: MS-PL

#include "Win32KeyCodes.hpp"

#include "Win32Common.hpp"
#include "Win32Utf.hpp"

#include <array>

namespace CNA::Platform::Win32 {

    KeyCode ToKeyCode(const std::uint32_t virtualKey)
    {
        if (virtualKey > 0xFFFFu)
            return KeyCode::None;
        const auto value = static_cast<std::uint16_t>(virtualKey);
        return IsKnownKeyCode(value) ? static_cast<KeyCode>(value) : KeyCode::None;
    }

    std::uint32_t ResolveSidedVirtualKey(const std::uint32_t virtualKey, const Win32PhysicalKey key)
    {
        switch (virtualKey)
        {
            case VK_SHIFT:
            {
                // Both Shift keys are non-extended, so the flag cannot separate them and the
                // layout has to be asked. MAPVK_VSC_TO_VK_EX is the only mapping that answers
                // with a sided key; MAPVK_VSC_TO_VK would hand VK_SHIFT straight back.
                const UINT sided = MapVirtualKeyW(key.scanCode, MAPVK_VSC_TO_VK_EX);
                return sided != 0 ? sided : static_cast<std::uint32_t>(VK_LSHIFT);
            }
            case VK_CONTROL:
                return key.extended ? static_cast<std::uint32_t>(VK_RCONTROL)
                                    : static_cast<std::uint32_t>(VK_LCONTROL);
            case VK_MENU:
                return key.extended ? static_cast<std::uint32_t>(VK_RMENU)
                                    : static_cast<std::uint32_t>(VK_LMENU);
            default:
                return virtualKey;
        }
    }

    KeyCode KeyCodeFromScancode(const Scancode scancode)
    {
        Win32PhysicalKey key;
        if (!ToWin32PhysicalKey(scancode, key))
            return KeyCode::None;

        const UINT packed = key.extended ? (0xE000u | key.scanCode) : key.scanCode;
        UINT virtualKey = MapVirtualKeyW(packed, MAPVK_VSC_TO_VK_EX);
        if (virtualKey == 0)
            virtualKey = MapVirtualKeyW(key.scanCode, MAPVK_VSC_TO_VK_EX);
        return ToKeyCode(virtualKey);
    }

    std::string LayoutKeyName(const Scancode scancode)
    {
        Win32PhysicalKey key;
        if (!ToWin32PhysicalKey(scancode, key))
            return {};

        // GetKeyNameTextW reads the same lParam layout a keyboard message carries, so the scan
        // code goes back into bits 16-23 and the extended flag into bit 24.
        LONG lParam = static_cast<LONG>(static_cast<LONG_PTR>(key.scanCode) << 16);
        if (key.extended)
            lParam |= (1L << 24);

        std::array<wchar_t, 128> buffer{};
        const int written = GetKeyNameTextW(lParam, buffer.data(), static_cast<int>(buffer.size()));
        if (written <= 0)
            return {};
        return ToUtf8(std::wstring(buffer.data(), static_cast<std::size_t>(written)));
    }

    KeyCode KeyCodeFromLayoutName(const std::string& name)
    {
        if (name.empty())
            return KeyCode::None;

        const std::wstring wide = ToWide(name);
        if (wide.empty())
            return KeyCode::None;

        // A single character is the common case and the layout can answer it directly: VkKeyScanW
        // maps a character to the virtual key that produces it on the active layout.
        if (wide.size() == 1)
        {
            const SHORT mapped = VkKeyScanW(wide[0]);
            if (mapped != -1)
                return ToKeyCode(static_cast<std::uint32_t>(mapped & 0xFF));
        }

        // Otherwise it is a multi-character key name ("Backspace", "Num Lock"). There is no
        // reverse of GetKeyNameTextW, so scan the physical keys this contract names and compare.
        for (std::uint16_t value = 0; value <= 0x1FF; ++value)
        {
            if (!IsKnownScancode(value))
                continue;
            const auto scancode = static_cast<Scancode>(value);
            if (LayoutKeyName(scancode) == name)
                return KeyCodeFromScancode(scancode);
        }
        return KeyCode::None;
    }

} // namespace CNA::Platform::Win32
