#include "CNA/Internal/Backends/Glide/GlideGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Glide/GlideVertexLayout.hpp"
#include "CNA/Internal/Graphics/BuiltInVertexStreams.hpp"

#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"

#include <SDL3/SDL.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace CNA::Internal::Backends::Glide
{
    namespace
    {
        using FxBool = std::uint32_t;
        using FxU32 = std::uint32_t;
        using FxI32 = std::int32_t;

        constexpr FxBool kFxFalse = 0;
        constexpr FxBool kFxTrue = 1;

        // The values below are part of the public Glide 3.x ABI. Keep this deliberately small
        // hand-written declaration set instead of vendoring Glide headers: dgVoodoo2 remains an
        // external runtime and CNA must not inherit or redistribute its SDK/license material.
        constexpr FxI32 kResolution640x480 = 0x7;
        constexpr FxI32 kResolution800x600 = 0x8;
        constexpr FxI32 kRefresh60Hz = 0x0;
        constexpr FxI32 kColorFormatArgb = 0x0;
        constexpr FxI32 kOriginUpperLeft = 0x0;
        constexpr FxI32 kBufferBack = 0x1;
        constexpr FxI32 kCullDisable = 0x0;
        constexpr FxI32 kCullNegative = 0x1;
        constexpr FxI32 kCullPositive = 0x2;
        constexpr FxI32 kPrimitiveTriangles = 0x6;
        constexpr FxU32 kWindowCoords = 0x00;
        constexpr FxI32 kQueryAny = -1;

        constexpr FxU32 kParamXY = 0x01;
        constexpr FxU32 kParamZ = 0x02;
        constexpr FxU32 kParamQ = 0x04;
        constexpr FxU32 kParamA = 0x10;
        constexpr FxU32 kParamRgb = 0x20;
        constexpr FxU32 kParamSt0 = 0x40;
        constexpr FxU32 kParamEnable = 0x01;

        constexpr FxI32 kCombineFunctionLocal = 0x1;
        constexpr FxI32 kCombineFunctionScaleOther = 0x3;
        constexpr FxI32 kCombineFactorZero = 0x0;
        constexpr FxI32 kCombineFactorLocal = 0x1;
        constexpr FxI32 kCombineFactorOne = 0x8;
        constexpr FxI32 kCombineLocalIterated = 0x0;
        constexpr FxI32 kCombineOtherTexture = 0x1;
        constexpr FxI32 kCombineOtherNone = 0x2;

        constexpr FxI32 kDepthBufferDisable = 0x0;
        constexpr FxI32 kDepthBufferZ = 0x1;
        constexpr FxI32 kDepthCompareNever = 0x0;
        constexpr FxI32 kDepthCompareLess = 0x1;
        constexpr FxI32 kDepthCompareEqual = 0x2;
        constexpr FxI32 kDepthCompareLessEqual = 0x3;
        constexpr FxI32 kDepthCompareGreater = 0x4;
        constexpr FxI32 kDepthCompareNotEqual = 0x5;
        constexpr FxI32 kDepthCompareGreaterEqual = 0x6;
        constexpr FxI32 kDepthCompareAlways = 0x7;

        constexpr FxI32 kTexFilterPoint = 0x0;
        constexpr FxI32 kTexFilterBilinear = 0x1;
        constexpr FxI32 kTexClampWrap = 0x0;
        constexpr FxI32 kTexClampClamp = 0x1;
        constexpr FxI32 kTexClampMirror = 0x2;
        constexpr FxI32 kTexFormatArgb4444 = 0xc;
        constexpr FxI32 kMipMapNearest = 0x1;
        constexpr FxU32 kMipMapBoth = 0x3;
        constexpr FxU32 kLfbSrc565 = 0x0;

        constexpr FxI32 kGetMaxTextureSize = 0x0a;
        constexpr FxI32 kGetMaxTextureAspectRatio = 0x0b;
        constexpr FxI32 kGetNumTmu = 0x13;

        constexpr FxI32 kBlendZero = 0x0;
        constexpr FxI32 kBlendSourceAlpha = 0x1;
        constexpr FxI32 kBlendSourceColor = 0x2;
        constexpr FxI32 kBlendDestinationAlpha = 0x3;
        constexpr FxI32 kBlendOne = 0x4;
        constexpr FxI32 kBlendInverseSourceAlpha = 0x5;
        constexpr FxI32 kBlendInverseSourceColor = 0x6;
        constexpr FxI32 kBlendInverseDestinationAlpha = 0x7;
        constexpr FxI32 kBlendAlphaSaturate = 0xf;

        struct GlideTexInfo
        {
            FxI32 smallLodLog2;
            FxI32 largeLodLog2;
            FxI32 aspectRatioLog2;
            FxI32 format;
            void* data;
        };

        struct GlideResolution
        {
            FxI32 resolution;
            FxI32 refresh;
            FxI32 numColorBuffers;
            FxI32 numAuxBuffers;
        };

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
        // The loader declarations intentionally do not vendor a Glide SDK. Keep the ABI-critical
        // data layouts checked in the x86 target that is allowed to load this backend.
        static_assert(sizeof(GlideTexInfo) == 20);
        static_assert(sizeof(GlideResolution) == 16);
        static_assert(sizeof(GlideVertex) == 48);
#endif

        [[nodiscard]] std::string LastWin32Error(const char* action)
        {
            const DWORD error = GetLastError();
            char* rawMessage = nullptr;
            const DWORD length = FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr, error, 0, reinterpret_cast<LPSTR>(&rawMessage), 0, nullptr);
            std::string message = std::string(action) + " failed (Win32 error " + std::to_string(error) + ")";
            if (length != 0 && rawMessage != nullptr)
            {
                message += ": ";
                message.append(rawMessage, length);
                LocalFree(rawMessage);
            }
            return message;
        }

        [[noreturn]] void ThrowUnsupported(const char* methodName)
        {
            throw std::runtime_error(std::string("GLIDE backend does not support this CNA operation: ") + methodName);
        }

        [[nodiscard]] int NextPowerOfTwo(int value, int maximum)
        {
            int result = 1;
            while (result < value && result < maximum)
            {
                result <<= 1;
            }
            if (result < value)
            {
                throw std::runtime_error("GLIDE texture tile exceeds the emulator-reported texture-size limit");
            }
            return result;
        }

        [[nodiscard]] int Log2PowerOfTwo(int value)
        {
            int result = 0;
            while (value > 1)
            {
                value >>= 1;
                ++result;
            }
            return result;
        }

        struct GlideDisplayMode
        {
            FxI32 resolution;
            int width;
            int height;
        };

        [[nodiscard]] GlideDisplayMode KnownDisplayMode(FxI32 resolution)
        {
            switch (resolution)
            {
                // These are the complete historical GR_RESOLUTION_* values from Glide 3.x.
                // Query first, then choose only a mode that this concrete runtime actually listed.
                case 0x0: return GlideDisplayMode{resolution, 320, 200};
                case 0x1: return GlideDisplayMode{resolution, 320, 240};
                case 0x2: return GlideDisplayMode{resolution, 400, 256};
                case 0x3: return GlideDisplayMode{resolution, 512, 384};
                case 0x4: return GlideDisplayMode{resolution, 640, 200};
                case 0x5: return GlideDisplayMode{resolution, 640, 350};
                case 0x6: return GlideDisplayMode{resolution, 640, 400};
                case kResolution640x480: return GlideDisplayMode{resolution, 640, 480};
                case kResolution800x600: return GlideDisplayMode{resolution, 800, 600};
                case 0x9: return GlideDisplayMode{resolution, 960, 720};
                case 0xa: return GlideDisplayMode{resolution, 856, 480};
                case 0xb: return GlideDisplayMode{resolution, 512, 256};
                case 0xc: return GlideDisplayMode{resolution, 1024, 768};
                case 0xd: return GlideDisplayMode{resolution, 1280, 1024};
                case 0xe: return GlideDisplayMode{resolution, 1600, 1200};
                case 0xf: return GlideDisplayMode{resolution, 400, 300};
                case 0x10: return GlideDisplayMode{resolution, 1152, 864};
                case 0x11: return GlideDisplayMode{resolution, 1280, 960};
                case 0x12: return GlideDisplayMode{resolution, 1600, 1024};
                case 0x13: return GlideDisplayMode{resolution, 1792, 1344};
                case 0x14: return GlideDisplayMode{resolution, 1856, 1392};
                case 0x15: return GlideDisplayMode{resolution, 1920, 1440};
                case 0x16: return GlideDisplayMode{resolution, 2048, 1536};
                case 0x17: return GlideDisplayMode{resolution, 2048, 2048};
                default: return GlideDisplayMode{0, 0, 0};
            }
        }

        [[nodiscard]] FxI32 ToGlideDepthCompare(int xnaCompare)
        {
            // CNA/XNA depth grows away from the camera, while this backend submits 1/Z to
            // Glide. Strict order comparisons are therefore reversed; equality is unchanged.
            switch (xnaCompare)
            {
                case 0: return kDepthCompareAlways;
                case 1: return kDepthCompareNever;
                case 2: return kDepthCompareGreater;
                case 3: return kDepthCompareGreaterEqual;
                case 4: return kDepthCompareEqual;
                case 5: return kDepthCompareLessEqual;
                case 6: return kDepthCompareLess;
                case 7: return kDepthCompareNotEqual;
                default: throw std::runtime_error("GLIDE backend received an unknown CompareFunction value");
            }
        }

        [[nodiscard]] std::uint16_t RgbaToArgb4444(const std::uint8_t* rgba)
        {
            return static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(rgba[3] >> 4) << 12) |
                (static_cast<std::uint16_t>(rgba[0] >> 4) << 8) |
                (static_cast<std::uint16_t>(rgba[1] >> 4) << 4) |
                static_cast<std::uint16_t>(rgba[2] >> 4));
        }

        [[nodiscard]] FxU32 PackArgb(float r, float g, float b, float a)
        {
            const auto pack = [](float component) -> FxU32
            {
                return static_cast<FxU32>(std::clamp(component, 0.0f, 1.0f) * 255.0f + 0.5f);
            };
            return (pack(a) << 24) | (pack(r) << 16) | (pack(g) << 8) | pack(b);
        }

        [[nodiscard]] FxI32 ToGlideBlend(int blend)
        {
            using Microsoft::Xna::Framework::Graphics::Blend;
            switch (static_cast<Blend>(blend))
            {
                case Blend::One:                     return kBlendOne;
                case Blend::Zero:                    return kBlendZero;
                case Blend::SourceColor:
                case Blend::DestinationColor:        return kBlendSourceColor;
                case Blend::InverseSourceColor:
                case Blend::InverseDestinationColor: return kBlendInverseSourceColor;
                case Blend::SourceAlpha:             return kBlendSourceAlpha;
                case Blend::InverseSourceAlpha:      return kBlendInverseSourceAlpha;
                case Blend::DestinationAlpha:        return kBlendDestinationAlpha;
                case Blend::InverseDestinationAlpha: return kBlendInverseDestinationAlpha;
                case Blend::SourceAlphaSaturation:   return kBlendAlphaSaturate;
                case Blend::BlendFactor:
                case Blend::InverseBlendFactor:
                    throw std::runtime_error(
                        "GLIDE backend does not support BlendFactor/InverseBlendFactor: Glide 3.x "
                        "has no programmable constant blend color in this 2D scope.");
            }
            throw std::runtime_error("GLIDE backend received an unknown XNA Blend value");
        }

        [[nodiscard]] FxI32 ToGlideTextureAddress(int address)
        {
            switch (address)
            {
                case 0: return kTexClampWrap;
                case 1: return kTexClampClamp;
                case 2: return kTexClampMirror;
                default: throw std::runtime_error("GLIDE backend received an unknown TextureAddressMode value");
            }
        }

        struct GlideSamplerSettings
        {
            FxI32 minFilter = kTexFilterBilinear;
            FxI32 magFilter = kTexFilterBilinear;
            FxBool lodBlend = kFxTrue;
        };

        [[nodiscard]] GlideSamplerSettings ToGlideSamplerSettings(int filter)
        {
            // XNA's TextureFilter includes independent minification, magnification and mip terms.
            // Glide can represent all non-anisotropic variants with min/mag filters plus its
            // native LOD blend switch (nearest-mip vs trilinear blend).
            switch (filter)
            {
                case 0: return GlideSamplerSettings{kTexFilterBilinear, kTexFilterBilinear, kFxTrue};
                case 1: return GlideSamplerSettings{kTexFilterPoint, kTexFilterPoint, kFxFalse};
                case 2: throw std::runtime_error("GLIDE backend does not support anisotropic texture filtering");
                case 3: return GlideSamplerSettings{kTexFilterBilinear, kTexFilterBilinear, kFxFalse};
                case 4: return GlideSamplerSettings{kTexFilterPoint, kTexFilterPoint, kFxTrue};
                case 5: return GlideSamplerSettings{kTexFilterBilinear, kTexFilterPoint, kFxTrue};
                case 6: return GlideSamplerSettings{kTexFilterBilinear, kTexFilterPoint, kFxFalse};
                case 7: return GlideSamplerSettings{kTexFilterPoint, kTexFilterBilinear, kFxTrue};
                case 8: return GlideSamplerSettings{kTexFilterPoint, kTexFilterBilinear, kFxFalse};
                default: throw std::runtime_error("GLIDE backend received an unknown TextureFilter value");
            }
        }

        [[nodiscard]] std::uint16_t ToGlideDepth(float depth)
        {
            // XNA's post-projection depth is 0 at the near plane and 1 at the far plane. Glide's
            // fixed-point Z buffer stores 1/Z, so its ordering is reversed.
            return static_cast<std::uint16_t>(
                std::clamp((1.0f - depth) * 65535.0f, 0.0f, 65535.0f) + 0.5f);
        }

        [[nodiscard]] int VertexCountForGlidePrimitives(PrimitiveType primitive, int primitiveCount)
        {
            if (primitiveCount <= 0)
            {
                throw std::runtime_error("GLIDE primitiveCount must be positive");
            }
            switch (primitive)
            {
                case PrimitiveType::TriangleList: return primitiveCount * 3;
                case PrimitiveType::TriangleStrip: return primitiveCount + 2;
                default:
                    throw std::runtime_error(
                        "GLIDE fixed-function 3D supports only PrimitiveType::TriangleList and TriangleStrip");
            }
        }

        class GlideApi
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
            using TexCombineFn = void (WINAPI*)(FxI32, FxI32, FxI32, FxI32, FxI32, FxBool, FxBool);
            using LfbReadRegionFn = FxBool (WINAPI*)(FxI32, FxU32, FxU32, FxU32, FxU32, FxU32, void*);

            GlideApi() = default;
            ~GlideApi()
            {
                if (module_ != nullptr)
                {
                    FreeLibrary(module_);
                }
            }

            GlideApi(const GlideApi&) = delete;
            GlideApi& operator=(const GlideApi&) = delete;

            void Load()
            {
                const char* explicitPath = std::getenv("CNA_GLIDE3X_DLL");
                if (explicitPath != nullptr && explicitPath[0] != '\0')
                {
                    module_ = LoadLibraryA(explicitPath);
                    if (module_ == nullptr)
                    {
                        throw std::runtime_error(LastWin32Error("LoadLibraryA(CNA_GLIDE3X_DLL)"));
                    }
                }
                else
                {
                    module_ = LoadLibraryA("glide3x.dll");
                    if (module_ == nullptr)
                    {
                        throw std::runtime_error(
                            LastWin32Error("LoadLibraryA(glide3x.dll)") +
                            ". Install/copy a Glide 3.x runtime beside the executable or set CNA_GLIDE3X_DLL "
                            "to its full path (dgVoodoo2 is the supported emulated runtime).");
                    }
                }

                grGlideInit = Required<GlideInitFn>("grGlideInit", 0);
                grGlideShutdown = Required<GlideShutdownFn>("grGlideShutdown", 0);
                grQueryResolutions = Required<QueryResolutionsFn>("grQueryResolutions", 8);
                grGet = Required<GetFn>("grGet", 12);
                grSstWinOpen = Required<SstWinOpenFn>("grSstWinOpen", 28);
                grSstWinClose = Required<SstWinCloseFn>("grSstWinClose", 4);
                grBufferClear = Required<BufferClearFn>("grBufferClear", 12);
                grBufferSwap = Required<BufferSwapFn>("grBufferSwap", 4);
                grRenderBuffer = Required<RenderBufferFn>("grRenderBuffer", 4);
                grFinish = Required<FinishFn>("grFinish", 0);
                grClipWindow = Required<ClipWindowFn>("grClipWindow", 16);
                grCoordinateSpace = Required<CoordinateSpaceFn>("grCoordinateSpace", 4);
                grCullMode = Required<CullModeFn>("grCullMode", 4);
                grVertexLayout = Required<VertexLayoutFn>("grVertexLayout", 12);
                grDrawTriangle = Required<DrawTriangleFn>("grDrawTriangle", 12);
                grDrawVertexArray = Required<DrawVertexArrayFn>("grDrawVertexArray", 12);
                grColorCombine = Required<ColorCombineFn>("grColorCombine", 20);
                grAlphaCombine = Required<AlphaCombineFn>("grAlphaCombine", 20);
                grAlphaTestFunction = Required<AlphaTestFunctionFn>("grAlphaTestFunction", 4);
                grAlphaTestReferenceValue = Required<AlphaTestReferenceValueFn>("grAlphaTestReferenceValue", 4);
                grAlphaBlendFunction = Required<AlphaBlendFunctionFn>("grAlphaBlendFunction", 16);
                grColorMask = Required<ColorMaskFn>("grColorMask", 8);
                grDepthBufferMode = Required<DepthBufferModeFn>("grDepthBufferMode", 4);
                grDepthBufferFunction = Required<DepthBufferFunctionFn>("grDepthBufferFunction", 4);
                grDepthMask = Required<DepthMaskFn>("grDepthMask", 4);
                grTexMinAddress = Required<TexMinAddressFn>("grTexMinAddress", 4);
                grTexMaxAddress = Required<TexMaxAddressFn>("grTexMaxAddress", 4);
                grTexTextureMemRequired = Required<TexTextureMemRequiredFn>("grTexTextureMemRequired", 8);
                grTexDownloadMipMap = Required<TexDownloadMipMapFn>("grTexDownloadMipMap", 16);
                grTexSource = Required<TexSourceFn>("grTexSource", 16);
                grTexFilterMode = Required<TexFilterModeFn>("grTexFilterMode", 12);
                grTexClampMode = Required<TexClampModeFn>("grTexClampMode", 12);
                grTexMipMapMode = Required<TexMipMapModeFn>("grTexMipMapMode", 12);
                grTexCombine = Required<TexCombineFn>("grTexCombine", 28);
                grLfbReadRegion = Required<LfbReadRegionFn>("grLfbReadRegion", 28);
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
            TexCombineFn grTexCombine = nullptr;
            LfbReadRegionFn grLfbReadRegion = nullptr;

        private:
            template <typename T>
            [[nodiscard]] T Required(const char* name, unsigned int stdcallBytes) const
            {
                FARPROC procedure = GetProcAddress(module_, name);
#if !defined(_WIN64)
                // dgVoodoo2's native x86 Glide DLL exports the historical stdcall spelling
                // (_grFunction@N), whereas other Glide runtimes expose undecorated aliases.
                // Resolve both forms so a real x86 runtime is accepted without bundling its SDK.
                if (procedure == nullptr)
                {
                    const std::string decorated = "_" + std::string(name) + "@" + std::to_string(stdcallBytes);
                    procedure = GetProcAddress(module_, decorated.c_str());
                }
                if (procedure == nullptr)
                {
                    const std::string decorated = std::string(name) + "@" + std::to_string(stdcallBytes);
                    procedure = GetProcAddress(module_, decorated.c_str());
                }
#else
                static_cast<void>(stdcallBytes);
#endif
                if (procedure == nullptr)
                {
                    throw std::runtime_error(
                        std::string("The loaded glide3x.dll does not export required Glide 3.x function '") + name + "'.");
                }
                return reinterpret_cast<T>(procedure);
            }

            HMODULE module_ = nullptr;
        };

        struct TextureRange
        {
            FxU32 address = 0;
            FxU32 size = 0;
        };
    } // namespace

    struct GlideGraphicsBackend::Impl
    {
        explicit Impl(const GraphicsBackendCreateArgs& args)
            : window(args.window)
            , virtualWidth(args.virtualWidth > 0 ? args.virtualWidth : 640)
            , virtualHeight(args.virtualHeight > 0 ? args.virtualHeight : 480)
            , presentationMode(static_cast<CnaPresentationMode>(args.presentationMode))
            , swapInterval(args.swapInterval)
        {
            if (window == nullptr)
            {
                throw std::runtime_error("GLIDE backend requires CNA's SDL window");
            }
            if (presentationMode != CnaPresentationMode::NativeBackBuffer)
            {
                throw std::runtime_error(
                    "GLIDE backend supports only PresentationMode::NativeBackBuffer; Glide 3.x has no faithful "
                    "logical-surface scaling path");
            }
            if (swapInterval < 0 || swapInterval > 1)
            {
                throw std::runtime_error("GLIDE backend supports only swap intervals 0 (immediate) and 1 (v-sync)");
            }
            const HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
                SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
            if (hwnd == nullptr)
            {
                throw std::runtime_error("GLIDE backend could not obtain a Win32 HWND from CNA's SDL window");
            }

            try
            {
                api.Load();
                api.grGlideInit();
                glideInitialized = true;

                const GlideResolution query{kQueryAny, kQueryAny, 2, 1};
                const FxI32 resultBytes = api.grQueryResolutions(&query, nullptr);
                if (resultBytes <= 0 || resultBytes % static_cast<FxI32>(sizeof(GlideResolution)) != 0)
                {
                    throw std::runtime_error("grQueryResolutions did not return a valid Glide 3.x mode list");
                }
                std::vector<GlideResolution> availableModes(
                    static_cast<std::size_t>(resultBytes) / sizeof(GlideResolution));
                if (api.grQueryResolutions(&query, availableModes.data()) != resultBytes)
                {
                    throw std::runtime_error("grQueryResolutions failed while reading the Glide 3.x mode list");
                }
                GlideDisplayMode displayMode{};
                for (const GlideResolution& candidate : availableModes)
                {
                    const GlideDisplayMode known = KnownDisplayMode(candidate.resolution);
                    if (known.width >= virtualWidth && known.height >= virtualHeight &&
                        (displayMode.width == 0 || known.width * known.height < displayMode.width * displayMode.height))
                    {
                        displayMode = known;
                    }
                }
                if (displayMode.width == 0)
                {
                    throw std::runtime_error(
                        "The loaded Glide runtime exposes no historical double-buffered Z mode large enough for CNA's virtual resolution");
                }
                nativeWidth = displayMode.width;
                nativeHeight = displayMode.height;
                context = api.grSstWinOpen(static_cast<FxU32>(reinterpret_cast<std::uintptr_t>(hwnd)), displayMode.resolution,
                                           kRefresh60Hz, kColorFormatArgb, kOriginUpperLeft, 2, 1);
                if (context == nullptr)
                {
                    throw std::runtime_error(
                        "grSstWinOpen failed for the selected double-buffered Glide context. Confirm that "
                        "the selected glide3x.dll is a working emulator and that it accepts CNA's HWND.");
                }

                api.grRenderBuffer(kBufferBack);
                api.grCoordinateSpace(kWindowCoords);
                api.grCullMode(kCullDisable);
                ConfigureVertexLayout();
                ConfigureSpriteCombiner();
                ApplyDepthState();
                scissorWidth = virtualWidth;
                scissorHeight = virtualHeight;
                viewportWidth = virtualWidth;
                viewportHeight = virtualHeight;
                ApplyEffectiveClipWindow();

                QueryHardwareLimits();

                const FxU32 minAddress = api.grTexMinAddress(0);
                const FxU32 maxAddress = api.grTexMaxAddress(0);
                if (maxAddress <= minAddress)
                {
                    throw std::runtime_error("Glide TMU0 reported no usable texture memory");
                }
                const std::uint64_t rangeSize = static_cast<std::uint64_t>(maxAddress) - minAddress + 1u;
                if (rangeSize > std::numeric_limits<FxU32>::max())
                {
                    throw std::runtime_error("Glide TMU0 reported an unrepresentable texture-memory range");
                }
                freeTextureRanges.push_back(TextureRange{minAddress, static_cast<FxU32>(rangeSize)});
            }
            catch (...)
            {
                if (context != nullptr)
                {
                    api.grSstWinClose(context);
                    context = nullptr;
                }
                if (glideInitialized)
                {
                    api.grGlideShutdown();
                    glideInitialized = false;
                }
                throw;
            }
        }

        ~Impl()
        {
            if (context != nullptr)
            {
                api.grSstWinClose(context);
            }
            if (glideInitialized)
            {
                api.grGlideShutdown();
            }
        }

        void ConfigureVertexLayout()
        {
            api.grVertexLayout(kParamXY, static_cast<FxI32>(offsetof(GlideVertex, x)), kParamEnable);
            api.grVertexLayout(kParamZ, static_cast<FxI32>(offsetof(GlideVertex, ooz)), kParamEnable);
            api.grVertexLayout(kParamQ, static_cast<FxI32>(offsetof(GlideVertex, oow)), kParamEnable);
            api.grVertexLayout(kParamRgb, static_cast<FxI32>(offsetof(GlideVertex, r)), kParamEnable);
            api.grVertexLayout(kParamA, static_cast<FxI32>(offsetof(GlideVertex, a)), kParamEnable);
            api.grVertexLayout(kParamSt0, static_cast<FxI32>(offsetof(GlideVertex, sow)), kParamEnable);
        }

        void QueryHardwareLimits()
        {
            const auto querySingle = [&](FxI32 selector, const char* name) -> FxI32
            {
                FxI32 value = 0;
                if (api.grGet(selector, static_cast<FxI32>(sizeof(value)), &value) != static_cast<FxI32>(sizeof(value)) || value <= 0)
                {
                    throw std::runtime_error(std::string("grGet failed for ") + name);
                }
                return value;
            };
            maxTextureDimension = querySingle(kGetMaxTextureSize, "GR_MAX_TEXTURE_SIZE");
            maxTextureAspectLog2 = querySingle(kGetMaxTextureAspectRatio, "GR_MAX_TEXTURE_ASPECT_RATIO");
            textureUnitCount = querySingle(kGetNumTmu, "GR_NUM_TMU");
            if ((maxTextureDimension & (maxTextureDimension - 1)) != 0 || maxTextureAspectLog2 < 0 ||
                maxTextureAspectLog2 > 8)
            {
                throw std::runtime_error("Glide reported unsupported texture-size or texture-aspect limits");
            }
            if (textureUnitCount < 1)
            {
                throw std::runtime_error("The loaded Glide runtime exposes no texture mapping units");
            }
        }

        void ConfigureSpriteCombiner()
        {
            // Iterated sprite tint multiplied by TMU0's sampled ARGB4444 texel.
            api.grTexCombine(0, kCombineFunctionLocal, kCombineFactorOne,
                              kCombineFunctionLocal, kCombineFactorOne, kFxFalse, kFxFalse);
            api.grColorCombine(kCombineFunctionScaleOther, kCombineFactorLocal,
                               kCombineLocalIterated, kCombineOtherTexture, kFxFalse);
            api.grAlphaCombine(kCombineFunctionScaleOther, kCombineFactorLocal,
                               kCombineLocalIterated, kCombineOtherTexture, kFxFalse);
        }

        void ConfigureColoredCombiner()
        {
            // This is Glide's native Gouraud-shaded, untextured path.  The factor and "other"
            // inputs are deliberately marked unused, matching the Glide 3.x reference example.
            api.grColorCombine(kCombineFunctionLocal, kCombineFactorZero,
                               kCombineLocalIterated, kCombineOtherNone, kFxFalse);
            api.grAlphaCombine(kCombineFunctionLocal, kCombineFactorZero,
                               kCombineLocalIterated, kCombineOtherNone, kFxFalse);
        }

        void ApplyBlendState()
        {
            if (blendEnabled)
            {
                api.grAlphaBlendFunction(colorSrcBlend, colorDstBlend, alphaSrcBlend, alphaDstBlend);
            }
            else
            {
                api.grAlphaBlendFunction(kBlendOne, kBlendZero, kBlendOne, kBlendZero);
            }
        }

        void ApplyDepthState()
        {
            api.grDepthBufferMode(depthTestEnabled ? kDepthBufferZ : kDepthBufferDisable);
            if (depthTestEnabled)
            {
                // CNA/XNA depth grows away from the camera. Glide Z uses 1/Z, therefore nearer
                // fragments have the greater native value.
                api.grDepthBufferFunction(depthCompare);
                api.grDepthMask(depthWriteEnabled ? kFxTrue : kFxFalse);
            }
        }

        void ApplyColorMask()
        {
            api.grColorMask(colorMaskRgb, colorMaskAlpha);
        }

        void ClearDepthOnly(std::uint16_t depth)
        {
            api.grDepthBufferMode(kDepthBufferZ);
            api.grDepthMask(kFxTrue);
            api.grColorMask(kFxFalse, kFxFalse);
            api.grBufferClear(0, 0, depth);
            ApplyColorMask();
            ApplyDepthState();
        }

        void ClearColorOnly(FxU32 color, std::uint8_t alpha)
        {
            // grBufferClear clears every enabled buffer. Preserve the auxiliary depth plane for
            // GraphicsDevice::Clear(Color), which is specified to be color-only.
            api.grDepthBufferMode(kDepthBufferZ);
            api.grDepthMask(kFxFalse);
            api.grColorMask(kFxTrue, kFxTrue);
            api.grBufferClear(color, alpha, 0);
            ApplyColorMask();
            ApplyDepthState();
        }

        void ClearColorAndDepth(FxU32 color, std::uint8_t alpha, std::uint16_t depth)
        {
            api.grDepthBufferMode(kDepthBufferZ);
            api.grDepthMask(kFxTrue);
            api.grColorMask(kFxTrue, kFxTrue);
            api.grBufferClear(color, alpha, depth);
            ApplyColorMask();
            ApplyDepthState();
        }

        void ApplyClipWindow(int x, int y, int width, int height)
        {
            api.grClipWindow(static_cast<FxU32>(x), static_cast<FxU32>(y),
                             static_cast<FxU32>(x + width), static_cast<FxU32>(y + height));
        }

        void ApplyEffectiveClipWindow()
        {
            // Glide has one clip rectangle, whereas XNA has an always-active viewport plus an
            // optional scissor rectangle. Intersect both with the virtual framebuffer before
            // handing the result to Glide; this also makes off-target viewport/scissor portions
            // harmless rather than wrapping when converted to FxU32.
            const auto clippedEnd = [](int origin, int extent, int limit) -> int
            {
                const std::int64_t value = static_cast<std::int64_t>(origin) + extent;
                return static_cast<int>(std::clamp<std::int64_t>(value, 0, limit));
            };
            int left = std::clamp(viewportX, 0, virtualWidth);
            int top = std::clamp(viewportY, 0, virtualHeight);
            int right = clippedEnd(viewportX, viewportWidth, virtualWidth);
            int bottom = clippedEnd(viewportY, viewportHeight, virtualHeight);
            if (scissorEnabled)
            {
                left = std::max(left, std::clamp(scissorX, 0, virtualWidth));
                top = std::max(top, std::clamp(scissorY, 0, virtualHeight));
                right = std::min(right, clippedEnd(scissorX, scissorWidth, virtualWidth));
                bottom = std::min(bottom, clippedEnd(scissorY, scissorHeight, virtualHeight));
            }
            right = std::max(left, right);
            bottom = std::max(top, bottom);
            ApplyClipWindow(left, top, right - left, bottom - top);
        }

        [[nodiscard]] TextureRange AllocateTexture(FxU32 size)
        {
            constexpr FxU32 alignment = 8;
            for (auto it = freeTextureRanges.begin(); it != freeTextureRanges.end(); ++it)
            {
                const std::uint64_t alignedAddress64 =
                    (static_cast<std::uint64_t>(it->address) + alignment - 1) & ~(static_cast<std::uint64_t>(alignment) - 1);
                if (alignedAddress64 > std::numeric_limits<FxU32>::max())
                {
                    continue;
                }
                const FxU32 alignedAddress = static_cast<FxU32>(alignedAddress64);
                const FxU32 padding = alignedAddress - it->address;
                if (padding <= it->size && size <= it->size - padding)
                {
                    const TextureRange allocation{alignedAddress, size};
                    const std::uint64_t oldEnd = static_cast<std::uint64_t>(it->address) + it->size;
                    if (padding == 0)
                    {
                        it->address += size;
                        it->size -= size;
                        if (it->size == 0)
                        {
                            freeTextureRanges.erase(it);
                        }
                    }
                    else
                    {
                        it->size = padding;
                        const std::uint64_t allocationEnd = static_cast<std::uint64_t>(alignedAddress) + size;
                        if (allocationEnd < oldEnd)
                        {
                            freeTextureRanges.insert(std::next(it), TextureRange{
                                static_cast<FxU32>(allocationEnd), static_cast<FxU32>(oldEnd - allocationEnd)});
                        }
                    }
                    return allocation;
                }
            }
            throw std::runtime_error("GLIDE TMU0 texture memory is exhausted");
        }

        void ReleaseTexture(TextureRange range)
        {
            if (range.size == 0)
            {
                return;
            }
            auto position = std::lower_bound(freeTextureRanges.begin(), freeTextureRanges.end(), range.address,
                [](const TextureRange& candidate, FxU32 address) { return candidate.address < address; });
            position = freeTextureRanges.insert(position, range);
            if (position != freeTextureRanges.begin())
            {
                const auto previous = std::prev(position);
                if (static_cast<std::uint64_t>(previous->address) + previous->size == position->address)
                {
                    const std::uint64_t combinedSize = static_cast<std::uint64_t>(previous->size) + position->size;
                    if (combinedSize > std::numeric_limits<FxU32>::max())
                    {
                        throw std::runtime_error("GLIDE TMU0 free-range coalescing overflowed");
                    }
                    previous->size = static_cast<FxU32>(combinedSize);
                    position = freeTextureRanges.erase(position);
                    position = previous;
                }
            }
            const auto next = std::next(position);
            if (next != freeTextureRanges.end() &&
                static_cast<std::uint64_t>(position->address) + position->size == next->address)
            {
                const std::uint64_t combinedSize = static_cast<std::uint64_t>(position->size) + next->size;
                if (combinedSize > std::numeric_limits<FxU32>::max())
                {
                    throw std::runtime_error("GLIDE TMU0 free-range coalescing overflowed");
                }
                position->size = static_cast<FxU32>(combinedSize);
                freeTextureRanges.erase(next);
            }
        }

        GlideApi api;
        SDL_Window* window = nullptr;
        GlideApi::Context context = nullptr;
        bool glideInitialized = false;
        int virtualWidth = 640;
        int virtualHeight = 480;
        int nativeWidth = 640;
        int nativeHeight = 480;
        CnaPresentationMode presentationMode = CnaPresentationMode::NativeBackBuffer;
        int swapInterval = 1;
        bool blendEnabled = true;
        FxI32 colorSrcBlend = kBlendOne;
        FxI32 colorDstBlend = kBlendInverseSourceAlpha;
        FxI32 alphaSrcBlend = kBlendOne;
        FxI32 alphaDstBlend = kBlendInverseSourceAlpha;
        bool depthTestEnabled = false;
        bool depthWriteEnabled = true;
        FxI32 depthCompare = kDepthCompareGreater;
        FxBool colorMaskRgb = kFxTrue;
        FxBool colorMaskAlpha = kFxTrue;
        int samplerFilter = 0;
        int samplerAddressU = 1;
        int samplerAddressV = 1;
        int scissorX = 0;
        int scissorY = 0;
        int scissorWidth = 640;
        int scissorHeight = 480;
        bool scissorEnabled = false;
        int viewportX = 0;
        int viewportY = 0;
        int viewportWidth = 640;
        int viewportHeight = 480;
        float viewportMinDepth = 0.0f;
        float viewportMaxDepth = 1.0f;
        int maxTextureDimension = 256;
        int maxTextureAspectLog2 = 3;
        int textureUnitCount = 1;
        std::vector<TextureRange> freeTextureRanges;
    };

    class GlideTextureBackend final : public ITextureBackend
    {
    public:
        struct Tile
        {
            int sourceX = 0;
            int sourceY = 0;
            int sourceWidth = 0;
            int sourceHeight = 0;
            // A tile owns source texels plus a one-texel halo wherever an adjacent logical tile
            // exists. The remaining power-of-two padding is filled with the active address mode.
            int gutterLeft = 0;
            int gutterTop = 0;
            int gutterRight = 0;
            int gutterBottom = 0;
            int paddedWidth = 0;
            int paddedHeight = 0;
            TextureRange range{};
            GlideTexInfo nativeInfo{};
            std::vector<std::uint16_t> nativeTexels;
        };

        GlideTextureBackend(GlideGraphicsBackend& owner, const ImageData& data)
            : impl_(owner.impl_)
            , width_(data.width)
            , height_(data.height)
        {
            if (width_ <= 0 || height_ <= 0)
            {
                throw std::runtime_error("GLIDE texture dimensions must be positive");
            }
            constexpr int kMaximumLogicalTextureDimension = 16384;
            if (width_ > kMaximumLogicalTextureDimension || height_ > kMaximumLogicalTextureDimension)
            {
                throw std::runtime_error("GLIDE logical texture exceeds CNA's 16384-pixel dimension limit");
            }
            const std::size_t width = static_cast<std::size_t>(width_);
            const std::size_t height = static_cast<std::size_t>(height_);
            if (width > std::numeric_limits<std::size_t>::max() / 4u ||
                height > std::numeric_limits<std::size_t>::max() / (width * 4u))
            {
                throw std::runtime_error("GLIDE texture byte count overflows size_t");
            }
            const std::size_t requiredBytes = width * height * 4u;
            if (data.pixels.size() < requiredBytes)
            {
                throw std::runtime_error("GLIDE texture upload is shorter than its declared RGBA8 dimensions");
            }
            rgba_.assign(data.pixels.begin(), data.pixels.begin() + static_cast<std::ptrdiff_t>(requiredBytes));
            try
            {
                BuildLogicalMipChain(uploadedAddressU_, uploadedAddressV_);
                BuildTiles();
            }
            catch (...)
            {
                for (const Tile& tile : tiles_)
                {
                    GetImpl().ReleaseTexture(tile.range);
                }
                throw;
            }
        }

        ~GlideTextureBackend() override
        {
            if (const std::shared_ptr<GlideGraphicsBackend::Impl> impl = impl_.lock())
            {
                // A source command can remain in Glide's FIFO after the C++ texture dies. Do
                // not make its TMU range reusable until the hardware/emulator has consumed it.
                impl->api.grFinish();
                for (const Tile& tile : tiles_)
                {
                    impl->ReleaseTexture(tile.range);
                }
            }
        }

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }

        void UpdatePixels(const std::uint8_t* rgba, int stride) override
        {
            if (rgba == nullptr)
            {
                throw std::runtime_error("GLIDE texture update received null pixel data");
            }
            const std::size_t rowBytes = static_cast<std::size_t>(width_) * 4u;
            const std::size_t sourceStride = stride > 0 ? static_cast<std::size_t>(stride) : rowBytes;
            if (sourceStride < rowBytes)
            {
                throw std::runtime_error("GLIDE texture update stride is shorter than one RGBA8 row");
            }
            for (int row = 0; row < height_; ++row)
            {
                std::memcpy(rgba_.data() + static_cast<std::size_t>(row) * rowBytes,
                            rgba + static_cast<std::size_t>(row) * sourceStride, rowBytes);
            }
            // Existing draws may still sample this native allocation. Synchronize before
            // downloading a replacement mip chain into the same TMU addresses.
            GetImpl().api.grFinish();
            BuildLogicalMipChain(uploadedAddressU_, uploadedAddressV_);
            for (Tile& tile : tiles_)
            {
                ConvertTileToGlideTexels(tile, uploadedAddressU_, uploadedAddressV_);
                Upload(tile);
            }
        }

        void UpdatePixelsLevel(int level, const std::uint8_t* rgba, int levelWidth, int levelHeight) override
        {
            if (level != 0 || levelWidth != width_ || levelHeight != height_)
            {
                throw std::runtime_error(
                    "GLIDE tiled textures generate their native mip chain from a complete level-0 upload; "
                    "partial explicit mip uploads are not representable across tile boundaries");
            }
            UpdatePixels(rgba, width_ * 4);
        }

        [[nodiscard]] const std::vector<Tile>& Tiles() const { return tiles_; }
        [[nodiscard]] bool IsTiled() const { return tiles_.size() != 1; }

        void EnsureAddressMode(int addressU, int addressV)
        {
            // The source image is retained in RGBA8, so changing the sampler address mode can
            // rebuild the global mip pyramid and tile padding without losing information to an
            // earlier ARGB4444 conversion. Lower LODs also depend on the mode at image edges.
            if (addressU == uploadedAddressU_ && addressV == uploadedAddressV_)
            {
                return;
            }
            static_cast<void>(ToGlideTextureAddress(addressU));
            static_cast<void>(ToGlideTextureAddress(addressV));
            GetImpl().api.grFinish();
            BuildLogicalMipChain(addressU, addressV);
            for (Tile& tile : tiles_)
            {
                ConvertTileToGlideTexels(tile, addressU, addressV);
                Upload(tile);
            }
            uploadedAddressU_ = addressU;
            uploadedAddressV_ = addressV;
        }

    private:
        struct LogicalMipLevel
        {
            int width = 0;
            int height = 0;
            std::vector<std::uint16_t> texels;
        };

        void BuildLogicalMipChain(int addressU, int addressV)
        {
            logicalMipLevels_.clear();
            LogicalMipLevel level{};
            // Glide stores a power-of-two chain even for a non-power-of-two logical CNA image.
            // Build one address-mode-aware virtual image first, then downsample it globally. This
            // gives every physical tile the same source mip texels at a shared logical boundary.
            level.width = NextPowerOfTwo(width_, 16384);
            level.height = NextPowerOfTwo(height_, 16384);
            level.texels.resize(static_cast<std::size_t>(level.width) * level.height);
            for (int y = 0; y < level.height; ++y)
            {
                const int sourceY = AddressTexel(y, height_, addressV);
                for (int x = 0; x < level.width; ++x)
                {
                    const int sourceX = AddressTexel(x, width_, addressU);
                    level.texels[static_cast<std::size_t>(y) * level.width + x] = RgbaToArgb4444(
                        rgba_.data() + (static_cast<std::size_t>(sourceY) * width_ + sourceX) * 4u);
                }
            }
            logicalMipLevels_.push_back(std::move(level));

            while (logicalMipLevels_.back().width != 1 || logicalMipLevels_.back().height != 1)
            {
                const LogicalMipLevel& previous = logicalMipLevels_.back();
                LogicalMipLevel next{};
                next.width = std::max(1, previous.width / 2);
                next.height = std::max(1, previous.height / 2);
                next.texels.resize(static_cast<std::size_t>(next.width) * next.height);
                for (int y = 0; y < next.height; ++y)
                {
                    for (int x = 0; x < next.width; ++x)
                    {
                        unsigned int channels[4]{};
                        for (int dy = 0; dy < 2; ++dy)
                        {
                            const int sourceY = std::min(previous.height - 1, y * 2 + dy);
                            for (int dx = 0; dx < 2; ++dx)
                            {
                                const int sourceX = std::min(previous.width - 1, x * 2 + dx);
                                const std::uint16_t sample = previous.texels[
                                    static_cast<std::size_t>(sourceY) * previous.width + sourceX];
                                channels[0] += (sample >> 12) & 0x0f;
                                channels[1] += (sample >> 8) & 0x0f;
                                channels[2] += (sample >> 4) & 0x0f;
                                channels[3] += sample & 0x0f;
                            }
                        }
                        next.texels[static_cast<std::size_t>(y) * next.width + x] = static_cast<std::uint16_t>(
                            (((channels[0] + 2) / 4) << 12) | (((channels[1] + 2) / 4) << 8) |
                            (((channels[2] + 2) / 4) << 4) | ((channels[3] + 2) / 4));
                    }
                }
                logicalMipLevels_.push_back(std::move(next));
            }
        }

        void BuildTiles()
        {
            const int maximum = GetImpl().maxTextureDimension;
            if (maximum < 1 || (maximum & (maximum - 1)) != 0)
            {
                throw std::runtime_error("Glide reported an invalid non-power-of-two maximum texture size");
            }
            if (maximum < 4 && (width_ > maximum || height_ > maximum))
            {
                throw std::runtime_error("Glide texture-size limit is too small to construct tile gutters");
            }
            for (int sourceY = 0; sourceY < height_; )
            {
                const int gutterTop = sourceY == 0 ? 0 : 1;
                const int remainingY = height_ - sourceY;
                const int gutterBottom = remainingY > maximum - gutterTop ? 1 : 0;
                const int sourceHeight = std::min(remainingY, maximum - gutterTop - gutterBottom);
                if (sourceHeight <= 0)
                {
                    throw std::runtime_error("GLIDE texture tiler could not reserve a vertical gutter");
                }
                for (int sourceX = 0; sourceX < width_; )
                {
                    const int gutterLeft = sourceX == 0 ? 0 : 1;
                    const int remainingX = width_ - sourceX;
                    const int gutterRight = remainingX > maximum - gutterLeft ? 1 : 0;
                    const int sourceWidth = std::min(remainingX, maximum - gutterLeft - gutterRight);
                    if (sourceWidth <= 0)
                    {
                        throw std::runtime_error("GLIDE texture tiler could not reserve a horizontal gutter");
                    }
                    Tile tile{};
                    tile.sourceX = sourceX;
                    tile.sourceY = sourceY;
                    tile.sourceWidth = sourceWidth;
                    tile.sourceHeight = sourceHeight;
                    tile.gutterLeft = gutterLeft;
                    tile.gutterTop = gutterTop;
                    tile.gutterRight = gutterRight;
                    tile.gutterBottom = gutterBottom;
                    tile.paddedWidth = NextPowerOfTwo(tile.sourceWidth + tile.gutterLeft + tile.gutterRight, maximum);
                    tile.paddedHeight = NextPowerOfTwo(tile.sourceHeight + tile.gutterTop + tile.gutterBottom, maximum);
                    const int maximumAspect = 1 << GetImpl().maxTextureAspectLog2;
                    while (tile.paddedWidth > tile.paddedHeight * maximumAspect && tile.paddedHeight < maximum)
                    {
                        tile.paddedHeight <<= 1;
                    }
                    while (tile.paddedHeight > tile.paddedWidth * maximumAspect && tile.paddedWidth < maximum)
                    {
                        tile.paddedWidth <<= 1;
                    }
                    if (tile.paddedWidth > maximum || tile.paddedHeight > maximum ||
                        tile.paddedWidth > tile.paddedHeight * maximumAspect ||
                        tile.paddedHeight > tile.paddedWidth * maximumAspect)
                    {
                        throw std::runtime_error("GLIDE texture tile cannot satisfy the emulator-reported aspect-ratio limit");
                    }
                    ConvertTileToGlideTexels(tile, uploadedAddressU_, uploadedAddressV_);
                    const FxU32 requiredBytes = GetImpl().api.grTexTextureMemRequired(kMipMapBoth, &tile.nativeInfo);
                    if (requiredBytes == 0)
                    {
                        throw std::runtime_error("grTexTextureMemRequired rejected an ARGB4444 tiled Glide texture");
                    }
                    tile.range = GetImpl().AllocateTexture(requiredBytes);
                    try
                    {
                        Upload(tile);
                    }
                    catch (...)
                    {
                        GetImpl().ReleaseTexture(tile.range);
                        throw;
                    }
                    tiles_.push_back(std::move(tile));
                    sourceX += sourceWidth;
                }
                sourceY += sourceHeight;
            }
        }

        [[nodiscard]] static int AddressTexel(int coordinate, int dimension, int addressMode)
        {
            if (dimension <= 0)
            {
                throw std::runtime_error("GLIDE texture address conversion received an invalid dimension");
            }
            switch (addressMode)
            {
                case 0: // TextureAddressMode::Wrap
                {
                    int result = coordinate % dimension;
                    return result < 0 ? result + dimension : result;
                }
                case 1: // TextureAddressMode::Clamp
                    return std::clamp(coordinate, 0, dimension - 1);
                case 2: // TextureAddressMode::Mirror
                {
                    const int period = dimension * 2;
                    int reflected = coordinate % period;
                    if (reflected < 0)
                    {
                        reflected += period;
                    }
                    return reflected < dimension ? reflected : period - 1 - reflected;
                }
                default:
                    throw std::runtime_error("GLIDE backend received an unknown TextureAddressMode value");
            }
        }

        [[nodiscard]] static int FloorDivide(int numerator, int denominator)
        {
            if (denominator <= 0)
            {
                throw std::runtime_error("GLIDE mip coordinate conversion received an invalid divisor");
            }
            if (numerator >= 0)
            {
                return numerator / denominator;
            }
            return -(((-numerator) + denominator - 1) / denominator);
        }

        void ConvertTileToGlideTexels(Tile& tile, int addressU, int addressV)
        {
            const int largeDimension = std::max(tile.paddedWidth, tile.paddedHeight);
            tile.nativeTexels.clear();
            tile.nativeTexels.reserve(static_cast<std::size_t>(tile.paddedWidth) * tile.paddedHeight * 2u);
            int levelWidth = tile.paddedWidth;
            int levelHeight = tile.paddedHeight;
            for (std::size_t levelIndex = 0; ; ++levelIndex)
            {
                if (levelIndex >= logicalMipLevels_.size())
                {
                    throw std::runtime_error("GLIDE tile mip chain exceeds the logical texture mip chain");
                }
                const LogicalMipLevel& logicalLevel = logicalMipLevels_[levelIndex];
                const int texelScale = 1 << levelIndex;
                const int logicalOriginX = FloorDivide(tile.sourceX - tile.gutterLeft, texelScale);
                const int logicalOriginY = FloorDivide(tile.sourceY - tile.gutterTop, texelScale);
                for (int y = 0; y < levelHeight; ++y)
                {
                    const int sourceY = AddressTexel(logicalOriginY + y, logicalLevel.height, addressV);
                    for (int x = 0; x < levelWidth; ++x)
                    {
                        const int sourceX = AddressTexel(logicalOriginX + x, logicalLevel.width, addressU);
                        tile.nativeTexels.push_back(logicalLevel.texels[
                            static_cast<std::size_t>(sourceY) * logicalLevel.width + sourceX]);
                    }
                }
                if (levelWidth == 1 && levelHeight == 1)
                {
                    break;
                }
                levelWidth = std::max(1, levelWidth / 2);
                levelHeight = std::max(1, levelHeight / 2);
            }
            tile.nativeInfo = GlideTexInfo{
                0, Log2PowerOfTwo(largeDimension),
                Log2PowerOfTwo(tile.paddedWidth) - Log2PowerOfTwo(tile.paddedHeight),
                kTexFormatArgb4444, tile.nativeTexels.data() };
        }

        void Upload(Tile& tile)
        {
            tile.nativeInfo.data = tile.nativeTexels.data();
            GetImpl().api.grTexDownloadMipMap(0, tile.range.address, kMipMapBoth, &tile.nativeInfo);
        }

        [[nodiscard]] GlideGraphicsBackend::Impl& GetImpl() const
        {
            const std::shared_ptr<GlideGraphicsBackend::Impl> impl = impl_.lock();
            if (!impl)
            {
                throw std::runtime_error("GLIDE texture outlived its graphics backend");
            }
            return *impl;
        }

        std::weak_ptr<GlideGraphicsBackend::Impl> impl_;
        int width_ = 0;
        int height_ = 0;
        int uploadedAddressU_ = 1;
        int uploadedAddressV_ = 1;
        std::vector<std::uint8_t> rgba_;
        std::vector<LogicalMipLevel> logicalMipLevels_;
        std::vector<Tile> tiles_;

        friend struct GlideGraphicsBackend::Impl;
    };

    class GlideVertexBufferBackend final : public IVertexBufferBackend
    {
    public:
        explicit GlideVertexBufferBackend(int capacity) : capacity_(capacity)
        {
            if (capacity <= 0)
            {
                throw std::runtime_error("GLIDE vertex-buffer capacity must be positive");
            }
        }

        void SetData(const void* data, int vertexCount, std::size_t strideInBytes) override
        {
            if (data == nullptr || vertexCount < 0 || vertexCount > capacity_)
            {
                throw std::runtime_error("GLIDE vertex-buffer upload is outside its declared capacity");
            }
            if (!layout_.has_value())
            {
                layout_ = KnownGlideVertexLayout(strideInBytes);
            }
            if (layout_->stride != strideInBytes)
            {
                throw std::runtime_error(
                    "GLIDE vertex-buffer upload stride does not match its VertexDeclaration");
            }
            bytes_.resize(static_cast<std::size_t>(vertexCount) * strideInBytes);
            std::memcpy(bytes_.data(), data, bytes_.size());
            vertexCount_ = vertexCount;
            stride_ = strideInBytes;
        }

        void SetVertexDeclaration(const VertexDeclaration& declaration) override
        {
            GlideVertexLayout parsed = ParseGlideVertexDeclaration(declaration);
            if (stride_ != 0 && parsed.stride != stride_)
            {
                throw std::runtime_error(
                    "GLIDE VertexDeclaration stride does not match vertex data already uploaded to this buffer");
            }
            layout_ = std::move(parsed);
        }

        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }
        [[nodiscard]] const std::vector<std::uint8_t>& Bytes() const { return bytes_; }
        [[nodiscard]] std::size_t Stride() const { return stride_; }
        [[nodiscard]] const GlideVertexLayout& Layout() const
        {
            if (!layout_.has_value())
            {
                throw std::runtime_error("GLIDE vertex buffer has no resolved vertex layout");
            }
            return *layout_;
        }

    private:
        int capacity_ = 0;
        int vertexCount_ = 0;
        std::size_t stride_ = 0;
        std::vector<std::uint8_t> bytes_;
        std::optional<GlideVertexLayout> layout_;
    };

    class GlideIndexBufferBackend final : public IIndexBufferBackend
    {
    public:
        explicit GlideIndexBufferBackend(int capacity) : capacity_(capacity)
        {
            if (capacity <= 0)
            {
                throw std::runtime_error("GLIDE index-buffer capacity must be positive");
            }
        }

        void SetData16(const void* data, int indexCount) override
        {
            if (data == nullptr || indexCount < 0 || indexCount > capacity_)
            {
                throw std::runtime_error("GLIDE index-buffer upload is outside its declared capacity");
            }
            const auto* source = static_cast<const std::uint16_t*>(data);
            indices_.assign(source, source + indexCount);
            thirtyTwoBit_ = false;
        }

        void SetData32(const void* data, int indexCount) override
        {
            if (data == nullptr || indexCount < 0 || indexCount > capacity_)
            {
                throw std::runtime_error("GLIDE index-buffer upload is outside its declared capacity");
            }
            const auto* source = static_cast<const std::uint32_t*>(data);
            indices_.assign(source, source + indexCount);
            thirtyTwoBit_ = true;
        }

        [[nodiscard]] int GetIndexCount() const override { return static_cast<int>(indices_.size()); }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }
        [[nodiscard]] std::uint32_t IndexAt(int index) const { return indices_.at(static_cast<std::size_t>(index)); }

    private:
        int capacity_ = 0;
        bool thirtyTwoBit_ = false;
        std::vector<std::uint32_t> indices_;
    };

    class GlideSpriteBatchBackend final : public ISpriteBatchBackend
    {
    public:
        explicit GlideSpriteBatchBackend(GlideGraphicsBackend& owner) : owner_(owner) {}

        void Begin() override
        {
            if (begun_)
            {
                throw std::runtime_error("GlideSpriteBatchBackend::Begin called without a matching End");
            }
            begun_ = true;
        }

        void End() override
        {
            if (!begun_)
            {
                throw std::runtime_error("GlideSpriteBatchBackend::End called without a matching Begin");
            }
            begun_ = false;
        }

        void SetTransformMatrix(const Matrix& matrix) override { transform_ = matrix; }

        void SetCustomEffect(Effect* effect) override
        {
            if (effect != nullptr)
            {
                throw std::runtime_error("GLIDE backend does not support custom SpriteBatch Effects");
            }
        }

        void SetSamplerFilter(int textureFilter) override { textureFilter_ = textureFilter; }
        void SetSamplerAddressMode(int addressU, int addressV) override
        {
            addressU_ = addressU;
            addressV_ = addressV;
        }

        void Draw(const ITextureBackend& texture, float x, float y) override
        {
            Draw(texture, Rectangle(static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight()),
                 Rectangle(0, 0, texture.GetWidth(), texture.GetHeight()), Color::White,
                 0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, 0.0f);
        }

        void Draw(const ITextureBackend& texture, const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle, const Color& color) override
        {
            Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0.0f, 0.0f),
                 SpriteEffects::None, 0.0f);
        }

        void Draw(const ITextureBackend& texture, const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle, const Color& color, float rotation,
                  const Vector2& origin, SpriteEffects effects, float /*layerDepth*/) override
        {
            if (!begun_)
            {
                throw std::runtime_error("GlideSpriteBatchBackend::Draw called before Begin");
            }
            owner_.DrawSprite(texture, destinationRectangle, sourceRectangle, color, rotation, origin,
                              effects, transform_, textureFilter_, addressU_, addressV_);
        }

    private:
        GlideGraphicsBackend& owner_;
        bool begun_ = false;
        Matrix transform_ = Matrix::getIdentityProperty();
        int textureFilter_ = 0;
        int addressU_ = 1;
        int addressV_ = 1;
    };

    GlideGraphicsBackend::GlideGraphicsBackend(const GraphicsBackendCreateArgs& args)
        : impl_(std::make_shared<Impl>(args))
    {
        ApplyBlendState(/*One*/ 0, /*One*/ 0, /*InverseSourceAlpha*/ 5, /*InverseSourceAlpha*/ 5,
                        /*Add*/ 0, /*Add*/ 0, BlendWriteState{});
    }

    GlideGraphicsBackend::~GlideGraphicsBackend() = default;

    void GlideGraphicsBackend::Clear(float r, float g, float b, float a)
    {
        impl_->api.grRenderBuffer(kBufferBack);
        impl_->ClearColorOnly(PackArgb(r, g, b, a),
                              static_cast<std::uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f));
    }

    void GlideGraphicsBackend::Present()
    {
        impl_->api.grBufferSwap(impl_->swapInterval > 0 ? 1u : 0u);
        impl_->api.grRenderBuffer(kBufferBack);
    }

    void GlideGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        width = impl_->virtualWidth;
        height = impl_->virtualHeight;
    }

    bool GlideGraphicsBackend::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        // The auxiliary plane is a real 16-bit depth buffer, but historical Glide has no stencil
        // plane. Keep the aggregate DepthStencilBuffer capability false rather than promising
        // stencil operations that the hardware cannot provide.
        return capability == CNA::GraphicsCapability::ThreeD;
    }

    void GlideGraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, std::uint8_t* pixels)
    {
        if (pixels == nullptr || x < 0 || y < 0 || w <= 0 || h <= 0 ||
            x + w > impl_->virtualWidth || y + h > impl_->virtualHeight)
        {
            throw std::runtime_error("GLIDE ReadBackbuffer requested an invalid rectangle");
        }

        impl_->api.grFinish();
        std::vector<std::uint16_t> source(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
        if (impl_->api.grLfbReadRegion(kBufferBack, static_cast<FxU32>(x), static_cast<FxU32>(y),
                                       static_cast<FxU32>(w), static_cast<FxU32>(h),
                                       static_cast<FxU32>(w * static_cast<int>(sizeof(std::uint16_t))), source.data()) == kFxFalse)
        {
            throw std::runtime_error("grLfbReadRegion failed while reading the Glide backbuffer");
        }

        for (std::size_t index = 0; index < source.size(); ++index)
        {
            const std::uint16_t packed = source[index];
            pixels[index * 4 + 0] = static_cast<std::uint8_t>(((packed >> 11) & 0x1f) * 255 / 31);
            pixels[index * 4 + 1] = static_cast<std::uint8_t>(((packed >> 5) & 0x3f) * 255 / 63);
            pixels[index * 4 + 2] = static_cast<std::uint8_t>((packed & 0x1f) * 255 / 31);
            pixels[index * 4 + 3] = 255;
        }
    }

    void GlideGraphicsBackend::SetVirtualResolution(int width, int height)
    {
        if (width <= 0 || height <= 0 || width > impl_->nativeWidth || height > impl_->nativeHeight)
        {
            throw std::runtime_error("GLIDE virtual resolution exceeds the native Glide context selected at startup");
        }
        impl_->virtualWidth = width;
        impl_->virtualHeight = height;
        if (impl_->scissorX + impl_->scissorWidth > width || impl_->scissorY + impl_->scissorHeight > height)
        {
            impl_->scissorX = 0;
            impl_->scissorY = 0;
            impl_->scissorWidth = width;
            impl_->scissorHeight = height;
        }
        impl_->ApplyEffectiveClipWindow();
    }

    void GlideGraphicsBackend::SetPresentationMode(int mode)
    {
        if (static_cast<CnaPresentationMode>(mode) != CnaPresentationMode::NativeBackBuffer)
        {
            throw std::runtime_error(
                "GLIDE backend supports only PresentationMode::NativeBackBuffer; scaling is owned by the external Glide runtime");
        }
        impl_->presentationMode = CnaPresentationMode::NativeBackBuffer;
    }

    void GlideGraphicsBackend::SetSwapInterval(int interval)
    {
        if (interval < 0 || interval > 1)
        {
            throw std::runtime_error("GLIDE backend supports only swap intervals 0 (immediate) and 1 (v-sync)");
        }
        impl_->swapInterval = interval;
    }

    void GlideGraphicsBackend::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        if (w < 0 || h < 0 || !std::isfinite(minDepth) || !std::isfinite(maxDepth) ||
            minDepth < 0.0f || maxDepth > 1.0f || minDepth > maxDepth)
        {
            throw std::runtime_error("GLIDE backend received an invalid Viewport");
        }
        impl_->viewportX = x;
        impl_->viewportY = y;
        impl_->viewportWidth = w;
        impl_->viewportHeight = h;
        impl_->viewportMinDepth = minDepth;
        impl_->viewportMaxDepth = maxDepth;
        impl_->ApplyEffectiveClipWindow();
    }

    SDL_Window* GlideGraphicsBackend::GetWindowInternal() const
    {
        return impl_->window;
    }

    std::unique_ptr<ITextureBackend> GlideGraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<GlideTextureBackend>(*this, data);
    }

    std::unique_ptr<ISpriteBatchBackend> GlideGraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<GlideSpriteBatchBackend>(*this);
    }

    std::unique_ptr<IRenderTargetBackend> GlideGraphicsBackend::CreateRenderTarget2D(
        int, int, int, bool, bool, int)
    {
        throw std::runtime_error(
            "GLIDE backend does not support RenderTarget2D in its initial front/back-buffer-only implementation");
    }

    void GlideGraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* rt)
    {
        if (rt != nullptr)
        {
            throw std::runtime_error("GLIDE backend does not support RenderTarget2D");
        }
        impl_->api.grRenderBuffer(kBufferBack);
    }

    void GlideGraphicsBackend::SetRenderTargets(const RenderTargetBindingDescriptor*, int count)
    {
        if (count != 0)
        {
            throw std::runtime_error("GLIDE backend does not support render targets or multiple render targets");
        }
        impl_->api.grRenderBuffer(kBufferBack);
    }

    void GlideGraphicsBackend::SetScissorRect(int x, int y, int w, int h)
    {
        impl_->scissorX = x;
        impl_->scissorY = y;
        impl_->scissorWidth = w;
        impl_->scissorHeight = h;
        if (impl_->scissorEnabled)
        {
            impl_->ApplyEffectiveClipWindow();
        }
    }

    void GlideGraphicsBackend::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                int colorDstBlend, int alphaDstBlend,
                                                int colorBlendFunc, int alphaBlendFunc,
                                                const BlendWriteState& writeState)
    {
        using Microsoft::Xna::Framework::Graphics::BlendFunction;
        if (colorBlendFunc != static_cast<int>(BlendFunction::Add) ||
            alphaBlendFunc != static_cast<int>(BlendFunction::Add))
        {
            throw std::runtime_error("GLIDE backend supports only BlendFunction::Add in its initial 2D scope");
        }
        const bool writeRed = ColorWriteHasRed(writeState.colorWriteChannels[0]);
        const bool writeGreen = ColorWriteHasGreen(writeState.colorWriteChannels[0]);
        const bool writeBlue = ColorWriteHasBlue(writeState.colorWriteChannels[0]);
        if (writeRed != writeGreen || writeRed != writeBlue ||
            writeState.multiSampleMask != std::numeric_limits<unsigned int>::max())
        {
            throw std::runtime_error(
                "GLIDE backend supports RGB and alpha write masks independently, but cannot mask individual RGB channels or samples");
        }
        impl_->colorSrcBlend = ToGlideBlend(colorSrcBlend);
        impl_->colorDstBlend = ToGlideBlend(colorDstBlend);
        impl_->alphaSrcBlend = ToGlideBlend(alphaSrcBlend);
        impl_->alphaDstBlend = ToGlideBlend(alphaDstBlend);
        impl_->colorMaskRgb = writeRed ? kFxTrue : kFxFalse;
        impl_->colorMaskAlpha = ColorWriteHasAlpha(writeState.colorWriteChannels[0]) ? kFxTrue : kFxFalse;
        impl_->ApplyColorMask();
        impl_->ApplyBlendState();
    }

    void GlideGraphicsBackend::ApplyDepthStencilState(
        bool depthEnable, bool depthWriteEnable, int depthFunc,
        bool stencilEnable, int, int, int, int, int, int, int, bool, int, int, int, int)
    {
        if (stencilEnable)
        {
            throw std::runtime_error("GLIDE has no stencil plane; stencil-enabled DepthStencilState is unsupported");
        }
        impl_->depthTestEnabled = depthEnable;
        impl_->depthWriteEnabled = depthWriteEnable;
        impl_->depthCompare = ToGlideDepthCompare(depthFunc);
        impl_->ApplyDepthState();
    }

    void GlideGraphicsBackend::ApplyRasterizerState(int cullMode, int fillMode,
                                                     bool scissorTestEnable,
                                                     float depthBias, float slopeScaleDepthBias)
    {
        if (fillMode != 0)
        {
            throw std::runtime_error("GLIDE backend currently supports FillMode::Solid only");
        }
        if (std::abs(depthBias) > 0.000001f || std::abs(slopeScaleDepthBias) > 0.000001f)
        {
            throw std::runtime_error(
                "GLIDE RasterizerState depth bias is unsupported: Glide's integer depth-bias scale "
                "cannot faithfully represent XNA's normalized/slope-scaled bias");
        }
        switch (cullMode)
        {
            case 0: impl_->api.grCullMode(kCullDisable); break;
            // CNA and the UpperLeft Glide window coordinates both define winding in screen space.
            case 1: impl_->api.grCullMode(kCullPositive); break;
            case 2: impl_->api.grCullMode(kCullNegative); break;
            default: throw std::runtime_error("GLIDE backend received an unknown CullMode value");
        }
        impl_->scissorEnabled = scissorTestEnable;
        impl_->ApplyEffectiveClipWindow();
    }

    void GlideGraphicsBackend::ApplySamplerState(int slot, int filter,
                                                  int addressU, int addressV, int maxAnisotropy)
    {
        if (slot < 0)
        {
            throw std::runtime_error("GLIDE backend received a negative texture slot");
        }
        // GraphicsDevice commits all 16 public sampler slots. Glide only has TMU0 in the
        // supported pipeline, so unused higher slots are deliberately inert rather than making
        // an otherwise valid single-texture draw fail during state synchronization.
        if (slot != 0)
        {
            return;
        }
        static_cast<void>(ToGlideSamplerSettings(filter));
        static_cast<void>(maxAnisotropy);
        static_cast<void>(ToGlideTextureAddress(addressU));
        static_cast<void>(ToGlideTextureAddress(addressV));
        impl_->samplerFilter = filter;
        impl_->samplerAddressU = addressU;
        impl_->samplerAddressV = addressV;
    }

    int GlideGraphicsBackend::GetMaxTextureDimension() const
    {
        // Each physical tile is limited by GR_MAX_TEXTURE_SIZE, queried during construction. CNA
        // transparently tiles a logical image, so retain the shared API's documented 16K ceiling.
        return 16384;
    }

    void GlideGraphicsBackend::DrawSprite(const ITextureBackend& texture,
                                          const Rectangle& destinationRectangle,
                                          const Rectangle& sourceRectangle,
                                          const Color& color, float rotation,
                                          const Vector2& origin, SpriteEffects effects,
                                          const Matrix& transform, int textureFilter,
                                          int addressU, int addressV)
    {
        const auto* glideTexture = dynamic_cast<const GlideTextureBackend*>(&texture);
        if (glideTexture == nullptr)
        {
            throw std::runtime_error("GLIDE SpriteBatch received a texture created by a different backend");
        }
        if (sourceRectangle.Width <= 0 || sourceRectangle.Height <= 0 || destinationRectangle.Width == 0 ||
            destinationRectangle.Height == 0)
        {
            return;
        }
        // `texture` is publicly const during a draw, but its backend owns mutable native cache
        // storage. Rebuild only address-dependent gutters/padding before TMU0 sees the tile.
        const_cast<GlideTextureBackend*>(glideTexture)->EnsureAddressMode(addressU, addressV);

        // A preceding colored 3D draw leaves the fixed function combiner untextured. Restore
        // the SpriteBatch path explicitly instead of relying on accidental Glide state.
        impl_->ConfigureSpriteCombiner();

        const GlideSamplerSettings sampler = ToGlideSamplerSettings(textureFilter);
        const float sourceWidth = static_cast<float>(sourceRectangle.Width);
        const float sourceHeight = static_cast<float>(sourceRectangle.Height);
        const float scaleX = static_cast<float>(destinationRectangle.Width) / sourceWidth;
        const float scaleY = static_cast<float>(destinationRectangle.Height) / sourceHeight;
        const float cosRotation = std::cos(rotation);
        const float sinRotation = std::sin(rotation);
        const auto place = [&](float x, float y) -> Vector2
        {
            const float scaledX = (x - origin.X) * scaleX;
            const float scaledY = (y - origin.Y) * scaleY;
            const Vector2 screen(
                static_cast<float>(destinationRectangle.X) + scaledX * cosRotation - scaledY * sinRotation,
                static_cast<float>(destinationRectangle.Y) + scaledX * sinRotation + scaledY * cosRotation);
            return Vector2::Transform(screen, transform);
        };
        const bool flipX = (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0;
        const bool flipY = (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) != 0;
        const int sourceRight = sourceRectangle.X + sourceRectangle.Width;
        const int sourceBottom = sourceRectangle.Y + sourceRectangle.Height;

        for (const GlideTextureBackend::Tile& tile : glideTexture->Tiles())
        {
            const int sampleLeft = std::max(sourceRectangle.X, tile.sourceX);
            const int sampleTop = std::max(sourceRectangle.Y, tile.sourceY);
            const int sampleRight = std::min(sourceRight, tile.sourceX + tile.sourceWidth);
            const int sampleBottom = std::min(sourceBottom, tile.sourceY + tile.sourceHeight);
            if (sampleLeft >= sampleRight || sampleTop >= sampleBottom)
            {
                continue;
            }

            // Tile in texture/sample space, then map that section back to the unflipped quad
            // geometry. This keeps face winding intact even when SpriteEffects mirrors a sprite.
            const float localLeft = static_cast<float>((flipX ? sourceRight - sampleRight : sampleLeft) - sourceRectangle.X);
            const float localRight = static_cast<float>((flipX ? sourceRight - sampleLeft : sampleRight) - sourceRectangle.X);
            const float localTop = static_cast<float>((flipY ? sourceBottom - sampleBottom : sampleTop) - sourceRectangle.Y);
            const float localBottom = static_cast<float>((flipY ? sourceBottom - sampleTop : sampleBottom) - sourceRectangle.Y);
            const int texLeft = flipX ? sampleRight : sampleLeft;
            const int texRight = flipX ? sampleLeft : sampleRight;
            const int texTop = flipY ? sampleBottom : sampleTop;
            const int texBottom = flipY ? sampleTop : sampleBottom;
            const std::array<Vector2, 4> positions = {
                place(localLeft, localTop), place(localRight, localTop),
                place(localRight, localBottom), place(localLeft, localBottom) };
            const std::array<Vector2, 4> texcoords = {
                Vector2(static_cast<float>(texLeft - tile.sourceX + tile.gutterLeft),
                        static_cast<float>(texTop - tile.sourceY + tile.gutterTop)),
                Vector2(static_cast<float>(texRight - tile.sourceX + tile.gutterLeft),
                        static_cast<float>(texTop - tile.sourceY + tile.gutterTop)),
                Vector2(static_cast<float>(texRight - tile.sourceX + tile.gutterLeft),
                        static_cast<float>(texBottom - tile.sourceY + tile.gutterTop)),
                Vector2(static_cast<float>(texLeft - tile.sourceX + tile.gutterLeft),
                        static_cast<float>(texBottom - tile.sourceY + tile.gutterTop)) };
            const auto makeVertex = [&](int index) -> GlideVertex
            {
                return GlideVertex{
                    positions[index].X, positions[index].Y,
                    1.0f, 1.0f,
                    static_cast<float>(color.getRProperty()), static_cast<float>(color.getGProperty()),
                    static_cast<float>(color.getBProperty()), static_cast<float>(color.getAProperty()),
                    0.0f,
                    texcoords[index].X,
                    texcoords[index].Y,
                    1.0f };
            };
            impl_->api.grTexSource(0, tile.range.address, kMipMapBoth,
                                    const_cast<GlideTexInfo*>(&tile.nativeInfo));
            impl_->api.grTexFilterMode(0, sampler.minFilter, sampler.magFilter);
            // Geometry has already been restricted to the selected tile; letting native Wrap or
            // Mirror escape into its power-of-two padding would sample a wrong logical tile.
            impl_->api.grTexClampMode(0, kTexClampClamp, kTexClampClamp);
            impl_->api.grTexMipMapMode(0, kMipMapNearest, sampler.lodBlend);
            const GlideVertex topLeft = makeVertex(0);
            const GlideVertex topRight = makeVertex(1);
            const GlideVertex bottomRight = makeVertex(2);
            const GlideVertex bottomLeft = makeVertex(3);
            impl_->api.grDrawTriangle(&topLeft, &topRight, &bottomRight);
            impl_->api.grDrawTriangle(&topLeft, &bottomRight, &bottomLeft);
        }
    }

    void GlideGraphicsBackend::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        impl_->api.grRenderBuffer(kBufferBack);
        impl_->ClearColorAndDepth(PackArgb(r, g, b, a),
                                  static_cast<std::uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f),
                                  ToGlideDepth(depth));
    }

    void GlideGraphicsBackend::ClearDepth(float depth)
    {
        impl_->api.grRenderBuffer(kBufferBack);
        impl_->ClearDepthOnly(ToGlideDepth(depth));
    }

    void GlideGraphicsBackend::ClearStencil(int) { ThrowUnsupported("ClearStencil"); }
    void GlideGraphicsBackend::ClearDepthAndStencil(float, int) { ThrowUnsupported("ClearDepthAndStencil"); }
    void GlideGraphicsBackend::ClearColorAndStencil(float, float, float, float, int) { ThrowUnsupported("ClearColorAndStencil"); }
    void GlideGraphicsBackend::ClearColorDepthAndStencil(float, float, float, float, float, int) { ThrowUnsupported("ClearColorDepthAndStencil"); }
    void GlideGraphicsBackend::SetDepthTestEnabled(bool enabled)
    {
        impl_->depthTestEnabled = enabled;
        impl_->ApplyDepthState();
    }

    void GlideGraphicsBackend::SetBlendEnabled(bool enabled)
    {
        impl_->blendEnabled = enabled;
        impl_->ApplyBlendState();
    }

    void GlideGraphicsBackend::SetDepthWriteEnabled(bool enabled)
    {
        impl_->depthWriteEnabled = enabled;
        impl_->ApplyDepthState();
    }

    std::unique_ptr<IVertexBufferBackend> GlideGraphicsBackend::CreateVertexBuffer(int vertexCapacity)
    {
        return std::make_unique<GlideVertexBufferBackend>(vertexCapacity);
    }

    std::unique_ptr<IIndexBufferBackend> GlideGraphicsBackend::CreateIndexBuffer16(int indexCapacity)
    {
        return std::make_unique<GlideIndexBufferBackend>(indexCapacity);
    }

    std::unique_ptr<IIndexBufferBackend> GlideGraphicsBackend::CreateIndexBuffer32(int indexCapacity)
    {
        // Indices are expanded into the CPU command stream before calling grDrawTriangle, so the
        // historical API's lack of an indexed primitive entry point is not a 16-bit limitation.
        return std::make_unique<GlideIndexBufferBackend>(indexCapacity);
    }

    std::unique_ptr<IOcclusionQueryBackend> GlideGraphicsBackend::CreateOcclusionQuery() { ThrowUnsupported("CreateOcclusionQuery"); }

    void GlideGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                                      const Matrix& world, const Matrix& view, const Matrix& projection,
                                                      PrimitiveType primitive, int primitiveCount)
    {
        const GpuDrawParams params{};
        DrawPrimitiveRange(vb, world, view, projection, primitive, primitiveCount, 0, params);
    }

    void GlideGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                                             const IIndexBufferBackend& ib,
                                                             const Matrix& world, const Matrix& view, const Matrix& projection,
                                                             PrimitiveType primitive, int primitiveCount)
    {
        const GpuDrawParams params{};
        DrawIndexedPrimitiveRange(vb, ib, world, view, projection, primitive, primitiveCount, 0, 0, params);
    }

    namespace
    {
        struct CpuVertex
        {
            float clipX = 0.0f;
            float clipY = 0.0f;
            float clipZ = 0.0f;
            float clipW = 0.0f;
            float r = 255.0f;
            float g = 255.0f;
            float b = 255.0f;
            float a = 255.0f;
            float u = 0.0f;
            float v = 0.0f;
        };

        [[nodiscard]] CpuVertex Interpolate(const CpuVertex& from, const CpuVertex& to, float amount)
        {
            const auto lerp = [amount](float a, float b) { return a + (b - a) * amount; };
            return CpuVertex{
                lerp(from.clipX, to.clipX), lerp(from.clipY, to.clipY),
                lerp(from.clipZ, to.clipZ), lerp(from.clipW, to.clipW),
                lerp(from.r, to.r), lerp(from.g, to.g), lerp(from.b, to.b), lerp(from.a, to.a),
                lerp(from.u, to.u), lerp(from.v, to.v)};
        }

        template <typename Distance>
        [[nodiscard]] std::vector<CpuVertex> ClipPolygon(const std::vector<CpuVertex>& input, Distance distance)
        {
            std::vector<CpuVertex> output;
            if (input.empty())
            {
                return output;
            }
            CpuVertex previous = input.back();
            float previousDistance = distance(previous);
            bool previousInside = previousDistance >= 0.0f;
            for (const CpuVertex& current : input)
            {
                const float currentDistance = distance(current);
                const bool currentInside = currentDistance >= 0.0f;
                if (currentInside != previousInside)
                {
                    const float denominator = previousDistance - currentDistance;
                    if (std::abs(denominator) > 0.0000001f)
                    {
                        output.push_back(Interpolate(previous, current, previousDistance / denominator));
                    }
                }
                if (currentInside)
                {
                    output.push_back(current);
                }
                previous = current;
                previousDistance = currentDistance;
                previousInside = currentInside;
            }
            return output;
        }

        [[nodiscard]] std::vector<CpuVertex> ClipToFrustum(std::vector<CpuVertex> polygon)
        {
            // D3D/XNA clip space: -w <= x,y <= w and 0 <= z <= w. Clip before dividing by w:
            // it is the only safe way to split triangles that cross the eye/near/far planes.
            polygon = ClipPolygon(polygon, [](const CpuVertex& v) { return v.clipW + v.clipX; });
            polygon = ClipPolygon(polygon, [](const CpuVertex& v) { return v.clipW - v.clipX; });
            polygon = ClipPolygon(polygon, [](const CpuVertex& v) { return v.clipW + v.clipY; });
            polygon = ClipPolygon(polygon, [](const CpuVertex& v) { return v.clipW - v.clipY; });
            polygon = ClipPolygon(polygon, [](const CpuVertex& v) { return v.clipZ; });
            polygon = ClipPolygon(polygon, [](const CpuVertex& v) { return v.clipW - v.clipZ; });
            return polygon;
        }

        struct TextureAddressSegment
        {
            float lower = 0.0f;
            float upper = 0.0f;
            // Convert an input coordinate in [lower, upper] back into the logical image's
            // [0, 1] coordinate range before it is converted to a tile-local Glide ST value.
            float scale = 1.0f;
            float offset = 0.0f;
        };

        [[nodiscard]] std::vector<TextureAddressSegment> MakeTextureAddressSegments(
            int addressMode, float minimum, float maximum, float tileBegin, float tileEnd)
        {
            if (minimum > maximum || tileBegin < 0.0f || tileEnd > 1.0f || tileBegin > tileEnd)
            {
                throw std::runtime_error("GLIDE texture-address partition received an invalid coordinate range");
            }

            std::vector<TextureAddressSegment> result;
            const auto appendIntersecting = [&](float lower, float upper, float scale, float offset)
            {
                const float clippedLower = std::max(lower, minimum);
                const float clippedUpper = std::min(upper, maximum);
                if (clippedLower <= clippedUpper)
                {
                    result.push_back(TextureAddressSegment{clippedLower, clippedUpper, scale, offset});
                }
            };

            switch (addressMode)
            {
                case 1: // TextureAddressMode::Clamp
                    appendIntersecting(tileBegin, tileEnd, 1.0f, 0.0f);
                    if (tileBegin == 0.0f && minimum < 0.0f)
                    {
                        appendIntersecting(minimum, 0.0f, 0.0f, 0.0f);
                    }
                    if (tileEnd == 1.0f && maximum > 1.0f)
                    {
                        appendIntersecting(1.0f, maximum, 0.0f, 1.0f);
                    }
                    return result;

                case 0: // TextureAddressMode::Wrap
                case 2: // TextureAddressMode::Mirror
                    break;

                default:
                    throw std::runtime_error("GLIDE backend received an unknown TextureAddressMode value");
            }

            // Logical texture tiling needs one Glide submission for every touched image tile and
            // every repeated unit interval. Keep an explicit limit instead of risking a runaway
            // draw loop from corrupt vertex data; normal CNA geometry is many orders below this.
            constexpr int kMaximumAddressRepeatIntervals = 4096;
            const float firstInterval = std::floor(minimum);
            const float lastInterval = std::floor(maximum);
            if (firstInterval < -static_cast<float>(kMaximumAddressRepeatIntervals) ||
                lastInterval > static_cast<float>(kMaximumAddressRepeatIntervals) ||
                lastInterval - firstInterval >= static_cast<float>(kMaximumAddressRepeatIntervals))
            {
                throw std::runtime_error(
                    "GLIDE 3D texture addressing spans too many Wrap/Mirror repeat intervals");
            }

            for (int interval = static_cast<int>(firstInterval);
                 interval <= static_cast<int>(lastInterval); ++interval)
            {
                const float intervalStart = static_cast<float>(interval);
                if (addressMode == 0 || interval % 2 == 0)
                {
                    appendIntersecting(intervalStart + tileBegin, intervalStart + tileEnd,
                                       1.0f, -intervalStart);
                }
                else
                {
                    appendIntersecting(intervalStart + 1.0f - tileEnd,
                                       intervalStart + 1.0f - tileBegin,
                                       -1.0f, intervalStart + 1.0f);
                }
            }
            return result;
        }

        [[nodiscard]] bool HasFiniteClipCoordinates(const CpuVertex& vertex)
        {
            return std::isfinite(vertex.clipX) && std::isfinite(vertex.clipY) &&
                   std::isfinite(vertex.clipZ) && std::isfinite(vertex.clipW) &&
                   std::isfinite(vertex.r) && std::isfinite(vertex.g) &&
                   std::isfinite(vertex.b) && std::isfinite(vertex.a) &&
                   std::isfinite(vertex.u) && std::isfinite(vertex.v);
        }

        [[nodiscard]] float ClampUnit(float value)
        {
            return std::clamp(value, 0.0f, 1.0f);
        }

        void ValidateFixedFunctionDrawParams(const GpuDrawParams& params)
        {
            if (params.texture1 != nullptr || params.envMap != nullptr || params.dualTexture ||
                params.envMapping || params.pbr || params.skinned || params.instanceCount != 1 ||
                params.instanceVb != nullptr || params.customEffectBackend != nullptr)
            {
                throw std::runtime_error(
                    "GLIDE 3D supports one TMU0 texture and the fixed-function BasicEffect subset only; "
                    "dual textures, environment mapping, PBR, skinning, instancing and custom Effects are unavailable");
            }
            if (params.lightingEnabled && params.preferPerPixelLighting)
            {
                throw std::runtime_error("GLIDE BasicEffect supports only per-vertex lighting, not PreferPerPixelLighting");
            }
        }

        struct AlphaTestState
        {
            FxI32 function = kDepthCompareAlways;
            std::uint8_t reference = 0;
        };

        [[nodiscard]] AlphaTestState DecodeAlphaTest(const GpuDrawParams& params)
        {
            const float x = params.alphaTest[0];
            const float tolerance = params.alphaTest[1];
            const float pass = params.alphaTest[2];
            const float fail = params.alphaTest[3];
            if (pass >= 0.0f && fail >= 0.0f)
            {
                return AlphaTestState{kDepthCompareAlways, 0};
            }
            if (pass < 0.0f && fail < 0.0f)
            {
                return AlphaTestState{kDepthCompareNever, 0};
            }
            const auto toReference = [](float normalized, bool ceiling) -> std::uint8_t
            {
                const float value = normalized * 255.0f;
                const float rounded = ceiling ? std::ceil(value - 0.00001f) : std::round(value);
                return static_cast<std::uint8_t>(std::clamp(rounded, 0.0f, 255.0f));
            };
            if (tolerance > 0.0f)
            {
                return AlphaTestState{
                    pass >= 0.0f ? kDepthCompareEqual : kDepthCompareNotEqual,
                    toReference(x, false)};
            }
            // AlphaTestEffect encodes all ordered comparisons as `a < x` plus pass/fail
            // weights. The conversion below is exact for its discrete 8-bit alpha domain,
            // including the half-texel thresholds used for LessEqual/Greater.
            return AlphaTestState{
                pass >= 0.0f ? kDepthCompareLess : kDepthCompareGreaterEqual,
                toReference(x, true)};
        }
    } // namespace

    void GlideGraphicsBackend::DrawPrimitiveRange(const IVertexBufferBackend& vbIn,
                                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                                  PrimitiveType primitive, int primitiveCount, int vertexStart,
                                                  const GpuDrawParams& params)
    {
        ValidateFixedFunctionDrawParams(params);
        const AlphaTestState alphaTest = DecodeAlphaTest(params);
        impl_->api.grAlphaTestReferenceValue(alphaTest.reference);
        impl_->api.grAlphaTestFunction(alphaTest.function);
        const auto* vb = dynamic_cast<const GlideVertexBufferBackend*>(&vbIn);
        if (vb == nullptr)
        {
            throw std::runtime_error("GLIDE 3D received a vertex buffer created by a different backend");
        }
        const int vertexCount = VertexCountForGlidePrimitives(primitive, primitiveCount);
        if (vertexStart < 0 || vertexStart > vb->GetVertexCount() - vertexCount)
        {
            throw std::runtime_error("GLIDE 3D draw reads outside the supplied vertex buffer");
        }
        const std::size_t stride = vb->Stride();
        const GlideVertexLayout& layout = vb->Layout();
        const bool streamHasColor = layout.HasColor();
        const bool streamHasTexture = layout.HasTextureCoordinate0();
        const bool streamHasNormal = layout.HasNormal();
        const bool textured = params.textureEnabled || params.texture0 != nullptr;
        if (textured && (!streamHasTexture || params.texture0 == nullptr))
        {
            throw std::runtime_error("GLIDE textured draw requires a TMU0 texture and a textured vertex stream");
        }
        if (params.lightingEnabled && !streamHasNormal)
        {
            throw std::runtime_error("GLIDE vertex-lit BasicEffect requires VertexPositionNormalTexture");
        }
        const auto* texture = textured ? dynamic_cast<const GlideTextureBackend*>(params.texture0) : nullptr;
        if (textured && texture == nullptr)
        {
            throw std::runtime_error("GLIDE textured draw received a texture created by a different backend");
        }
        if (textured)
        {
            const_cast<GlideTextureBackend*>(texture)->EnsureAddressMode(
                impl_->samplerAddressU, impl_->samplerAddressV);
        }
        const Matrix wvp = world * view * projection;
        // CNA's matrix convention transforms positions as row vectors. Normals therefore need
        // the transpose of World^{-1}; using World's upper 3x3 is only correct for rigid or
        // uniform-scale transforms and visibly bends directional lighting otherwise.
        std::array<float, 9> normalMatrix{};
        if (params.lightingEnabled)
        {
            const float determinant =
                world.M11 * (world.M22 * world.M33 - world.M23 * world.M32) -
                world.M12 * (world.M21 * world.M33 - world.M23 * world.M31) +
                world.M13 * (world.M21 * world.M32 - world.M22 * world.M31);
            if (!std::isfinite(determinant) || std::abs(determinant) <= 0.000001f)
            {
                throw std::runtime_error(
                    "GLIDE vertex-lit BasicEffect cannot transform normals through a singular World matrix");
            }
            const float inverseDeterminant = 1.0f / determinant;
            // Row-major entries of transpose(inverse(World3x3)).
            normalMatrix = {
                (world.M22 * world.M33 - world.M23 * world.M32) * inverseDeterminant,
                (world.M12 * world.M33 - world.M13 * world.M32) * -inverseDeterminant,
                (world.M12 * world.M23 - world.M13 * world.M22) * inverseDeterminant,
                (world.M21 * world.M33 - world.M23 * world.M31) * -inverseDeterminant,
                (world.M11 * world.M33 - world.M13 * world.M31) * inverseDeterminant,
                (world.M11 * world.M32 - world.M12 * world.M31) * -inverseDeterminant,
                (world.M11 * world.M23 - world.M13 * world.M21) * -inverseDeterminant,
                (world.M11 * world.M22 - world.M12 * world.M21) * inverseDeterminant};
        }
        const auto readVertex = [&](int vertexIndex) -> CpuVertex
        {
            const std::uint8_t* bytes = vb->Bytes().data() + static_cast<std::size_t>(vertexIndex) * stride;
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float u = 0.0f;
            float v = 0.0f;
            float r = 1.0f;
            float g = 1.0f;
            float b = 1.0f;
            float a = 1.0f;
            float nx = 0.0f;
            float ny = 0.0f;
            float nz = 1.0f;
            std::memcpy(&x, bytes + layout.positionOffset + 0, sizeof(float));
            std::memcpy(&y, bytes + layout.positionOffset + 4, sizeof(float));
            std::memcpy(&z, bytes + layout.positionOffset + 8, sizeof(float));
            if (layout.HasColor())
            {
                r = static_cast<float>(bytes[layout.colorOffset + 0]) / 255.0f;
                g = static_cast<float>(bytes[layout.colorOffset + 1]) / 255.0f;
                b = static_cast<float>(bytes[layout.colorOffset + 2]) / 255.0f;
                a = static_cast<float>(bytes[layout.colorOffset + 3]) / 255.0f;
            }
            if (layout.HasTextureCoordinate0())
            {
                std::memcpy(&u, bytes + layout.textureCoordinate0Offset + 0, sizeof(float));
                std::memcpy(&v, bytes + layout.textureCoordinate0Offset + 4, sizeof(float));
            }
            if (layout.HasNormal())
            {
                std::memcpy(&nx, bytes + layout.normalOffset + 0, sizeof(float));
                std::memcpy(&ny, bytes + layout.normalOffset + 4, sizeof(float));
                std::memcpy(&nz, bytes + layout.normalOffset + 8, sizeof(float));
            }
            if (!params.vertexColorEnabled || !streamHasColor)
            {
                r = g = b = a = 1.0f;
            }
            r *= params.diffuseColor[0];
            g *= params.diffuseColor[1];
            b *= params.diffuseColor[2];
            a *= params.diffuseColor[3];
            if (params.lightingEnabled)
            {
                const float worldNx = nx * normalMatrix[0] + ny * normalMatrix[3] + nz * normalMatrix[6];
                const float worldNy = nx * normalMatrix[1] + ny * normalMatrix[4] + nz * normalMatrix[7];
                const float worldNz = nx * normalMatrix[2] + ny * normalMatrix[5] + nz * normalMatrix[8];
                const float length = std::sqrt(worldNx * worldNx + worldNy * worldNy + worldNz * worldNz);
                if (length <= 0.000001f)
                {
                    throw std::runtime_error("GLIDE vertex-lit BasicEffect received a zero-length transformed normal");
                }
                const auto light = [&](const float (&direction)[3], const float (&diffuse)[3], int component) -> float
                {
                    const float amount = std::max(0.0f, -(worldNx * direction[0] + worldNy * direction[1] + worldNz * direction[2]) / length);
                    return diffuse[component] * amount;
                };
                const auto illumination = [&](int component) -> float
                {
                    return params.ambientColor[component] + light(params.light0Dir, params.light0Diffuse, component) +
                           light(params.light1Dir, params.light1Diffuse, component) + light(params.light2Dir, params.light2Diffuse, component);
                };
                r = r * illumination(0) + params.emissiveColor[0];
                g = g * illumination(1) + params.emissiveColor[1];
                b = b * illumination(2) + params.emissiveColor[2];
            }
            if (params.fogEnabled)
            {
                const float fogAmount = ClampUnit(x * params.fogVector[0] + y * params.fogVector[1] + z * params.fogVector[2] + params.fogVector[3]);
                const float keep = 1.0f - fogAmount;
                r = r * keep + params.fogColor[0] * (1.0f - keep);
                g = g * keep + params.fogColor[1] * (1.0f - keep);
                b = b * keep + params.fogColor[2] * (1.0f - keep);
            }
            CpuVertex result{
                x * wvp.M11 + y * wvp.M21 + z * wvp.M31 + wvp.M41,
                x * wvp.M12 + y * wvp.M22 + z * wvp.M32 + wvp.M42,
                x * wvp.M13 + y * wvp.M23 + z * wvp.M33 + wvp.M43,
                x * wvp.M14 + y * wvp.M24 + z * wvp.M34 + wvp.M44,
                ClampUnit(r) * 255.0f, ClampUnit(g) * 255.0f, ClampUnit(b) * 255.0f, ClampUnit(a) * 255.0f, u, v};
            if (!HasFiniteClipCoordinates(result))
            {
                throw std::runtime_error("GLIDE 3D draw received non-finite transformed vertex data");
            }
            return result;
        };
        const auto makeGlideVertex = [&](const CpuVertex& input, const GlideTextureBackend::Tile* tile) -> GlideVertex
        {
            if (input.clipW <= 0.000001f)
            {
                throw std::runtime_error("GLIDE frustum clipper emitted a non-positive homogeneous W");
            }
            const float reciprocalW = 1.0f / input.clipW;
            const float ndcX = input.clipX * reciprocalW;
            const float ndcY = input.clipY * reciprocalW;
            const float ndcZ = input.clipZ * reciprocalW;
            const float s = tile == nullptr ? 0.0f :
                input.u * static_cast<float>(texture->GetWidth()) - tile->sourceX + tile->gutterLeft;
            const float t = tile == nullptr ? 0.0f :
                input.v * static_cast<float>(texture->GetHeight()) - tile->sourceY + tile->gutterTop;
            const float viewportDepth = impl_->viewportMinDepth + ndcZ *
                (impl_->viewportMaxDepth - impl_->viewportMinDepth);
            return GlideVertex{
                static_cast<float>(impl_->viewportX) +
                    (ndcX + 1.0f) * static_cast<float>(impl_->viewportWidth) * 0.5f,
                static_cast<float>(impl_->viewportY) +
                    (1.0f - ndcY) * static_cast<float>(impl_->viewportHeight) * 0.5f,
                static_cast<float>(ToGlideDepth(viewportDepth)), reciprocalW,
                input.r, input.g, input.b, input.a, ndcZ,
                s * reciprocalW, t * reciprocalW, reciprocalW};
        };
        std::vector<GlideVertex> pendingTriangles;
        constexpr std::size_t kMaximumBatchedVertices = 3u * 1024u;
        const auto flushTriangleBatch = [&]
        {
            if (pendingTriangles.empty())
            {
                return;
            }
            std::vector<void*> pointers;
            pointers.reserve(pendingTriangles.size());
            for (GlideVertex& vertex : pendingTriangles)
            {
                pointers.push_back(&vertex);
            }
            impl_->api.grDrawVertexArray(kPrimitiveTriangles, static_cast<FxU32>(pointers.size()), pointers.data());
            pendingTriangles.clear();
        };
        const auto drawFan = [&](const std::vector<CpuVertex>& polygon, const GlideTextureBackend::Tile* tile)
        {
            for (std::size_t index = 1; index + 1 < polygon.size(); ++index)
            {
                pendingTriangles.push_back(makeGlideVertex(polygon[0], tile));
                pendingTriangles.push_back(makeGlideVertex(polygon[index], tile));
                pendingTriangles.push_back(makeGlideVertex(polygon[index + 1], tile));
                if (pendingTriangles.size() >= kMaximumBatchedVertices)
                {
                    flushTriangleBatch();
                }
            }
        };
        if (textured)
        {
            impl_->ConfigureSpriteCombiner();
            const GlideSamplerSettings sampler = ToGlideSamplerSettings(impl_->samplerFilter);
            for (const GlideTextureBackend::Tile& tile : texture->Tiles())
            {
                impl_->api.grTexSource(0, tile.range.address, kMipMapBoth, const_cast<GlideTexInfo*>(&tile.nativeInfo));
                impl_->api.grTexFilterMode(0, sampler.minFilter, sampler.magFilter);
                // CPU partitions the logical image at every tile and address-mode boundary, so
                // each submitted polygon is sampled only from its selected native tile.
                impl_->api.grTexClampMode(0, kTexClampClamp, kTexClampClamp);
                impl_->api.grTexMipMapMode(0, kMipMapNearest, sampler.lodBlend);
                const auto drawTriangleForTile = [&](CpuVertex a, CpuVertex b, CpuVertex c)
                {
                    std::vector<CpuVertex> polygon{a, b, c};
                    polygon = ClipToFrustum(std::move(polygon));
                    if (polygon.size() < 3)
                    {
                        return;
                    }
                    const float u0 = static_cast<float>(tile.sourceX) / texture->GetWidth();
                    const float u1 = static_cast<float>(tile.sourceX + tile.sourceWidth) / texture->GetWidth();
                    const float v0 = static_cast<float>(tile.sourceY) / texture->GetHeight();
                    const float v1 = static_cast<float>(tile.sourceY + tile.sourceHeight) / texture->GetHeight();
                    const auto minMaxU = std::minmax_element(
                        polygon.begin(), polygon.end(), [](const CpuVertex& left, const CpuVertex& right)
                        {
                            return left.u < right.u;
                        });
                    const auto minMaxV = std::minmax_element(
                        polygon.begin(), polygon.end(), [](const CpuVertex& left, const CpuVertex& right)
                        {
                            return left.v < right.v;
                        });
                    const std::vector<TextureAddressSegment> uSegments = MakeTextureAddressSegments(
                        impl_->samplerAddressU, minMaxU.first->u, minMaxU.second->u, u0, u1);
                    const std::vector<TextureAddressSegment> vSegments = MakeTextureAddressSegments(
                        impl_->samplerAddressV, minMaxV.first->v, minMaxV.second->v, v0, v1);
                    for (const TextureAddressSegment& uSegment : uSegments)
                    {
                        std::vector<CpuVertex> uPolygon = ClipPolygon(
                            polygon, [uSegment](const CpuVertex& v) { return v.u - uSegment.lower; });
                        uPolygon = ClipPolygon(
                            uPolygon, [uSegment](const CpuVertex& v) { return uSegment.upper - v.u; });
                        if (uPolygon.size() < 3)
                        {
                            continue;
                        }
                        for (const TextureAddressSegment& vSegment : vSegments)
                        {
                            std::vector<CpuVertex> tilePolygon = ClipPolygon(
                                uPolygon, [vSegment](const CpuVertex& v) { return v.v - vSegment.lower; });
                            tilePolygon = ClipPolygon(
                                tilePolygon, [vSegment](const CpuVertex& v) { return vSegment.upper - v.v; });
                            if (tilePolygon.size() < 3)
                            {
                                continue;
                            }
                            for (CpuVertex& vertex : tilePolygon)
                            {
                                vertex.u = vertex.u * uSegment.scale + uSegment.offset;
                                vertex.v = vertex.v * vSegment.scale + vSegment.offset;
                            }
                            drawFan(tilePolygon, &tile);
                        }
                    }
                };
                for (int triangle = 0; triangle < primitiveCount; ++triangle)
                {
                    const int offset = vertexStart + (primitive == PrimitiveType::TriangleList ? triangle * 3 : triangle);
                    if (primitive == PrimitiveType::TriangleList || (triangle & 1) == 0)
                    {
                        drawTriangleForTile(readVertex(offset), readVertex(offset + 1), readVertex(offset + 2));
                    }
                    else
                    {
                        drawTriangleForTile(readVertex(offset + 1), readVertex(offset), readVertex(offset + 2));
                    }
                }
                flushTriangleBatch();
            }
        }
        else
        {
            impl_->ConfigureColoredCombiner();
            for (int triangle = 0; triangle < primitiveCount; ++triangle)
            {
                const int offset = vertexStart + (primitive == PrimitiveType::TriangleList ? triangle * 3 : triangle);
                std::vector<CpuVertex> polygon;
                if (primitive == PrimitiveType::TriangleList || (triangle & 1) == 0)
                {
                    polygon = {readVertex(offset), readVertex(offset + 1), readVertex(offset + 2)};
                }
                else
                {
                    polygon = {readVertex(offset + 1), readVertex(offset), readVertex(offset + 2)};
                }
                polygon = ClipToFrustum(std::move(polygon));
                if (polygon.size() >= 3)
                {
                    drawFan(polygon, nullptr);
                }
            }
            flushTriangleBatch();
        }
    }

    void GlideGraphicsBackend::DrawIndexedPrimitiveRange(
        const IVertexBufferBackend& vbIn, const IIndexBufferBackend& ibIn,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, int startIndex, int baseVertex, const GpuDrawParams& params)
    {
        const auto* vb = dynamic_cast<const GlideVertexBufferBackend*>(&vbIn);
        const auto* ib = dynamic_cast<const GlideIndexBufferBackend*>(&ibIn);
        if (vb == nullptr || ib == nullptr)
        {
            throw std::runtime_error("GLIDE 3D received a buffer created by a different backend");
        }
        const int indexCount = VertexCountForGlidePrimitives(primitive, primitiveCount);
        if (startIndex < 0 || startIndex > ib->GetIndexCount() - indexCount)
        {
            throw std::runtime_error("GLIDE 3D indexed draw reads outside the supplied index buffer");
        }
        std::vector<std::uint8_t> ordered(static_cast<std::size_t>(indexCount) * vb->Stride());
        for (int index = 0; index < indexCount; ++index)
        {
            const std::int64_t resolved = static_cast<std::int64_t>(ib->IndexAt(startIndex + index)) + baseVertex;
            if (resolved < 0 || resolved >= vb->GetVertexCount())
            {
                throw std::runtime_error("GLIDE 3D index plus baseVertex lies outside the vertex buffer");
            }
            std::memcpy(ordered.data() + static_cast<std::size_t>(index) * vb->Stride(),
                        vb->Bytes().data() + static_cast<std::size_t>(resolved) * vb->Stride(), vb->Stride());
        }
        GlideVertexBufferBackend expanded(indexCount);
        expanded.SetData(ordered.data(), indexCount, vb->Stride());
        DrawPrimitiveRange(expanded, world, view, projection, primitive, primitiveCount, 0, params);
    }

    void GlideGraphicsBackend::DrawPrimitivesEx(const IVertexBufferBackend& vb,
                                                const Matrix& world, const Matrix& view, const Matrix& projection,
                                                PrimitiveType primitive, int primitiveCount,
                                                const GpuDrawParams& params)
    {
        DrawPrimitiveRange(vb, world, view, projection, primitive, primitiveCount, params.vertexStart, params);
    }

    void GlideGraphicsBackend::DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb,
                                                       const IIndexBufferBackend& ib,
                                                       const Matrix& world, const Matrix& view, const Matrix& projection,
                                                       PrimitiveType primitive, int primitiveCount,
                                                       const GpuDrawParams& params)
    {
        DrawIndexedPrimitiveRange(vb, ib, world, view, projection, primitive, primitiveCount,
                                  params.startIndex, params.baseVertex, params);
    }
} // namespace CNA::Internal::Backends::Glide

namespace CNA::Internal::Backends
{
#ifdef CNA_BACKEND_GLIDE
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<Glide::GlideGraphicsBackend>(args);
    }
#endif
} // namespace CNA::Internal::Backends
