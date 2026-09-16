// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/DefaultWindowTitle.hpp"

#include "CNA/AssemblyInfo.hpp"
#include "CNA/Internal/PathUtf8.hpp"

#if defined(_WIN32)
#include <filesystem>
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <vector>
#elif !defined(__EMSCRIPTEN__)
#include <unistd.h>
#include <vector>
#endif

namespace CNA::Internal
{
    namespace
    {
        // The path of the running executable, or an empty string where the platform has
        // no such thing. A wasm bundle has no executable file, so Emscripten takes the
        // last resort rather than inventing a name from the page.
        std::string executablePath()
        {
#if defined(__EMSCRIPTEN__)
            return {};
#elif defined(_WIN32)
            // GetModuleFileNameW, not ...A: the ANSI twin substitutes '?' for anything the code
            // page cannot spell, so a game installed under a non-ASCII directory used to show a
            // mangled window title. This was the only ANSI Win32 call left in the platform and
            // core modules.
            wchar_t buffer[MAX_PATH];
            const DWORD length = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
            if (length == 0 || length >= MAX_PATH) return {};
            return PathToUtf8(std::filesystem::path(std::wstring(buffer, length)));
#elif defined(__APPLE__)
            uint32_t size = 0;
            _NSGetExecutablePath(nullptr, &size);
            if (size == 0) return {};
            std::vector<char> buffer(size);
            if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
            return std::string(buffer.data());
#else
            std::vector<char> buffer(4096);
            const ssize_t length =
                ::readlink("/proc/self/exe", buffer.data(), buffer.size());
            if (length <= 0 || static_cast<std::size_t>(length) >= buffer.size()) return {};
            return std::string(buffer.data(), static_cast<std::size_t>(length));
#endif
        }

        std::string fileNameWithoutExtension(const std::string& path)
        {
            const std::size_t slash = path.find_last_of("/\\");
            std::string name =
                slash == std::string::npos ? path : path.substr(slash + 1);
            const std::size_t dot = name.find_last_of('.');
            // A leading dot is the whole name of a hidden file, not an extension.
            if (dot != std::string::npos && dot != 0) name.erase(dot);
            return name;
        }
    }

    std::string GetDefaultWindowTitle()
    {
        const std::string& declared = CNA::GetAssemblyTitleEXT();
        if (!declared.empty()) return declared;

        const std::string name = fileNameWithoutExtension(executablePath());
        if (!name.empty()) return name;

        return "Game";
    }
}
