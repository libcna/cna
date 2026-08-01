#include "CNA/Internal/Backends/Glide/GlideGraphicsBackend.hpp"
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
        constexpr FxU32 kWindowCoords = 0x00;

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
        constexpr FxI32 kDepthCompareGreater = 0x4;

        constexpr FxI32 kTexFilterPoint = 0x0;
        constexpr FxI32 kTexFilterBilinear = 0x1;
        constexpr FxI32 kTexClampWrap = 0x0;
        constexpr FxI32 kTexClampClamp = 0x1;
        constexpr FxI32 kTexClampMirror = 0x2;
        constexpr FxI32 kTexFormatArgb4444 = 0xc;
        constexpr FxU32 kMipMapBoth = 0x3;
        constexpr FxU32 kLfbSrc565 = 0x0;

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

        [[nodiscard]] int NextPowerOfTwo(int value)
        {
            int result = 1;
            while (result < value && result < 256)
            {
                result <<= 1;
            }
            if (result < value)
            {
                throw std::runtime_error("GLIDE texture dimension exceeds the 256-pixel initial-scope limit");
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

        [[nodiscard]] int ScaleTextureDimension(int dimension, int largestDimension)
        {
            return std::max(1, (dimension * 256 + largestDimension - 1) / largestDimension);
        }

        struct GlideDisplayMode
        {
            FxI32 resolution;
            int width;
            int height;
        };

        [[nodiscard]] GlideDisplayMode SelectDisplayMode(int requestedWidth, int requestedHeight)
        {
            if (requestedWidth <= 640 && requestedHeight <= 480)
            {
                return GlideDisplayMode{kResolution640x480, 640, 480};
            }
            if (requestedWidth <= 800 && requestedHeight <= 600)
            {
                return GlideDisplayMode{kResolution800x600, 800, 600};
            }
            throw std::runtime_error(
                "GLIDE backend's initial display-mode scope supports virtual resolutions only through 800x600");
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
            using ColorCombineFn = void (WINAPI*)(FxI32, FxI32, FxI32, FxI32, FxBool);
            using AlphaCombineFn = void (WINAPI*)(FxI32, FxI32, FxI32, FxI32, FxBool);
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
                grColorCombine = Required<ColorCombineFn>("grColorCombine", 20);
                grAlphaCombine = Required<AlphaCombineFn>("grAlphaCombine", 20);
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
                grTexCombine = Required<TexCombineFn>("grTexCombine", 28);
                grLfbReadRegion = Required<LfbReadRegionFn>("grLfbReadRegion", 28);
            }

            GlideInitFn grGlideInit = nullptr;
            GlideShutdownFn grGlideShutdown = nullptr;
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
            ColorCombineFn grColorCombine = nullptr;
            AlphaCombineFn grAlphaCombine = nullptr;
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
            const GlideDisplayMode displayMode = SelectDisplayMode(virtualWidth, virtualHeight);
            nativeWidth = displayMode.width;
            nativeHeight = displayMode.height;

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
                ApplyClipWindow(0, 0, virtualWidth, virtualHeight);

                const FxU32 minAddress = api.grTexMinAddress(0);
                const FxU32 maxAddress = api.grTexMaxAddress(0);
                if (maxAddress <= minAddress)
                {
                    throw std::runtime_error("Glide TMU0 reported no usable texture memory");
                }
                freeTextureRanges.push_back(TextureRange{minAddress, maxAddress - minAddress + 1});
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
                api.grDepthBufferFunction(kDepthCompareGreater);
                api.grDepthMask(depthWriteEnabled ? kFxTrue : kFxFalse);
            }
        }

        void ClearDepthOnly(std::uint16_t depth)
        {
            api.grDepthBufferMode(kDepthBufferZ);
            api.grDepthMask(kFxTrue);
            api.grColorMask(kFxFalse, kFxFalse);
            api.grBufferClear(0, 0, depth);
            api.grColorMask(kFxTrue, kFxTrue);
            ApplyDepthState();
        }

        void ClearColorOnly(FxU32 color, std::uint8_t alpha)
        {
            // grBufferClear clears every enabled buffer. Preserve the auxiliary depth plane for
            // GraphicsDevice::Clear(Color), which is specified to be color-only.
            api.grDepthBufferMode(kDepthBufferZ);
            api.grDepthMask(kFxFalse);
            api.grBufferClear(color, alpha, 0);
            ApplyDepthState();
        }

        void ClearColorAndDepth(FxU32 color, std::uint8_t alpha, std::uint16_t depth)
        {
            api.grDepthBufferMode(kDepthBufferZ);
            api.grDepthMask(kFxTrue);
            api.grBufferClear(color, alpha, depth);
            ApplyDepthState();
        }

        void ApplyClipWindow(int x, int y, int width, int height)
        {
            if (width < 0 || height < 0 || x < 0 || y < 0 || x + width > virtualWidth || y + height > virtualHeight)
            {
                throw std::runtime_error("GLIDE scissor rectangle lies outside the active virtual framebuffer");
            }
            api.grClipWindow(static_cast<FxU32>(x), static_cast<FxU32>(y),
                             static_cast<FxU32>(x + width), static_cast<FxU32>(y + height));
        }

        [[nodiscard]] TextureRange AllocateTexture(FxU32 size)
        {
            constexpr FxU32 alignment = 8;
            for (auto it = freeTextureRanges.begin(); it != freeTextureRanges.end(); ++it)
            {
                const FxU32 alignedAddress = (it->address + alignment - 1) & ~(alignment - 1);
                const FxU32 padding = alignedAddress - it->address;
                if (padding <= it->size && size <= it->size - padding)
                {
                    const TextureRange allocation{alignedAddress, size};
                    const FxU32 oldEnd = it->address + it->size;
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
                        const FxU32 allocationEnd = alignedAddress + size;
                        if (allocationEnd < oldEnd)
                        {
                            freeTextureRanges.insert(std::next(it), TextureRange{allocationEnd, oldEnd - allocationEnd});
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
                if (previous->address + previous->size == position->address)
                {
                    previous->size += position->size;
                    position = freeTextureRanges.erase(position);
                    position = previous;
                }
            }
            const auto next = std::next(position);
            if (next != freeTextureRanges.end() && position->address + position->size == next->address)
            {
                position->size += next->size;
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
        std::vector<TextureRange> freeTextureRanges;
    };

    class GlideTextureBackend final : public ITextureBackend
    {
    public:
        GlideTextureBackend(GlideGraphicsBackend& owner, const ImageData& data)
            : owner_(owner)
            , width_(data.width)
            , height_(data.height)
        {
            if (width_ <= 0 || height_ <= 0)
            {
                throw std::runtime_error("GLIDE texture dimensions must be positive");
            }
            const int largestDimension = std::max(width_, height_);
            uploadWidth_ = largestDimension > 256 ? ScaleTextureDimension(width_, largestDimension) : width_;
            uploadHeight_ = largestDimension > 256 ? ScaleTextureDimension(height_, largestDimension) : height_;
            paddedWidth_ = NextPowerOfTwo(uploadWidth_);
            paddedHeight_ = NextPowerOfTwo(uploadHeight_);
            while (paddedWidth_ > paddedHeight_ * 8) paddedHeight_ <<= 1;
            while (paddedHeight_ > paddedWidth_ * 8) paddedWidth_ <<= 1;
            if (paddedWidth_ > 256 || paddedHeight_ > 256)
            {
                throw std::runtime_error("GLIDE texture exceeds the supported 256x256/8:1 Glide 3.x texture envelope");
            }

            rgba_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4u, 0);
            std::memcpy(rgba_.data(), data.pixels.data(), std::min(rgba_.size(), data.pixels.size()));
            ConvertToGlideTexels();
            nativeInfo_ = GlideTexInfo{
                Log2PowerOfTwo(paddedWidth_), Log2PowerOfTwo(paddedWidth_),
                Log2PowerOfTwo(paddedWidth_) - Log2PowerOfTwo(paddedHeight_),
                kTexFormatArgb4444, nativeTexels_.data() };

            const FxU32 requiredBytes = owner_.impl_->api.grTexTextureMemRequired(kMipMapBoth, &nativeInfo_);
            if (requiredBytes == 0)
            {
                throw std::runtime_error("grTexTextureMemRequired rejected this ARGB4444 Glide texture");
            }
            range_ = owner_.impl_->AllocateTexture(requiredBytes);
            Upload();
        }

        ~GlideTextureBackend() override
        {
            if (range_.size != 0 && owner_.impl_)
            {
                owner_.impl_->ReleaseTexture(range_);
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
            ConvertToGlideTexels();
            Upload();
        }

        void UpdatePixelsLevel(int level, const std::uint8_t* rgba, int levelWidth, int levelHeight) override
        {
            if (level != 0 || levelWidth != width_ || levelHeight != height_)
            {
                throw std::runtime_error("GLIDE backend supports only level-0 texture uploads");
            }
            UpdatePixels(rgba, width_ * 4);
        }

        [[nodiscard]] FxU32 Address() const { return range_.address; }
        [[nodiscard]] const GlideTexInfo& NativeInfo() const { return nativeInfo_; }
        [[nodiscard]] int PaddedWidth() const { return paddedWidth_; }
        [[nodiscard]] int PaddedHeight() const { return paddedHeight_; }
        [[nodiscard]] float SourceToNativeX(float sourceX) const
        {
            return sourceX * static_cast<float>(uploadWidth_) / static_cast<float>(width_);
        }
        [[nodiscard]] float SourceToNativeY(float sourceY) const
        {
            return sourceY * static_cast<float>(uploadHeight_) / static_cast<float>(height_);
        }

    private:
        void ConvertToGlideTexels()
        {
            nativeTexels_.resize(static_cast<std::size_t>(paddedWidth_) * static_cast<std::size_t>(paddedHeight_));
            for (int y = 0; y < paddedHeight_; ++y)
            {
                const int sourceY = std::min(y * height_ / uploadHeight_, height_ - 1);
                for (int x = 0; x < paddedWidth_; ++x)
                {
                    const int sourceX = std::min(x * width_ / uploadWidth_, width_ - 1);
                    nativeTexels_[static_cast<std::size_t>(y) * paddedWidth_ + x] = RgbaToArgb4444(
                        rgba_.data() + (static_cast<std::size_t>(sourceY) * width_ + sourceX) * 4u);
                }
            }
            nativeInfo_.data = nativeTexels_.data();
        }

        void Upload()
        {
            nativeInfo_.data = nativeTexels_.data();
            owner_.impl_->api.grTexDownloadMipMap(0, range_.address, kMipMapBoth, &nativeInfo_);
        }

        GlideGraphicsBackend& owner_;
        int width_ = 0;
        int height_ = 0;
        int uploadWidth_ = 0;
        int uploadHeight_ = 0;
        int paddedWidth_ = 0;
        int paddedHeight_ = 0;
        TextureRange range_{};
        GlideTexInfo nativeInfo_{};
        std::vector<std::uint8_t> rgba_;
        std::vector<std::uint16_t> nativeTexels_;

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
            if (data == nullptr || vertexCount < 0 || vertexCount > capacity_ || strideInBytes != sizeof(CNA::Internal::Graphics::PositionColorStream))
            {
                throw std::runtime_error(
                    "GLIDE 3D accepts only a non-null VertexPositionColor stream (16 bytes per vertex) within its declared capacity");
            }
            bytes_.resize(static_cast<std::size_t>(vertexCount) * strideInBytes);
            std::memcpy(bytes_.data(), data, bytes_.size());
            vertexCount_ = vertexCount;
        }

        void SetVertexDeclaration(const VertexDeclaration& declaration) override
        {
            if (declaration.getVertexStrideProperty() != static_cast<int>(sizeof(CNA::Internal::Graphics::PositionColorStream)))
            {
                throw std::runtime_error(
                    "GLIDE 3D supports only the VertexPositionColor declaration (float3 position + RGBA8 color)");
            }
        }

        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }
        [[nodiscard]] const std::vector<std::uint8_t>& Bytes() const { return bytes_; }

    private:
        int capacity_ = 0;
        int vertexCount_ = 0;
        std::vector<std::uint8_t> bytes_;
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
        : impl_(std::make_unique<Impl>(args))
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
        impl_->ApplyClipWindow(0, 0, width, height);
    }

    void GlideGraphicsBackend::SetPresentationMode(int mode)
    {
        impl_->presentationMode = static_cast<CnaPresentationMode>(mode);
    }

    void GlideGraphicsBackend::SetSwapInterval(int interval)
    {
        impl_->swapInterval = std::max(0, interval);
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
        impl_->ApplyClipWindow(x, y, w, h);
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
        if (!ColorWriteHasRed(writeState.colorWriteChannels[0]) ||
            !ColorWriteHasGreen(writeState.colorWriteChannels[0]) ||
            !ColorWriteHasBlue(writeState.colorWriteChannels[0]) ||
            !ColorWriteHasAlpha(writeState.colorWriteChannels[0]) ||
            writeState.multiSampleMask != std::numeric_limits<unsigned int>::max())
        {
            throw std::runtime_error("GLIDE backend does not support partial color-write or multisample masks");
        }
        impl_->colorSrcBlend = ToGlideBlend(colorSrcBlend);
        impl_->colorDstBlend = ToGlideBlend(colorDstBlend);
        impl_->alphaSrcBlend = ToGlideBlend(alphaSrcBlend);
        impl_->alphaDstBlend = ToGlideBlend(alphaDstBlend);
        impl_->ApplyBlendState();
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

        // A preceding colored 3D draw leaves the fixed function combiner untextured. Restore
        // the SpriteBatch path explicitly instead of relying on accidental Glide state.
        impl_->ConfigureSpriteCombiner();

        impl_->api.grTexSource(0, glideTexture->Address(), kMipMapBoth,
                                const_cast<GlideTexInfo*>(&glideTexture->NativeInfo()));
        const FxI32 filter = textureFilter == 0 ? kTexFilterBilinear : kTexFilterPoint;
        impl_->api.grTexFilterMode(0, filter, filter);
        impl_->api.grTexClampMode(0, ToGlideTextureAddress(addressU), ToGlideTextureAddress(addressV));

        float u0 = glideTexture->SourceToNativeX(static_cast<float>(sourceRectangle.X));
        float v0 = glideTexture->SourceToNativeY(static_cast<float>(sourceRectangle.Y));
        float u1 = glideTexture->SourceToNativeX(static_cast<float>(sourceRectangle.X + sourceRectangle.Width));
        float v1 = glideTexture->SourceToNativeY(static_cast<float>(sourceRectangle.Y + sourceRectangle.Height));
        if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0)
        {
            std::swap(u0, u1);
        }
        if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) != 0)
        {
            std::swap(v0, v1);
        }

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

        const std::array<Vector2, 4> positions = {
            place(0.0f, 0.0f), place(sourceWidth, 0.0f),
            place(sourceWidth, sourceHeight), place(0.0f, sourceHeight) };
        // Glide's ST parameters are texel-space s/w and t/w values, not normalized UVs.
        // The texture can be downscaled and padded for historical Glide limits, so source
        // coordinates were converted to the uploaded texel space above.
        const std::array<Vector2, 4> texcoords = {
            Vector2(u0, v0), Vector2(u1, v0), Vector2(u1, v1), Vector2(u0, v1) };
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
        const GlideVertex topLeft = makeVertex(0);
        const GlideVertex topRight = makeVertex(1);
        const GlideVertex bottomRight = makeVertex(2);
        const GlideVertex bottomLeft = makeVertex(3);
        impl_->api.grDrawTriangle(&topLeft, &topRight, &bottomRight);
        impl_->api.grDrawTriangle(&topLeft, &bottomRight, &bottomLeft);
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
        DrawColoredPrimitiveRange(vb, world, view, projection, primitive, primitiveCount, 0);
    }

    void GlideGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                                             const IIndexBufferBackend& ib,
                                                             const Matrix& world, const Matrix& view, const Matrix& projection,
                                                             PrimitiveType primitive, int primitiveCount)
    {
        DrawIndexedColoredPrimitiveRange(vb, ib, world, view, projection, primitive, primitiveCount, 0, 0);
    }

    void GlideGraphicsBackend::DrawColoredPrimitiveRange(const IVertexBufferBackend& vbIn,
                                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                                          PrimitiveType primitive, int primitiveCount, int vertexStart)
    {
        const auto* vb = dynamic_cast<const GlideVertexBufferBackend*>(&vbIn);
        if (vb == nullptr)
        {
            throw std::runtime_error("GLIDE 3D received a vertex buffer created by a different backend");
        }
        const int vertexCount = VertexCountForGlidePrimitives(primitive, primitiveCount);
        if (vertexStart < 0 || vertexStart > vb->GetVertexCount() - vertexCount)
        {
            throw std::runtime_error("GLIDE 3D draw reads outside the supplied VertexPositionColor buffer");
        }

        const Matrix wvp = world * view * projection;
        const auto project = [&](int vertexIndex, GlideVertex& result) -> bool
        {
            CNA::Internal::Graphics::PositionColorStream vertex{};
            std::memcpy(&vertex, vb->Bytes().data() + static_cast<std::size_t>(vertexIndex) * sizeof(vertex), sizeof(vertex));
            const float clipX = vertex.x * wvp.M11 + vertex.y * wvp.M21 + vertex.z * wvp.M31 + wvp.M41;
            const float clipY = vertex.x * wvp.M12 + vertex.y * wvp.M22 + vertex.z * wvp.M32 + wvp.M42;
            const float clipZ = vertex.x * wvp.M13 + vertex.y * wvp.M23 + vertex.z * wvp.M33 + wvp.M43;
            const float clipW = vertex.x * wvp.M14 + vertex.y * wvp.M24 + vertex.z * wvp.M34 + wvp.M44;
            if (!std::isfinite(clipX) || !std::isfinite(clipY) || !std::isfinite(clipZ) ||
                !std::isfinite(clipW) || clipW <= 0.000001f)
            {
                return false;
            }
            const float reciprocalW = 1.0f / clipW;
            const float ndcX = clipX * reciprocalW;
            const float ndcY = clipY * reciprocalW;
            const float ndcZ = clipZ * reciprocalW;
            // A full homogeneous frustum clipper is deliberately a follow-up task. Rejecting a
            // crossing triangle is safe and prevents a behind-camera vertex from corrupting a
            // real Glide FIFO; fully visible triangles still use native hardware rasterization.
            if (ndcZ < 0.0f || ndcZ > 1.0f)
            {
                return false;
            }
            result = GlideVertex{
                (ndcX + 1.0f) * static_cast<float>(impl_->virtualWidth) * 0.5f,
                (1.0f - ndcY) * static_cast<float>(impl_->virtualHeight) * 0.5f,
                static_cast<float>(ToGlideDepth(ndcZ)), reciprocalW,
                static_cast<float>(vertex.r), static_cast<float>(vertex.g),
                static_cast<float>(vertex.b), static_cast<float>(vertex.a),
                ndcZ, 0.0f, 0.0f, reciprocalW};
            return true;
        };
        const auto draw = [&](int a, int b, int c)
        {
            GlideVertex vertices[3]{};
            if (project(a, vertices[0]) && project(b, vertices[1]) && project(c, vertices[2]))
            {
                impl_->api.grDrawTriangle(&vertices[0], &vertices[1], &vertices[2]);
            }
        };

        impl_->ConfigureColoredCombiner();
        if (primitive == PrimitiveType::TriangleList)
        {
            for (int triangle = 0; triangle < primitiveCount; ++triangle)
            {
                const int offset = vertexStart + triangle * 3;
                draw(offset, offset + 1, offset + 2);
            }
        }
        else
        {
            for (int triangle = 0; triangle < primitiveCount; ++triangle)
            {
                const int offset = vertexStart + triangle;
                // Flipping every second strip triangle preserves the source winding for callers
                // that later enable native culling through an extended Glide state path.
                if ((triangle & 1) == 0) draw(offset, offset + 1, offset + 2);
                else                     draw(offset + 1, offset, offset + 2);
            }
        }
    }

    void GlideGraphicsBackend::DrawIndexedColoredPrimitiveRange(
        const IVertexBufferBackend& vbIn, const IIndexBufferBackend& ibIn,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, int startIndex, int baseVertex)
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
        if (vb->GetVertexCount() <= 0)
        {
            throw std::runtime_error("GLIDE 3D indexed draw requires a non-empty vertex buffer");
        }

        std::vector<CNA::Internal::Graphics::PositionColorStream> vertices(
            static_cast<std::size_t>(vb->GetVertexCount()));
        std::memcpy(vertices.data(), vb->Bytes().data(), vb->Bytes().size());
        std::vector<std::uint32_t> remapped(static_cast<std::size_t>(indexCount));
        for (int index = 0; index < indexCount; ++index)
        {
            const std::int64_t resolved = static_cast<std::int64_t>(ib->IndexAt(startIndex + index)) + baseVertex;
            if (resolved < 0 || resolved >= vb->GetVertexCount())
            {
                throw std::runtime_error("GLIDE 3D index plus baseVertex lies outside the vertex buffer");
            }
            remapped[static_cast<std::size_t>(index)] = static_cast<std::uint32_t>(resolved);
        }

        // Glide only exposes grDrawTriangle, so indexed topology is expanded into a compact
        // transient vertex stream. This retains 32-bit CNA index support without a fake API path.
        std::vector<CNA::Internal::Graphics::PositionColorStream> ordered(static_cast<std::size_t>(indexCount));
        for (int index = 0; index < indexCount; ++index)
        {
            ordered[static_cast<std::size_t>(index)] = vertices[remapped[static_cast<std::size_t>(index)]];
        }
        GlideVertexBufferBackend orderedBuffer(indexCount);
        orderedBuffer.SetData(ordered.data(), indexCount, sizeof(ordered.front()));
        DrawColoredPrimitiveRange(orderedBuffer, world, view, projection, primitive, primitiveCount, 0);
    }

    namespace
    {
        void ValidateFixedFunctionDrawParams(const GpuDrawParams& params)
        {
            const auto isOne = [](float value) { return std::abs(value - 1.0f) < 0.00001f; };
            if (params.textureEnabled || params.texture0 != nullptr || params.texture1 != nullptr ||
                params.envMap != nullptr || params.dualTexture || params.envMapping || params.pbr ||
                params.skinned || params.lightingEnabled || params.fogEnabled || !params.vertexColorEnabled ||
                !isOne(params.diffuseColor[0]) || !isOne(params.diffuseColor[1]) ||
                !isOne(params.diffuseColor[2]) || !isOne(params.diffuseColor[3]))
            {
                throw std::runtime_error(
                    "GLIDE 3D currently supports only untextured, unlit VertexPositionColor draws with a white BasicEffect");
            }
            if (params.instanceCount != 1 || params.instanceVb != nullptr || params.customEffectBackend != nullptr)
            {
                throw std::runtime_error("GLIDE 3D does not support instancing or custom Effect programs");
            }
        }
    } // namespace

    void GlideGraphicsBackend::DrawPrimitivesEx(const IVertexBufferBackend& vb,
                                                const Matrix& world, const Matrix& view, const Matrix& projection,
                                                PrimitiveType primitive, int primitiveCount,
                                                const GpuDrawParams& params)
    {
        ValidateFixedFunctionDrawParams(params);
        DrawColoredPrimitiveRange(vb, world, view, projection, primitive, primitiveCount, params.vertexStart);
    }

    void GlideGraphicsBackend::DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb,
                                                       const IIndexBufferBackend& ib,
                                                       const Matrix& world, const Matrix& view, const Matrix& projection,
                                                       PrimitiveType primitive, int primitiveCount,
                                                       const GpuDrawParams& params)
    {
        ValidateFixedFunctionDrawParams(params);
        DrawIndexedColoredPrimitiveRange(vb, ib, world, view, projection, primitive, primitiveCount,
                                         params.startIndex, params.baseVertex);
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
