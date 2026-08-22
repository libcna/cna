#include "CNA/Internal/Renderers/Glide/GlideRenderer.hpp"
#include "CNA/Internal/Renderers/Glide/GlideAbi.hpp"
#include "CNA/Internal/Renderers/Glide/GlideBlendFactor.hpp"
#include "CNA/Internal/Renderers/Glide/GlideCapability.hpp"
#include "CNA/Internal/Renderers/Glide/GlideDisplayModeSelection.hpp"
#include "CNA/Internal/Renderers/Glide/GlideDrawValidation.hpp"
#include "CNA/Internal/Renderers/Glide/GlideExtensionCapabilities.hpp"
#include "CNA/Internal/Renderers/Glide/GlideLighting.hpp"
#include "CNA/Internal/Renderers/Glide/GlidePrimitiveClip.hpp"
#include "CNA/Internal/Renderers/Glide/GlideTextureCoordinate.hpp"
#include "CNA/Internal/Renderers/Glide/GlideTextureEviction.hpp"
#include "CNA/Internal/Renderers/Glide/GlideTextureFormat.hpp"
#include "CNA/Internal/Renderers/Glide/GlideTextureMip.hpp"
#include "CNA/Internal/Renderers/Glide/GlideVertexLayout.hpp"
#include "CNA/Internal/Graphics/BuiltInVertexStreams.hpp"

