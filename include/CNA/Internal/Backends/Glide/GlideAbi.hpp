#pragma once

#include "CNA/Internal/Backends/Glide/GlideAbiLoader.hpp"

#include <cstddef>
#include <cstdint>

namespace CNA::Internal::Backends::Glide::Abi
{
    using FxBool = std::uint32_t;
    using FxU32 = std::uint32_t;
    using FxI32 = std::int32_t;

    /**
     * The hand-declared subset of the public Glide 3.x ABI that the native backend uses. CNA
     * intentionally keeps this independent of a redistributed Glide SDK; the x86 fake-DLL
     * contract target compiles and exercises these declarations directly.
     */
    struct GlideTexInfo
    {
        FxI32 smallLodLog2;
        FxI32 largeLodLog2;
        FxI32 aspectRatioLog2;
        FxI32 format;
        void* data;
    };

    /** A historical Glide display mode returned by grQueryResolutions. */
    struct GlideResolution
    {
        FxI32 resolution;
        FxI32 refresh;
        FxI32 numColorBuffers;
        FxI32 numAuxBuffers;
    };

    /**
     * Post-transform vertex layout supplied to Glide's fixed-function rasterizer.
     *
     * The offsets are registered with grVertexLayout before the first draw and are therefore an
     * ABI contract, not an internal packing detail.
     */
    struct GlideVertex
    {
        float x;
        float y;
        float ooz;
        float oow;
        float r;
        float g;
        float b;
        float a;
        float z;
        float sow;
        float tow;
        float tmuOow;
    };

    static_assert(offsetof(GlideVertex, x) == 0);
    static_assert(offsetof(GlideVertex, r) == 16);
    static_assert(offsetof(GlideVertex, a) == 28);
    static_assert(offsetof(GlideVertex, sow) == 36);
#if !defined(_WIN64)
    static_assert(sizeof(GlideTexInfo) == 20);
    static_assert(sizeof(GlideResolution) == 16);
    static_assert(sizeof(GlideVertex) == 48);
#endif

