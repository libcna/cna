// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/WindowDescription.hpp"

#include "Win32Common.hpp"

namespace CNA::Platform::Win32 {

    /**
     * @brief The windowed appearance a window is restored to when it leaves fullscreen.
     *
     * Going fullscreen on Win32 means editing the window's style, extended style and placement in
     * place; there is no mode the system remembers for you. Anything not recorded before the edit
     * is gone, and the classic symptom is a window that comes back from fullscreen without its
     * title bar, or maximised when it was not, or 1x1 because only the size was saved.
     *
     * So all three are captured together, and restoring puts back exactly what was taken. Kept in
     * its own type with no `HWND` in it so `Win32FullscreenStateTests` can drive repeated
     * `Windowed -> Borderless -> Windowed` cycles and assert the round trip without a window
     * manager.
     */
    class Win32FullscreenState
    {
    public:
        /**
         * @brief Records the windowed appearance before a fullscreen transition.
         *
         * Recording twice without an intervening restore is ignored, so a second
         * `SetFullscreenMode(BorderlessFullscreen)` cannot overwrite the genuine windowed state
         * with the already-fullscreen one -- the bug that makes a window unrestorable after two
         * calls rather than one.
         *
         * @param style The window style.
         * @param exStyle The extended window style.
         * @param placement The full `WINDOWPLACEMENT`, which carries the restored rectangle as
         *        well as the current show command.
         */
        void Capture(LONG_PTR style, LONG_PTR exStyle, const WINDOWPLACEMENT& placement);

        /**
         * @brief Gets whether a windowed appearance is currently recorded.
         *
         * @return True between a Capture() and its matching Restore().
         */
        [[nodiscard]] bool IsCaptured() const { return captured_; }

        /**
         * @brief Hands back the recorded appearance and forgets it.
         *
         * @param style Receives the window style; untouched when this returns false.
         * @param exStyle Receives the extended style; untouched when this returns false.
         * @param placement Receives the placement; untouched when this returns false.
         * @return True when there was something to restore.
         */
        bool Restore(LONG_PTR& style, LONG_PTR& exStyle, WINDOWPLACEMENT& placement);

        /**
         * @brief Computes the style a fullscreen window is given.
         *
         * Strips the whole decorated-frame set -- caption, thick frame, system menu, minimise and
         * maximise boxes -- while preserving everything else the caller's style carried.
         *
         * @param baseStyle The windowed style.
         * @return The fullscreen style.
         */
        [[nodiscard]] static LONG_PTR ToFullscreenStyle(LONG_PTR baseStyle);

        /**
         * @brief Computes the extended style a fullscreen window is given.
         *
         * @param baseExStyle The windowed extended style.
         * @return The fullscreen extended style, without the raised/sunken client edges that
         *         would otherwise inset the image.
         */
        [[nodiscard]] static LONG_PTR ToFullscreenExStyle(LONG_PTR baseExStyle);

    private:
        bool captured_ = false;
        LONG_PTR style_ = 0;
        LONG_PTR exStyle_ = 0;
        WINDOWPLACEMENT placement_{};
    };

} // namespace CNA::Platform::Win32
