#include "CNA/Internal/Backends/Glide/GlideAbiLoader.hpp"

#include <cstdint>
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
        using VoidFn = void (WINAPI*)();
        using WinOpenFn = void* (WINAPI*)(std::uint32_t, int, int, int, int, int, int);
        using VertexLayoutFn = void (WINAPI*)(std::uint32_t, int, std::uint32_t);
        using ColorCombineFn = void (WINAPI*)(int, int, int, int, std::uint32_t);
        using TexSourceFn = void (WINAPI*)(int, std::uint32_t, std::uint32_t, void*);
        using ClearFn = void (WINAPI*)(std::uint32_t, std::uint8_t, std::uint16_t);
        using ReadbackFn = std::uint32_t (WINAPI*)(int, std::uint32_t, std::uint32_t,
                                                    std::uint32_t, std::uint32_t, std::uint32_t, void*);
        using MaskFn = unsigned int (WINAPI*)();
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
        const auto resolve = [&](const char* name, unsigned int bytes)
        {
            return CNA::Internal::Backends::Glide::ResolveGlideExport(module, name, bytes);
        };
        reinterpret_cast<VoidFn>(resolve("FakeGlideReset", 0))();
        reinterpret_cast<VoidFn>(resolve("grGlideInit", 0))();
        if (reinterpret_cast<WinOpenFn>(resolve("grSstWinOpen", 28))(0, 7, 0, 0, 0, 2, 1) == nullptr)
        {
            FreeLibrary(module);
            return 5;
        }
        reinterpret_cast<VertexLayoutFn>(resolve("grVertexLayout", 12))(1, 0, 1);
        reinterpret_cast<ColorCombineFn>(resolve("grColorCombine", 20))(1, 0, 0, 2, 0);
        reinterpret_cast<TexSourceFn>(resolve("grTexSource", 16))(0, 0, 3, nullptr);
        reinterpret_cast<ClearFn>(resolve("grBufferClear", 12))(0, 0, 0);
        if (reinterpret_cast<ReadbackFn>(resolve("grLfbReadRegion", 28))(0, 0, 0, 1, 1, 2, nullptr) != 1)
        {
            FreeLibrary(module);
            return 6;
        }
        reinterpret_cast<VoidFn>(resolve("grGlideShutdown", 0))();
        constexpr unsigned int expectedMask = 0xff;
        if (reinterpret_cast<MaskFn>(resolve("FakeGlideCallMask", 0))() != expectedMask)
        {
            std::fputs("fake Glide recorder did not observe the complete ABI call sequence\n", stderr);
            FreeLibrary(module);
            return 7;
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