#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace CNA::Internal::Renderers::Glide
{
    namespace
    {
        using Abi::FxBool;
        using Abi::FxI32;
        using Abi::FxU32;
        using Abi::GlideResolution;
        using Abi::GlideTexInfo;
        using Abi::GlideVertex;

        constexpr FxBool kFxFalse = 0;
        constexpr FxBool kFxTrue = 1;

        // The values below are part of the public Glide 3.x ABI. Function signatures and native
        // layouts live in GlideAbi.hpp so the independent x86 fake-DLL contract can audit them.
        constexpr FxI32 kColorFormatArgb = 0x0;
        constexpr FxI32 kOriginUpperLeft = 0x0;
        constexpr FxI32 kBufferBack = 0x1;
        constexpr FxI32 kCullDisable = 0x0;
        constexpr FxI32 kCullNegative = 0x1;
        constexpr FxI32 kCullPositive = 0x2;
        constexpr FxI32 kPrimitivePoints = 0x0;
        constexpr FxI32 kPrimitiveLines = 0x2;
        constexpr FxI32 kPrimitiveTriangles = 0x6;
        constexpr FxU32 kWindowCoords = 0x00;
        constexpr FxI32 kQueryAny = -1;

        constexpr FxU32 kParamXY = 0x01;
        constexpr FxU32 kParamZ = 0x02;
        constexpr FxU32 kParamQ = 0x04;
        constexpr FxU32 kParamA = 0x10;
        constexpr FxU32 kParamRgb = 0x20;
        constexpr FxU32 kParamSt0 = 0x40;
        constexpr FxU32 kParamSt1 = 0x41;
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
        constexpr FxI32 kTexFormatRgb565 = 0xa;
        constexpr FxI32 kTexFormatArgb1555 = 0xb;
        constexpr FxI32 kTexFormatArgb4444 = 0xc;

        [[nodiscard]] FxI32 ToGlideNativeTextureFormat(GlideTextureAlphaClass alphaClass)
        {
            switch (alphaClass)
            {
                case GlideTextureAlphaClass::Opaque: return kTexFormatRgb565;
                case GlideTextureAlphaClass::Binary: return kTexFormatArgb1555;
                case GlideTextureAlphaClass::Fractional: return kTexFormatArgb4444;
            }
            return kTexFormatArgb4444;
        }
        constexpr FxI32 kMipMapNearest = 0x1;
        constexpr FxU32 kMipMapBoth = 0x3;
        constexpr FxU32 kLfbSrc565 = 0x0;

        constexpr FxI32 kGetMaxTextureSize = 0x0a;
        constexpr FxI32 kGetMaxTextureAspectRatio = 0x0b;
        constexpr FxI32 kGetNumTmu = 0x13;

        // grGetString() selectors (Glide 3.0 Reference Manual). GR_EXTENSION plus
        // grGetProcAddress() is the only documented, DLL-name-independent way to negotiate an
        // optional Glide capability.
        constexpr FxU32 kGlideStringExtension = 0xa0;
        constexpr FxU32 kGlideStringHardware = 0xa1;
        constexpr FxU32 kGlideStringRenderer = 0xa2;
        constexpr FxU32 kGlideStringVendor = 0xa3;
        constexpr FxU32 kGlideStringVersion = 0xa4;

        constexpr FxI32 kBlendZero = 0x0;
        constexpr FxI32 kBlendSourceAlpha = 0x1;
        constexpr FxI32 kBlendSourceColor = 0x2;
        constexpr FxI32 kBlendDestinationAlpha = 0x3;
        constexpr FxI32 kBlendOne = 0x4;
        constexpr FxI32 kBlendInverseSourceAlpha = 0x5;
        constexpr FxI32 kBlendInverseSourceColor = 0x6;
        constexpr FxI32 kBlendInverseDestinationAlpha = 0x7;
        constexpr FxI32 kBlendAlphaSaturate = 0xf;

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
            throw std::runtime_error(std::string("GLIDE renderer does not support this CNA operation: ") + methodName);
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


        [[nodiscard]] FxI32 ToGlideDepthCompare(int xnaCompare)
        {
            // CNA/XNA depth grows away from the camera, while this renderer submits 1/Z to
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
                default: throw std::runtime_error("GLIDE renderer received an unknown CompareFunction value");
            }
        }

        [[nodiscard]] FxU32 PackArgb(float r, float g, float b, float a)
        {
            const auto pack = [](float component) -> FxU32
            {
                return static_cast<FxU32>(std::clamp(component, 0.0f, 1.0f) * 255.0f + 0.5f);
            };
            return (pack(a) << 24) | (pack(r) << 16) | (pack(g) << 8) | pack(b);
        }


        [[nodiscard]] FxI32 ToGlideTextureAddress(int address)
        {
            switch (address)
            {
                case 0: return kTexClampWrap;
                case 1: return kTexClampClamp;
                case 2: return kTexClampMirror;
                default: throw std::runtime_error("GLIDE renderer received an unknown TextureAddressMode value");
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
                case 2: throw std::runtime_error("GLIDE renderer does not support anisotropic texture filtering");
                case 3: return GlideSamplerSettings{kTexFilterBilinear, kTexFilterBilinear, kFxFalse};
                case 4: return GlideSamplerSettings{kTexFilterPoint, kTexFilterPoint, kFxTrue};
                case 5: return GlideSamplerSettings{kTexFilterBilinear, kTexFilterPoint, kFxTrue};
                case 6: return GlideSamplerSettings{kTexFilterBilinear, kTexFilterPoint, kFxFalse};
                case 7: return GlideSamplerSettings{kTexFilterPoint, kTexFilterBilinear, kFxTrue};
                case 8: return GlideSamplerSettings{kTexFilterPoint, kTexFilterBilinear, kFxFalse};
                default: throw std::runtime_error("GLIDE renderer received an unknown TextureFilter value");
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
            std::int64_t count = 0;
            switch (primitive)
            {
                case PrimitiveType::TriangleList: count = static_cast<std::int64_t>(primitiveCount) * 3; break;
                case PrimitiveType::TriangleStrip: count = static_cast<std::int64_t>(primitiveCount) + 2; break;
                case PrimitiveType::LineList: count = static_cast<std::int64_t>(primitiveCount) * 2; break;
                case PrimitiveType::LineStrip: count = static_cast<std::int64_t>(primitiveCount) + 1; break;
                case PrimitiveType::PointListEXT: count = primitiveCount; break;
                default:
                    throw std::runtime_error("GLIDE 3D received an unknown primitive topology");
            }
            if (count > std::numeric_limits<int>::max())
            {
                throw std::runtime_error("GLIDE primitiveCount overflows its vertex count");
            }
            return static_cast<int>(count);
        }

        class GlideApi final : public Abi::GlideApiFunctions
        {
        public:
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

            [[nodiscard]] std::string ModulePath() const
            {
                char path[MAX_PATH]{};
                const DWORD length = GetModuleFileNameA(module_, path, static_cast<DWORD>(std::size(path)));
                if (length == 0 || length >= std::size(path))
                {
                    return "<unavailable>";
                }
                return std::string(path, path + length);
            }

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

                Resolve(module_);
            }

        private:
            HMODULE module_ = nullptr;
        };

        struct TextureRange
        {
            FxU32 address = 0;
            FxU32 size = 0;
        };

        /**
         * A pointer-stable, forward-usable view of a Glide texture that Impl's TMU allocator can
         * evict under memory pressure without needing GlideTextureRenderer's full definition
         * (which is declared later in this file). Evicting only releases native TMU memory; the
         * texture keeps its CPU-side source and can rebuild its tiles on next use.
         */
        class IGlideResidentTexture
        {
        public:
            virtual ~IGlideResidentTexture() = default;
            [[nodiscard]] virtual bool IsResident() const = 0;
            [[nodiscard]] virtual std::uint64_t LastUsedCounter() const = 0;
            /** Fences the FIFO, releases every native tile range, and returns the bytes freed. */
            virtual FxU32 EvictAndReleaseNativeMemory() = 0;
        };
    } // namespace

    struct GlideRenderer::Impl
    {
        explicit Impl(const GraphicsRendererCreateArgs& args)
            : virtualWidth(args.virtualWidth > 0 ? args.virtualWidth : 640)
            , virtualHeight(args.virtualHeight > 0 ? args.virtualHeight : 480)
            , presentationMode(CnaPresentationMode::NativeBackBuffer)
            , swapInterval(args.swapInterval)
        {
            if (swapInterval < 0 || swapInterval > 1)
            {
                throw std::runtime_error("GLIDE renderer supports only swap intervals 0 (immediate) and 1 (v-sync)");
            }
            CNA::Platform::Win32NativeWindow nativeWindow;
            if (!CNA::Platform::TryGetWin32(args.surface.nativeHandle, nativeWindow))
            {
                throw std::runtime_error("GLIDE renderer requires a Win32 native window");
            }
            const HWND hwnd = static_cast<HWND>(nativeWindow.hwnd);

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
                // grQueryResolutions was asked for GR_QUERY_ANY resolution/refresh, so the same
                // resolution token can legitimately appear multiple times at different refresh
                // rates, and the runtime may not offer 60 Hz at all for the chosen dimensions.
                // Keep each candidate's resolution and refresh paired together instead of
                // selecting dimensions and then opening a hardcoded refresh that was never
                // actually validated against this candidate.
                std::vector<GlideResolutionCandidate> candidates;
                candidates.reserve(availableModes.size());
                for (const GlideResolution& candidate : availableModes)
                {
                    candidates.push_back(GlideResolutionCandidate{candidate.resolution, candidate.refresh});
                }
                const std::optional<GlideSelectedDisplayMode> selected =
                    SelectGlideDisplayMode(candidates, virtualWidth, virtualHeight);
                if (!selected.has_value())
                {
                    throw std::runtime_error(
                        "The loaded Glide runtime exposes no historical double-buffered Z mode large enough for CNA's virtual resolution");
                }
                nativeWidth = selected->mode.width;
                nativeHeight = selected->mode.height;
                context = api.grSstWinOpen(static_cast<FxU32>(reinterpret_cast<std::uintptr_t>(hwnd)), selected->mode.resolution,
                                           selected->refresh, kColorFormatArgb, kOriginUpperLeft, 2, 1);
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
                QueryRuntimeCapabilities();

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
                freeTextureRangesByTmu[0].push_back(TextureRange{minAddress, static_cast<FxU32>(rangeSize)});

                // GLIDE-FUT-004: a second TMU, when the runtime actually reports one, is used for
                // DualTextureEffect's second texture slot. GR_PARAM_ST1 is registered pointing at
                // the SAME sow/tow bytes as GR_PARAM_ST0 (Glide defaults an unset GR_PARAM_Q1 to
                // the shared GR_PARAM_Q, so this needs no other vertex-layout changes): this
                // renderer's vertex-declaration parser already accepts only a single texture-
                // coordinate semantic (GLIDE-AUD-012/FUT-002), so a shared UV between TMU0 and
                // TMU1 is that pre-existing boundary, not a new one. Registering GR_PARAM_ST1 is
                // harmless for ordinary single-texture draws: their TMU0 combiner always uses
                // GR_COMBINE_FUNCTION_LOCAL (see ConfigureSpriteCombiner/ConfigureColoredCombiner),
                // which never reads TMU1's upstream contribution at all.
                if (textureUnitCount >= 2)
                {
                    const FxU32 minAddress1 = api.grTexMinAddress(1);
                    const FxU32 maxAddress1 = api.grTexMaxAddress(1);
                    if (maxAddress1 > minAddress1)
                    {
                        const std::uint64_t rangeSize1 = static_cast<std::uint64_t>(maxAddress1) - minAddress1 + 1u;
                        if (rangeSize1 <= std::numeric_limits<FxU32>::max())
                        {
                            freeTextureRangesByTmu[1].push_back(
                                TextureRange{minAddress1, static_cast<FxU32>(rangeSize1)});
                            api.grVertexLayout(kParamSt1, static_cast<FxI32>(offsetof(GlideVertex, sow)), kParamEnable);
                            secondTmuAvailable = true;
                        }
                    }
                }
                LogStartupDiagnostics();
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
                // REMED-GFX-227: a final SpriteBatch may still be staged in CNA memory even
                // though no more Present() call will occur. Submit and fence it before closing
                // the context so device teardown preserves deferred command ordering.
                FlushSpriteBatch();
                api.grFinish();
                api.grSstWinClose(context);
                context = nullptr;
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

        // Records what the runtime advertises via grGetString so future optional-capability work
        // (e.g. native TEXMIRROR clamp, or any other GR_EXTENSION-gated feature) has a single,
        // DLL-name-independent source of truth instead of re-querying ad hoc. Nothing here changes
        // rendering behaviour yet: no extension is consumed until a separate task adds one, tests
        // it against the x86 fake DLL, and validates it visually.
        void QueryRuntimeCapabilities()
        {
            const auto queryString = [&](FxU32 selector, const char* name) -> std::string
            {
                const char* value = api.grGetString(selector);
                if (value == nullptr)
                {
                    throw std::runtime_error(std::string("grGetString failed for ") + name);
                }
                return value;
            };
            hardwareName = queryString(kGlideStringHardware, "GR_HARDWARE");
            rendererName = queryString(kGlideStringRenderer, "GR_RENDERER");
            vendorName = queryString(kGlideStringVendor, "GR_VENDOR");
            versionString = queryString(kGlideStringVersion, "GR_VERSION");
            supportedExtensions = ParseGlideExtensionList(api.grGetString(kGlideStringExtension));
        }

        void LogStartupDiagnostics() const
        {
            if (!diagnosticsEnabled)
            {
                return;
            }
            const std::vector<TextureRange>& freeTmu0 = freeTextureRangesByTmu[0];
            const std::uint64_t tmu0Bytes = freeTmu0.empty() ? 0u : freeTmu0.front().size;
            const std::vector<TextureRange>& freeTmu1 = freeTextureRangesByTmu[1];
            const std::uint64_t tmu1Bytes = freeTmu1.empty() ? 0u : freeTmu1.front().size;
            std::fprintf(stderr,
                         "[CNA GLIDE] runtime=%s, virtual=%dx%d, native=%dx%d, TMUs=%d, "
                         "maxTexture=%d, maxAspectLog2=%d, TMU0Bytes=%llu, secondTmuAvailable=%d, "
                         "TMU1Bytes=%llu\n",
                         api.ModulePath().c_str(), virtualWidth, virtualHeight, nativeWidth, nativeHeight,
                         textureUnitCount, maxTextureDimension, maxTextureAspectLog2,
                         static_cast<unsigned long long>(tmu0Bytes), secondTmuAvailable ? 1 : 0,
                         static_cast<unsigned long long>(tmu1Bytes));
            std::string extensionSummary;
            for (const std::string& extension : supportedExtensions)
            {
                if (!extensionSummary.empty())
                {
                    extensionSummary += ',';
                }
                extensionSummary += extension;
            }
            std::fprintf(stderr,
                         "[CNA GLIDE] hardware=%s, renderer=%s, vendor=%s, version=%s, "
                         "extensions=%s (none used yet -- detection only)\n",
                         hardwareName.c_str(), rendererName.c_str(), vendorName.c_str(), versionString.c_str(),
                         extensionSummary.empty() ? "(none)" : extensionSummary.c_str());
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

        void ConfigureDualTextureCombiner()
        {
            // GLIDE-FUT-004: DualTextureEffect. TMU1 is upstream of TMU0 in Glide's combiner
            // chain: TMU1 first outputs its own sampled texel unchanged as Cother/Aother for TMU0.
            api.grTexCombine(1, kCombineFunctionLocal, kCombineFactorOne,
                              kCombineFunctionLocal, kCombineFactorOne, kFxFalse, kFxFalse);
            // TMU0 then multiplies its own sampled texel by TMU1's upstream output, chaining
            // tex0 * tex1 as this stage's own Cother/Aother for the final iterated combiner below.
            api.grTexCombine(0, kCombineFunctionScaleOther, kCombineFactorLocal,
                              kCombineFunctionScaleOther, kCombineFactorLocal, kFxFalse, kFxFalse);
            // Final stage multiplies the CPU-iterated colour (already RGB-prescaled by 2, see
            // readVertex in DrawPrimitiveRange) by TMU0's chained tex0*tex1 output. Alpha is never
            // doubled, matching FNA's DualTextureEffect.fx: iteratedAlpha * tex0.a * tex1.a.
            api.grColorCombine(kCombineFunctionScaleOther, kCombineFactorLocal,
                               kCombineLocalIterated, kCombineOtherTexture, kFxFalse);
            api.grAlphaCombine(kCombineFunctionScaleOther, kCombineFactorLocal,
                               kCombineLocalIterated, kCombineOtherTexture, kFxFalse);
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

        // Adjacent SpriteBatch quads that keep the same native TMU binding and sampler settings
        // are queued here and submitted with one grDrawVertexArrayContiguous call instead of two
        // grDrawTriangle calls per sprite. Every other GlideRenderer entry point that
        // changes rendering state, submits geometry through a different path, or reads/clears/
        // presents the framebuffer MUST call FlushSpriteBatch() first, so a queued-but-unsubmitted
        // sprite is never rendered under a state it wasn't actually drawn with, and never
        // reordered relative to other native submissions.
        static constexpr std::size_t kMaxPendingSpriteVertices = 3u * 1024u;
        std::vector<GlideVertex> pendingSpriteTriangles;
        bool spriteBatchBound = false;
        FxU32 spriteBoundTmuAddress = 0;
        int spriteSamplerFilter = 0;
        int spriteSamplerAddressU = 0;
        int spriteSamplerAddressV = 0;

        void FlushSpriteBatch()
        {
            if (pendingSpriteTriangles.empty())
            {
                return;
            }
            api.grDrawVertexArrayContiguous(
                kPrimitiveTriangles, static_cast<FxU32>(pendingSpriteTriangles.size()),
                pendingSpriteTriangles.data(), static_cast<FxU32>(sizeof(GlideVertex)));
            pendingSpriteTriangles.clear();
            spriteBatchBound = false;
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

        [[nodiscard]] std::optional<TextureRange> TryFitTexture(int tmu, FxU32 size)
        {
            constexpr FxU32 alignment = 8;
            std::vector<TextureRange>& freeRanges = freeTextureRangesByTmu[static_cast<std::size_t>(tmu)];
            for (auto it = freeRanges.begin(); it != freeRanges.end(); ++it)
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
                            freeRanges.erase(it);
                        }
                    }
                    else
                    {
                        it->size = padding;
                        const std::uint64_t allocationEnd = static_cast<std::uint64_t>(alignedAddress) + size;
                        if (allocationEnd < oldEnd)
                        {
                            freeRanges.insert(std::next(it), TextureRange{
                                static_cast<FxU32>(allocationEnd), static_cast<FxU32>(oldEnd - allocationEnd)});
                        }
                    }
                    return allocation;
                }
            }
            return std::nullopt;
        }

        /**
         * `requester` is always excluded from eviction, even before it is registered (during its
         * own construction) or while it is only partially resident (mid-rebuild after its own
         * prior eviction) -- otherwise a texture could evict tiles it is in the middle of
         * allocating for itself, corrupting its own atomic rebuild.
         *
         * `residentTexturesByTmu[1]` is never populated (GLIDE-FUT-004's single-tile-only TMU1
         * support does not participate in LRU eviction), so a TMU1 request that cannot be
         * satisfied simply fails cleanly here instead of ever evicting a TMU0 resident -- doing
         * that would free TMU0 memory while leaving TMU1 exactly as exhausted as before.
         */
        [[nodiscard]] TextureRange AllocateTexture(int tmu, FxU32 size, const IGlideResidentTexture* requester)
        {
            for (;;)
            {
                if (const std::optional<TextureRange> fit = TryFitTexture(tmu, size))
                {
                    return *fit;
                }
                const std::vector<IGlideResidentTexture*>& residents =
                    residentTexturesByTmu[static_cast<std::size_t>(tmu)];
                std::vector<GlideResidentTextureView> candidates;
                candidates.reserve(residents.size());
                for (const IGlideResidentTexture* candidate : residents)
                {
                    candidates.push_back(GlideResidentTextureView{
                        candidate, candidate->IsResident(), candidate->LastUsedCounter()});
                }
                const void* victimIdentity = SelectGlideEvictionVictim(candidates, requester);
                if (victimIdentity == nullptr)
                {
                    throw std::runtime_error("GLIDE TMU texture memory is exhausted");
                }
                const auto victimIt = std::find_if(residents.begin(), residents.end(),
                    [victimIdentity](const IGlideResidentTexture* candidate)
                    {
                        return static_cast<const void*>(candidate) == victimIdentity;
                    });
                IGlideResidentTexture* victim = *victimIt;
                // A pending SpriteBatch quad can reference any currently-resident tile via native
                // TMU state that was already set up when it was queued, but has not reached
                // Glide's FIFO yet. Submit it before reclaiming any tile's memory, so eviction can
                // never invalidate geometry that has not been drawn -- deferred submission
                // (GLIDE-FUT-015) must not change what a queued sprite actually samples from.
                FlushSpriteBatch();
                const FxU32 freedBytes = victim->EvictAndReleaseNativeMemory();
                if (diagnosticsEnabled)
                {
                    std::fprintf(stderr,
                                 "[CNA GLIDE] TMU%d memory pressure: evicted a least-recently-used texture, "
                                 "freed %u bytes\n",
                                 tmu, freedBytes);
                }
                if (freedBytes == 0)
                {
                    // A resident candidate with nothing to free would spin forever; the pool is
                    // genuinely exhausted for this request.
                    throw std::runtime_error("GLIDE TMU texture memory is exhausted");
                }
            }
        }

        [[nodiscard]] std::uint64_t NextTextureUseCounter() { return ++textureUseCounter; }

        void ReleaseTexture(int tmu, TextureRange range)
        {
            if (range.size == 0)
            {
                return;
            }
            std::vector<TextureRange>& freeRanges = freeTextureRangesByTmu[static_cast<std::size_t>(tmu)];
            auto position = std::lower_bound(freeRanges.begin(), freeRanges.end(), range.address,
                [](const TextureRange& candidate, FxU32 address) { return candidate.address < address; });
            position = freeRanges.insert(position, range);
            if (position != freeRanges.begin())
            {
                const auto previous = std::prev(position);
                if (static_cast<std::uint64_t>(previous->address) + previous->size == position->address)
                {
                    const std::uint64_t combinedSize = static_cast<std::uint64_t>(previous->size) + position->size;
                    if (combinedSize > std::numeric_limits<FxU32>::max())
                    {
                        throw std::runtime_error("GLIDE TMU free-range coalescing overflowed");
                    }
                    previous->size = static_cast<FxU32>(combinedSize);
                    position = freeRanges.erase(position);
                    position = previous;
                }
            }
            const auto next = std::next(position);
            if (next != freeRanges.end() &&
                static_cast<std::uint64_t>(position->address) + position->size == next->address)
            {
                const std::uint64_t combinedSize = static_cast<std::uint64_t>(position->size) + next->size;
                if (combinedSize > std::numeric_limits<FxU32>::max())
                {
                    throw std::runtime_error("GLIDE TMU free-range coalescing overflowed");
                }
                position->size = static_cast<FxU32>(combinedSize);
                freeRanges.erase(next);
            }
        }

        GlideApi api;
        bool diagnosticsEnabled = []
        {
            const char* value = std::getenv("CNA_GLIDE_DIAGNOSTICS");
            return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
        }();
        // GLIDE-FUT-007: classifying and re-packing a logical texture's mip pyramid as RGB565/
        // ARGB1555 is implemented and unit-tested, but has never been checked against a real
        // Glide runtime or dgVoodoo image (blocked by the sibling sharp-runtime i686 dependency).
        // Keep it opt-in so every already-validated ARGB4444 texture is unaffected by default.
        bool adaptiveTextureFormatEnabled = []
        {
            const char* value = std::getenv("CNA_GLIDE_ADAPTIVE_TEXTURE_FORMAT");
            return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
        }();
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
        // REMED-GFX-226: DualTextureEffect must retain and submit each public sampler slot's
        // independent filter/LOD state. Addressing is also retained independently, then the
        // shared-coordinate limitation is validated explicitly before a dual-TMU draw.
        std::array<int, 2> samplerFilter{0, 0};
        std::array<int, 2> samplerAddressU{1, 1};
        std::array<int, 2> samplerAddressV{1, 1};
        std::array<float, 2> samplerLodBias{0.0f, 0.0f};
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
        // True only once startup has confirmed both GR_NUM_TMU >= 2 and a usable TMU1 memory
        // range; DualTextureEffect draws must check this rather than textureUnitCount alone.
        bool secondTmuAvailable = false;
        // Index 0 is TMU0 (always present); index 1 is TMU1, only ever populated when
        // GLIDE-FUT-004 finds GR_NUM_TMU >= 2 at startup.
        std::array<std::vector<TextureRange>, 2> freeTextureRangesByTmu;
        // Every live GlideTextureRenderer registers itself in index 0 (after successful
        // construction) so AllocateTexture() can evict the least-recently-used other TMU0-
        // resident texture under memory pressure instead of failing outright.
        // NextTextureUseCounter() is a deterministic logical clock, not wall-clock time, so LRU
        // ordering is reproducible. Index 1 is deliberately never populated: GLIDE-FUT-004's
        // single-tile-only TMU1 support does not participate in eviction.
        std::array<std::vector<IGlideResidentTexture*>, 2> residentTexturesByTmu;
        std::uint64_t textureUseCounter = 0;
        std::string hardwareName;
        std::string rendererName;
        std::string vendorName;
        std::string versionString;
        std::vector<std::string> supportedExtensions;
    };

    class GlideTextureRenderer final : public ITextureRenderer, public IGlideResidentTexture
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

        GlideTextureRenderer(GlideRenderer& owner, const ImageData& data)
            : impl_(owner.impl_)
            , width_(data.width)
            , height_(data.height)
            , mipLevels_(data.mipLevels)
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
            if (mipLevels_ <= 0)
            {
                throw std::runtime_error("GLIDE texture declares an invalid mip-level count");
            }
            if (mipLevels_ > GlideMipLevelCountForDimensions(width_, height_))
            {
                throw std::runtime_error("GLIDE texture declares more mip levels than its dimensions permit");
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
                    GetImpl().ReleaseTexture(0, tile.range);
                }
                throw;
            }
            // Only a fully, successfully constructed texture becomes eligible for eviction.
            GetImpl().residentTexturesByTmu[0].push_back(this);
        }

        ~GlideTextureRenderer() override
        {
            if (const std::shared_ptr<GlideRenderer::Impl> impl = impl_.lock())
            {
                // REMED-GFX-227: SpriteBatch is deferred in CNA-owned memory. grFinish() alone
                // fences only commands already submitted to Glide, so submit pending geometry
                // before this texture's TMU ranges can be returned to the allocator.
                impl->FlushSpriteBatch();
                auto& residents = impl->residentTexturesByTmu[0];
                residents.erase(std::remove(residents.begin(), residents.end(), this), residents.end());
                // A source command can remain in Glide's FIFO after the C++ texture dies. Do
                // not make its TMU range reusable until the hardware/emulator has consumed it.
                impl->api.grFinish();
                for (const Tile& tile : tiles_)
                {
                    impl->ReleaseTexture(0, tile.range);
                }
                if (tmu1Built_)
                {
                    impl->ReleaseTexture(1, tmu1Range_);
                }
            }
        }

        [[nodiscard]] bool IsResident() const override { return !tiles_.empty(); }
        [[nodiscard]] std::uint64_t LastUsedCounter() const override { return lastUsedCounter_; }

        FxU32 EvictAndReleaseNativeMemory() override
        {
            if (tiles_.empty())
            {
                return 0;
            }
            GlideRenderer::Impl& impl = GetImpl();
            // The retained CPU-side source (rgba_/logicalMipLevels_/explicitMipLevels_) is
            // untouched: eviction only reclaims native TMU memory, which BuildTiles() can
            // reconstruct from that source the next time this texture is actually used.
            impl.api.grFinish();
            FxU32 freedBytes = 0;
            for (const Tile& tile : tiles_)
            {
                freedBytes += tile.range.size;
                impl.ReleaseTexture(0, tile.range);
            }
            tiles_.clear();
            return freedBytes;
        }

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }

        void UpdatePixels(const std::uint8_t* rgba, int stride) override
        {
            CopyGlideRgba8Rows(rgba_, width_, height_, rgba, stride);
            // A pending SpriteBatch quad was already computed against this texture's current
            // tile content; submit it before that content changes underneath it.
            GetImpl().FlushSpriteBatch();
            // Existing draws may still sample this native allocation. Synchronize before
            // downloading a replacement mip chain into the same TMU addresses.
            GetImpl().api.grFinish();
            BuildLogicalMipChain(uploadedAddressU_, uploadedAddressV_);
            for (Tile& tile : tiles_)
            {
                ConvertTileToGlideTexels(tile, uploadedAddressU_, uploadedAddressV_);
                Upload(tile);
            }
            RefreshTmu1IfBuilt();
        }

        void UpdatePixelsLevel(int level, const std::uint8_t* rgba, int levelWidth, int levelHeight) override
        {
            if (level == 0)
            {
                if (levelWidth != width_ || levelHeight != height_)
                {
                    throw std::runtime_error("GLIDE level-zero upload dimensions do not match its texture");
                }
                UpdatePixels(rgba, width_ * 4);
                return;
            }
            if (rgba == nullptr || level < 0 || level >= mipLevels_ ||
                levelWidth != MipDimension(width_, level) || levelHeight != MipDimension(height_, level))
            {
                throw std::runtime_error("GLIDE explicit mip upload has an invalid level or dimensions");
            }
            const std::size_t byteCount = static_cast<std::size_t>(levelWidth) * levelHeight * 4u;
            explicitMipLevels_.resize(static_cast<std::size_t>(level + 1));
            explicitMipLevels_[level].assign(rgba, rgba + byteCount);

            // A pending SpriteBatch quad was already computed against this texture's current
            // tile content; submit it before that content changes underneath it.
            GetImpl().FlushSpriteBatch();
            // Every native tile references the same logical pyramid. Rebuild it before replacing
            // tile data so this explicit source level, lower generated levels and tile gutters all
            // change atomically after the existing Glide FIFO has consumed prior draws.
            GetImpl().api.grFinish();
            BuildLogicalMipChain(uploadedAddressU_, uploadedAddressV_);
            for (Tile& tile : tiles_)
            {
                ConvertTileToGlideTexels(tile, uploadedAddressU_, uploadedAddressV_);
                Upload(tile);
            }
            RefreshTmu1IfBuilt();
        }

        [[nodiscard]] const std::vector<Tile>& Tiles() const { return tiles_; }
        [[nodiscard]] bool IsTiled() const { return tiles_.size() != 1; }

        void EnsureAddressMode(int addressU, int addressV)
        {
            // Every real draw use funnels through here (even when nothing below actually needs
            // to change), so this is also where residency/LRU state gets touched.
            lastUsedCounter_ = GetImpl().NextTextureUseCounter();
            const bool wasEvicted = tiles_.empty();
            const bool addressModeChanged = addressU != uploadedAddressU_ || addressV != uploadedAddressV_;
            if (!wasEvicted && !addressModeChanged)
            {
                return;
            }
            // A pending SpriteBatch quad's vertex data was already computed against whatever
            // this texture's tiles contained when it was queued. Mutating that tile content in
            // place (address-mode change) or reclaiming/reconstructing it (eviction) must not
            // happen while such a quad is still unsubmitted, or it would render with texture
            // data it was never actually queued against.
            GetImpl().FlushSpriteBatch();
            // The source image is retained in RGBA8, so changing the sampler address mode can
            // rebuild the global mip pyramid and tile padding without losing information to an
            // earlier ARGB4444 conversion. Lower LODs also depend on the mode at image edges.
            if (addressModeChanged)
            {
                static_cast<void>(ToGlideTextureAddress(addressU));
                static_cast<void>(ToGlideTextureAddress(addressV));
            }
            GetImpl().api.grFinish();
            if (addressModeChanged)
            {
                BuildLogicalMipChain(addressU, addressV);
                uploadedAddressU_ = addressU;
                uploadedAddressV_ = addressV;
            }
            if (wasEvicted)
            {
                // Reconstruct every tile from the retained logical pyramid, atomically: a
                // mid-rebuild failure (e.g. TMU0 still exhausted after evicting everything else)
                // must leave this texture fully evicted again, never partially resident.
                try
                {
                    BuildTiles();
                }
                catch (...)
                {
                    for (const Tile& tile : tiles_)
                    {
                        GetImpl().ReleaseTexture(0, tile.range);
                    }
                    tiles_.clear();
                    throw;
                }
            }
            else
            {
                for (Tile& tile : tiles_)
                {
                    ConvertTileToGlideTexels(tile, addressU, addressV);
                    Upload(tile);
                }
            }
        }

        /**
         * GLIDE-FUT-004: makes this texture resident on TMU1 as DualTextureEffect's second
         * texture slot. Single-tile only -- throws if this logical texture would need more than
         * one physical tile at the runtime's reported GR_MAX_TEXTURE_SIZE, rather than attempting
         * the substantially harder problem of partitioning geometry against two independent
         * tile/address-mode grids at once.
         */
        void EnsureTmu1Resident(int addressU, int addressV)
        {
            // Guarantees logicalMipLevels_ is current for this address mode; this is also this
            // texture's own TMU0 use-counter/residency touch, which is always legal even if this
            // particular draw only reads it through TMU1.
            EnsureAddressMode(addressU, addressV);
            if (tmu1Built_ && addressU == tmu1UploadedAddressU_ && addressV == tmu1UploadedAddressV_)
            {
                return;
            }
            GlideRenderer::Impl& impl = GetImpl();
            // Same hazard as EnsureAddressMode(): a queued-but-unsubmitted SpriteBatch quad must
            // not have TMU1 content it never asked for swapped out from under it. SpriteBatch
            // itself never uses TMU1, but a 3D dual-textured draw could still be preceded by one.
            impl.FlushSpriteBatch();
            if (tmu1Built_)
            {
                impl.api.grFinish();
                impl.ReleaseTexture(1, tmu1Range_);
                tmu1Built_ = false;
            }
            try
            {
                BuildSingleTmu1Tile(addressU, addressV);
            }
            catch (...)
            {
                tmu1Built_ = false;
                throw;
            }
            tmu1UploadedAddressU_ = addressU;
            tmu1UploadedAddressV_ = addressV;
            tmu1Built_ = true;
        }

        [[nodiscard]] FxU32 Tmu1TextureAddress() const { return tmu1Range_.address; }
        [[nodiscard]] const GlideTexInfo& Tmu1NativeInfo() const { return tmu1NativeInfo_; }

    private:
        struct LogicalMipLevel
        {
            int width = 0;
            int height = 0;
            std::vector<std::uint16_t> texels;
        };

        [[nodiscard]] static int MipDimension(int dimension, int level)
        {
            for (int index = 0; index < level; ++index)
            {
                dimension = std::max(1, dimension / 2);
            }
            return dimension;
        }

        void ApplyExplicitMipLevel(LogicalMipLevel& level, int levelIndex, int addressU, int addressV) const
        {
            if (levelIndex >= static_cast<int>(explicitMipLevels_.size()) || explicitMipLevels_[levelIndex].empty())
            {
                return;
            }
            const int sourceWidth = MipDimension(width_, levelIndex);
            const int sourceHeight = MipDimension(height_, levelIndex);
            const std::vector<std::uint8_t>& source = explicitMipLevels_[levelIndex];
            const std::size_t requiredBytes = static_cast<std::size_t>(sourceWidth) * sourceHeight * 4u;
            if (source.size() != requiredBytes)
            {
                throw std::runtime_error("GLIDE retained explicit mip data has an invalid byte count");
            }
            level.texels = BuildAddressedGlideArgb4444Mip(
                source.data(), sourceWidth, sourceHeight, level.width, level.height, addressU, addressV);
        }

        void BuildLogicalMipChain(int addressU, int addressV)
        {
            logicalMipLevels_.clear();
            LogicalMipLevel level{};
            // Glide stores a power-of-two chain even for a non-power-of-two logical CNA image.
            // Build one address-mode-aware virtual image first, then downsample it globally. This
            // gives every physical tile the same source mip texels at a shared logical boundary.
            level.width = NextPowerOfTwo(width_, 16384);
            level.height = NextPowerOfTwo(height_, 16384);
            level.texels = BuildAddressedGlideArgb4444Mip(
                rgba_.data(), width_, height_, level.width, level.height, addressU, addressV);
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
                ApplyExplicitMipLevel(next, static_cast<int>(logicalMipLevels_.size()), addressU, addressV);
                logicalMipLevels_.push_back(std::move(next));
            }
            // Classify every explicit and generated level (already address-padded above) so a
            // format narrower than ARGB4444 is only ever chosen when genuinely lossless for it.
            classifiedAlphaClass_ = GlideTextureAlphaClass::Opaque;
            for (const LogicalMipLevel& level : logicalMipLevels_)
            {
                classifiedAlphaClass_ = CombineGlideTextureAlphaClass(
                    classifiedAlphaClass_, ClassifyGlideArgb4444AlphaCoverage(level.texels));
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
                    tile.range = GetImpl().AllocateTexture(0, requiredBytes, this);
                    try
                    {
                        Upload(tile);
                    }
                    catch (...)
                    {
                        GetImpl().ReleaseTexture(0, tile.range);
                        throw;
                    }
                    tiles_.push_back(std::move(tile));
                    sourceX += sourceWidth;
                }
                sourceY += sourceHeight;
            }
        }

        /**
         * Builds this texture's single TMU1 tile straight from logicalMipLevels_ (already current
         * for `addressU`/`addressV` by the time EnsureTmu1Resident() calls this). No gutters are
         * needed: a single tile covering the whole logical image never touches another tile's
         * edge, only the address-mode-aware power-of-two padding ConvertTileToGlideTexels() also
         * relies on via AddressGlideTextureTexel().
         */
        void BuildSingleTmu1Tile(int addressU, int addressV)
        {
            GlideRenderer::Impl& impl = GetImpl();
            const int maximum = impl.maxTextureDimension;
            if (maximum < 1 || (maximum & (maximum - 1)) != 0)
            {
                throw std::runtime_error("Glide reported an invalid non-power-of-two maximum texture size");
            }
            if (width_ > maximum || height_ > maximum)
            {
                throw std::runtime_error(
                    "GLIDE DualTextureEffect's second texture exceeds the native tile size limit; "
                    "multi-tile dual-texture rendering is not supported");
            }
            const int paddedWidth = NextPowerOfTwo(width_, maximum);
            const int paddedHeight = NextPowerOfTwo(height_, maximum);
            const int maximumAspect = 1 << impl.maxTextureAspectLog2;
            if (paddedWidth > paddedHeight * maximumAspect || paddedHeight > paddedWidth * maximumAspect)
            {
                throw std::runtime_error(
                    "GLIDE DualTextureEffect's second texture cannot satisfy the emulator-reported "
                    "aspect-ratio limit");
            }
            const GlideTextureAlphaClass alphaClass =
                impl.adaptiveTextureFormatEnabled ? classifiedAlphaClass_ : GlideTextureAlphaClass::Fractional;
            const int largeDimension = std::max(paddedWidth, paddedHeight);
            tmu1NativeTexels_.clear();
            tmu1NativeTexels_.reserve(static_cast<std::size_t>(paddedWidth) * paddedHeight * 2u);
            int levelWidth = paddedWidth;
            int levelHeight = paddedHeight;
            for (std::size_t levelIndex = 0; ; ++levelIndex)
            {
                if (levelIndex >= logicalMipLevels_.size())
                {
                    throw std::runtime_error("GLIDE TMU1 tile mip chain exceeds the logical texture mip chain");
                }
                const LogicalMipLevel& logicalLevel = logicalMipLevels_[levelIndex];
                for (int y = 0; y < levelHeight; ++y)
                {
                    const int sourceY = AddressGlideTextureTexel(y, logicalLevel.height, addressV);
                    for (int x = 0; x < levelWidth; ++x)
                    {
                        const int sourceX = AddressGlideTextureTexel(x, logicalLevel.width, addressU);
                        const std::uint16_t argb4444 = logicalLevel.texels[
                            static_cast<std::size_t>(sourceY) * logicalLevel.width + sourceX];
                        switch (alphaClass)
                        {
                            case GlideTextureAlphaClass::Opaque:
                                tmu1NativeTexels_.push_back(GlideArgb4444ToRgb565(argb4444));
                                break;
                            case GlideTextureAlphaClass::Binary:
                                tmu1NativeTexels_.push_back(GlideArgb4444ToArgb1555(argb4444));
                                break;
                            case GlideTextureAlphaClass::Fractional:
                                tmu1NativeTexels_.push_back(argb4444);
                                break;
                        }
                    }
                }
                if (levelWidth == 1 && levelHeight == 1)
                {
                    break;
                }
                levelWidth = std::max(1, levelWidth / 2);
                levelHeight = std::max(1, levelHeight / 2);
            }
            tmu1NativeInfo_ = GlideTexInfo{
                0, Log2PowerOfTwo(largeDimension),
                Log2PowerOfTwo(paddedWidth) - Log2PowerOfTwo(paddedHeight),
                ToGlideNativeTextureFormat(alphaClass), tmu1NativeTexels_.data() };
            const FxU32 requiredBytes = impl.api.grTexTextureMemRequired(kMipMapBoth, &tmu1NativeInfo_);
            if (requiredBytes == 0)
            {
                throw std::runtime_error("grTexTextureMemRequired rejected a tiled Glide texture for TMU1");
            }
            tmu1Range_ = impl.AllocateTexture(1, requiredBytes, this);
            try
            {
                tmu1NativeInfo_.data = tmu1NativeTexels_.data();
                impl.api.grTexDownloadMipMap(1, tmu1Range_.address, kMipMapBoth, &tmu1NativeInfo_);
            }
            catch (...)
            {
                impl.ReleaseTexture(1, tmu1Range_);
                throw;
            }
        }

        /**
         * GLIDE-FUT-004: UpdatePixels()/UpdatePixelsLevel() already refresh TMU0's tiles_ from the
         * freshly rebuilt logicalMipLevels_, but a texture currently resident on TMU1 has its own,
         * separately-uploaded copy (tmu1NativeTexels_/tmu1Range_) that would otherwise keep
         * sampling the pre-update pixels for as long as it stays bound as DualTextureEffect's
         * second texture. Called only after logicalMipLevels_ is already current for
         * `tmu1UploadedAddressU_`/`tmu1UploadedAddressV_` and after the caller's own grFinish().
         */
        void RefreshTmu1IfBuilt()
        {
            if (!tmu1Built_)
            {
                return;
            }
            GetImpl().ReleaseTexture(1, tmu1Range_);
            tmu1Built_ = false;
            BuildSingleTmu1Tile(tmu1UploadedAddressU_, tmu1UploadedAddressV_);
            tmu1Built_ = true;
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
            const bool useAdaptiveFormat = GetImpl().adaptiveTextureFormatEnabled;
            const GlideTextureAlphaClass alphaClass =
                useAdaptiveFormat ? classifiedAlphaClass_ : GlideTextureAlphaClass::Fractional;
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
                    const int sourceY = AddressGlideTextureTexel(logicalOriginY + y, logicalLevel.height, addressV);
                    for (int x = 0; x < levelWidth; ++x)
                    {
                        const int sourceX = AddressGlideTextureTexel(logicalOriginX + x, logicalLevel.width, addressU);
                        const std::uint16_t argb4444 = logicalLevel.texels[
                            static_cast<std::size_t>(sourceY) * logicalLevel.width + sourceX];
                        switch (alphaClass)
                        {
                            case GlideTextureAlphaClass::Opaque:
                                tile.nativeTexels.push_back(GlideArgb4444ToRgb565(argb4444));
                                break;
                            case GlideTextureAlphaClass::Binary:
                                tile.nativeTexels.push_back(GlideArgb4444ToArgb1555(argb4444));
                                break;
                            case GlideTextureAlphaClass::Fractional:
                                tile.nativeTexels.push_back(argb4444);
                                break;
                        }
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
                ToGlideNativeTextureFormat(alphaClass), tile.nativeTexels.data() };
        }

        void Upload(Tile& tile)
        {
            tile.nativeInfo.data = tile.nativeTexels.data();
            GetImpl().api.grTexDownloadMipMap(0, tile.range.address, kMipMapBoth, &tile.nativeInfo);
        }

        [[nodiscard]] GlideRenderer::Impl& GetImpl() const
        {
            const std::shared_ptr<GlideRenderer::Impl> impl = impl_.lock();
            if (!impl)
            {
                throw std::runtime_error("GLIDE texture outlived its graphics renderer");
            }
            return *impl;
        }

        std::weak_ptr<GlideRenderer::Impl> impl_;
        int width_ = 0;
        int height_ = 0;
        int mipLevels_ = 1;
        int uploadedAddressU_ = 1;
        int uploadedAddressV_ = 1;
        std::vector<std::uint8_t> rgba_;
        std::vector<std::vector<std::uint8_t>> explicitMipLevels_;
        std::vector<LogicalMipLevel> logicalMipLevels_;
        // Only meaningful when GLIDE-FUT-007's opt-in classifier is enabled; ARGB4444 otherwise.
        GlideTextureAlphaClass classifiedAlphaClass_ = GlideTextureAlphaClass::Fractional;
        std::vector<Tile> tiles_;
        std::uint64_t lastUsedCounter_ = 0;

        // GLIDE-FUT-004: this texture's residency as DualTextureEffect's TMU1 (second) slot,
        // single-tile only (throws in BuildSingleTmu1Tile() if it would need more than one tile).
        // Deliberately separate from tiles_/uploadedAddressU_/V_ above: the same texture object
        // can legally be used as an ordinary TMU0 texture in one draw and as texture1 in another,
        // and does not participate in eviction (see Impl::residentTexturesByTmu[1]).
        bool tmu1Built_ = false;
        int tmu1UploadedAddressU_ = 1;
        int tmu1UploadedAddressV_ = 1;
        TextureRange tmu1Range_{};
        GlideTexInfo tmu1NativeInfo_{};
        std::vector<std::uint16_t> tmu1NativeTexels_{};

        friend struct GlideRenderer::Impl;
    };

    class GlideVertexBufferRenderer final : public IVertexBufferRenderer
    {
    public:
        explicit GlideVertexBufferRenderer(int capacity) : capacity_(capacity)
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

        /**
         * Uploads bytes under an already-resolved layout instead of parsing a VertexDeclaration
         * or guessing one from stride. Used to carry a source buffer's exact resolved layout
         * (whichever way it was resolved) into a derived buffer, e.g. the indexed-draw index
         * expansion, so decoding never has a chance to disagree with the source it was copied
         * from -- a stride that happens to match a built-in packed layout is not proof that the
         * fields are actually arranged that way.
         */
        void SetDataWithLayout(const void* data, int vertexCount, const GlideVertexLayout& layout)
        {
            if (data == nullptr || vertexCount < 0 || vertexCount > capacity_)
            {
                throw std::runtime_error("GLIDE vertex-buffer upload is outside its declared capacity");
            }
            layout_ = layout;
            bytes_.resize(static_cast<std::size_t>(vertexCount) * layout.stride);
            std::memcpy(bytes_.data(), data, bytes_.size());
            vertexCount_ = vertexCount;
            stride_ = layout.stride;
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

    class GlideIndexBufferRenderer final : public IIndexBufferRenderer
    {
    public:
        explicit GlideIndexBufferRenderer(int capacity) : capacity_(capacity)
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

    class GlideSpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        explicit GlideSpriteBatchRenderer(GlideRenderer& owner) : owner_(owner) {}

        void Begin() override
        {
            if (begun_)
            {
                throw std::runtime_error("GlideSpriteBatchRenderer::Begin called without a matching End");
            }
            begun_ = true;
        }

        void End() override
        {
            if (!begun_)
            {
                throw std::runtime_error("GlideSpriteBatchRenderer::End called without a matching Begin");
            }
            begun_ = false;
        }

        void SetTransformMatrix(const Matrix& matrix) override { transform_ = matrix; }

        void SetCustomEffect(Effect* effect) override
        {
            if (effect != nullptr)
            {
                throw std::runtime_error("GLIDE renderer does not support custom SpriteBatch Effects");
            }
        }

        void SetSamplerFilter(int textureFilter) override { textureFilter_ = textureFilter; }
        void SetSamplerAddressMode(int addressU, int addressV) override
        {
            addressU_ = addressU;
            addressV_ = addressV;
        }

        void Draw(const ITextureRenderer& texture, float x, float y) override
        {
            Draw(texture, Rectangle(static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight()),
                 Rectangle(0, 0, texture.GetWidth(), texture.GetHeight()), Color::White,
                 0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, 0.0f);
        }

        void Draw(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle, const Color& color) override
        {
            Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0.0f, 0.0f),
                 SpriteEffects::None, 0.0f);
        }

        void Draw(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle, const Color& color, float rotation,
                  const Vector2& origin, SpriteEffects effects, float /*layerDepth*/) override
        {
            if (!begun_)
            {
                throw std::runtime_error("GlideSpriteBatchRenderer::Draw called before Begin");
            }
            owner_.DrawSprite(texture, destinationRectangle, sourceRectangle, color, rotation, origin,
                              effects, transform_, textureFilter_, addressU_, addressV_);
        }

    private:
        GlideRenderer& owner_;
        bool begun_ = false;
        Matrix transform_ = Matrix::getIdentityProperty();
        int textureFilter_ = 0;
        int addressU_ = 1;
        int addressV_ = 1;
    };

    GlideRenderer::GlideRenderer(const GraphicsRendererCreateArgs& args)
        : impl_(std::make_shared<Impl>(args))
    {
        ApplyBlendState(/*One*/ 0, /*One*/ 0, /*InverseSourceAlpha*/ 5, /*InverseSourceAlpha*/ 5,
                        /*Add*/ 0, /*Add*/ 0, BlendWriteState{});
    }

    GlideRenderer::~GlideRenderer() = default;

    void GlideRenderer::Clear(float r, float g, float b, float a)
    {
        impl_->FlushSpriteBatch();
        impl_->api.grRenderBuffer(kBufferBack);
        impl_->ClearColorOnly(PackArgb(r, g, b, a),
                              static_cast<std::uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f));
    }

    void GlideRenderer::Present()
    {
        impl_->FlushSpriteBatch();
        impl_->api.grBufferSwap(impl_->swapInterval > 0 ? 1u : 0u);
        impl_->api.grRenderBuffer(kBufferBack);
    }

    void GlideRenderer::GetViewportSize(int& width, int& height)
    {
        width = impl_->virtualWidth;
        height = impl_->virtualHeight;
    }

    bool GlideRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        // The auxiliary plane is a real 16-bit depth buffer, but Glide has no stencil plane, so
        // the aggregate capability remains false; SupportsDepthBuffer()/SupportsStencilBuffer()
        // carry that split to Clear routing.
        return SupportsGlideCapability(capability);
    }

    void GlideRenderer::ReadBackbuffer(int x, int y, int w, int h, std::uint8_t* pixels)
    {
        if (pixels == nullptr || x < 0 || y < 0 || w <= 0 || h <= 0 ||
            x + w > impl_->virtualWidth || y + h > impl_->virtualHeight)
        {
            throw std::runtime_error("GLIDE ReadBackbuffer requested an invalid rectangle");
        }

        impl_->FlushSpriteBatch();
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

    void GlideRenderer::SetVirtualResolution(int width, int height)
    {
        if (width <= 0 || height <= 0 || width > impl_->nativeWidth || height > impl_->nativeHeight)
        {
            throw std::runtime_error("GLIDE virtual resolution exceeds the native Glide context selected at startup");
        }
        impl_->FlushSpriteBatch();
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

    void GlideRenderer::SetPresentationMode(int mode)
    {
        switch (static_cast<CnaPresentationMode>(mode))
        {
        case CnaPresentationMode::Letterbox:
        case CnaPresentationMode::Overscan:
        case CnaPresentationMode::Stretch:
        case CnaPresentationMode::NativeBackBuffer:
        case CnaPresentationMode::FixedHeightDynamicWidth:
            break;
        default:
            throw std::invalid_argument("GLIDE renderer received an invalid presentation mode");
        }

        // XNA applications do not select a backend-specific presentation mode.
        // Glide owns the physical display mode and scales it through its runtime,
        // so every CNA presentation policy degrades to the same native-backbuffer
        // path instead of making an otherwise valid game fail during startup.
        impl_->presentationMode = CnaPresentationMode::NativeBackBuffer;
    }

    void GlideRenderer::SetSwapInterval(int interval)
    {
        if (interval < 0 || interval > 1)
        {
            throw std::runtime_error("GLIDE renderer supports only swap intervals 0 (immediate) and 1 (v-sync)");
        }
        impl_->swapInterval = interval;
    }

    void GlideRenderer::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        if (w < 0 || h < 0 || !std::isfinite(minDepth) || !std::isfinite(maxDepth) ||
            minDepth < 0.0f || maxDepth > 1.0f || minDepth > maxDepth)
        {
            throw std::runtime_error("GLIDE renderer received an invalid Viewport");
        }
        impl_->FlushSpriteBatch();
        impl_->viewportX = x;
        impl_->viewportY = y;
        impl_->viewportWidth = w;
        impl_->viewportHeight = h;
        impl_->viewportMinDepth = minDepth;
        impl_->viewportMaxDepth = maxDepth;
        impl_->ApplyEffectiveClipWindow();
    }

    std::unique_ptr<ITextureRenderer> GlideRenderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<GlideTextureRenderer>(*this, data);
    }

    std::unique_ptr<ISpriteBatchRenderer> GlideRenderer::CreateSpriteBatch()
    {
        return std::make_unique<GlideSpriteBatchRenderer>(*this);
    }

    std::unique_ptr<IRenderTargetRenderer> GlideRenderer::CreateRenderTarget2D(
        int, int, int, bool, bool, int)
    {
        throw std::runtime_error(
            "GLIDE renderer does not support RenderTarget2D in its initial front/back-buffer-only implementation");
    }

    void GlideRenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        if (rt != nullptr)
        {
            throw std::runtime_error("GLIDE renderer does not support RenderTarget2D");
        }
        impl_->FlushSpriteBatch();
        impl_->api.grRenderBuffer(kBufferBack);
    }

    void GlideRenderer::SetRenderTargets(const RenderTargetBindingDescriptor*, int count)
    {
        if (count != 0)
        {
            throw std::runtime_error("GLIDE renderer does not support render targets or multiple render targets");
        }
        impl_->FlushSpriteBatch();
        impl_->api.grRenderBuffer(kBufferBack);
    }

    void GlideRenderer::SetScissorRect(int x, int y, int w, int h)
    {
        impl_->FlushSpriteBatch();
        impl_->scissorX = x;
        impl_->scissorY = y;
        impl_->scissorWidth = w;
        impl_->scissorHeight = h;
        if (impl_->scissorEnabled)
        {
            impl_->ApplyEffectiveClipWindow();
        }
    }

    void GlideRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                int colorDstBlend, int alphaDstBlend,
                                                int colorBlendFunc, int alphaBlendFunc,
                                                const BlendWriteState& writeState)
    {
        using Microsoft::Xna::Framework::Graphics::BlendFunction;
        if (colorBlendFunc != static_cast<int>(BlendFunction::Add) ||
            alphaBlendFunc != static_cast<int>(BlendFunction::Add))
        {
            throw std::runtime_error("GLIDE renderer supports only BlendFunction::Add in its initial 2D scope");
        }
        const bool writeRed = ColorWriteHasRed(writeState.colorWriteChannels[0]);
        const bool writeGreen = ColorWriteHasGreen(writeState.colorWriteChannels[0]);
        const bool writeBlue = ColorWriteHasBlue(writeState.colorWriteChannels[0]);
        if (writeRed != writeGreen || writeRed != writeBlue ||
            writeState.multiSampleMask != std::numeric_limits<unsigned int>::max())
        {
            throw std::runtime_error(
                "GLIDE renderer supports RGB and alpha write masks independently, but cannot mask individual RGB channels or samples");
        }
        // Resolve every factor into a local before touching any cached or native state: if a
        // later slot is invalid, the renderer must still look exactly as it did before this call.
        using Microsoft::Xna::Framework::Graphics::Blend;
        const auto resolvedColorSrc = static_cast<FxI32>(
            ToGlideBlendFactor(static_cast<Blend>(colorSrcBlend), GlideBlendSlot::RgbSource));
        const auto resolvedColorDst = static_cast<FxI32>(
            ToGlideBlendFactor(static_cast<Blend>(colorDstBlend), GlideBlendSlot::RgbDestination));
        const auto resolvedAlphaSrc = static_cast<FxI32>(
            ToGlideBlendFactor(static_cast<Blend>(alphaSrcBlend), GlideBlendSlot::AlphaSource));
        const auto resolvedAlphaDst = static_cast<FxI32>(
            ToGlideBlendFactor(static_cast<Blend>(alphaDstBlend), GlideBlendSlot::AlphaDestination));
        if (impl_->depthTestEnabled && GlideBlendFactorsNeedAuxiliaryAlpha(
                resolvedColorSrc, resolvedColorDst, resolvedAlphaSrc, resolvedAlphaDst))
        {
            throw std::runtime_error(
                "GLIDE renderer cannot use a DestinationAlpha/InverseDestinationAlpha/"
                "SourceAlphaSaturation blend factor while depth buffering is enabled: Glide's "
                "auxiliary buffer cannot hold both a Z plane and destination-alpha data, which the "
                "Glide 3.0 reference documents as producing undefined results");
        }
        impl_->FlushSpriteBatch();
        impl_->colorSrcBlend = resolvedColorSrc;
        impl_->colorDstBlend = resolvedColorDst;
        impl_->alphaSrcBlend = resolvedAlphaSrc;
        impl_->alphaDstBlend = resolvedAlphaDst;
        impl_->colorMaskRgb = writeRed ? kFxTrue : kFxFalse;
        impl_->colorMaskAlpha = ColorWriteHasAlpha(writeState.colorWriteChannels[0]) ? kFxTrue : kFxFalse;
        impl_->ApplyColorMask();
        impl_->ApplyBlendState();
    }

    void GlideRenderer::ApplyDepthStencilState(
        bool depthEnable, bool depthWriteEnable, int depthFunc,
        bool stencilEnable, int, int, int, int, int, int, int, bool, int, int, int, int)
    {
        if (stencilEnable)
        {
            throw std::runtime_error("GLIDE has no stencil plane; stencil-enabled DepthStencilState is unsupported");
        }
        // Resolve before mutating: an invalid depthFunc must leave depth state untouched, and
        // enabling depth buffering while an active blend factor needs the auxiliary buffer for
        // destination-alpha must be rejected rather than silently corrupting future draws.
        const FxI32 resolvedDepthCompare = ToGlideDepthCompare(depthFunc);
        if (depthEnable && impl_->blendEnabled && GlideBlendFactorsNeedAuxiliaryAlpha(
                impl_->colorSrcBlend, impl_->colorDstBlend, impl_->alphaSrcBlend, impl_->alphaDstBlend))
        {
            throw std::runtime_error(
                "GLIDE renderer cannot enable depth buffering while a DestinationAlpha/"
                "InverseDestinationAlpha/SourceAlphaSaturation blend factor is active: Glide's "
                "auxiliary buffer cannot hold both a Z plane and destination-alpha data at once");
        }
        impl_->FlushSpriteBatch();
        impl_->depthTestEnabled = depthEnable;
        impl_->depthWriteEnabled = depthWriteEnable;
        impl_->depthCompare = resolvedDepthCompare;
        impl_->ApplyDepthState();
    }

    void GlideRenderer::ApplyRasterizerState(int cullMode, int fillMode,
                                                     bool scissorTestEnable,
                                                     float depthBias, float slopeScaleDepthBias)
    {
        if (fillMode != 0)
        {
            throw std::runtime_error("GLIDE renderer currently supports FillMode::Solid only");
        }
        if (std::abs(depthBias) > 0.000001f || std::abs(slopeScaleDepthBias) > 0.000001f)
        {
            throw std::runtime_error(
                "GLIDE RasterizerState depth bias is unsupported: Glide's integer depth-bias scale "
                "cannot faithfully represent XNA's normalized/slope-scaled bias");
        }
        impl_->FlushSpriteBatch();
        switch (cullMode)
        {
            case 0: impl_->api.grCullMode(kCullDisable); break;
            // CNA and the UpperLeft Glide window coordinates both define winding in screen space.
            case 1: impl_->api.grCullMode(kCullPositive); break;
            case 2: impl_->api.grCullMode(kCullNegative); break;
            default: throw std::runtime_error("GLIDE renderer received an unknown CullMode value");
        }
        impl_->scissorEnabled = scissorTestEnable;
        impl_->ApplyEffectiveClipWindow();
    }

    void GlideRenderer::ApplySamplerState(int slot, int filter,
                                                  int addressU, int addressV, int maxAnisotropy)
    {
        if (slot < 0)
        {
            throw std::runtime_error("GLIDE renderer received a negative texture slot");
        }
        // GraphicsDevice commits all 16 public sampler slots. Glide's implemented pipeline uses
        // TMU0 and, for DualTextureEffect, TMU1. Higher unused slots are deliberately inert rather
        // than making an otherwise valid draw fail during state synchronization.
        if (slot > 1)
        {
            return;
        }
        static_cast<void>(ToGlideSamplerSettings(filter));
        static_cast<void>(maxAnisotropy);
        static_cast<void>(ToGlideTextureAddress(addressU));
        static_cast<void>(ToGlideTextureAddress(addressV));
        impl_->samplerFilter[static_cast<std::size_t>(slot)] = filter;
        impl_->samplerAddressU[static_cast<std::size_t>(slot)] = addressU;
        impl_->samplerAddressV[static_cast<std::size_t>(slot)] = addressV;
    }

    void GlideRenderer::ApplySamplerMipState(int slot, int maxMipLevel, float lodBias)
    {
        if (slot < 0)
        {
            throw std::runtime_error("GLIDE renderer received a negative texture slot");
        }
        if (slot > 1)
        {
            return;
        }
        // Glide's fixed-function LOD selector has a native bias control but no per-sampler
        // maximum-mip clamp. Keep the unsupported half explicit rather than silently applying a
        // different texture level than CNA requested.
        ValidateGlideSamplerMipState(maxMipLevel, lodBias);
        // DrawSprite() reads slot 0's bias too; a mid-batch change must not be silently skipped for
        // sprites already queued under the old bias. Slot 1 never participates in SpriteBatch.
        if (slot == 0)
        {
            impl_->FlushSpriteBatch();
        }
        impl_->samplerLodBias[static_cast<std::size_t>(slot)] = lodBias;
    }

    int GlideRenderer::GetMaxTextureDimension() const
    {
        // Each physical tile is limited by GR_MAX_TEXTURE_SIZE, queried during construction. CNA
        // transparently tiles a logical image, so retain the shared API's documented 16K ceiling.
        return 16384;
    }

    void GlideRenderer::DrawSprite(const ITextureRenderer& texture,
                                          const Rectangle& destinationRectangle,
                                          const Rectangle& sourceRectangle,
                                          const Color& color, float rotation,
                                          const Vector2& origin, SpriteEffects effects,
                                          const Matrix& transform, int textureFilter,
                                          int addressU, int addressV)
    {
        const auto* glideTexture = dynamic_cast<const GlideTextureRenderer*>(&texture);
        if (glideTexture == nullptr)
        {
            throw std::runtime_error("GLIDE SpriteBatch received a texture created by a different renderer");
        }
        if (sourceRectangle.Width <= 0 || sourceRectangle.Height <= 0 || destinationRectangle.Width == 0 ||
            destinationRectangle.Height == 0)
        {
            return;
        }
        // `texture` is publicly const during a draw, but its renderer owns mutable native cache
        // storage. Rebuild only address-dependent gutters/padding before TMU0 sees the tile.
        const_cast<GlideTextureRenderer*>(glideTexture)->EnsureAddressMode(addressU, addressV);

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

        for (const GlideTextureRenderer::Tile& tile : glideTexture->Tiles())
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
            // Convert the tile-local texel offset (including its gutter padding) into Glide's
            // native "0..256 per repeat" window-coordinate S/T units, same as the 3D draw path.
            const float coordinateScale =
                GlideNativeTextureCoordinateScale(tile.paddedWidth, tile.paddedHeight);
            const std::array<Vector2, 4> texcoords = {
                Vector2(static_cast<float>(texLeft - tile.sourceX + tile.gutterLeft) * coordinateScale,
                        static_cast<float>(texTop - tile.sourceY + tile.gutterTop) * coordinateScale),
                Vector2(static_cast<float>(texRight - tile.sourceX + tile.gutterLeft) * coordinateScale,
                        static_cast<float>(texTop - tile.sourceY + tile.gutterTop) * coordinateScale),
                Vector2(static_cast<float>(texRight - tile.sourceX + tile.gutterLeft) * coordinateScale,
                        static_cast<float>(texBottom - tile.sourceY + tile.gutterTop) * coordinateScale),
                Vector2(static_cast<float>(texLeft - tile.sourceX + tile.gutterLeft) * coordinateScale,
                        static_cast<float>(texBottom - tile.sourceY + tile.gutterTop) * coordinateScale) };
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
            // Adjacent sprites that keep the same native TMU binding and sampler settings share
            // one pending batch; anything else (a different tile/texture, a different filter or
            // address mode, or any other GlideRenderer call in between) flushes first, so
            // a queued sprite is never rendered under state it wasn't actually drawn with.
            const bool sameBinding = impl_->spriteBatchBound &&
                impl_->spriteBoundTmuAddress == tile.range.address &&
                impl_->spriteSamplerFilter == textureFilter &&
                impl_->spriteSamplerAddressU == addressU && impl_->spriteSamplerAddressV == addressV;
            if (!sameBinding)
            {
                impl_->FlushSpriteBatch();
                // A preceding colored 3D draw leaves the fixed function combiner untextured.
                // Restore the SpriteBatch path explicitly instead of relying on accidental state.
                impl_->ConfigureSpriteCombiner();
                impl_->api.grTexSource(0, tile.range.address, kMipMapBoth,
                                        const_cast<GlideTexInfo*>(&tile.nativeInfo));
                impl_->api.grTexFilterMode(0, sampler.minFilter, sampler.magFilter);
                // Geometry has already been restricted to the selected tile; letting native Wrap
                // or Mirror escape into its power-of-two padding would sample a wrong logical tile.
                impl_->api.grTexClampMode(0, kTexClampClamp, kTexClampClamp);
                impl_->api.grTexMipMapMode(0, kMipMapNearest, sampler.lodBlend);
                impl_->api.grTexLodBiasValue(0, impl_->samplerLodBias[0]);
                impl_->spriteBatchBound = true;
                impl_->spriteBoundTmuAddress = tile.range.address;
                impl_->spriteSamplerFilter = textureFilter;
                impl_->spriteSamplerAddressU = addressU;
                impl_->spriteSamplerAddressV = addressV;
            }
            const GlideVertex topLeft = makeVertex(0);
            const GlideVertex topRight = makeVertex(1);
            const GlideVertex bottomRight = makeVertex(2);
            const GlideVertex bottomLeft = makeVertex(3);
            impl_->pendingSpriteTriangles.push_back(topLeft);
            impl_->pendingSpriteTriangles.push_back(topRight);
            impl_->pendingSpriteTriangles.push_back(bottomRight);
            impl_->pendingSpriteTriangles.push_back(topLeft);
            impl_->pendingSpriteTriangles.push_back(bottomRight);
            impl_->pendingSpriteTriangles.push_back(bottomLeft);
            if (impl_->pendingSpriteTriangles.size() >= GlideRenderer::Impl::kMaxPendingSpriteVertices)
            {
                impl_->FlushSpriteBatch();
            }
        }
    }

    void GlideRenderer::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        impl_->FlushSpriteBatch();
        impl_->api.grRenderBuffer(kBufferBack);
        impl_->ClearColorAndDepth(PackArgb(r, g, b, a),
                                  static_cast<std::uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f),
                                  ToGlideDepth(depth));
    }

    void GlideRenderer::ClearDepth(float depth)
    {
        impl_->FlushSpriteBatch();
        impl_->api.grRenderBuffer(kBufferBack);
        impl_->ClearDepthOnly(ToGlideDepth(depth));
    }

    void GlideRenderer::ClearStencil(int) { ThrowUnsupported("ClearStencil"); }
    void GlideRenderer::ClearDepthAndStencil(float, int) { ThrowUnsupported("ClearDepthAndStencil"); }
    void GlideRenderer::ClearColorAndStencil(float, float, float, float, int) { ThrowUnsupported("ClearColorAndStencil"); }
    void GlideRenderer::ClearColorDepthAndStencil(float, float, float, float, float, int) { ThrowUnsupported("ClearColorDepthAndStencil"); }
    void GlideRenderer::SetDepthTestEnabled(bool enabled)
    {
        if (enabled && impl_->blendEnabled && GlideBlendFactorsNeedAuxiliaryAlpha(
                impl_->colorSrcBlend, impl_->colorDstBlend, impl_->alphaSrcBlend, impl_->alphaDstBlend))
        {
            throw std::runtime_error(
                "GLIDE renderer cannot enable depth buffering while a DestinationAlpha/"
                "InverseDestinationAlpha/SourceAlphaSaturation blend factor is active: Glide's "
                "auxiliary buffer cannot hold both a Z plane and destination-alpha data at once");
        }
        impl_->FlushSpriteBatch();
        impl_->depthTestEnabled = enabled;
        impl_->ApplyDepthState();
    }

    void GlideRenderer::SetBlendEnabled(bool enabled)
    {
        if (enabled && impl_->depthTestEnabled && GlideBlendFactorsNeedAuxiliaryAlpha(
                impl_->colorSrcBlend, impl_->colorDstBlend, impl_->alphaSrcBlend, impl_->alphaDstBlend))
        {
            throw std::runtime_error(
                "GLIDE renderer cannot enable a DestinationAlpha/InverseDestinationAlpha/"
                "SourceAlphaSaturation blend factor while depth buffering is enabled: Glide's "
                "auxiliary buffer cannot hold both a Z plane and destination-alpha data at once");
        }
        impl_->FlushSpriteBatch();
        impl_->blendEnabled = enabled;
        impl_->ApplyBlendState();
    }

    void GlideRenderer::SetDepthWriteEnabled(bool enabled)
    {
        impl_->FlushSpriteBatch();
        impl_->depthWriteEnabled = enabled;
        impl_->ApplyDepthState();
    }

    std::unique_ptr<IVertexBufferRenderer> GlideRenderer::CreateVertexBuffer(int vertexCapacity)
    {
        return std::make_unique<GlideVertexBufferRenderer>(vertexCapacity);
    }

    std::unique_ptr<IIndexBufferRenderer> GlideRenderer::CreateIndexBuffer16(int indexCapacity)
    {
        return std::make_unique<GlideIndexBufferRenderer>(indexCapacity);
    }

    std::unique_ptr<IIndexBufferRenderer> GlideRenderer::CreateIndexBuffer32(int indexCapacity)
    {
        // Indices are expanded into the CPU command stream before calling grDrawTriangle, so the
        // historical API's lack of an indexed primitive entry point is not a 16-bit limitation.
        return std::make_unique<GlideIndexBufferRenderer>(indexCapacity);
    }

    std::unique_ptr<IOcclusionQueryRenderer> GlideRenderer::CreateOcclusionQuery() { ThrowUnsupported("CreateOcclusionQuery"); }

    void GlideRenderer::DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                                      const Matrix& world, const Matrix& view, const Matrix& projection,
                                                      PrimitiveType primitive, int primitiveCount)
    {
        const GpuDrawParams params{};
        DrawPrimitiveRange(vb, world, view, projection, primitive, primitiveCount, 0, params);
    }

    void GlideRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                                             const IIndexBufferRenderer& ib,
                                                             const Matrix& world, const Matrix& view, const Matrix& projection,
                                                             PrimitiveType primitive, int primitiveCount)
    {
        const GpuDrawParams params{};
        DrawIndexedPrimitiveRange(vb, ib, world, view, projection, primitive, primitiveCount, 0, 0, params);
    }

    namespace
    {
        using CpuVertex = GlideClipVertex;

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
                    throw std::runtime_error("GLIDE renderer received an unknown TextureAddressMode value");
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

        [[nodiscard]] bool TileOwnsGlideTextureCoordinate(
            const GlideTextureRenderer::Tile& tile, const GlideTextureRenderer& texture, float u, float v)
        {
            const auto owns = [](float coordinate, int sourceBegin, int sourceLength, int fullLength)
            {
                const float begin = static_cast<float>(sourceBegin) / fullLength;
                const float end = static_cast<float>(sourceBegin + sourceLength) / fullLength;
                return coordinate >= begin && (coordinate < end || end == 1.0f);
            };
            return owns(u, tile.sourceX, tile.sourceWidth, texture.GetWidth()) &&
                   owns(v, tile.sourceY, tile.sourceHeight, texture.GetHeight());
        }

        [[nodiscard]] float ClampUnit(float value)
        {
            return std::clamp(value, 0.0f, 1.0f);
        }

        void ValidateFixedFunctionDrawParams(const GpuDrawParams& params)
        {
            if (params.envMap != nullptr || params.envMapping || params.pbr || params.skinned ||
                params.instanceCount != 1 || params.customEffectRenderer != nullptr)
            {
                throw std::runtime_error(
                    "GLIDE 3D supports the fixed-function BasicEffect/DualTextureEffect subset only; "
                    "environment mapping, PBR, skinning, instancing and custom Effects are unavailable");
            }
            if (params.lightingEnabled && params.preferPerPixelLighting)
            {
                throw std::runtime_error("GLIDE BasicEffect supports only per-vertex lighting, not PreferPerPixelLighting");
            }
            if (params.lightingEnabled && params.dualTexture)
            {
                throw std::runtime_error("GLIDE renderer does not support combining BasicEffect lighting with DualTextureEffect");
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

    void GlideRenderer::DrawPrimitiveRange(const IVertexBufferRenderer& vbIn,
                                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                                  PrimitiveType primitive, int primitiveCount, int vertexStart,
                                                  const GpuDrawParams& params)
    {
        // Every 3D draw entry point funnels through here. A pending SpriteBatch queue must
        // submit before this draw's own combiner/texture/alpha-test state takes over TMU0,
        // otherwise queued sprites would render after this 3D geometry instead of before it.
        impl_->FlushSpriteBatch();
        ValidateFixedFunctionDrawParams(params);
        // Decoding never fails (DecodeAlphaTest is total over its float inputs), but do not push
        // it to native Glide yet: several checks below (buffer/texture casts, bounds, the lighting
        // normal-matrix inversion) can still throw and abort this draw entirely, and native alpha-
        // test state must not change for a draw that never actually submits geometry.
        const AlphaTestState alphaTest = DecodeAlphaTest(params);
        const auto* vb = dynamic_cast<const GlideVertexBufferRenderer*>(&vbIn);
        if (vb == nullptr)
        {
            throw std::runtime_error("GLIDE 3D received a vertex buffer created by a different renderer");
        }
        ValidateGlideVertexStreams(params, &vbIn, static_cast<int>(vb->Stride()),
                                   vb->GetVertexCount());
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
        const auto* texture = textured ? dynamic_cast<const GlideTextureRenderer*>(params.texture0) : nullptr;
        if (textured && texture == nullptr)
        {
            throw std::runtime_error("GLIDE textured draw received a texture created by a different renderer");
        }
        // GLIDE-FUT-004: DualTextureEffect's second texture slot. Validate everything before
        // touching any native state (matching this renderer's usual exception-atomicity rule).
        const GlideTextureRenderer* dualTexture1 = nullptr;
        if (params.dualTexture)
        {
            if (!textured)
            {
                throw std::runtime_error("GLIDE DualTextureEffect requires a TMU0 texture");
            }
            if (primitive != PrimitiveType::TriangleList && primitive != PrimitiveType::TriangleStrip)
            {
                throw std::runtime_error("GLIDE DualTextureEffect is only supported for triangle primitives");
            }
            if (!impl_->secondTmuAvailable)
            {
                throw std::runtime_error(
                    "GLIDE DualTextureEffect requires a second TMU, which this Glide runtime did not report "
                    "(GR_NUM_TMU < 2)");
            }
            dualTexture1 = dynamic_cast<const GlideTextureRenderer*>(params.texture1);
            if (dualTexture1 == nullptr)
            {
                throw std::runtime_error(
                    "GLIDE DualTextureEffect requires a second texture created by this same renderer");
            }
            if (texture->GetWidth() != dualTexture1->GetWidth() || texture->GetHeight() != dualTexture1->GetHeight())
            {
                // A genuinely independent second UV channel would remove this restriction, but
                // this renderer's vertex-declaration parser (GLIDE-AUD-012/FUT-002) only accepts a
                // single TextureCoordinate0 semantic, so both TMUs necessarily read the same
                // native s/t value; that is only correct when both textures share one scale.
                throw std::runtime_error(
                    "GLIDE DualTextureEffect requires both textures to have identical dimensions: this "
                    "renderer shares one texture-coordinate channel between TMU0 and TMU1");
            }
            ValidateGlideDualSamplerAddressModes(
                impl_->samplerAddressU[0], impl_->samplerAddressV[0],
                impl_->samplerAddressU[1], impl_->samplerAddressV[1]);
        }
        if (textured)
        {
            const_cast<GlideTextureRenderer*>(texture)->EnsureAddressMode(
                impl_->samplerAddressU[0], impl_->samplerAddressV[0]);
            if (params.dualTexture)
            {
                // texture0 has no reason to reject being tiled on its own (that is its normal,
                // fully-supported mode); only the dual-texture combination cannot handle it, since
                // TMU1's single tile shares a native coordinate with whichever of texture0's tiles
                // a given fragment lands in. texture1's own single-tile requirement is enforced
                // directly inside EnsureTmu1Resident()/BuildSingleTmu1Tile().
                if (texture->IsTiled())
                {
                    throw std::runtime_error(
                        "GLIDE DualTextureEffect does not support a first texture that needs more than "
                        "one physical tile at the runtime's reported GR_MAX_TEXTURE_SIZE");
                }
                const_cast<GlideTextureRenderer*>(dualTexture1)->EnsureTmu1Resident(
                    impl_->samplerAddressU[1], impl_->samplerAddressV[1]);
                // REMED-GFX-228: EnsureTmu1Resident currently prepares texture1's logical
                // pyramid through its ordinary TMU0 residency path. Under TMU0 pressure that
                // allocation may evict texture0 after we validated it above. Restore texture0 as
                // the final TMU0 requester; evicting texture1's now-unneeded TMU0 copy leaves its
                // independent TMU1 allocation intact.
                const_cast<GlideTextureRenderer*>(texture)->EnsureAddressMode(
                    impl_->samplerAddressU[0], impl_->samplerAddressV[0]);
                if (texture->IsTiled())
                {
                    throw std::runtime_error(
                        "GLIDE DualTextureEffect cannot restore its first texture as one native tile");
                }
            }
        }
        const Matrix wvp = world * view * projection;
        // CNA's matrix convention transforms positions as row vectors. Normals therefore need
        // the transpose of World^{-1}; using World's upper 3x3 is only correct for rigid or
        // uniform-scale transforms and visibly bends directional lighting otherwise.
        std::array<float, 9> normalMatrix{};
        GlideBasicEffectLightingState lightingState{};
        if (params.lightingEnabled)
        {
            normalMatrix = InvertGlideLightingWorld3x3({
                world.M11, world.M12, world.M13,
                world.M21, world.M22, world.M23,
                world.M31, world.M32, world.M33});
            lightingState.ambient = {params.ambientColor[0], params.ambientColor[1], params.ambientColor[2]};
            lightingState.eyePosition = {
                params.eyePositionWorld[0], params.eyePositionWorld[1], params.eyePositionWorld[2]};
            lightingState.materialSpecular = {
                params.specularColor[0], params.specularColor[1], params.specularColor[2]};
            lightingState.lights = {{
                {{params.light0Dir[0], params.light0Dir[1], params.light0Dir[2]},
                 {params.light0Diffuse[0], params.light0Diffuse[1], params.light0Diffuse[2]},
                 {params.light0Specular[0], params.light0Specular[1], params.light0Specular[2]}},
                {{params.light1Dir[0], params.light1Dir[1], params.light1Dir[2]},
                 {params.light1Diffuse[0], params.light1Diffuse[1], params.light1Diffuse[2]},
                 {params.light1Specular[0], params.light1Specular[1], params.light1Specular[2]}},
                {{params.light2Dir[0], params.light2Dir[1], params.light2Dir[2]},
                 {params.light2Diffuse[0], params.light2Diffuse[1], params.light2Diffuse[2]},
                 {params.light2Specular[0], params.light2Specular[1], params.light2Specular[2]}}}};
            lightingState.specularPower = params.specularPower;
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
            const GlideLightingVector vertexColor{r, g, b};
            r *= params.diffuseColor[0];
            g *= params.diffuseColor[1];
            b *= params.diffuseColor[2];
            a *= params.diffuseColor[3];
            if (params.lightingEnabled)
            {
                const GlideLightingVector worldNormal = TransformGlideLightingNormal({nx, ny, nz}, normalMatrix);
                if (DotGlideLightingVectors(worldNormal, worldNormal) <= 0.0f)
                {
                    throw std::runtime_error("GLIDE vertex-lit BasicEffect received a zero-length transformed normal");
                }
                const GlideLightingVector worldPosition{
                    x * world.M11 + y * world.M21 + z * world.M31 + world.M41,
                    x * world.M12 + y * world.M22 + z * world.M32 + world.M42,
                    x * world.M13 + y * world.M23 + z * world.M33 + world.M43};
                const GlideBasicEffectLightingResult lighting =
                    EvaluateGlideBasicEffectLighting(lightingState, worldPosition, worldNormal);
                const GlideLightingVector color = ComposeGlideBasicEffectLitColor(
                    {r, g, b}, lighting,
                    {params.emissiveColor[0] * vertexColor.x, params.emissiveColor[1] * vertexColor.y,
                     params.emissiveColor[2] * vertexColor.z},
                    a);
                r = color.x;
                g = color.y;
                b = color.z;
            }
            if (params.fogEnabled)
            {
                const float fogAmount = ClampUnit(x * params.fogVector[0] + y * params.fogVector[1] + z * params.fogVector[2] + params.fogVector[3]);
                const GlideLightingVector fogged = ApplyGlideBasicEffectFog(
                    {r, g, b}, a, {params.fogColor[0], params.fogColor[1], params.fogColor[2]}, fogAmount);
                r = fogged.x;
                g = fogged.y;
                b = fogged.z;
            }
            if (params.dualTexture)
            {
                // FNA's DualTextureEffect.fx: color = tex0; color.rgb *= 2; color *= tex1 * diffuse
                // (alpha is never doubled). Glide's texture combiner can compute tex0*tex1*diffuse
                // exactly (SCALE_OTHER chained TMU1 -> TMU0 -> iterated), but has no native "x2"
                // scale; folding it into the CPU-computed iterated RGB here is exactly equivalent,
                // since scalar multiplication is associative: 2*tex0*tex1*diffuse ==
                // tex0*tex1*(2*diffuse). The final ClampUnit() below matches the shader's implicit
                // clamp when writing the doubled result to a render target.
                r *= 2.0f;
                g *= 2.0f;
                b *= 2.0f;
            }
            CpuVertex result{
                x * wvp.M11 + y * wvp.M21 + z * wvp.M31 + wvp.M41,
                x * wvp.M12 + y * wvp.M22 + z * wvp.M32 + wvp.M42,
                x * wvp.M13 + y * wvp.M23 + z * wvp.M33 + wvp.M43,
                x * wvp.M14 + y * wvp.M24 + z * wvp.M34 + wvp.M44,
                ClampUnit(r) * 255.0f, ClampUnit(g) * 255.0f, ClampUnit(b) * 255.0f, ClampUnit(a) * 255.0f, u, v};
            if (!HasFiniteGlideClipCoordinates(result))
            {
                throw std::runtime_error("GLIDE 3D draw received non-finite transformed vertex data");
            }
            return result;
        };
        const auto makeGlideVertex = [&](const CpuVertex& input, const GlideTextureRenderer::Tile* tile) -> GlideVertex
        {
            if (input.clipW < kGlideMinimumPositiveClipW)
            {
                throw std::runtime_error("GLIDE frustum clipper emitted a non-positive homogeneous W");
            }
            const float reciprocalW = 1.0f / input.clipW;
            const float ndcX = input.clipX * reciprocalW;
            const float ndcY = input.clipY * reciprocalW;
            const float ndcZ = input.clipZ * reciprocalW;
            // Convert the tile-local texel offset (including its gutter padding) into Glide's
            // native "0..256 per repeat" window-coordinate S/T units before dividing by W.
            const float coordinateScale = tile == nullptr ? 0.0f :
                GlideNativeTextureCoordinateScale(tile->paddedWidth, tile->paddedHeight);
            const float s = tile == nullptr ? 0.0f :
                (input.u * static_cast<float>(texture->GetWidth()) - tile->sourceX + tile->gutterLeft) * coordinateScale;
            const float t = tile == nullptr ? 0.0f :
                (input.v * static_cast<float>(texture->GetHeight()) - tile->sourceY + tile->gutterTop) * coordinateScale;
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
        // Every throw-capable validation above has now passed: this draw will definitely submit
        // geometry, so it is safe to commit the decoded alpha-test state to native Glide.
        impl_->api.grAlphaTestReferenceValue(alphaTest.reference);
        impl_->api.grAlphaTestFunction(alphaTest.function);
        const bool pointPrimitive = primitive == PrimitiveType::PointListEXT;
        const bool linePrimitive = primitive == PrimitiveType::LineList || primitive == PrimitiveType::LineStrip;
        if (pointPrimitive || linePrimitive)
        {
            // A clipped LineStrip can become several disjoint runs (and a textured line can be
            // split again at logical tile/address boundaries). Submit those runs as GR_LINES so a
            // later piece never gains an unintended connection to an earlier visible one.
            const FxI32 nativePrimitive = pointPrimitive ? kPrimitivePoints : kPrimitiveLines;
            constexpr std::size_t kMaximumBatchedPrimitiveVertices = 2u * 1024u;
            std::vector<GlideVertex> pendingPrimitiveVertices;
            const auto flushPrimitiveBatch = [&]
            {
                if (pendingPrimitiveVertices.empty())
                {
                    return;
                }
                impl_->api.grDrawVertexArrayContiguous(
                    nativePrimitive, static_cast<FxU32>(pendingPrimitiveVertices.size()),
                    pendingPrimitiveVertices.data(), static_cast<FxU32>(sizeof(GlideVertex)));
                pendingPrimitiveVertices.clear();
            };
            const auto drawPoint = [&](const CpuVertex& point, const GlideTextureRenderer::Tile* tile)
            {
                pendingPrimitiveVertices.push_back(makeGlideVertex(point, tile));
                if (pendingPrimitiveVertices.size() >= kMaximumBatchedPrimitiveVertices)
                {
                    flushPrimitiveBatch();
                }
            };
            const auto drawSegment = [&](const GlideClipSegment& segment, const GlideTextureRenderer::Tile* tile)
            {
                pendingPrimitiveVertices.push_back(makeGlideVertex(segment.first, tile));
                pendingPrimitiveVertices.push_back(makeGlideVertex(segment.second, tile));
                if (pendingPrimitiveVertices.size() >= kMaximumBatchedPrimitiveVertices)
                {
                    flushPrimitiveBatch();
                }
            };
            if (textured)
            {
                impl_->ConfigureSpriteCombiner();
                const GlideSamplerSettings sampler = ToGlideSamplerSettings(impl_->samplerFilter[0]);
                // Traversal must stay primitive-major so two primitives whose tiled fragments
                // interleave (A samples tile1 where B samples tile0 at the same pixel, or vice
                // versa) still submit in their original relative order. Rebinding/flushing only
                // when the tile actually changes keeps a single-tile texture's behaviour and cost
                // identical to before.
                const GlideTextureRenderer::Tile* boundTile = nullptr;
                const auto bindPrimitiveTile = [&](const GlideTextureRenderer::Tile& tile)
                {
                    if (boundTile == &tile)
                    {
                        return;
                    }
                    flushPrimitiveBatch();
                    impl_->api.grTexSource(
                        0, tile.range.address, kMipMapBoth, const_cast<GlideTexInfo*>(&tile.nativeInfo));
                    impl_->api.grTexFilterMode(0, sampler.minFilter, sampler.magFilter);
                    impl_->api.grTexClampMode(0, kTexClampClamp, kTexClampClamp);
                    impl_->api.grTexMipMapMode(0, kMipMapNearest, sampler.lodBlend);
                    impl_->api.grTexLodBiasValue(0, impl_->samplerLodBias[0]);
                    boundTile = &tile;
                };
                if (pointPrimitive)
                {
                    for (int point = 0; point < primitiveCount; ++point)
                    {
                        CpuVertex input = readVertex(vertexStart + point);
                        if (!IsGlidePointInsideFrustum(input))
                        {
                            continue;
                        }
                        input.u = MapGlideTextureCoordinateToUnit(input.u, impl_->samplerAddressU[0]);
                        input.v = MapGlideTextureCoordinateToUnit(input.v, impl_->samplerAddressV[0]);
                        for (const GlideTextureRenderer::Tile& tile : texture->Tiles())
                        {
                            if (TileOwnsGlideTextureCoordinate(tile, *texture, input.u, input.v))
                            {
                                bindPrimitiveTile(tile);
                                drawPoint(input, &tile);
                                break; // tiles partition UV space; a point owns exactly one.
                            }
                        }
                    }
                }
                else
                {
                    for (int line = 0; line < primitiveCount; ++line)
                    {
                        const int offset = vertexStart +
                            (primitive == PrimitiveType::LineList ? line * 2 : line);
                        const std::optional<GlideClipSegment> frustumSegment = ClipGlideSegmentToFrustum(
                            readVertex(offset), readVertex(offset + 1));
                        if (!frustumSegment)
                        {
                            continue;
                        }
                        for (const GlideTextureRenderer::Tile& tile : texture->Tiles())
                        {
                            const float u0 = static_cast<float>(tile.sourceX) / texture->GetWidth();
                            const float u1 = static_cast<float>(tile.sourceX + tile.sourceWidth) / texture->GetWidth();
                            const float v0 = static_cast<float>(tile.sourceY) / texture->GetHeight();
                            const float v1 = static_cast<float>(tile.sourceY + tile.sourceHeight) / texture->GetHeight();
                            const std::vector<TextureAddressSegment> uSegments = MakeTextureAddressSegments(
                                impl_->samplerAddressU[0], std::min(frustumSegment->first.u, frustumSegment->second.u),
                                std::max(frustumSegment->first.u, frustumSegment->second.u), u0, u1);
                            const std::vector<TextureAddressSegment> vSegments = MakeTextureAddressSegments(
                                impl_->samplerAddressV[0], std::min(frustumSegment->first.v, frustumSegment->second.v),
                                std::max(frustumSegment->first.v, frustumSegment->second.v), v0, v1);
                            for (const TextureAddressSegment& uSegment : uSegments)
                            {
                                const std::optional<GlideClipSegment> uClipped = ClipGlideSegmentToHalfSpace(
                                    *frustumSegment,
                                    [uSegment](const CpuVertex& vertex) { return vertex.u - uSegment.lower; });
                                if (!uClipped)
                                {
                                    continue;
                                }
                                const std::optional<GlideClipSegment> uBounded = ClipGlideSegmentToHalfSpace(
                                    *uClipped,
                                    [uSegment](const CpuVertex& vertex) { return uSegment.upper - vertex.u; });
                                if (!uBounded)
                                {
                                    continue;
                                }
                                for (const TextureAddressSegment& vSegment : vSegments)
                                {
                                    const std::optional<GlideClipSegment> vClipped = ClipGlideSegmentToHalfSpace(
                                        *uBounded,
                                        [vSegment](const CpuVertex& vertex) { return vertex.v - vSegment.lower; });
                                    if (!vClipped)
                                    {
                                        continue;
                                    }
                                    const std::optional<GlideClipSegment> tileSegment = ClipGlideSegmentToHalfSpace(
                                        *vClipped,
                                        [vSegment](const CpuVertex& vertex) { return vSegment.upper - vertex.v; });
                                    if (!tileSegment)
                                    {
                                        continue;
                                    }
                                    GlideClipSegment mapped = *tileSegment;
                                    mapped.first.u = mapped.first.u * uSegment.scale + uSegment.offset;
                                    mapped.second.u = mapped.second.u * uSegment.scale + uSegment.offset;
                                    mapped.first.v = mapped.first.v * vSegment.scale + vSegment.offset;
                                    mapped.second.v = mapped.second.v * vSegment.scale + vSegment.offset;
                                    const float midpointU = (mapped.first.u + mapped.second.u) * 0.5f;
                                    const float midpointV = (mapped.first.v + mapped.second.v) * 0.5f;
                                    if (TileOwnsGlideTextureCoordinate(tile, *texture, midpointU, midpointV))
                                    {
                                        bindPrimitiveTile(tile);
                                        drawSegment(mapped, &tile);
                                    }
                                }
                            }
                        }
                    }
                }
                flushPrimitiveBatch();
            }
            else
            {
                impl_->ConfigureColoredCombiner();
                if (pointPrimitive)
                {
                    for (int point = 0; point < primitiveCount; ++point)
                    {
                        const CpuVertex input = readVertex(vertexStart + point);
                        if (IsGlidePointInsideFrustum(input))
                        {
                            drawPoint(input, nullptr);
                        }
                    }
                }
                else
                {
                    for (int line = 0; line < primitiveCount; ++line)
                    {
                        const int offset = vertexStart +
                            (primitive == PrimitiveType::LineList ? line * 2 : line);
                        const std::optional<GlideClipSegment> segment = ClipGlideSegmentToFrustum(
                            readVertex(offset), readVertex(offset + 1));
                        if (segment)
                        {
                            drawSegment(*segment, nullptr);
                        }
                    }
                }
                flushPrimitiveBatch();
            }
            return;
        }
        std::vector<GlideVertex> pendingTriangles;
        constexpr std::size_t kMaximumBatchedVertices = 3u * 1024u;
        const auto flushTriangleBatch = [&]
        {
            if (pendingTriangles.empty())
            {
                return;
            }
            impl_->api.grDrawVertexArrayContiguous(
                kPrimitiveTriangles, static_cast<FxU32>(pendingTriangles.size()), pendingTriangles.data(),
                static_cast<FxU32>(sizeof(GlideVertex)));
            pendingTriangles.clear();
        };
        const auto drawFan = [&](const std::vector<CpuVertex>& polygon, const GlideTextureRenderer::Tile* tile)
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
            if (params.dualTexture)
            {
                impl_->ConfigureDualTextureCombiner();
            }
            else
            {
                impl_->ConfigureSpriteCombiner();
            }
            const GlideSamplerSettings sampler0 = ToGlideSamplerSettings(impl_->samplerFilter[0]);
            const GlideSamplerSettings sampler1 = ToGlideSamplerSettings(impl_->samplerFilter[1]);
            // Traversal is primitive-major, not tile-major: a tile-major outer loop would emit
            // every triangle's tile-0 fragment before any triangle's tile-1 fragment, silently
            // reordering two triangles whenever one samples tile-1 where the other samples tile-0
            // at an overlapping pixel. Rebinding/flushing only on an actual tile change keeps a
            // single-tile texture's native call count and batching identical to before.
            const GlideTextureRenderer::Tile* boundTile = nullptr;
            const auto bindTriangleTile = [&](const GlideTextureRenderer::Tile& tile)
            {
                if (boundTile == &tile)
                {
                    return;
                }
                flushTriangleBatch();
                impl_->api.grTexSource(0, tile.range.address, kMipMapBoth, const_cast<GlideTexInfo*>(&tile.nativeInfo));
                impl_->api.grTexFilterMode(0, sampler0.minFilter, sampler0.magFilter);
                // CPU partitions the logical image at every tile and address-mode boundary, so
                // each submitted polygon is sampled only from its selected native tile.
                impl_->api.grTexClampMode(0, kTexClampClamp, kTexClampClamp);
                impl_->api.grTexMipMapMode(0, kMipMapNearest, sampler0.lodBlend);
                impl_->api.grTexLodBiasValue(0, impl_->samplerLodBias[0]);
                if (params.dualTexture && dualTexture1 != nullptr)
                {
                    // texture0 is validated single-tile whenever dualTexture is set, so this runs
                    // at most once per draw call -- TMU1's single tile never changes mid-loop.
                    impl_->api.grTexSource(1, dualTexture1->Tmu1TextureAddress(), kMipMapBoth,
                                            const_cast<GlideTexInfo*>(&dualTexture1->Tmu1NativeInfo()));
                    impl_->api.grTexFilterMode(1, sampler1.minFilter, sampler1.magFilter);
                    impl_->api.grTexClampMode(1, kTexClampClamp, kTexClampClamp);
                    impl_->api.grTexMipMapMode(1, kMipMapNearest, sampler1.lodBlend);
                    impl_->api.grTexLodBiasValue(1, impl_->samplerLodBias[1]);
                }
                boundTile = &tile;
            };
            const auto drawTriangleForTile = [&](const GlideTextureRenderer::Tile& tile,
                                                  const CpuVertex& a, const CpuVertex& b, const CpuVertex& c)
            {
                std::vector<CpuVertex> polygon{a, b, c};
                polygon = ClipGlidePolygonToFrustum(std::move(polygon));
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
                    impl_->samplerAddressU[0], minMaxU.first->u, minMaxU.second->u, u0, u1);
                const std::vector<TextureAddressSegment> vSegments = MakeTextureAddressSegments(
                    impl_->samplerAddressV[0], minMaxV.first->v, minMaxV.second->v, v0, v1);
                for (const TextureAddressSegment& uSegment : uSegments)
                {
                    std::vector<CpuVertex> uPolygon = ClipGlidePolygonToHalfSpace(
                        polygon, [uSegment](const CpuVertex& v) { return v.u - uSegment.lower; });
                    uPolygon = ClipGlidePolygonToHalfSpace(
                        uPolygon, [uSegment](const CpuVertex& v) { return uSegment.upper - v.u; });
                    if (uPolygon.size() < 3)
                    {
                        continue;
                    }
                    for (const TextureAddressSegment& vSegment : vSegments)
                    {
                        std::vector<CpuVertex> tilePolygon = ClipGlidePolygonToHalfSpace(
                            uPolygon, [vSegment](const CpuVertex& v) { return v.v - vSegment.lower; });
                        tilePolygon = ClipGlidePolygonToHalfSpace(
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
                        bindTriangleTile(tile);
                        drawFan(tilePolygon, &tile);
                    }
                }
            };
            for (int triangle = 0; triangle < primitiveCount; ++triangle)
            {
                const int offset = vertexStart + (primitive == PrimitiveType::TriangleList ? triangle * 3 : triangle);
                CpuVertex a;
                CpuVertex b;
                CpuVertex c;
                if (primitive == PrimitiveType::TriangleList || (triangle & 1) == 0)
                {
                    a = readVertex(offset);
                    b = readVertex(offset + 1);
                    c = readVertex(offset + 2);
                }
                else
                {
                    a = readVertex(offset + 1);
                    b = readVertex(offset);
                    c = readVertex(offset + 2);
                }
                for (const GlideTextureRenderer::Tile& tile : texture->Tiles())
                {
                    drawTriangleForTile(tile, a, b, c);
                }
            }
            flushTriangleBatch();
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
                polygon = ClipGlidePolygonToFrustum(std::move(polygon));
                if (polygon.size() >= 3)
                {
                    drawFan(polygon, nullptr);
                }
            }
            flushTriangleBatch();
        }
    }

    void GlideRenderer::DrawIndexedPrimitiveRange(
        const IVertexBufferRenderer& vbIn, const IIndexBufferRenderer& ibIn,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, int startIndex, int baseVertex, const GpuDrawParams& params)
    {
        const auto* vb = dynamic_cast<const GlideVertexBufferRenderer*>(&vbIn);
        const auto* ib = dynamic_cast<const GlideIndexBufferRenderer*>(&ibIn);
        if (vb == nullptr || ib == nullptr)
        {
            throw std::runtime_error("GLIDE 3D received a buffer created by a different renderer");
        }
        ValidateGlideVertexStreams(params, &vbIn, static_cast<int>(vb->Stride()),
                                   vb->GetVertexCount());
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
        GlideVertexBufferRenderer expanded(indexCount);
        // Carry the source buffer's already-resolved layout forward exactly, rather than letting
        // SetData() re-derive one from stride alone: a custom VertexDeclaration can legally use a
        // stride that matches a built-in packed layout while arranging its fields differently, and
        // guessing from stride would silently decode this expanded copy with the wrong offsets.
        expanded.SetDataWithLayout(ordered.data(), indexCount, vb->Layout());
        const GpuDrawParams expandedParams = MakeGlideExpandedIndexedDrawParams(params);
        DrawPrimitiveRange(expanded, world, view, projection, primitive, primitiveCount, 0,
                           expandedParams);
    }

    void GlideRenderer::DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                                                const Matrix& world, const Matrix& view, const Matrix& projection,
                                                PrimitiveType primitive, int primitiveCount,
                                                const GpuDrawParams& params)
    {
        DrawPrimitiveRange(vb, world, view, projection, primitive, primitiveCount, params.vertexStart, params);
    }

    void GlideRenderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb,
                                                       const IIndexBufferRenderer& ib,
                                                       const Matrix& world, const Matrix& view, const Matrix& projection,
                                                       PrimitiveType primitive, int primitiveCount,
                                                       const GpuDrawParams& params)
    {
        DrawIndexedPrimitiveRange(vb, ib, world, view, projection, primitive, primitiveCount,
                                  params.startIndex, params.baseVertex, params);
    }
} // namespace CNA::Internal::Renderers::Glide

namespace CNA::Internal::Renderers
{
#ifdef CNA_RENDERER_GLIDE
    // plans/plan_runtimerenderer.md design decision 4: declared in this family's own
    // namespace so several renderer archives can link into one binary, then defined
    // below with a qualified name -- the body keeps its place unchanged.
    namespace Glide { std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args); }

    std::unique_ptr<IGraphicsRenderer> Glide::CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<Glide::GlideRenderer>(args);
    }
#endif
} // namespace CNA::Internal::Renderers