    /**
     * Resolved function pointers for every Glide entry point used by the backend.
     *
     * This does not own the DLL. Keeping resolution separate lets the x86 contract executable
     * validate the complete renderer-facing export surface without linking CNA or SDL.
     */
    class GlideApiFunctions
    {
    public:
        using Context = void*;
        using GlideInitFn = void (WINAPI*)();
        using GlideShutdownFn = void (WINAPI*)();
        using QueryResolutionsFn = FxI32 (WINAPI*)(const GlideResolution*, GlideResolution*);
        using GetFn = FxI32 (WINAPI*)(FxI32, FxI32, FxI32*);
        using SstWinOpenFn = Context (WINAPI*)(FxU32, FxI32, FxI32, FxI32, FxI32, int, int);
        using SstWinCloseFn = FxBool (WINAPI*)(Context);
        using BufferClearFn = void (WINAPI*)(FxU32, std::uint8_t, std::uint16_t);
        using BufferSwapFn = void (WINAPI*)(FxU32);
        using RenderBufferFn = void (WINAPI*)(FxI32);
        using FinishFn = void (WINAPI*)();
        using ClipWindowFn = void (WINAPI*)(FxU32, FxU32, FxU32, FxU32);
        using CoordinateSpaceFn = void (WINAPI*)(FxU32);
        using CullModeFn = void (WINAPI*)(FxI32);
        using VertexLayoutFn = void (WINAPI*)(FxU32, FxI32, FxU32);
        using DrawTriangleFn = void (WINAPI*)(const void*, const void*, const void*);
        using DrawVertexArrayFn = void (WINAPI*)(FxI32, FxU32, void**);
        using DrawVertexArrayContiguousFn = void (WINAPI*)(FxI32, FxU32, void*, FxU32);
        using ColorCombineFn = void (WINAPI*)(FxI32, FxI32, FxI32, FxI32, FxBool);
        using AlphaCombineFn = void (WINAPI*)(FxI32, FxI32, FxI32, FxI32, FxBool);
        using AlphaTestFunctionFn = void (WINAPI*)(FxI32);
        using AlphaTestReferenceValueFn = void (WINAPI*)(std::uint8_t);
        using AlphaBlendFunctionFn = void (WINAPI*)(FxI32, FxI32, FxI32, FxI32);
        using ColorMaskFn = void (WINAPI*)(FxBool, FxBool);
        using DepthBufferModeFn = void (WINAPI*)(FxI32);
        using DepthBufferFunctionFn = void (WINAPI*)(FxI32);
        using DepthMaskFn = void (WINAPI*)(FxBool);
        using TexMinAddressFn = FxU32 (WINAPI*)(FxI32);
        using TexMaxAddressFn = FxU32 (WINAPI*)(FxI32);
        using TexTextureMemRequiredFn = FxU32 (WINAPI*)(FxU32, GlideTexInfo*);
        using TexDownloadMipMapFn = void (WINAPI*)(FxI32, FxU32, FxU32, GlideTexInfo*);
        using TexSourceFn = void (WINAPI*)(FxI32, FxU32, FxU32, GlideTexInfo*);
        using TexFilterModeFn = void (WINAPI*)(FxI32, FxI32, FxI32);
        using TexClampModeFn = void (WINAPI*)(FxI32, FxI32, FxI32);
        using TexMipMapModeFn = void (WINAPI*)(FxI32, FxI32, FxBool);
        using TexLodBiasValueFn = void (WINAPI*)(FxI32, float);
        using TexCombineFn = void (WINAPI*)(FxI32, FxI32, FxI32, FxI32, FxI32, FxBool, FxBool);
        using LfbReadRegionFn = FxBool (WINAPI*)(FxI32, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
        using GetStringFn = const char* (WINAPI*)(FxU32);
        /** Matches the documented GrProc typedef: callers reinterpret_cast to the real signature. */
        using GetProcAddressFn = int (WINAPI*)();
        using GetProcAddressLookupFn = GetProcAddressFn (WINAPI*)(const char*);

        /** Resolves every renderer-facing Glide export from an already-loaded module. */
        void Resolve(HMODULE module)
        {
            grGlideInit = Required<GlideInitFn>(module, "grGlideInit", 0);
            grGlideShutdown = Required<GlideShutdownFn>(module, "grGlideShutdown", 0);
            grQueryResolutions = Required<QueryResolutionsFn>(module, "grQueryResolutions", 8);
            grGet = Required<GetFn>(module, "grGet", 12);
            grSstWinOpen = Required<SstWinOpenFn>(module, "grSstWinOpen", 28);
            grSstWinClose = Required<SstWinCloseFn>(module, "grSstWinClose", 4);
            grBufferClear = Required<BufferClearFn>(module, "grBufferClear", 12);
            grBufferSwap = Required<BufferSwapFn>(module, "grBufferSwap", 4);
            grRenderBuffer = Required<RenderBufferFn>(module, "grRenderBuffer", 4);
            grFinish = Required<FinishFn>(module, "grFinish", 0);
            grClipWindow = Required<ClipWindowFn>(module, "grClipWindow", 16);
            grCoordinateSpace = Required<CoordinateSpaceFn>(module, "grCoordinateSpace", 4);
            grCullMode = Required<CullModeFn>(module, "grCullMode", 4);
            grVertexLayout = Required<VertexLayoutFn>(module, "grVertexLayout", 12);
            grDrawTriangle = Required<DrawTriangleFn>(module, "grDrawTriangle", 12);
            grDrawVertexArray = Required<DrawVertexArrayFn>(module, "grDrawVertexArray", 12);
            grDrawVertexArrayContiguous = Required<DrawVertexArrayContiguousFn>(module, "grDrawVertexArrayContiguous", 16);
            grColorCombine = Required<ColorCombineFn>(module, "grColorCombine", 20);
            grAlphaCombine = Required<AlphaCombineFn>(module, "grAlphaCombine", 20);
            grAlphaTestFunction = Required<AlphaTestFunctionFn>(module, "grAlphaTestFunction", 4);
            grAlphaTestReferenceValue = Required<AlphaTestReferenceValueFn>(module, "grAlphaTestReferenceValue", 4);
            grAlphaBlendFunction = Required<AlphaBlendFunctionFn>(module, "grAlphaBlendFunction", 16);
            grColorMask = Required<ColorMaskFn>(module, "grColorMask", 8);
            grDepthBufferMode = Required<DepthBufferModeFn>(module, "grDepthBufferMode", 4);
            grDepthBufferFunction = Required<DepthBufferFunctionFn>(module, "grDepthBufferFunction", 4);
            grDepthMask = Required<DepthMaskFn>(module, "grDepthMask", 4);
            grTexMinAddress = Required<TexMinAddressFn>(module, "grTexMinAddress", 4);
            grTexMaxAddress = Required<TexMaxAddressFn>(module, "grTexMaxAddress", 4);
            grTexTextureMemRequired = Required<TexTextureMemRequiredFn>(module, "grTexTextureMemRequired", 8);
            grTexDownloadMipMap = Required<TexDownloadMipMapFn>(module, "grTexDownloadMipMap", 16);
            grTexSource = Required<TexSourceFn>(module, "grTexSource", 16);
            grTexFilterMode = Required<TexFilterModeFn>(module, "grTexFilterMode", 12);
            grTexClampMode = Required<TexClampModeFn>(module, "grTexClampMode", 12);
            grTexMipMapMode = Required<TexMipMapModeFn>(module, "grTexMipMapMode", 12);
            grTexLodBiasValue = Required<TexLodBiasValueFn>(module, "grTexLodBiasValue", 8);
            grTexCombine = Required<TexCombineFn>(module, "grTexCombine", 28);
            grLfbReadRegion = Required<LfbReadRegionFn>(module, "grLfbReadRegion", 28);
            grGetString = Required<GetStringFn>(module, "grGetString", 4);
            grGetProcAddress = Required<GetProcAddressLookupFn>(module, "grGetProcAddress", 4);
        }

        GlideInitFn grGlideInit = nullptr;
        GlideShutdownFn grGlideShutdown = nullptr;
        QueryResolutionsFn grQueryResolutions = nullptr;
        GetFn grGet = nullptr;
        SstWinOpenFn grSstWinOpen = nullptr;
        SstWinCloseFn grSstWinClose = nullptr;
        BufferClearFn grBufferClear = nullptr;
        BufferSwapFn grBufferSwap = nullptr;
        RenderBufferFn grRenderBuffer = nullptr;
        FinishFn grFinish = nullptr;
        ClipWindowFn grClipWindow = nullptr;
        CoordinateSpaceFn grCoordinateSpace = nullptr;
        CullModeFn grCullMode = nullptr;
        VertexLayoutFn grVertexLayout = nullptr;
        DrawTriangleFn grDrawTriangle = nullptr;
        DrawVertexArrayFn grDrawVertexArray = nullptr;
        DrawVertexArrayContiguousFn grDrawVertexArrayContiguous = nullptr;
        ColorCombineFn grColorCombine = nullptr;
        AlphaCombineFn grAlphaCombine = nullptr;
        AlphaTestFunctionFn grAlphaTestFunction = nullptr;
        AlphaTestReferenceValueFn grAlphaTestReferenceValue = nullptr;
        AlphaBlendFunctionFn grAlphaBlendFunction = nullptr;
        ColorMaskFn grColorMask = nullptr;
        DepthBufferModeFn grDepthBufferMode = nullptr;
        DepthBufferFunctionFn grDepthBufferFunction = nullptr;
        DepthMaskFn grDepthMask = nullptr;
        TexMinAddressFn grTexMinAddress = nullptr;
        TexMaxAddressFn grTexMaxAddress = nullptr;
        TexTextureMemRequiredFn grTexTextureMemRequired = nullptr;
        TexDownloadMipMapFn grTexDownloadMipMap = nullptr;
        TexSourceFn grTexSource = nullptr;
        TexFilterModeFn grTexFilterMode = nullptr;
        TexClampModeFn grTexClampMode = nullptr;
        TexMipMapModeFn grTexMipMapMode = nullptr;
        TexLodBiasValueFn grTexLodBiasValue = nullptr;
        TexCombineFn grTexCombine = nullptr;
        LfbReadRegionFn grLfbReadRegion = nullptr;
        GetStringFn grGetString = nullptr;
        GetProcAddressLookupFn grGetProcAddress = nullptr;

    private:
        template <typename T>
        [[nodiscard]] static T Required(HMODULE module, const char* name, unsigned int stdcallBytes)
        {
            return reinterpret_cast<T>(ResolveGlideExport(module, name, stdcallBytes));
        }
    };
} // namespace CNA::Internal::Backends::Glide::Abi
