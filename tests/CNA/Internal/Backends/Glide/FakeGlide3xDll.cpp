#include <windows.h>

#include <cstdint>

namespace
{
    unsigned int g_callMask = 0;
    constexpr unsigned int kInit = 1u << 0;
    constexpr unsigned int kWinOpen = 1u << 1;
    constexpr unsigned int kVertexLayout = 1u << 2;
    constexpr unsigned int kColorCombine = 1u << 3;
    constexpr unsigned int kTexSource = 1u << 4;
    constexpr unsigned int kClear = 1u << 5;
    constexpr unsigned int kReadback = 1u << 6;
    constexpr unsigned int kShutdown = 1u << 7;
}

extern "C" __declspec(dllexport) int GlideAbiPlainProbe()
{
    return 17;
}

extern "C" __declspec(dllexport) int WINAPI GlideAbiStdcallProbe(int value)
{
    return value + 5;
}

extern "C" __declspec(dllexport) void WINAPI FakeGlideReset() { g_callMask = 0; }
extern "C" __declspec(dllexport) unsigned int WINAPI FakeGlideCallMask() { return g_callMask; }

extern "C" __declspec(dllexport) void WINAPI grGlideInit() { g_callMask |= kInit; }
extern "C" __declspec(dllexport) void WINAPI grGlideShutdown() { g_callMask |= kShutdown; }
extern "C" __declspec(dllexport) int WINAPI grQueryResolutions(const void*, void*) { return 0; }
extern "C" __declspec(dllexport) int WINAPI grGet(int selector, int length, int* value)
{
    if (value != nullptr && length == static_cast<int>(sizeof(int)))
    {
        *value = selector == 0x13 ? 1 : 256;
        return length;
    }
    return 0;
}
extern "C" __declspec(dllexport) void* WINAPI grSstWinOpen(std::uint32_t, int, int, int, int, int, int)
{
    g_callMask |= kWinOpen;
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(1));
}
extern "C" __declspec(dllexport) std::uint32_t WINAPI grSstWinClose(void*) { return 1; }
extern "C" __declspec(dllexport) void WINAPI grBufferClear(std::uint32_t, std::uint8_t, std::uint16_t) { g_callMask |= kClear; }
extern "C" __declspec(dllexport) void WINAPI grBufferSwap(std::uint32_t) {}
extern "C" __declspec(dllexport) void WINAPI grRenderBuffer(int) {}
extern "C" __declspec(dllexport) void WINAPI grFinish() {}
extern "C" __declspec(dllexport) void WINAPI grClipWindow(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) {}
extern "C" __declspec(dllexport) void WINAPI grCoordinateSpace(std::uint32_t) {}
extern "C" __declspec(dllexport) void WINAPI grCullMode(int) {}
extern "C" __declspec(dllexport) void WINAPI grVertexLayout(std::uint32_t, int, std::uint32_t) { g_callMask |= kVertexLayout; }
extern "C" __declspec(dllexport) void WINAPI grDrawTriangle(const void*, const void*, const void*) {}
extern "C" __declspec(dllexport) void WINAPI grDrawVertexArray(int, std::uint32_t, void**) {}
extern "C" __declspec(dllexport) void WINAPI grDrawVertexArrayContiguous(int, std::uint32_t, void*, std::uint32_t) {}
extern "C" __declspec(dllexport) void WINAPI grColorCombine(int, int, int, int, std::uint32_t) { g_callMask |= kColorCombine; }
extern "C" __declspec(dllexport) void WINAPI grAlphaCombine(int, int, int, int, std::uint32_t) {}
extern "C" __declspec(dllexport) void WINAPI grAlphaTestFunction(int) {}
extern "C" __declspec(dllexport) void WINAPI grAlphaTestReferenceValue(std::uint8_t) {}
extern "C" __declspec(dllexport) void WINAPI grAlphaBlendFunction(int, int, int, int) {}
extern "C" __declspec(dllexport) void WINAPI grColorMask(std::uint32_t, std::uint32_t) {}
extern "C" __declspec(dllexport) void WINAPI grDepthBufferMode(int) {}
extern "C" __declspec(dllexport) void WINAPI grDepthBufferFunction(int) {}
extern "C" __declspec(dllexport) void WINAPI grDepthMask(std::uint32_t) {}
extern "C" __declspec(dllexport) std::uint32_t WINAPI grTexMinAddress(int) { return 0; }
extern "C" __declspec(dllexport) std::uint32_t WINAPI grTexMaxAddress(int) { return 4u * 1024u * 1024u; }
extern "C" __declspec(dllexport) std::uint32_t WINAPI grTexTextureMemRequired(std::uint32_t, void*) { return 0; }
extern "C" __declspec(dllexport) void WINAPI grTexDownloadMipMap(int, std::uint32_t, std::uint32_t, void*) {}
extern "C" __declspec(dllexport) void WINAPI grTexSource(int, std::uint32_t, std::uint32_t, void*) { g_callMask |= kTexSource; }
extern "C" __declspec(dllexport) void WINAPI grTexFilterMode(int, int, int) {}
extern "C" __declspec(dllexport) void WINAPI grTexClampMode(int, int, int) {}
extern "C" __declspec(dllexport) void WINAPI grTexMipMapMode(int, int, std::uint32_t) {}
extern "C" __declspec(dllexport) void WINAPI grTexLodBiasValue(int, float) {}
extern "C" __declspec(dllexport) void WINAPI grTexCombine(int, int, int, int, int, std::uint32_t, std::uint32_t) {}
extern "C" __declspec(dllexport) std::uint32_t WINAPI grLfbReadRegion(int, std::uint32_t, std::uint32_t,
                                                                         std::uint32_t, std::uint32_t, std::uint32_t, void*)
{
    g_callMask |= kReadback;
    return 1;
}
