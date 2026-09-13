// SPDX-License-Identifier: MS-PL
#pragma once

#include "Win32Common.hpp"

namespace CNA::Platform::Win32 {

    /**
     * @brief The per-monitor DPI entry points, resolved at run time.
     *
     * `GetDpiForWindow` and `AdjustWindowRectExForDpi` arrived in Windows 10 1607;
     * `GetDpiForMonitor` in 8.1. Importing them statically would make the produced binary refuse
     * to load on anything older, for a feature that has a perfectly good fallback. They are
     * therefore resolved from `user32.dll`/`shcore.dll` once, on first use, and every caller goes
     * through the helpers below rather than the pointers.
     *
     * ### What this deliberately does not do
     *
     * It never calls `SetProcessDpiAwarenessContext` or `SetProcessDPIAware`. DPI awareness is
     * process-global policy, and CNA is a framework inside someone else's process: seizing it
     * would change how every window that application already owns is laid out. The backend reads
     * the awareness the host chose and reports values that are coherent for it.
     */
    class Win32DpiSupport
    {
    public:
        /**
         * @brief Gets the process-wide instance, resolving the entry points on first use.
         *
         * @return The shared instance.
         */
        [[nodiscard]] static const Win32DpiSupport& Get();

        /**
         * @brief Gets a window's effective DPI.
         *
         * @param window The window to measure.
         * @return The DPI, falling back to the monitor's and then the device context's; never zero.
         */
        [[nodiscard]] unsigned int GetWindowDpi(HWND window) const;

        /**
         * @brief Gets a monitor's effective DPI.
         *
         * @param monitor The monitor to measure.
         * @return The DPI; never zero.
         */
        [[nodiscard]] unsigned int GetMonitorDpi(HMONITOR monitor) const;

        /**
         * @brief Gets the system DPI.
         *
         * @return The DPI; never zero.
         */
        [[nodiscard]] unsigned int GetSystemDpi() const;

        /**
         * @brief Grows a client rectangle into the outer window rectangle that contains it.
         *
         * `CreateWindowExW` and `SetWindowPos` take the **outer** size, while
         * `WindowDescription::width`/`height` and `IPlatformWindow::SetSize` are the **client**
         * size. Every creation and resize goes through here; skipping it silently loses the
         * frame's width and the title bar's height, which the WIN32-0000 probe measured as 8x34
         * on a default decorated window.
         *
         * @param rect In/out: the client rectangle, replaced by the outer rectangle.
         * @param style The window style.
         * @param exStyle The extended window style.
         * @param hasMenu Whether the window has a menu bar.
         * @param dpi The DPI to size the frame for.
         * @return True when the adjustment succeeded.
         */
        [[nodiscard]] bool AdjustWindowRect(RECT& rect, DWORD style, DWORD exStyle, bool hasMenu,
                                            unsigned int dpi) const;

        /**
         * @brief Converts a DPI into the contract's display scale.
         *
         * @param dpi The DPI, where 96 means unscaled.
         * @return The scale factor; 1.0 for a zero or nonsensical DPI, because a zero scale
         *         divides to infinity in any layout computation the caller performs.
         */
        [[nodiscard]] static float ToDisplayScale(unsigned int dpi);

    private:
        Win32DpiSupport();

        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        using GetDpiForSystemFn = UINT(WINAPI*)();
        using AdjustWindowRectExForDpiFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
        using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);

        GetDpiForWindowFn getDpiForWindow_ = nullptr;
        GetDpiForSystemFn getDpiForSystem_ = nullptr;
        AdjustWindowRectExForDpiFn adjustWindowRectExForDpi_ = nullptr;
        GetDpiForMonitorFn getDpiForMonitor_ = nullptr;
    };

    /** @brief The DPI at which one logical unit is one physical pixel. */
    inline constexpr unsigned int kDefaultDpi = 96;

} // namespace CNA::Platform::Win32
