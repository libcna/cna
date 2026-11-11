// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Runtime/DebugContextHotkeys.hpp"

#if !defined(CNA_DEBUG_CONTEXT_HOTKEYS)
#error "CNA_DEBUG_CONTEXT_HOTKEYS must be defined to 0 or 1 by modules/runtime/CMakeLists.txt"
#endif

namespace CNA::Internal::Runtime {

bool DebugContextHotkeysEnabled() noexcept
{
    return CNA_DEBUG_CONTEXT_HOTKEYS != 0;
}

DebugContextHotkey ClassifyDebugContextHotkey(const CNA::Platform::KeyEvent& event,
                                              const bool enabled) noexcept
{
    if (!enabled || !event.pressed || event.repeat)
        return DebugContextHotkey::None;
    switch (event.keycode)
    {
    case CNA::Platform::KeyCode::F9:
        return DebugContextHotkey::SimulateLoss;
    case CNA::Platform::KeyCode::F10:
        return DebugContextHotkey::Restore;
    default:
        return DebugContextHotkey::None;
    }
}

}
