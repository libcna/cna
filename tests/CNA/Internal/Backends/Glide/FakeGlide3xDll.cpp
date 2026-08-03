#include <windows.h>

#include <cstdint>

namespace
{
    enum class Call : unsigned int
    {
        Init, Shutdown, QueryResolutions, Get, WinOpen, WinClose, BufferClear, BufferSwap,
        RenderBuffer, Finish, ClipWindow, CoordinateSpace, CullMode, VertexLayout, DrawTriangle,
        DrawVertexArray, DrawVertexArrayContiguous, ColorCombine, AlphaCombine, AlphaTestFunction,
        AlphaTestReferenceValue, AlphaBlendFunction, ColorMask, DepthBufferMode,
        DepthBufferFunction, DepthMask, TexMinAddress, TexMaxAddress, TexTextureMemRequired,
        TexDownloadMipMap, TexSource, TexFilterMode, TexClampMode, TexMipMapMode, TexLodBiasValue,
        TexCombine, LfbReadRegion, GetString, GetProcAddress, Count,
    };

    std::uint64_t g_callMask = 0;

    void Record(Call call)
    {
        g_callMask |= std::uint64_t{1} << static_cast<unsigned int>(call);
    }
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
extern "C" __declspec(dllexport) std::uint64_t WINAPI FakeGlideCallMask() { return g_callMask; }

extern "C" __declspec(dllexport) void WINAPI grGlideInit() { Record(Call::Init); }
extern "C" __declspec(dllexport) void WINAPI grGlideShutdown() { Record(Call::Shutdown); }
extern "C" __declspec(dllexport) int WINAPI grQueryResolutions(const void*, void*) { Record(Call::QueryResolutions); return 0; }
extern "C" __declspec(dllexport) int WINAPI grGet(int selector, int length, int* value)
{
    Record(Call::Get);
    if (value != nullptr && length == static_cast<int>(sizeof(int)))
    {
        *value = selector == 0x13 ? 1 : 256;
        return length;
    }
    return 0;
}
extern "C" __declspec(dllexport) void* WINAPI grSstWinOpen(std::uint32_t, int, int, int, int, int, int)
{
    Record(Call::WinOpen);
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(1));
}
extern "C" __declspec(dllexport) std::uint32_t WINAPI grSstWinClose(void*) { Record(Call::WinClose); return 1; }
extern "C" __declspec(dllexport) void WINAPI grBufferClear(std::uint32_t, std::uint8_t, std::uint16_t) { Record(Call::BufferClear); }
extern "C" __declspec(dllexport) void WINAPI grBufferSwap(std::uint32_t) { Record(Call::BufferSwap); }
extern "C" __declspec(dllexport) void WINAPI grRenderBuffer(int) { Record(Call::RenderBuffer); }
extern "C" __declspec(dllexport) void WINAPI grFinish() { Record(Call::Finish); }
extern "C" __declspec(dllexport) void WINAPI grClipWindow(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) { Record(Call::ClipWindow); }
extern "C" __declspec(dllexport) void WINAPI grCoordinateSpace(std::uint32_t) { Record(Call::CoordinateSpace); }
extern "C" __declspec(dllexport) void WINAPI grCullMode(int) { Record(Call::CullMode); }
extern "C" __declspec(dllexport) void WINAPI grVertexLayout(std::uint32_t, int, std::uint32_t) { Record(Call::VertexLayout); }
extern "C" __declspec(dllexport) void WINAPI grDrawTriangle(const void*, const void*, const void*) { Record(Call::DrawTriangle); }
extern "C" __declspec(dllexport) void WINAPI grDrawVertexArray(int, std::uint32_t, void**) { Record(Call::DrawVertexArray); }
extern "C" __declspec(dllexport) void WINAPI grDrawVertexArrayContiguous(int, std::uint32_t, void*, std::uint32_t) { Record(Call::DrawVertexArrayContiguous); }
extern "C" __declspec(dllexport) void WINAPI grColorCombine(int, int, int, int, std::uint32_t) { Record(Call::ColorCombine); }
extern "C" __declspec(dllexport) void WINAPI grAlphaCombine(int, int, int, int, std::uint32_t) { Record(Call::AlphaCombine); }
extern "C" __declspec(dllexport) void WINAPI grAlphaTestFunction(int) { Record(Call::AlphaTestFunction); }
extern "C" __declspec(dllexport) void WINAPI grAlphaTestReferenceValue(std::uint8_t) { Record(Call::AlphaTestReferenceValue); }
extern "C" __declspec(dllexport) void WINAPI grAlphaBlendFunction(int, int, int, int) { Record(Call::AlphaBlendFunction); }
extern "C" __declspec(dllexport) void WINAPI grColorMask(std::uint32_t, std::uint32_t) { Record(Call::ColorMask); }
extern "C" __declspec(dllexport) void WINAPI grDepthBufferMode(int) { Record(Call::DepthBufferMode); }
extern "C" __declspec(dllexport) void WINAPI grDepthBufferFunction(int) { Record(Call::DepthBufferFunction); }
extern "C" __declspec(dllexport) void WINAPI grDepthMask(std::uint32_t) { Record(Call::DepthMask); }
extern "C" __declspec(dllexport) std::uint32_t WINAPI grTexMinAddress(int) { Record(Call::TexMinAddress); return 0; }
extern "C" __declspec(dllexport) std::uint32_t WINAPI grTexMaxAddress(int) { Record(Call::TexMaxAddress); return 4u * 1024u * 1024u; }
extern "C" __declspec(dllexport) std::uint32_t WINAPI grTexTextureMemRequired(std::uint32_t, void*) { Record(Call::TexTextureMemRequired); return 4; }
extern "C" __declspec(dllexport) void WINAPI grTexDownloadMipMap(int, std::uint32_t, std::uint32_t, void*) { Record(Call::TexDownloadMipMap); }
extern "C" __declspec(dllexport) void WINAPI grTexSource(int, std::uint32_t, std::uint32_t, void*) { Record(Call::TexSource); }
extern "C" __declspec(dllexport) void WINAPI grTexFilterMode(int, int, int) { Record(Call::TexFilterMode); }
extern "C" __declspec(dllexport) void WINAPI grTexClampMode(int, int, int) { Record(Call::TexClampMode); }
extern "C" __declspec(dllexport) void WINAPI grTexMipMapMode(int, int, std::uint32_t) { Record(Call::TexMipMapMode); }
extern "C" __declspec(dllexport) void WINAPI grTexLodBiasValue(int, float) { Record(Call::TexLodBiasValue); }
extern "C" __declspec(dllexport) void WINAPI grTexCombine(int, int, int, int, int, std::uint32_t, std::uint32_t) { Record(Call::TexCombine); }
extern "C" __declspec(dllexport) std::uint32_t WINAPI grLfbReadRegion(int, std::uint32_t, std::uint32_t,
                                                                         std::uint32_t, std::uint32_t, std::uint32_t, void*)
{
    Record(Call::LfbReadRegion);
    return 1;
}
extern "C" __declspec(dllexport) const char* WINAPI grGetString(std::uint32_t selector)
{
    Record(Call::GetString);
    switch (selector)
    {
        case 0xa0: return " "; // GR_EXTENSION: this fake advertises no optional extensions.
        case 0xa1: return "Voodoo2";
        case 0xa2: return "Glide";
        case 0xa3: return "3Dfx Interactive";
        case 0xa4: return "3.0";
        default: return nullptr;
    }
}
// Matches the documented GrProc typedef (int (__stdcall *)()) that grGetProcAddress returns.
using FakeGrProc = int(WINAPI*)();
extern "C" __declspec(dllexport) FakeGrProc WINAPI grGetProcAddress(const char*)
{
    Record(Call::GetProcAddress);
    return nullptr; // No extension functions are consumed yet; every lookup legitimately misses.
}
