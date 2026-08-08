#include "CNA/Internal/Backends/Glide/GlideAbi.hpp"

#include <array>
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
        using MaskFn = std::uint64_t (WINAPI*)();
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

        reinterpret_cast<VoidFn>(
            CNA::Internal::Backends::Glide::ResolveGlideExport(module, "FakeGlideReset", 0))();
        CNA::Internal::Backends::Glide::Abi::GlideApiFunctions glide;
        glide.Resolve(module);

        using namespace CNA::Internal::Backends::Glide::Abi;
        GlideResolution resolution{};
        FxI32 capability = 0;
        GlideTexInfo textureInfo{0, 0, 0, 0, nullptr};
        GlideVertex vertex{};
        void* vertexPointers[] = {&vertex};
        std::array<std::uint8_t, 2> readback{};

        glide.grGlideInit();
        glide.grQueryResolutions(nullptr, &resolution);
        if (glide.grGet(0x13, static_cast<FxI32>(sizeof(capability)), &capability) != sizeof(capability) ||
            capability != 1)
        {
            std::fputs("fake Glide grGet contract returned an unexpected value\n", stderr);
            FreeLibrary(module);
            return 5;
        }
        const GlideApiFunctions::Context context = glide.grSstWinOpen(0, 7, 0, 0, 0, 2, 1);
        if (context == nullptr)
        {
            FreeLibrary(module);
            return 6;
        }
        glide.grBufferClear(0, 0, 0);
        glide.grBufferSwap(0);
        glide.grRenderBuffer(1);
        glide.grFinish();
        glide.grClipWindow(0, 0, 1, 1);
        glide.grCoordinateSpace(0);
        glide.grCullMode(0);
        glide.grVertexLayout(1, 0, 1);
        glide.grDrawTriangle(&vertex, &vertex, &vertex);
        glide.grDrawVertexArray(6, 1, vertexPointers);
        glide.grDrawVertexArrayContiguous(6, 1, &vertex, sizeof(vertex));
        glide.grColorCombine(1, 0, 0, 2, 0);
        glide.grAlphaCombine(1, 0, 0, 2, 0);
        glide.grAlphaTestFunction(7);
        glide.grAlphaTestReferenceValue(0);
        glide.grAlphaBlendFunction(4, 0, 4, 0);
        glide.grColorMask(1, 1);
        glide.grDepthBufferMode(1);
        glide.grDepthBufferFunction(3);
        glide.grDepthMask(1);
        if (glide.grTexMinAddress(0) != 0 || glide.grTexMaxAddress(0) != 4u * 1024u * 1024u ||
            glide.grTexTextureMemRequired(0, &textureInfo) != 4)
        {
            std::fputs("fake Glide texture-memory contract returned an unexpected value\n", stderr);
            FreeLibrary(module);
            return 7;
        }
        glide.grTexDownloadMipMap(0, 0, 3, &textureInfo);
        glide.grTexSource(0, 0, 3, &textureInfo);
        glide.grTexFilterMode(0, 0, 0);
        glide.grTexClampMode(0, 1, 1);
        glide.grTexMipMapMode(0, 1, 1);
        glide.grTexLodBiasValue(0, 0.0f);
        glide.grTexCombine(0, 1, 8, 1, 8, 0, 0);
        if (glide.grLfbReadRegion(0, 0, 0, 1, 1, 2, readback.data()) != 1)
        {
            FreeLibrary(module);
            return 8;
        }
        const char* extensionList = glide.grGetString(0xa0);
        const char* hardwareName = glide.grGetString(0xa1);
        if (extensionList == nullptr || hardwareName == nullptr)
        {
            std::fputs("fake Glide grGetString contract returned an unexpected value\n", stderr);
            FreeLibrary(module);
            return 11;
        }
        static_cast<void>(glide.grGetProcAddress("grChromaRangeExt"));
        if (glide.grSstWinClose(context) != 1)
        {
            FreeLibrary(module);
            return 9;
        }
        glide.grGlideShutdown();

        constexpr std::uint64_t expectedMask = (std::uint64_t{1} << 39) - 1;
        const auto callMask = reinterpret_cast<MaskFn>(
            CNA::Internal::Backends::Glide::ResolveGlideExport(module, "FakeGlideCallMask", 0));
        if (callMask() != expectedMask)
        {
            std::fputs("fake Glide recorder did not observe every renderer-facing ABI call\n", stderr);
            FreeLibrary(module);
            return 10;
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
