// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformSystemServices.hpp"

namespace CNA::Platform::Sdl3 {

    /** @brief SDL3-backed creation of independently owned system-tray icons. */
    class Sdl3Tray final : public IPlatformTray
    {
    public:
        /**
         * @brief Gets whether SDL compiled a native tray implementation for this target.
         * @return True on Windows, Linux and macOS; false on mobile and web targets.
         */
        [[nodiscard]] static bool IsSupported();

        /**
         * @brief Creates a native tray icon and an empty menu.
         * @param tooltip The initial tooltip.
         * @return The owned native icon.
         */
        [[nodiscard]] std::unique_ptr<IPlatformTrayIcon> CreateTray(
            const std::string& tooltip) override;
    };

} // namespace CNA::Platform::Sdl3
