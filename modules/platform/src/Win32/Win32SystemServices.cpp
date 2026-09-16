// SPDX-License-Identifier: MS-PL

#include "Win32SystemServices.hpp"

#include "Win32DpiSupport.hpp"
#include "Win32Error.hpp"
#include "Win32Utf.hpp"
#include "Win32Window.hpp"

#include "CNA/Internal/PathUtf8.hpp"
#include "CNA/Platform/PlatformException.hpp"

#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <utility>
#include <vector>

namespace CNA::Platform::Win32 {

    namespace {

        HWND ParentHandle(IPlatformWindow* const parent)
        {
            const auto* window = dynamic_cast<const Win32Window*>(parent);
            return window != nullptr ? window->GetHwnd() : nullptr;
        }

        /// Releases a COM interface exactly once. A `unique_ptr` deleter rather than a smart COM
        /// pointer class, because this module needs exactly this and nothing else.
        struct ComReleaser
        {
            void operator()(IUnknown* const object) const
            {
                if (object != nullptr)
                    object->Release();
            }
        };

        template <typename T>
        using ComPtr = std::unique_ptr<T, ComReleaser>;

        struct MonitorCollection
        {
            std::vector<DisplayInfo> displays;
            std::vector<HMONITOR> handles;
        };

        BOOL CALLBACK CollectMonitor(const HMONITOR monitor, HDC, LPRECT, const LPARAM userData)
        {
            auto* collection = reinterpret_cast<MonitorCollection*>(userData);
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(monitor, &info) == FALSE)
                return TRUE;

