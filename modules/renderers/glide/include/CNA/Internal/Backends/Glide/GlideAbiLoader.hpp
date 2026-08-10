#pragma once

#include <windows.h>

#include <stdexcept>
#include <string>

namespace CNA::Internal::Backends::Glide
{
    /** Resolves both normal and historical x86 stdcall Glide export spellings. */
    [[nodiscard]] inline FARPROC ResolveGlideExport(HMODULE module, const char* name,
                                                     unsigned int stdcallBytes)
    {
        FARPROC procedure = GetProcAddress(module, name);
#if !defined(_WIN64)
        if (procedure == nullptr)
        {
            const std::string decorated = "_" + std::string(name) + "@" + std::to_string(stdcallBytes);
            procedure = GetProcAddress(module, decorated.c_str());
        }
        if (procedure == nullptr)
        {
            const std::string decorated = std::string(name) + "@" + std::to_string(stdcallBytes);
            procedure = GetProcAddress(module, decorated.c_str());
        }
#else
        static_cast<void>(stdcallBytes);
#endif
        if (procedure == nullptr)
        {
            throw std::runtime_error(
                std::string("The loaded glide3x.dll does not export required Glide 3.x function '") + name + "'.");
        }
        return procedure;
    }
} // namespace CNA::Internal::Backends::Glide
