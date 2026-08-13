// SPDX-License-Identifier: MS-PL

#include "Sdl3SystemServices.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "Sdl3Window.hpp"

#include <memory>

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

    bool Sdl3Displays::TryGetSafeAreaForWindow(
        const IPlatformWindow& window, WindowBounds& safeArea) const
    {
        const auto* sdlWindow = dynamic_cast<const Sdl3Window*>(&window);
        if (sdlWindow == nullptr)
        {
            return false;
        }

        SDL_Rect area{};
        if (!SDL_GetWindowSafeArea(sdlWindow->GetSdlWindow(), &area))
        {
            return false;
        }

        safeArea = WindowBounds{area.x, area.y, area.w, area.h};
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

    // --- dialogs (PLAT-103) ---------------------------------------------------------------------

    namespace {

        SDL_MessageBoxFlags ToSdlSeverity(const MessageBoxSeverity severity)
        {
            switch (severity)
            {
                case MessageBoxSeverity::Error:       return SDL_MESSAGEBOX_ERROR;
                case MessageBoxSeverity::Warning:     return SDL_MESSAGEBOX_WARNING;
                case MessageBoxSeverity::Information: return SDL_MESSAGEBOX_INFORMATION;
            }
            return SDL_MESSAGEBOX_INFORMATION;
        }

        /// Resolves the native window to parent a dialog to. A null parent is normal -- a fatal
        /// error box is often shown after the window is gone, which is exactly when it matters.
        SDL_Window* ToSdlWindow(IPlatformWindow* parent)
        {
            auto* sdlWindow = dynamic_cast<Sdl3Window*>(parent);
            return sdlWindow != nullptr ? sdlWindow->GetSdlWindow() : nullptr;
        }

    } // namespace

    void Sdl3Dialogs::ShowMessageBox(const MessageBoxSeverity severity, const std::string& title,
                                     const std::string& message, IPlatformWindow* parent)
    {
        if (!SDL_ShowSimpleMessageBox(ToSdlSeverity(severity), title.c_str(), message.c_str(),
                                      ToSdlWindow(parent)))
        {
            throw PlatformException("ShowMessageBox", SDL_GetError());
        }
    }

    int Sdl3Dialogs::ShowMessageBoxWithButtons(const MessageBoxSeverity severity,
                                               const std::string& title,
                                               const std::string& message,
                                               const std::vector<std::string>& buttons,
                                               IPlatformWindow* parent)
    {
        if (buttons.empty())
        {
            throw PlatformException("ShowMessageBoxWithButtons", "no buttons were supplied");
        }

        // The label strings are borrowed by SDL_MessageBoxButtonData, and the vector is the
        // caller's, so nothing here may copy or reallocate them before the modal call returns.
        std::vector<SDL_MessageBoxButtonData> sdlButtons;
        sdlButtons.reserve(buttons.size());
        for (std::size_t i = 0; i < buttons.size(); ++i)
        {
            SDL_MessageBoxButtonData button{};
            button.flags = 0;
            button.buttonID = static_cast<int>(i);
            button.text = buttons[i].c_str();
            sdlButtons.push_back(button);
        }

        SDL_MessageBoxData data{};
        data.flags = ToSdlSeverity(severity);
        data.window = ToSdlWindow(parent);
        data.title = title.c_str();
        data.message = message.c_str();
        data.numbuttons = static_cast<int>(sdlButtons.size());
        data.buttons = sdlButtons.data();
        data.colorScheme = nullptr;

        // Seeded to -1 because SDL leaves it untouched when the dialog is closed without a
        // choice, which is the contract's "dismissed" answer rather than a failure.
        int chosen = -1;
        if (!SDL_ShowMessageBox(&data, &chosen))
        {
            throw PlatformException("ShowMessageBoxWithButtons", SDL_GetError());
        }
        return chosen;
    }

    // --- file dialogs (PLAT-104) ----------------------------------------------------------------

    namespace {

        /// Everything one dialog's callback needs to stay alive until it fires.
        ///
        /// SDL is explicit that the filter array must remain valid until the callback runs, which
        /// is *after* the Show* call returns. The name and pattern strings are therefore owned
        /// here and reserved to their final size before any pointer into them is taken, so
        /// std::string's storage never moves out from under the SDL_DialogFileFilter array.
        struct DialogContext
        {
            FileDialogCallback callback;
            std::string defaultLocation;
            std::vector<std::string> filterNames;
            std::vector<std::string> filterPatterns;
            std::vector<SDL_DialogFileFilter> sdlFilters;
        };

        std::unique_ptr<DialogContext> MakeContext(FileDialogCallback callback,
                                                   const std::vector<FileDialogFilter>& filters,
                                                   const std::string& defaultLocation)
        {
            auto context = std::make_unique<DialogContext>();
            context->callback = std::move(callback);
            context->defaultLocation = defaultLocation;

            context->filterNames.reserve(filters.size());
            context->filterPatterns.reserve(filters.size());
            for (const FileDialogFilter& filter : filters)
            {
                context->filterNames.push_back(filter.name);
                context->filterPatterns.push_back(filter.patterns);
            }

            context->sdlFilters.reserve(filters.size());
            for (std::size_t i = 0; i < filters.size(); ++i)
            {
                context->sdlFilters.push_back(SDL_DialogFileFilter{
                    context->filterNames[i].c_str(), context->filterPatterns[i].c_str()});
            }
            return context;
        }

        /// Reclaims the context and delivers the result. SDL fires each dialog callback exactly
        /// once, so taking ownership back here is the whole lifetime, not a leak or a double free.
        void ResultTrampoline(void* userdata, const char* const* filelist, int /*filter*/)
        {
            const std::unique_ptr<DialogContext> context(static_cast<DialogContext*>(userdata));

            std::vector<std::string> paths;
            if (filelist != nullptr)
            {
                for (const char* const* entry = filelist; *entry != nullptr; ++entry)
                {
                    paths.emplace_back(*entry);
                }
            }

            // A null filelist means SDL failed rather than the user cancelling, and an empty one
            // means cancelled. Both are reported to the caller the same way -- there is no path
            // to open either way -- but only cancellation is ordinary, which is why the
            // distinction is noted here rather than silently collapsed.
            if (context->callback)
            {
                context->callback(paths);
            }
        }

        /// The location SDL should start in, or null for its own default.
        const char* LocationOrNull(const DialogContext& context)
        {
            return context.defaultLocation.empty() ? nullptr : context.defaultLocation.c_str();
        }

        void RequireCallback(const FileDialogCallback& callback, const char* operation)
        {
            if (!callback)
            {
                // Without a callback the dialog's result has nowhere to go, so the call would
                // show a dialog whose answer is discarded -- worse than refusing, because the
                // user has already been interrupted by then.
                throw PlatformException(operation, "no result callback was supplied");
            }
        }

    } // namespace

    void Sdl3Dialogs::ShowOpenFileDialog(FileDialogCallback onResult,
                                         const std::vector<FileDialogFilter>& filters,
                                         const std::string& defaultLocation,
                                         const bool allowMultiple, IPlatformWindow* parent)
    {
        RequireCallback(onResult, "ShowOpenFileDialog");

        DialogContext* context = MakeContext(std::move(onResult), filters, defaultLocation).release();
        SDL_ShowOpenFileDialog(ResultTrampoline, context, ToSdlWindow(parent),
                               context->sdlFilters.empty() ? nullptr : context->sdlFilters.data(),
                               static_cast<int>(context->sdlFilters.size()),
                               LocationOrNull(*context), allowMultiple);
    }

    void Sdl3Dialogs::ShowSaveFileDialog(FileDialogCallback onResult,
                                         const std::vector<FileDialogFilter>& filters,
                                         const std::string& defaultLocation,
                                         IPlatformWindow* parent)
    {
        RequireCallback(onResult, "ShowSaveFileDialog");

        DialogContext* context = MakeContext(std::move(onResult), filters, defaultLocation).release();
        SDL_ShowSaveFileDialog(ResultTrampoline, context, ToSdlWindow(parent),
                               context->sdlFilters.empty() ? nullptr : context->sdlFilters.data(),
                               static_cast<int>(context->sdlFilters.size()),
                               LocationOrNull(*context));
    }

    void Sdl3Dialogs::ShowOpenFolderDialog(FileDialogCallback onResult,
                                           const std::string& defaultLocation,
                                           const bool allowMultiple, IPlatformWindow* parent)
    {
        RequireCallback(onResult, "ShowOpenFolderDialog");

        DialogContext* context = MakeContext(std::move(onResult), {}, defaultLocation).release();
        SDL_ShowOpenFolderDialog(ResultTrampoline, context, ToSdlWindow(parent),
                                 LocationOrNull(*context), allowMultiple);
    }

} // namespace CNA::Platform::Sdl3