            DisplayInfo display;
            display.id = static_cast<std::uint32_t>(collection->displays.size() + 1);
            display.name = ToUtf8(std::wstring(info.szDevice));
            display.x = info.rcMonitor.left;
            display.y = info.rcMonitor.top;
            display.width = info.rcMonitor.right - info.rcMonitor.left;
            display.height = info.rcMonitor.bottom - info.rcMonitor.top;
            display.contentScale =
                Win32DpiSupport::ToDisplayScale(Win32DpiSupport::Get().GetMonitorDpi(monitor));

            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);
            if (EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) != FALSE)
            {
                display.desktopMode.width = static_cast<int>(mode.dmPelsWidth);
                display.desktopMode.height = static_cast<int>(mode.dmPelsHeight);
                display.desktopMode.refreshRate = static_cast<float>(mode.dmDisplayFrequency);
            }
            else
            {
                display.desktopMode.width = display.width;
                display.desktopMode.height = display.height;
            }

            // The primary monitor goes first, so a caller that takes displays[0] without looking
            // gets the one the user thinks of as their main screen.
            if ((info.dwFlags & MONITORINFOF_PRIMARY) != 0 && !collection->displays.empty())
            {
                collection->displays.insert(collection->displays.begin(), std::move(display));
                collection->handles.insert(collection->handles.begin(), monitor);
                for (std::size_t index = 0; index < collection->displays.size(); ++index)
                    collection->displays[index].id = static_cast<std::uint32_t>(index + 1);
            }
            else
            {
                collection->displays.push_back(std::move(display));
                collection->handles.push_back(monitor);
            }
            return TRUE;
        }

        MonitorCollection EnumerateMonitors()
        {
            MonitorCollection collection;
            EnumDisplayMonitors(nullptr, nullptr, &CollectMonitor,
                                reinterpret_cast<LPARAM>(&collection));
            return collection;
        }

        std::string KnownFolder(const KNOWNFOLDERID& folder)
        {
            PWSTR path = nullptr;
            if (FAILED(SHGetKnownFolderPath(folder, 0, nullptr, &path)) || path == nullptr)
            {
                if (path != nullptr)
                    CoTaskMemFree(path);
                return {};
            }
            std::string result = ToUtf8(std::wstring(path));
            CoTaskMemFree(path);
            if (!result.empty() && result.back() != '\\' && result.back() != '/')
                result.push_back('\\');
            return result;
        }

        std::vector<std::wstring> BuildFilterStorage(const std::vector<FileDialogFilter>& filters,
                                                     std::vector<COMDLG_FILTERSPEC>& specs)
        {
            // The COMDLG_FILTERSPEC array holds raw pointers into strings that must outlive the
            // dialog call; returning the storage is what keeps them alive.
            std::vector<std::wstring> storage;
            storage.reserve(filters.size() * 2);
            specs.reserve(filters.size());

            for (const FileDialogFilter& filter : filters)
            {
                std::string pattern;
                std::size_t start = 0;
                while (start <= filter.patterns.size())
                {
                    const std::size_t end = filter.patterns.find(';', start);
                    const std::string piece = filter.patterns.substr(
                        start, end == std::string::npos ? std::string::npos : end - start);
                    if (!piece.empty())
                    {
                        if (!pattern.empty())
                            pattern.push_back(';');
                        pattern += "*." + piece;
                    }
                    if (end == std::string::npos)
                        break;
                    start = end + 1;
                }
                if (pattern.empty())
                    pattern = "*.*";

                storage.push_back(ToWide(filter.name));
                storage.push_back(ToWide(pattern));
            }

            for (std::size_t index = 0; index + 1 < storage.size(); index += 2)
                specs.push_back({storage[index].c_str(), storage[index + 1].c_str()});
            return storage;
        }

        std::string ShellItemPath(IShellItem* const item)
        {
            PWSTR path = nullptr;
            if (item == nullptr || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) ||
                path == nullptr)
            {
                if (path != nullptr)
                    CoTaskMemFree(path);
                return {};
            }
            std::string result = ToUtf8(std::wstring(path));
            CoTaskMemFree(path);
            return result;
        }

        void SetDialogFolder(IFileDialog& dialog, const std::string& location)
        {
            if (location.empty())
                return;
            const std::wstring wide = ToWide(location);
            ComPtr<IShellItem> folder;
            IShellItem* raw = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(wide.c_str(), nullptr, IID_PPV_ARGS(&raw))) &&
                raw != nullptr)
            {
                folder.reset(raw);
                dialog.SetFolder(folder.get());
            }
        }

    } // namespace

    // --- clipboard -----------------------------------------------------------------------------

    bool Win32Clipboard::HasText() const
    {
        return IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE;
    }

    std::string Win32Clipboard::GetText() const
    {
        if (!HasText())
            return {};
        if (OpenClipboard(nullptr) == FALSE)
            return {};

        std::string text;
        if (const HANDLE handle = GetClipboardData(CF_UNICODETEXT))
        {
            if (const auto* locked = static_cast<const wchar_t*>(GlobalLock(handle)))
            {
                text = ToUtf8(locked);
                GlobalUnlock(handle);
            }
        }
        CloseClipboard();
        return text;
    }

    void Win32Clipboard::SetText(const std::string& text)
    {
        const std::wstring wide = ToWide(text);
        const std::size_t bytes = (wide.size() + 1) * sizeof(wchar_t);

        // GMEM_MOVEABLE because the clipboard takes ownership of the block and may relocate it.
        const HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (block == nullptr)
            ThrowLastError("Win32Clipboard::SetText");

        auto* destination = static_cast<wchar_t*>(GlobalLock(block));
        if (destination == nullptr)
        {
            const DWORD error = GetLastError();
            GlobalFree(block);
            ThrowError("Win32Clipboard::SetText", error);
        }
        std::copy(wide.begin(), wide.end(), destination);
        destination[wide.size()] = L'\0';
        GlobalUnlock(block);

        if (OpenClipboard(nullptr) == FALSE)
        {
            const DWORD error = GetLastError();
            GlobalFree(block);
            ThrowError("Win32Clipboard::SetText", error);
        }
        EmptyClipboard();
        if (SetClipboardData(CF_UNICODETEXT, block) == nullptr)
        {
            const DWORD error = GetLastError();
            CloseClipboard();
            // Ownership only transfers on success, so the block is still ours to free.
            GlobalFree(block);
            ThrowError("Win32Clipboard::SetText", error);
        }
        CloseClipboard();
    }

    // --- displays ------------------------------------------------------------------------------

    Win32Displays::Win32Displays(Win32PlatformAccess& access)
        : access_(&access)
    {
    }

    std::vector<DisplayInfo> Win32Displays::GetDisplays() const
    {
        return EnumerateMonitors().displays;
    }

    bool Win32Displays::TryGetDisplayForWindow(const IPlatformWindow& window,
                                               DisplayInfo& display) const
    {
        const auto* native = dynamic_cast<const Win32Window*>(&window);
        if (native == nullptr)
            return false;

        const HMONITOR monitor = MonitorFromWindow(native->GetHwnd(), MONITOR_DEFAULTTONEAREST);
        if (monitor == nullptr)
            return false;

        const MonitorCollection collection = EnumerateMonitors();
        for (std::size_t index = 0; index < collection.handles.size(); ++index)
        {
            if (collection.handles[index] == monitor)
            {
                display = collection.displays[index];
                return true;
            }
        }
        return false;
    }

    bool Win32Displays::TryGetSafeAreaForWindow(const IPlatformWindow& window,
                                                WindowBounds& safeArea) const
    {
        const auto* native = dynamic_cast<const Win32Window*>(&window);
        if (native == nullptr)
            return false;

        // A desktop window's whole client area is interactive: nothing is notched, rounded or
        // covered by system chrome. Reporting the full area is the correct answer rather than a
        // refusal -- the question has one here, and it is "all of it".
        RECT client{};
        if (GetClientRect(native->GetHwnd(), &client) == FALSE)
            return false;
        safeArea.x = 0;
        safeArea.y = 0;
        safeArea.width = client.right - client.left;
        safeArea.height = client.bottom - client.top;
        return true;
    }

    std::vector<DisplayMode> Win32Displays::GetDisplayModes(const std::uint32_t displayId) const
    {
        const MonitorCollection collection = EnumerateMonitors();
        if (displayId == 0 || displayId > collection.displays.size())
            return {};

        const std::wstring device = ToWide(collection.displays[displayId - 1].name);
        std::vector<DisplayMode> modes;
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        for (DWORD index = 0; EnumDisplaySettingsW(device.c_str(), index, &mode) != FALSE; ++index)
        {
            DisplayMode entry;
            entry.width = static_cast<int>(mode.dmPelsWidth);
            entry.height = static_cast<int>(mode.dmPelsHeight);
            entry.refreshRate = static_cast<float>(mode.dmDisplayFrequency);

            // EnumDisplaySettingsW enumerates the cross product with colour depth, so the same
            // resolution and refresh rate comes back several times.
            const bool duplicate = std::any_of(
                modes.begin(), modes.end(), [&entry](const DisplayMode& existing) {
                    return existing.width == entry.width && existing.height == entry.height &&
                           existing.refreshRate == entry.refreshRate;
                });
            if (!duplicate)
                modes.push_back(entry);
            mode = DEVMODEW{};
            mode.dmSize = sizeof(mode);
        }
        return modes;
    }

    bool Win32Displays::TryGetCurrentDisplayMode(const std::uint32_t displayId,
                                                 DisplayMode& mode) const
    {
        const MonitorCollection collection = EnumerateMonitors();
        if (displayId == 0 || displayId > collection.displays.size())
            return false;

        const std::wstring device = ToWide(collection.displays[displayId - 1].name);
        DEVMODEW current{};
        current.dmSize = sizeof(current);
        if (EnumDisplaySettingsW(device.c_str(), ENUM_CURRENT_SETTINGS, &current) == FALSE)
            return false;

        mode.width = static_cast<int>(current.dmPelsWidth);
        mode.height = static_cast<int>(current.dmPelsHeight);
        mode.refreshRate = static_cast<float>(current.dmDisplayFrequency);
        return true;
    }

    bool Win32Displays::IsScreenSaverEnabled() const
    {
        return !screenSaverSuppressed_;
    }

    void Win32Displays::SetScreenSaverEnabled(const bool enabled)
    {
        // SetThreadExecutionState rather than changing the user's screen-saver setting: the
        // setting is theirs, and a game that crashed while holding it would leave it changed.
        // ES_CONTINUOUS alone restores the default behaviour.
        if (enabled)
            SetThreadExecutionState(ES_CONTINUOUS);
        else
            SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
        screenSaverSuppressed_ = !enabled;
    }

    // --- dialogs -------------------------------------------------------------------------------

    Win32Dialogs::Win32Dialogs(Win32PlatformAccess& access)
        : access_(&access)
    {
    }

    void Win32Dialogs::ShowMessageBox(const MessageBoxSeverity severity, const std::string& title,
                                      const std::string& message, IPlatformWindow* const parent)
    {
        if (!access_->GetPlatformCapabilities().messageBox)
        {
            throw PlatformNotSupportedException(PlatformCapability::MessageBox,
                                                access_->GetPlatformName());
        }

        UINT flags = MB_OK;
        switch (severity)
        {
            case MessageBoxSeverity::Information: flags |= MB_ICONINFORMATION; break;
            case MessageBoxSeverity::Warning:     flags |= MB_ICONWARNING; break;
            case MessageBoxSeverity::Error:       flags |= MB_ICONERROR; break;
        }

        const std::wstring wideTitle = ToWide(title);
        const std::wstring wideMessage = ToWide(message);
        MessageBoxW(ParentHandle(parent), wideMessage.c_str(), wideTitle.c_str(), flags);
    }

    int Win32Dialogs::ShowMessageBoxWithButtons(const MessageBoxSeverity severity,
                                                const std::string& title,
                                                const std::string& message,
                                                const std::vector<std::string>& buttons,
                                                IPlatformWindow* const parent)
    {
        if (!access_->GetPlatformCapabilities().messageBox)
        {
            throw PlatformNotSupportedException(PlatformCapability::MessageBox,
                                                access_->GetPlatformName());
        }
        if (buttons.empty())
            throw PlatformException("Win32Dialogs::ShowMessageBoxWithButtons", "no buttons given");

        // TaskDialogIndirect would allow arbitrary labels, but it lives in comctl32 v6 and needs
        // an activation context a host may not have. MessageBoxW is always available, so the
        // labels are mapped onto its fixed button sets and the chosen INDEX is what comes back --
        // which is what the contract specifies, not the label.
        UINT flags = MB_OK;
        if (buttons.size() >= 3)
            flags = MB_YESNOCANCEL;
        else if (buttons.size() == 2)
            flags = MB_OKCANCEL;

        switch (severity)
        {
            case MessageBoxSeverity::Information: flags |= MB_ICONINFORMATION; break;
            case MessageBoxSeverity::Warning:     flags |= MB_ICONWARNING; break;
            case MessageBoxSeverity::Error:       flags |= MB_ICONERROR; break;
        }

        std::string body = message;
        if (buttons.size() > 3 || flags == MB_YESNOCANCEL || flags == MB_OKCANCEL)
        {
            // The labels the caller wanted still have to reach the user, so they are listed in
            // the body rather than silently dropped.
            body += "\n";
            for (std::size_t index = 0; index < buttons.size(); ++index)
                body += "\n" + std::to_string(index + 1) + ". " + buttons[index];
        }

        const std::wstring wideTitle = ToWide(title);
        const std::wstring wideMessage = ToWide(body);
        const int chosen =
            MessageBoxW(ParentHandle(parent), wideMessage.c_str(), wideTitle.c_str(), flags);

        switch (chosen)
        {
            case IDOK:
            case IDYES:
                return 0;
            case IDNO:
                return buttons.size() >= 2 ? 1 : -1;
            case IDCANCEL:
                if (flags == MB_YESNOCANCEL)
                    return buttons.size() >= 3 ? 2 : -1;
                return buttons.size() >= 2 ? 1 : -1;
            default:
                return -1;
        }
    }

    void Win32Dialogs::ShowOpenFileDialog(FileDialogCallback onResult,
                                          const std::vector<FileDialogFilter>& filters,
                                          const std::string& defaultLocation,
                                          const bool allowMultiple, IPlatformWindow* const parent)
    {
        if (!access_->GetPlatformCapabilities().nativeFileDialog)
        {
            throw PlatformNotSupportedException(PlatformCapability::NativeFileDialog,
                                                access_->GetPlatformName());
        }
        if (!onResult)
            throw PlatformException("Win32Dialogs::ShowOpenFileDialog", "no result callback given");

        IFileOpenDialog* raw = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&raw))) ||
            raw == nullptr)
        {
            onResult({});
            return;
        }
        ComPtr<IFileOpenDialog> dialog(raw);

        std::vector<COMDLG_FILTERSPEC> specs;
        const std::vector<std::wstring> storage = BuildFilterStorage(filters, specs);
        if (!specs.empty())
            dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
        SetDialogFolder(*dialog, defaultLocation);

        DWORD options = 0;
        dialog->GetOptions(&options);
        options |= FOS_FORCEFILESYSTEM;
        if (allowMultiple)
            options |= FOS_ALLOWMULTISELECT;
        dialog->SetOptions(options);

        if (FAILED(dialog->Show(ParentHandle(parent))))
        {
            // Cancellation is an ordinary outcome: the callback still fires, with nothing in it.
            onResult({});
            return;
        }

        std::vector<std::string> paths;
        IShellItemArray* rawItems = nullptr;
        if (SUCCEEDED(dialog->GetResults(&rawItems)) && rawItems != nullptr)
        {
            ComPtr<IShellItemArray> items(rawItems);
            DWORD count = 0;
            items->GetCount(&count);
            for (DWORD index = 0; index < count; ++index)
            {
                IShellItem* rawItem = nullptr;
                if (SUCCEEDED(items->GetItemAt(index, &rawItem)) && rawItem != nullptr)
                {
                    ComPtr<IShellItem> item(rawItem);
                    std::string path = ShellItemPath(item.get());
                    if (!path.empty())
                        paths.push_back(std::move(path));
                }
            }
        }
        onResult(paths);
    }

    void Win32Dialogs::ShowSaveFileDialog(FileDialogCallback onResult,
                                          const std::vector<FileDialogFilter>& filters,
                                          const std::string& defaultLocation,
                                          IPlatformWindow* const parent)
    {
        if (!access_->GetPlatformCapabilities().nativeFileDialog)
        {
            throw PlatformNotSupportedException(PlatformCapability::NativeFileDialog,
                                                access_->GetPlatformName());
        }
        if (!onResult)
            throw PlatformException("Win32Dialogs::ShowSaveFileDialog", "no result callback given");

        IFileSaveDialog* raw = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&raw))) ||
            raw == nullptr)
        {
            onResult({});
            return;
        }
        ComPtr<IFileSaveDialog> dialog(raw);

        std::vector<COMDLG_FILTERSPEC> specs;
        const std::vector<std::wstring> storage = BuildFilterStorage(filters, specs);
        if (!specs.empty())
            dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());

        // A save dialog's default location may name a file rather than a directory, which
        // SetFolder cannot take.
        // defaultLocation is UTF-8 by the IPlatformSystemServices contract. It used to be widened
        // by the narrow path constructor -- ANSI -- and the filename then went back out through
        // .string() and into ToWide() as if it were UTF-8 again. Three conversions, and because
        // ToWide() uses MB_ERR_INVALID_CHARS it returned an empty string rather than mangling one,
        // so SetFileName silently received nothing. The path is native here, so nothing narrows.
        const std::filesystem::path suggested = CNA::Internal::PathFromUtf8(defaultLocation);
        if (!defaultLocation.empty() && suggested.has_filename() &&
            !std::filesystem::is_directory(suggested))
        {
            const std::wstring name = suggested.filename().wstring();
            dialog->SetFileName(name.c_str());
            SetDialogFolder(*dialog, CNA::Internal::PathToUtf8(suggested.parent_path()));
        }
        else
        {
            SetDialogFolder(*dialog, defaultLocation);
        }

        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT);

        if (FAILED(dialog->Show(ParentHandle(parent))))
        {
            onResult({});
            return;
        }

        std::vector<std::string> paths;
        IShellItem* rawItem = nullptr;
        if (SUCCEEDED(dialog->GetResult(&rawItem)) && rawItem != nullptr)
        {
            ComPtr<IShellItem> item(rawItem);
            std::string path = ShellItemPath(item.get());
            if (!path.empty())
                paths.push_back(std::move(path));
        }
        onResult(paths);
    }

    void Win32Dialogs::ShowOpenFolderDialog(FileDialogCallback onResult,
                                            const std::string& defaultLocation,
                                            const bool allowMultiple, IPlatformWindow* const parent)
    {
        if (!access_->GetPlatformCapabilities().nativeFileDialog)
        {
            throw PlatformNotSupportedException(PlatformCapability::NativeFileDialog,
                                                access_->GetPlatformName());
        }
        if (!onResult)
            throw PlatformException("Win32Dialogs::ShowOpenFolderDialog", "no result callback given");

        IFileOpenDialog* raw = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&raw))) ||
            raw == nullptr)
        {
            onResult({});
            return;
        }
        ComPtr<IFileOpenDialog> dialog(raw);
        SetDialogFolder(*dialog, defaultLocation);

        DWORD options = 0;
        dialog->GetOptions(&options);
        options |= FOS_FORCEFILESYSTEM | FOS_PICKFOLDERS;
        if (allowMultiple)
            options |= FOS_ALLOWMULTISELECT;
        dialog->SetOptions(options);

        if (FAILED(dialog->Show(ParentHandle(parent))))
        {
            onResult({});
            return;
        }

        std::vector<std::string> paths;
        IShellItemArray* rawItems = nullptr;
        if (SUCCEEDED(dialog->GetResults(&rawItems)) && rawItems != nullptr)
        {
            ComPtr<IShellItemArray> items(rawItems);
            DWORD count = 0;
            items->GetCount(&count);
            for (DWORD index = 0; index < count; ++index)
            {
                IShellItem* rawItem = nullptr;
                if (SUCCEEDED(items->GetItemAt(index, &rawItem)) && rawItem != nullptr)
                {
                    ComPtr<IShellItem> item(rawItem);
                    std::string path = ShellItemPath(item.get());
                    if (!path.empty())
                        paths.push_back(std::move(path));
                }
            }
        }
        onResult(paths);
    }

    // --- system information --------------------------------------------------------------------

    std::string Win32SystemInfo::GetPlatformName() const
    {
        return "Windows";
    }

    int Win32SystemInfo::GetSystemMemoryMegabytes() const
    {
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof(status);
        if (GlobalMemoryStatusEx(&status) == FALSE)
            return 0;
        return static_cast<int>(status.ullTotalPhys / (1024ull * 1024ull));
    }

    int Win32SystemInfo::GetLogicalCoreCount() const
    {
        SYSTEM_INFO info{};
        GetNativeSystemInfo(&info);
        return static_cast<int>(info.dwNumberOfProcessors);
    }

    std::vector<PlatformLocale> Win32SystemInfo::GetPreferredLocales() const
    {
        std::vector<PlatformLocale> locales;

        ULONG count = 0;
        ULONG length = 0;
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &length) == FALSE ||
            length == 0)
        {
            return locales;
        }

        std::wstring buffer(length, L'\0');
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buffer.data(), &length) == FALSE)
            return locales;

        // A double-null-terminated sequence of BCP 47 tags, e.g. L"en-GB\0cs-CZ\0\0".
        std::size_t start = 0;
        while (start < buffer.size() && buffer[start] != L'\0')
        {
            const std::size_t end = buffer.find(L'\0', start);
            if (end == std::wstring::npos)
                break;

            const std::string tag = ToUtf8(buffer.substr(start, end - start));
            if (!tag.empty())
            {
                PlatformLocale locale;
                const std::size_t separator = tag.find('-');
                locale.language = tag.substr(0, separator);
                if (separator != std::string::npos)
                    locale.country = tag.substr(separator + 1);
                if (!locale.language.empty())
                    locales.push_back(std::move(locale));
            }
            start = end + 1;
        }
        return locales;
    }

    PowerInfo Win32SystemInfo::GetPowerInfo() const
    {
        SYSTEM_POWER_STATUS status{};
        if (GetSystemPowerStatus(&status) == FALSE)
            return PowerInfo{PowerState::Error, -1, -1};

        PowerInfo info;
        // 255 is the documented "unknown" for both fields, and passing it through would report a
        // 255% battery.
        info.percent = status.BatteryLifePercent == 255
                           ? -1
                           : static_cast<int>(status.BatteryLifePercent);
        info.secondsRemaining = status.BatteryLifeTime == static_cast<DWORD>(-1)
                                    ? -1
                                    : static_cast<int>(status.BatteryLifeTime);

        const bool noBattery = (status.BatteryFlag & 128) != 0;
        const bool charging = (status.BatteryFlag & 8) != 0;
        if (status.BatteryFlag == 255)
            info.state = PowerState::Unknown;
        else if (noBattery)
            info.state = PowerState::NoBattery;
        else if (status.ACLineStatus == 1)
            info.state = charging ? PowerState::Charging : PowerState::Charged;
        else if (status.ACLineStatus == 0)
            info.state = PowerState::OnBattery;
        else
            info.state = PowerState::Unknown;
        return info;
    }

    bool Win32SystemInfo::OpenUrl(const std::string& url)
    {
        if (url.empty())
            return false;
        const std::wstring wide = ToWide(url);
        const HINSTANCE result =
            ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        // ShellExecuteW returns a value above 32 on success; below that it is an error code
        // wearing an HINSTANCE's clothes.
        return reinterpret_cast<std::uintptr_t>(result) > 32;
    }

    // --- filesystem ----------------------------------------------------------------------------

    Win32FileSystem::Win32FileSystem()
        : standard_("cna-win32")
    {
    }

    std::string Win32FileSystem::GetBasePath() const
    {
        return standard_.GetBasePath();
    }

    std::string Win32FileSystem::GetPreferencesPath(const std::string& organization,
                                                    const std::string& application) const
    {
        std::string root = KnownFolder(FOLDERID_RoamingAppData);
        if (root.empty())
            return standard_.GetPreferencesPath(organization, application);

        // KnownFolder() already did the correct wide-to-UTF-8 conversion; re-reading its result
        // through the narrow path constructor undid it. organization and application are UTF-8 on
        // the same contract and may legitimately be non-ASCII.
        std::filesystem::path path = CNA::Internal::PathFromUtf8(root);
        if (!organization.empty())
            path /= CNA::Internal::PathFromUtf8(organization);
        path /= application.empty() ? std::filesystem::path("CNA")
                                    : CNA::Internal::PathFromUtf8(application);

        std::error_code error;
        std::filesystem::create_directories(path, error);
        if (error)
        {
            throw PlatformException("Win32FileSystem::GetPreferencesPath",
                                    CNA::Internal::PathToUtf8(path) + ": " + error.message());
        }

        std::string result = CNA::Internal::PathToUtf8(path);
        if (!result.empty() && result.back() != '\\' && result.back() != '/')
            result.push_back('\\');
        return result;
    }

    std::string Win32FileSystem::GetUserFolder(const UserFolder folder) const
    {
        switch (folder)
        {
            case UserFolder::Music:    return KnownFolder(FOLDERID_Music);
            case UserFolder::Pictures: return KnownFolder(FOLDERID_Pictures);
        }
        return {};
    }

    bool Win32FileSystem::TryLoadFile(const std::string& path,
                                      std::vector<std::uint8_t>& data) const
    {
        return standard_.TryLoadFile(path, data);
    }

    bool Win32FileSystem::TryLoadFileIgnoringCase(const std::string& path,
                                                  std::vector<std::uint8_t>& data) const
    {
        return standard_.TryLoadFileIgnoringCase(path, data);
    }

    void Win32FileSystem::CreateDirectory(const std::string& path)
    {
        standard_.CreateDirectory(path);
    }

} // namespace CNA::Platform::Win32
