// SPDX-License-Identifier: MS-PL

#include "Sdl2SystemServices.hpp"

#include "Sdl2Window.hpp"

#include <SDL.h>

namespace CNA::Platform::Sdl2 {

    namespace {

        // SDL2 indexes displays from zero; the contract's id must never be zero, because
        // GraphicsAdapter reads zero as "no display". See Sdl2Displays' own documentation.
        constexpr std::uint32_t ToDisplayId(const int index)
        {
            return static_cast<std::uint32_t>(index) + 1U;
        }

        // Returns -1 for an id no SDL2 display can have, so every caller below refuses rather
        // than reaching for index -1 or wrapping around.
        int ToDisplayIndex(const std::uint32_t id)
        {
            if (id == 0) { return -1; }
            const int index = static_cast<int>(id - 1U);
            return index < SDL_GetNumVideoDisplays() ? index : -1;
        }

        DisplayMode ToDisplayMode(const SDL_DisplayMode& mode)
        {
            DisplayMode result;
            result.width = mode.w;
            result.height = mode.h;
            result.refreshRate = static_cast<float>(mode.refresh_rate);
            return result;
        }

        DisplayInfo DescribeDisplay(const int index)
        {
            DisplayInfo info;
            info.id = ToDisplayId(index);

            const char* name = SDL_GetDisplayName(index);
            info.name = name != nullptr ? name : "";

            SDL_Rect bounds{};
            if (SDL_GetDisplayBounds(index, &bounds) == 0)
            {
                info.x = bounds.x;
                info.y = bounds.y;
                info.width = bounds.w;
                info.height = bounds.h;
            }

            // Deliberately left at 1.0. SDL2 has no content-scale query: SDL_GetDisplayDPI reports
            // the panel's physical dot pitch, not the desktop's scaling factor, so deriving a scale
            // from it would tell a caller that an unscaled 4K desktop is scaled ~1.7x and break
            // every layout computation that trusted it. The genuine high-DPI signal on SDL2 is the
            // per-window drawable-to-logical ratio, which Sdl2Window::GetDisplayScale measures.
            info.contentScale = 1.0f;

            SDL_DisplayMode desktop{};
            if (SDL_GetDesktopDisplayMode(index, &desktop) == 0)
            {
                info.desktopMode = ToDisplayMode(desktop);
            }

            return info;
        }

    } // namespace

    std::vector<DisplayInfo> Sdl2Displays::GetDisplays() const
    {
        const int count = SDL_GetNumVideoDisplays();
        if (count <= 0)
        {
            return {};
        }

        std::vector<DisplayInfo> displays;
        displays.reserve(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index)
        {
            displays.push_back(DescribeDisplay(index));
        }
        return displays;
    }

    bool Sdl2Displays::TryGetDisplayForWindow(const IPlatformWindow& window,
                                              DisplayInfo& display) const
    {
        // The contract deals in IPlatformWindow; only this implementation knows the concrete type,
        // and a window from a different platform is a programming error rather than a runtime
        // condition -- so it reports false instead of crashing.
        const auto* sdlWindow = dynamic_cast<const Sdl2Window*>(&window);
        if (sdlWindow == nullptr)
        {
            return false;
        }

        const int index = SDL_GetWindowDisplayIndex(sdlWindow->GetSdlWindow());
        if (index < 0)
        {
            return false;
        }

        display = DescribeDisplay(index);
        return true;
    }

    bool Sdl2Displays::TryGetSafeAreaForWindow(const IPlatformWindow&, WindowBounds&) const
    {
        return false;
    }

    std::vector<DisplayMode> Sdl2Displays::GetDisplayModes(const std::uint32_t displayId) const
    {
        const int index = ToDisplayIndex(displayId);
        if (index < 0)
        {
            return {};
        }

        const int count = SDL_GetNumDisplayModes(index);
        if (count <= 0)
        {
            return {};
        }

        std::vector<DisplayMode> modes;
        modes.reserve(static_cast<std::size_t>(count));
        for (int modeIndex = 0; modeIndex < count; ++modeIndex)
        {
            SDL_DisplayMode mode{};
            if (SDL_GetDisplayMode(index, modeIndex, &mode) == 0)
            {
                modes.push_back(ToDisplayMode(mode));
            }
        }
        return modes;
    }

    bool Sdl2Displays::TryGetCurrentDisplayMode(const std::uint32_t displayId, DisplayMode& mode) const
    {
        const int index = ToDisplayIndex(displayId);
        if (index < 0)
        {
            return false;
        }

        SDL_DisplayMode current{};
        if (SDL_GetCurrentDisplayMode(index, &current) != 0)
        {
            return false;
        }

        mode = ToDisplayMode(current);
        return true;
    }

    bool Sdl2Displays::IsScreenSaverEnabled() const { return SDL_IsScreenSaverEnabled() == SDL_TRUE; }

    void Sdl2Displays::SetScreenSaverEnabled(const bool enabled)
    {
        if (enabled)
        {
            SDL_EnableScreenSaver();
        }
        else
        {
            SDL_DisableScreenSaver();
        }
    }

} // namespace CNA::Platform::Sdl2
