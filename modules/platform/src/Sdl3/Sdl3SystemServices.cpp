// SPDX-License-Identifier: MS-PL

#include "Sdl3SystemServices.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "Sdl3Window.hpp"

#include <SDL3/SDL.h>

namespace CNA::Platform::Sdl3 {

    namespace {

        /// SDL hands back heap strings its caller must free. Wrapping the free in one place keeps
        /// every call site from having to remember it.
        std::string TakeSdlString(char* owned)
        {
            if (owned == nullptr)
            {
                return {};
            }
            std::string result(owned);
            SDL_free(owned);
            return result;
        }

        DisplayInfo DescribeDisplay(const SDL_DisplayID id)
        {
            DisplayInfo info;
            info.id = static_cast<std::uint32_t>(id);

            const char* name = SDL_GetDisplayName(id);
            info.name = name != nullptr ? name : "";

            SDL_Rect bounds{};
            if (SDL_GetDisplayBounds(id, &bounds))
            {
                info.x = bounds.x;
                info.y = bounds.y;
                info.width = bounds.w;
                info.height = bounds.h;
            }

            const float scale = SDL_GetDisplayContentScale(id);
            // A zero scale would divide to infinity in any layout computation, so unknown is 1:1.
            info.contentScale = scale > 0.0f ? scale : 1.0f;

            if (const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(id); mode != nullptr)
            {
                info.desktopMode.width = mode->w;
                info.desktopMode.height = mode->h;
                info.desktopMode.refreshRate = mode->refresh_rate;
            }

            return info;
        }

    } // namespace

    // --- clipboard --------------------------------------------------------------------------------

    bool Sdl3Clipboard::HasText() const { return SDL_HasClipboardText(); }

    std::string Sdl3Clipboard::GetText() const
    {
        // An empty clipboard is ordinary, not exceptional, so this returns a status rather than
        // throwing -- PLAT-21's split applied.
        return TakeSdlString(SDL_GetClipboardText());
    }

    void Sdl3Clipboard::SetText(const std::string& text)
    {
        if (!SDL_SetClipboardText(text.c_str()))
        {
            throw PlatformException("Clipboard::SetText", SDL_GetError());
        }
    }

    // --- displays ---------------------------------------------------------------------------------

    std::vector<DisplayInfo> Sdl3Displays::GetDisplays() const
    {
        int count = 0;
        SDL_DisplayID* ids = SDL_GetDisplays(&count);
        if (ids == nullptr)
        {
            return {};
        }

        std::vector<DisplayInfo> displays;
        displays.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            displays.push_back(DescribeDisplay(ids[i]));
        }
        SDL_free(ids);
        return displays;
    }

    bool Sdl3Displays::TryGetDisplayForWindow(const IPlatformWindow& window, DisplayInfo& display) const
    {
        // The contract deals in IPlatformWindow; only this implementation knows the concrete type,
        // and a window from a different platform would be a programming error rather than a
        // runtime condition -- so it reports false instead of crashing.
        const auto* sdlWindow = dynamic_cast<const Sdl3Window*>(&window);
        if (sdlWindow == nullptr)
        {
            return false;
        }

        const SDL_DisplayID id = SDL_GetDisplayForWindow(sdlWindow->GetSdlWindow());
        if (id == 0)
        {
            return false;
        }

        display = DescribeDisplay(id);
        return true;
    }

    std::vector<DisplayMode> Sdl3Displays::GetDisplayModes(const std::uint32_t displayId) const
    {
        int count = 0;
        SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(static_cast<SDL_DisplayID>(displayId), &count);
        if (modes == nullptr)
        {
            return {};
        }

        std::vector<DisplayMode> result;
        result.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            DisplayMode mode;
            mode.width = modes[i]->w;
            mode.height = modes[i]->h;
            mode.refreshRate = modes[i]->refresh_rate;
            result.push_back(mode);
        }
        SDL_free(modes);
        return result;
    }

    // --- filesystem -------------------------------------------------------------------------------

    std::string Sdl3FileSystem::GetBasePath() const
    {
        const char* base = SDL_GetBasePath();
        return base != nullptr ? std::string(base) : std::string();
    }

    std::string Sdl3FileSystem::GetPreferencesPath(const std::string& organization,
                                                   const std::string& application) const
    {
        std::string path = TakeSdlString(SDL_GetPrefPath(organization.c_str(), application.c_str()));
        if (path.empty())
        {
            throw PlatformException("FileSystem::GetPreferencesPath", SDL_GetError());
        }
        return path;
    }

    bool Sdl3FileSystem::TryLoadFile(const std::string& path, std::vector<std::uint8_t>& data) const
    {
        std::size_t size = 0;
        void* contents = SDL_LoadFile(path.c_str(), &size);
        if (contents == nullptr)
        {
            // A missing file is an ordinary outcome a caller branches on, not an exception.
            return false;
        }

        const auto* bytes = static_cast<const std::uint8_t*>(contents);
        data.assign(bytes, bytes + size);
        SDL_free(contents);
        return true;
    }

    void Sdl3FileSystem::CreateDirectory(const std::string& path)
    {
        if (!SDL_CreateDirectory(path.c_str()))
        {
            throw PlatformException("FileSystem::CreateDirectory(" + path + ")", SDL_GetError());
        }
    }

    // --- system information -------------------------------------------------------------------------

    std::string Sdl3SystemInfo::GetPlatformName() const
    {
        const char* platform = SDL_GetPlatform();
        return platform != nullptr ? std::string(platform) : std::string();
    }

    int Sdl3SystemInfo::GetSystemMemoryMegabytes() const { return SDL_GetSystemRAM(); }

    int Sdl3SystemInfo::GetLogicalCoreCount() const { return SDL_GetNumLogicalCPUCores(); }

    std::vector<PlatformLocale> Sdl3SystemInfo::GetPreferredLocales() const
    {
        int count = 0;
        SDL_Locale** locales = SDL_GetPreferredLocales(&count);
        if (locales == nullptr)
        {
            return {};
        }

        std::vector<PlatformLocale> result;
        result.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            if (locales[i] == nullptr || locales[i]->language == nullptr)
            {
                continue;
            }
            PlatformLocale locale;
            locale.language = locales[i]->language;
            locale.country = locales[i]->country != nullptr ? locales[i]->country : "";
            result.push_back(std::move(locale));
        }
        SDL_free(locales);
        return result;
    }

    PowerInfo Sdl3SystemInfo::GetPowerInfo() const
    {
        PowerInfo info;
        int seconds = -1;
        int percent = -1;
        const SDL_PowerState state = SDL_GetPowerInfo(&seconds, &percent);

        switch (state)
        {
            case SDL_POWERSTATE_ON_BATTERY: info.state = PowerState::OnBattery; break;
            case SDL_POWERSTATE_NO_BATTERY: info.state = PowerState::NoBattery; break;
            case SDL_POWERSTATE_CHARGING:   info.state = PowerState::Charging; break;
            case SDL_POWERSTATE_CHARGED:    info.state = PowerState::Charged; break;
            case SDL_POWERSTATE_ERROR:      info.state = PowerState::Error; break;
            case SDL_POWERSTATE_UNKNOWN:
            default:                        info.state = PowerState::Unknown; break;
        }

        // SDL already uses -1 for unknown, which is the convention PowerInfo documents: it keeps
        // "unknown" distinguishable from "empty battery".
        info.secondsRemaining = seconds;
        info.percent = percent;
        return info;
    }

    bool Sdl3SystemInfo::OpenUrl(const std::string& url) { return SDL_OpenURL(url.c_str()); }

} // namespace CNA::Platform::Sdl3
