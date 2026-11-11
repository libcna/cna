// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/PlatformEvent.hpp"

namespace CNA::Internal::Runtime {

/** @brief The renderer action a key event asks of Game::PollEvents' debug hotkeys. */
enum class DebugContextHotkey
{
    /** @brief Not a debug hotkey, or the hotkeys are not compiled into this build. */
    None,
    /** @brief F9: IGraphicsRenderer::DebugSimulateContextLoss(). */
    SimulateLoss,
    /** @brief F10: IGraphicsRenderer::DebugRestoreContext(). */
    Restore,
};

/**
 * @brief Reports whether this build maps F9/F10 to a simulated context loss and restore.
 *
 * Set by the CMake option `CNA_DEBUG_CONTEXT_HOTKEYS`: AUTO (the default) enables the hotkeys in
 * the Debug configuration only, ON and OFF in every configuration.
 *
 * @return True when Game::PollEvents reacts to F9/F10.
 */
[[nodiscard]] bool DebugContextHotkeysEnabled() noexcept;

/**
 * @brief Classifies a key event as one of the debug context hotkeys.
 *
 * Only a first press counts: a release or an auto-repeat is never a hotkey.
 *
 * @param event The platform key event.
 * @param enabled Whether the hotkeys are active; defaults to this build's setting.
 * @return The renderer action to take, or DebugContextHotkey::None.
 */
[[nodiscard]] DebugContextHotkey ClassifyDebugContextHotkey(
    const CNA::Platform::KeyEvent& event, bool enabled = DebugContextHotkeysEnabled()) noexcept;

}
