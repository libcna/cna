#include "CNA/Internal/Backends/Glide/GlideAbiLoader.hpp"

#include <cstdio>

int main()
{
    HMODULE module = LoadLibraryA("fake_glide3x.dll");
    if (module == nullptr)
    {
        std::fputs("failed to load fake_glide3x.dll\n", stderr);
        return 1;
    }
    try
    {
        using PlainFn = int (*)();
        using StdcallFn = int (WINAPI*)(int);
        const auto plain = reinterpret_cast<PlainFn>(
            CNA::Internal::Backends::Glide::ResolveGlideExport(module, "GlideAbiPlainProbe", 0));
        const auto stdcall = reinterpret_cast<StdcallFn>(
            CNA::Internal::Backends::Glide::ResolveGlideExport(module, "GlideAbiStdcallProbe", 4));
        if (plain() != 17 || stdcall(12) != 17)
        {
            std::fputs("fake Glide ABI probe returned an unexpected value\n", stderr);
            FreeLibrary(module);
            return 2;
        }
        bool missingRejected = false;
        try
        {
            static_cast<void>(CNA::Internal::Backends::Glide::ResolveGlideExport(module, "MissingGlideProbe", 0));
        }
        catch (const std::runtime_error&)
        {
            missingRejected = true;
        }
        FreeLibrary(module);
        return missingRejected ? 0 : 3;
    }
    catch (...)
    {
        FreeLibrary(module);
        return 4;
    }
}
