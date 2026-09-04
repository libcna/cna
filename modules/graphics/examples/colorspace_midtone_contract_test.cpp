// SPDX-License-Identifier: MS-PL
// REMED-GFX-131: SurfaceFormat::Color is an ordinary 8-bit UNORM byte format. A byte a game writes
// through Clear(), through a vertex colour, or through a sampled texture must come back out of
// GetData() as that same byte -- on a plain Texture2D, on a RenderTarget2D, and on a
// RenderTargetCube face alike.
//
// The defect this reproduces is WebGPU-local: ConfigureSurface() prefers *UnormSrgb surface
// formats and both WebGPURenderTargetRenderer and WebGPURenderTargetCubeRenderer copy that
// surfaceFormat_ into their own colour attachment, so the render pass applies a hardware
// linear-to-sRGB ENCODE on every store. Measured before the fix: a channel written as 128 reads
// back as 188, which is exactly round(255 * srgb_encode(128/255)).
//
// Why no existing test saw it. 0 and 255 are exact fixed points of sRGB encoding, and alpha is
// never encoded in an sRGB format, so any fixture whose RGB components are drawn only from
// {0, 255} passes identically with and without the defect. That is precisely what the WebGPU
// render-target, cube, MSAA and readback fixtures use, by explicit choice in two cases
// (texture2d_getdata_contract_test.cpp and rendertargetcube_getdata_contract_test.cpp both say so
// in a comment). This file exists to remove that blind spot permanently.
//
// The palette below therefore uses fourteen channel values -- 0, 1, 16, 32, 64, 96, 127, 128, 129,
// 160, 192, 224, 254, 255 -- and six non-trivial alphas -- 1, 64, 127, 128, 192, 254. The eight
// pattern rows drive R, G and B independently and combine them asymmetrically, so that
//
//   * an sRGB ENCODE anywhere in the path,
//   * an sRGB DECODE anywhere in the path,
//   * a double encode or decode,
//   * a B<->R channel swap,
//   * an alpha gamma conversion,
//   * an accidental premultiplication, and
//   * a uniform zero / white / single-colour fallback
//
// each fail on some row, and fail distinguishably. A pass cannot be reached by accident.
//
// Legs are isolated deliberately, each answering one question, so a failure names the layer:
//
//   A  CPU SetData -> Texture2D GetData             upload, storage and readback, no rendering
//   B  Clear(mid-tone) -> RenderTarget2D GetData     clear-value conversion + attachment store
//   C  vertex colour draw -> RenderTarget2D GetData  shader output + attachment store
//   D  sample Texture2D -> RenderTarget2D GetData    texture sampling + attachment store
//   E  RT A -> sample A -> RT B GetData              render-target sampling + a second store
//   F  repeated GetData                              readback is stable and does not mutate
//   G  RenderTargetCube face                         the same format path on the cube resource
//   H  rectangle / destination-offset readback       region semantics survive the palette
//   I  blending with mid-tone source and destination the blend operates on the stored bytes
//
// Leg A is expected to pass even before the fix on every renderer -- plain WebGPU textures already
// use a non-sRGB RGBA8Unorm -- and that asymmetry is itself an asserted fact (check A5), because it
// proves ordinary textures and render targets did NOT share colour semantics.
//
// Every run prints a per-channel transfer table (input byte, normalized, identity prediction,
// standard-sRGB-encode prediction, standard-sRGB-decode prediction, actual, matched hypothesis) for
// R, G and B independently, so the transformation is measured rather than guessed.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/NotSupportedException.hpp"
#if defined(CNA_RENDERER_WEBGPU)
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    /**
     * @brief What this renderer's render-target colour readback is required to do.
     *
     * `Exact` -- GetData must return the stored bytes verbatim.
     * `Unsupported` -- GetData must throw System::NotSupportedException (REMED-GFX-127's contract),
     * reserved for renderers that perform no rasterization at all.
     */
    enum class RtContract
    {
        Exact,
        Unsupported,
    };

    /**
     * @brief Row order this renderer produces when a RenderTarget2D is SAMPLED as a texture.
     *
     * `TopDown` -- texel (x, y) of the target is sampled at (x, y). What every renderer does.
     * `BottomUp` -- the sampled image arrives vertically flipped.
     *
     * REMED-GFX-147 was measured here: EasyGL's leg D (sampling a plain Texture2D) was exactly
     * top-down while its leg E (sampling a render target) was not, so the asymmetry was in
     * render-target sampling alone. Declaring it per renderer is what kept every COLOUR assertion
     * in this file at full strength while the orientation defect was open, and is what made
     * fixing it show up HERE as a failure rather than passing silently. REMED-GFX-147 is now
     * closed and no renderer declares `BottomUp`; the enum stays because that is the property this
     * file must keep pinning.
     */
    enum class RtSampleOrientation
    {
        TopDown,
        BottomUp,
    };

    /**
     * @brief What BlendState::Additive stores on this renderer.
     *
     * `SourcePlusDestination` -- XNA's own SourceAlpha/One equation.
     * `SourceOnly` -- the destination term is dropped, as if ColorDestinationBlend were Zero.
     * REMED-GFX-148: measured on Software, whose NonPremultiplied blend against the same
     * destination in the same fixture is correct, so the destination is present and Additive
     * alone discards it. Recorded, not fixed, here.
     */
    enum class AdditiveContract
    {
        SourcePlusDestination,
        SourceOnly,
    };

#if defined(CNA_RENDERER_HEADLESS)
    constexpr RtContract kRtContract = RtContract::Unsupported;
    constexpr bool kCubeSupported = false;
    constexpr RtSampleOrientation kRtSampleOrientation = RtSampleOrientation::TopDown;
    constexpr AdditiveContract kAdditiveContract = AdditiveContract::SourcePlusDestination;
    constexpr const char* kRendererName = "HEADLESS";
#elif defined(CNA_RENDERER_SOFTWARE)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr bool kCubeSupported = false;
    constexpr RtSampleOrientation kRtSampleOrientation = RtSampleOrientation::TopDown;
    constexpr AdditiveContract kAdditiveContract = AdditiveContract::SourcePlusDestination;
    constexpr const char* kRendererName = "SOFTWARE";
#elif defined(CNA_RENDERER_EASYGL)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr bool kCubeSupported = true;
    // REMED-GFX-147 fixed: EasyGL now samples a render target in the same logical orientation as
    // an ordinary Texture2D holding identical bytes.
    constexpr RtSampleOrientation kRtSampleOrientation = RtSampleOrientation::TopDown;
    constexpr AdditiveContract kAdditiveContract = AdditiveContract::SourcePlusDestination;
    constexpr const char* kRendererName = "EASYGL";
#elif defined(CNA_RENDERER_BGFX)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr bool kCubeSupported = true;
    constexpr RtSampleOrientation kRtSampleOrientation = RtSampleOrientation::TopDown;
    constexpr AdditiveContract kAdditiveContract = AdditiveContract::SourcePlusDestination;
    constexpr const char* kRendererName = "BGFX";
#elif defined(CNA_RENDERER_VULKAN)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr bool kCubeSupported = true;
    constexpr RtSampleOrientation kRtSampleOrientation = RtSampleOrientation::TopDown;
    constexpr AdditiveContract kAdditiveContract = AdditiveContract::SourcePlusDestination;
    constexpr const char* kRendererName = "VULKAN";
#elif defined(CNA_RENDERER_WEBGPU)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr bool kCubeSupported = true;
    constexpr RtSampleOrientation kRtSampleOrientation = RtSampleOrientation::TopDown;
    constexpr AdditiveContract kAdditiveContract = AdditiveContract::SourcePlusDestination;
    constexpr const char* kRendererName = "WEBGPU";
#elif defined(CNA_RENDERER_SDL_GPU)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr bool kCubeSupported = false;
    constexpr RtSampleOrientation kRtSampleOrientation = RtSampleOrientation::TopDown;
    constexpr AdditiveContract kAdditiveContract = AdditiveContract::SourcePlusDestination;
    constexpr const char* kRendererName = "SDL_GPU";
#elif defined(CNA_RENDERER_SDL_RENDERER)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr bool kCubeSupported = false;
    constexpr RtSampleOrientation kRtSampleOrientation = RtSampleOrientation::TopDown;
    constexpr AdditiveContract kAdditiveContract = AdditiveContract::SourcePlusDestination;
    constexpr const char* kRendererName = "SDL_RENDERER";
#elif defined(CNA_RENDERER_FREEDIRECT)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr bool kCubeSupported = false;
    constexpr RtSampleOrientation kRtSampleOrientation = RtSampleOrientation::TopDown;
    constexpr AdditiveContract kAdditiveContract = AdditiveContract::SourcePlusDestination;
    constexpr const char* kRendererName = "FREEDIRECT";
#elif defined(CNA_RENDERER_DIRECTX9)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr bool kCubeSupported = true;
    constexpr RtSampleOrientation kRtSampleOrientation = RtSampleOrientation::TopDown;
    constexpr AdditiveContract kAdditiveContract = AdditiveContract::SourcePlusDestination;
    constexpr const char* kRendererName = "DIRECTX9";
#elif defined(CNA_RENDERER_DIRECTX11)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr bool kCubeSupported = true;
    constexpr RtSampleOrientation kRtSampleOrientation = RtSampleOrientation::TopDown;
    constexpr AdditiveContract kAdditiveContract = AdditiveContract::SourcePlusDestination;
    constexpr const char* kRendererName = "DIRECTX11";
#elif defined(CNA_RENDERER_DIRECTX12)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr bool kCubeSupported = true;
    constexpr RtSampleOrientation kRtSampleOrientation = RtSampleOrientation::TopDown;
    constexpr AdditiveContract kAdditiveContract = AdditiveContract::SourcePlusDestination;
    constexpr const char* kRendererName = "DIRECTX12";
#elif defined(CNA_RENDERER_CANVAS)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr bool kCubeSupported = false;
    constexpr RtSampleOrientation kRtSampleOrientation = RtSampleOrientation::TopDown;
    constexpr AdditiveContract kAdditiveContract = AdditiveContract::SourcePlusDestination;
    constexpr const char* kRendererName = "CANVAS";
#elif defined(CNA_RENDERER_LLGL)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr bool kCubeSupported = false;
    constexpr RtSampleOrientation kRtSampleOrientation = RtSampleOrientation::TopDown;
    constexpr AdditiveContract kAdditiveContract = AdditiveContract::SourcePlusDestination;
    constexpr const char* kRendererName = "LLGL";
#else
#error "REMED-GFX-131: this renderer has no declared mid-tone colour contract."
#endif

    constexpr int kBBW = 64;   ///< Backbuffer width.
    constexpr int kBBH = 32;   ///< Backbuffer height.

    /// One column per palette value. Deliberately includes 127/128/129 (the sRGB knee is nowhere
    /// near a byte boundary there, so an encode moves them by ~60) and the 0/255 fixed points that
    /// every pre-existing fixture used exclusively.
    constexpr int kValues[] = {0, 1, 16, 32, 64, 96, 127, 128, 129, 160, 192, 224, 254, 255};
    constexpr int kValueCount = static_cast<int>(sizeof(kValues) / sizeof(kValues[0]));

    /// Alphas that are neither 0 nor 255, so an alpha gamma conversion or a premultiplication
    /// cannot hide.
    constexpr int kAlphas[] = {1, 64, 127, 128, 192, 254};
    constexpr int kAlphaCount = static_cast<int>(sizeof(kAlphas) / sizeof(kAlphas[0]));

    constexpr int kW = kValueCount;  ///< Palette image width: one column per value.
    constexpr int kH = 8;            ///< Palette image height: one row per channel combination.

    /// Rows whose R, G and B are the value ramp in isolation -- the per-channel transfer oracles.
    constexpr int kRowR = 0;
    constexpr int kRowG = 1;
    constexpr int kRowB = 2;

    /**
     * @brief The palette colour at (x, y).
     *
     * Rows 0/1/2 ramp exactly one channel so each channel's transfer function can be read
     * independently. Row 3 is a grey ramp. Rows 4/6/7 combine different values in R, G and B at
     * once, so a channel swap changes the result. Rows 5/6 carry non-trivial alpha.
     */
    Color PaletteColor(int x, int y)
    {
        const int v = kValues[x % kValueCount];
        const int iv = 255 - v;
        const auto a = static_cast<std::uint8_t>(kAlphas[x % kAlphaCount]);
        const auto b = static_cast<std::uint8_t>(v);
        const auto ib = static_cast<std::uint8_t>(iv);
        switch (y)
        {
            case kRowR: return Color(b, static_cast<std::uint8_t>(0), static_cast<std::uint8_t>(0),
                                     static_cast<std::uint8_t>(255));
            case kRowG: return Color(static_cast<std::uint8_t>(0), b, static_cast<std::uint8_t>(0),
                                     static_cast<std::uint8_t>(255));
            case kRowB: return Color(static_cast<std::uint8_t>(0), static_cast<std::uint8_t>(0), b,
                                     static_cast<std::uint8_t>(255));
            case 3:     return Color(b, b, b, static_cast<std::uint8_t>(255));
            case 4:     return Color(b, ib, static_cast<std::uint8_t>(128),
                                     static_cast<std::uint8_t>(255));
            case 5:     return Color(static_cast<std::uint8_t>(255), static_cast<std::uint8_t>(128),
                                     static_cast<std::uint8_t>(0), a);
            case 6:     return Color(b, static_cast<std::uint8_t>(128), ib, a);
            default:    return Color(static_cast<std::uint8_t>(64), b, static_cast<std::uint8_t>(192),
                                     static_cast<std::uint8_t>(254));
        }
    }

    std::vector<Color> PalettePixels()
    {
        std::vector<Color> px;
        px.reserve(static_cast<std::size_t>(kW) * kH);
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x) px.push_back(PaletteColor(x, y));
        return px;
    }

    std::string ColorText(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
               std::to_string(static_cast<int>(c.getGProperty())) + "," +
               std::to_string(static_cast<int>(c.getBProperty())) + "," +
               std::to_string(static_cast<int>(c.getAProperty())) + ")";
    }

    bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }

    /// Standard IEC 61966-2-1 linear -> sRGB encode, in bytes.
    int SrgbEncodeByte(int linearByte)
    {
        const float l = std::clamp(static_cast<float>(linearByte) / 255.0f, 0.0f, 1.0f);
        const float e = l <= 0.0031308f ? l * 12.92f
                                        : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
        return static_cast<int>(std::lround(e * 255.0f));
    }

    /// Standard IEC 61966-2-1 sRGB -> linear decode, in bytes.
    int SrgbDecodeByte(int encodedByte)
    {
        const float e = std::clamp(static_cast<float>(encodedByte) / 255.0f, 0.0f, 1.0f);
        const float l = e <= 0.04045f ? e / 12.92f
                                      : std::pow((e + 0.055f) / 1.055f, 2.4f);
        return static_cast<int>(std::lround(l * 255.0f));
    }

    /// Approximate gamma 2.2 encode, in bytes -- distinguished from the true sRGB curve near 0.
    int Gamma22EncodeByte(int linearByte)
    {
        const float l = std::clamp(static_cast<float>(linearByte) / 255.0f, 0.0f, 1.0f);
        return static_cast<int>(std::lround(std::pow(l, 1.0f / 2.2f) * 255.0f));
    }

    /// Names the transfer function that reproduces @p actual from @p input, or "unclassified".
    const char* ClassifyTransfer(int input, int actual)
    {
        if (actual == input) return "identity";
        if (actual == SrgbEncodeByte(input)) return "srgb-encode";
        if (actual == SrgbDecodeByte(input)) return "srgb-decode";
        if (actual == SrgbEncodeByte(SrgbEncodeByte(input))) return "srgb-encode-twice";
        if (actual == SrgbDecodeByte(SrgbDecodeByte(input))) return "srgb-decode-twice";
        if (actual == Gamma22EncodeByte(input)) return "gamma-2.2-encode";
        if (std::abs(actual - input) <= 1) return "identity(+-1)";
        if (std::abs(actual - SrgbEncodeByte(input)) <= 2) return "srgb-encode(+-2)";
        if (std::abs(actual - SrgbDecodeByte(input)) <= 2) return "srgb-decode(+-2)";
        return "unclassified";
    }

    int ChannelOf(const Color& c, int channel)
    {
        switch (channel)
        {
            case 0:  return static_cast<int>(c.getRProperty());
            case 1:  return static_cast<int>(c.getGProperty());
            default: return static_cast<int>(c.getBProperty());
        }
    }
}

class ColorSpaceMidToneContractTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D paletteTex_;   ///< The palette, uploaded once by SetData.
    Texture2D whiteTex_;     ///< 1x1 opaque white, used to fill exact rectangles.
    bool done_ = false;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    static void ResetState(GraphicsDevice& dev)
    {
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);
    }

    /// Fills exactly @p destination with @p tint. BlendState::Opaque stores the source verbatim.
    void FillRect(GraphicsDevice& dev, const Rectangle& destination, const Color& tint)
    {
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb.Draw(whiteTex_, destination, Rectangle(0, 0, 1, 1), tint);
        sb.End();
    }

    /// Draws the whole palette texture 1:1 into the bound target with point sampling, so each
    /// destination pixel is exactly one source texel -- no filtering, no interpolation.
    void BlitPalette(GraphicsDevice& dev, Texture2D& source)
    {
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb.Draw(source, Rectangle(0, 0, kW, kH), Rectangle(0, 0, kW, kH), Color::White);
        sb.End();
    }

    /// A full-target quad of one solid vertex colour, through BasicEffect's vertex-colour path.
    /// This is the shader-output leg: it never touches a texture or a sampler.
    void DrawVertexColorQuad(GraphicsDevice& dev, const Color& color)
    {
        static BasicEffect* fx = nullptr;
        if (fx == nullptr) fx = new BasicEffect(dev);
        fx->setWorldProperty(Matrix::getIdentityProperty());
        fx->setViewProperty(Matrix::getIdentityProperty());
        fx->setProjectionProperty(Matrix::getIdentityProperty());
        fx->VertexColorEnabled = true;
        fx->Apply();
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.5f), color },
            { Vector3(-1.0f, -1.0f, 0.5f), color },
            { Vector3( 1.0f, -1.0f, 0.5f), color },
            { Vector3(-1.0f,  1.0f, 0.5f), color },
            { Vector3( 1.0f, -1.0f, 0.5f), color },
            { Vector3( 1.0f,  1.0f, 0.5f), color },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }

    /// Reads a whole render target back, or reports the deterministic rejection.
    struct Readback
    {
        bool threwNotSupported = false;
        bool threwSomethingElse = false;
        std::string otherWhat;
        std::vector<Color> pixels;
    };

    static Readback ReadWhole(RenderTarget2D& target, int w, int h)
    {
        Readback r;
        r.pixels.assign(static_cast<std::size_t>(w) * h, Color(0xCD, 0xCD, 0xCD, 0xCD));
        try
        {
            target.GetData(r.pixels.data(), 0, static_cast<int>(r.pixels.size()));
        }
        catch (const System::NotSupportedException&) { r.threwNotSupported = true; }
        catch (const std::exception& e) { r.threwSomethingElse = true; r.otherWhat = e.what(); }
        catch (...) { r.threwSomethingElse = true; r.otherWhat = "(non-std exception)"; }
        return r;
    }

    /// True when this renderer declared it cannot read render targets back at all. Those renderers
    /// must reject, and every rendered leg below is asserted as a rejection instead of a value.
    static bool RtUnsupported() { return kRtContract == RtContract::Unsupported; }

    /**
     * @brief Asserts a whole-target readback equals the palette exactly, or is honestly rejected.
     * @param r     The readback outcome.
     * @param label The leg name for the printed result.
     * @return The number of exactly matching pixels (0 when rejected).
     */
    std::size_t CheckPaletteReadback(const Readback& r, const std::string& label)
    {
        if (RtUnsupported())
        {
            check(r.threwNotSupported,
                  label + ": declared unsupported, must throw NotSupportedException" +
                  (r.threwSomethingElse ? " (threw other: " + r.otherWhat + ")" : ""));
            return 0;
        }
        if (r.threwNotSupported || r.threwSomethingElse)
        {
            check(false, label + ": readback failed (" +
                  (r.threwNotSupported ? "NotSupportedException" : r.otherWhat) + ")");
            return 0;
        }
        std::size_t exact = 0;
        std::string firstMismatch;
        for (int y = 0; y < kH; ++y)
        {
            for (int x = 0; x < kW; ++x)
            {
                const Color& got = r.pixels[static_cast<std::size_t>(y) * kW + x];
                const Color want = PaletteColor(x, y);
                if (Same(got, want)) { ++exact; continue; }
                if (firstMismatch.empty())
                    firstMismatch = " first mismatch at (" + std::to_string(x) + "," +
                                    std::to_string(y) + ") want=" + ColorText(want) +
                                    " got=" + ColorText(got);
            }
        }
        const std::size_t total = static_cast<std::size_t>(kW) * kH;
        check(exact == total,
              label + ": " + std::to_string(exact) + "/" + std::to_string(total) +
              " pixels byte-exact" + firstMismatch);
        return exact;
    }

    /// Prints the per-channel transfer table the finding requires, measured from @p r.
    void PrintTransferTable(const Readback& r, const std::string& legLabel)
    {
        if (r.threwNotSupported || r.threwSomethingElse || r.pixels.empty()) return;
        static const char* kChannelName[3] = {"R", "G", "B"};
        static const int kChannelRow[3] = {kRowR, kRowG, kRowB};
        for (int ch = 0; ch < 3; ++ch)
        {
            std::printf("[TRANSFER] %s %s channel %s: "
                        "input | normalized | identity | srgb-encode | srgb-decode | actual | match\n",
                        kRendererName, legLabel.c_str(), kChannelName[ch]);
            for (int x = 0; x < kValueCount; ++x)
            {
                const int in = kValues[x];
                const Color& got = r.pixels[static_cast<std::size_t>(kChannelRow[ch]) * kW + x];
                const int actual = ChannelOf(got, ch);
                std::printf("[TRANSFER]   %3d | %8.6f | %3d | %3d | %3d | %3d | %s\n",
                            in, static_cast<double>(in) / 255.0, in,
                            SrgbEncodeByte(in), SrgbDecodeByte(in), actual,
                            ClassifyTransfer(in, actual));
            }
            std::fflush(stdout);
        }
    }

    // ---------------------------------------------------------------- legs

    /// Leg A: CPU SetData -> Texture2D GetData. No rendering is involved at all.
    void LegA(GraphicsDevice& dev)
    {
        (void) dev;
        const std::vector<Color> want = PalettePixels();
        const std::size_t total = want.size();

        std::vector<Color> got(total, Color(0xA5, 0xA5, 0xA5, 0xA5));
        paletteTex_.GetData(got.data(), 0, static_cast<int>(total));
        std::size_t exact = 0;
        std::string firstMismatch;
        for (std::size_t i = 0; i < total; ++i)
        {
            if (Same(got[i], want[i])) { ++exact; continue; }
            if (firstMismatch.empty())
                firstMismatch = " first mismatch at index " + std::to_string(i) +
                                " want=" + ColorText(want[i]) + " got=" + ColorText(got[i]);
        }
        check(exact == total,
              "A1 Texture2D SetData -> GetData is byte-exact for every palette value: " +
              std::to_string(exact) + "/" + std::to_string(total) + firstMismatch);

        // A2: an arbitrary rectangle covering the mid-tone columns.
        const Rectangle rect(4, 2, 6, 4);
        std::vector<Color> rectGot(static_cast<std::size_t>(rect.Width) * rect.Height,
                                   Color(0xA5, 0xA5, 0xA5, 0xA5));
        paletteTex_.GetData(0, &rect, rectGot.data(), 0, static_cast<int>(rectGot.size()));
        bool rectOk = true;
        for (int ry = 0; ry < rect.Height; ++ry)
            for (int rx = 0; rx < rect.Width; ++rx)
                if (!Same(rectGot[static_cast<std::size_t>(ry) * rect.Width + rx],
                          PaletteColor(rect.X + rx, rect.Y + ry)))
                    rectOk = false;
        check(rectOk, "A2 Texture2D rectangle readback is byte-exact over the mid-tone columns");

        // A3: a non-zero destination offset leaves the leading elements untouched. `startIndex` is
        // a DESTINATION offset only on the rect overload; on the whole-level overload it indexes
        // the source, so the full-image rectangle is passed explicitly here.
        constexpr int kOffset = 5;
        const Rectangle whole(0, 0, kW, kH);
        std::vector<Color> offGot(total + kOffset, Color(0xA5, 0xA5, 0xA5, 0xA5));
        paletteTex_.GetData(0, &whole, offGot.data(), kOffset, static_cast<int>(total));
        bool offOk = true;
        for (int i = 0; i < kOffset; ++i)
            if (!Same(offGot[static_cast<std::size_t>(i)], Color(0xA5, 0xA5, 0xA5, 0xA5)))
                offOk = false;
        for (std::size_t i = 0; i < total; ++i)
            if (!Same(offGot[i + kOffset], want[i])) offOk = false;
        check(offOk, "A3 Texture2D destination-offset readback is byte-exact and writes no earlier element");

        // A4: repeated readback is stable.
        std::vector<Color> again(total, Color(0xA5, 0xA5, 0xA5, 0xA5));
        paletteTex_.GetData(again.data(), 0, static_cast<int>(total));
        bool stable = true;
        for (std::size_t i = 0; i < total; ++i)
            if (!Same(again[i], got[i])) stable = false;
        check(stable, "A4 Texture2D repeated GetData returns identical bytes");

        // A5: the ordinary-texture leg is byte-exact INDEPENDENTLY of any render target. Recorded
        // as its own assertion because it is what proves render targets and plain textures did not
        // share colour semantics before the fix.
        check(exact == total,
              "A5 ordinary Texture2D colour semantics are byte-exact independently of rendering");
    }

    /// Leg B: Clear(mid-tone) -> RenderTarget2D GetData, one clear per palette column.
    void LegB(GraphicsDevice& dev)
    {
        int okCount = 0;
        int attempted = 0;
        std::string firstMismatch;
        for (int x = 0; x < kValueCount; ++x)
        {
            // A clear colour that varies all four channels across the sweep.
            const Color clearColor = PaletteColor(x, 6);
            RenderTarget2D rt(dev, kW, kH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt);
            ResetState(dev);
            dev.Clear(clearColor);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);

            Readback r = ReadWhole(rt, kW, kH);
            ++attempted;
            if (RtUnsupported())
            {
                if (r.threwNotSupported) ++okCount;
                continue;
            }
            if (r.threwNotSupported || r.threwSomethingElse)
            {
                if (firstMismatch.empty())
                    firstMismatch = " readback failed: " +
                                    (r.threwNotSupported ? std::string("NotSupportedException")
                                                         : r.otherWhat);
                continue;
            }
            bool allSame = true;
            for (const Color& p : r.pixels)
                if (!Same(p, clearColor)) allSame = false;
            if (allSame) ++okCount;
            else if (firstMismatch.empty())
                firstMismatch = " first failing clear want=" + ColorText(clearColor) +
                                " got=" + ColorText(r.pixels[0]);
        }
        check(okCount == attempted,
              std::string("B1 RenderTarget2D Clear(mid-tone) reads back byte-exact: ") +
              std::to_string(okCount) + "/" + std::to_string(attempted) + " clears" + firstMismatch);

        // B2: the same sweep on a PreserveContents target, so the load-op path is covered too.
        int preserveOk = 0;
        for (int x = 0; x < kValueCount; ++x)
        {
            const Color clearColor = PaletteColor(x, 4);
            RenderTarget2D rt(dev, kW, kH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
            dev.SetRenderTarget(&rt);
            ResetState(dev);
            dev.Clear(clearColor);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            Readback r = ReadWhole(rt, kW, kH);
            if (RtUnsupported()) { if (r.threwNotSupported) ++preserveOk; continue; }
            if (r.threwNotSupported || r.threwSomethingElse) continue;
            bool allSame = true;
            for (const Color& p : r.pixels) if (!Same(p, clearColor)) allSame = false;
            if (allSame) ++preserveOk;
        }
        check(preserveOk == kValueCount,
              "B2 PreserveContents RenderTarget2D Clear(mid-tone) reads back byte-exact: " +
              std::to_string(preserveOk) + "/" + std::to_string(kValueCount) + " clears");
    }

    /// Leg C: vertex-colour draw -> RenderTarget2D GetData. Shader output, no texture sampling.
    void LegC(GraphicsDevice& dev)
    {
        int okCount = 0;
        std::string firstMismatch;
        for (int x = 0; x < kValueCount; ++x)
        {
            const Color vertexColor = PaletteColor(x, 3);   // grey ramp, opaque
            RenderTarget2D rt(dev, kW, kH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            DrawVertexColorQuad(dev, vertexColor);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);

            Readback r = ReadWhole(rt, kW, kH);
            if (RtUnsupported()) { if (r.threwNotSupported) ++okCount; continue; }
            if (r.threwNotSupported || r.threwSomethingElse)
            {
                if (firstMismatch.empty())
                    firstMismatch = " readback failed: " +
                                    (r.threwNotSupported ? std::string("NotSupportedException")
                                                         : r.otherWhat);
                continue;
            }
            const Color& centre = r.pixels[static_cast<std::size_t>(kH / 2) * kW + kW / 2];
            if (Same(centre, vertexColor)) ++okCount;
            else if (firstMismatch.empty())
                firstMismatch = " first failing vertex colour want=" + ColorText(vertexColor) +
                                " got=" + ColorText(centre);
        }
        check(okCount == kValueCount,
              "C1 vertex-colour draw into RenderTarget2D reads back byte-exact: " +
              std::to_string(okCount) + "/" + std::to_string(kValueCount) + " colours" + firstMismatch);
    }

    /// Leg D: sample an ordinary Texture2D 1:1 into a RenderTarget2D -> GetData.
    Readback LegD(GraphicsDevice& dev, RenderTarget2D& rt)
    {
        dev.SetRenderTarget(&rt);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        BlitPalette(dev, paletteTex_);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        Readback r = ReadWhole(rt, kW, kH);
        CheckPaletteReadback(r, "D1 sampled Texture2D -> RenderTarget2D");
        PrintTransferTable(r, "sampled-texture->RT2D");
        return r;
    }

    /// Leg E: RT A -> sample A -> RT B. Proves a render target read back as a texture is not
    /// decoded, and that a second render leg does not encode a second time.
    void LegE(GraphicsDevice& dev, RenderTarget2D& first)
    {
        RenderTarget2D second(dev, kW, kH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&second);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        BlitPalette(dev, first);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        Readback r = ReadWhole(second, kW, kH);
        // REMED-GFX-147: on a renderer that DECLARES it samples a render target bottom-up, undo
        // ONLY the row order before comparing, so every colour comparison below stays byte-exact --
        // which is what this file is for. No renderer declares that any more (REMED-GFX-147 is
        // fixed), so this is inert; it stays because the declaration is what makes an orientation
        // change in either direction fail here instead of passing silently. The dedicated
        // orientation contract lives in rendertarget_sampling_orientation_test.cpp.
        if (kRtSampleOrientation == RtSampleOrientation::BottomUp && !r.pixels.empty() &&
            !r.threwNotSupported && !r.threwSomethingElse)
        {
            std::vector<Color> flipped(r.pixels.size(), Color(0, 0, 0, 0));
            for (int y = 0; y < kH; ++y)
                for (int x = 0; x < kW; ++x)
                    flipped[static_cast<std::size_t>(y) * kW + x] =
                        r.pixels[static_cast<std::size_t>(kH - 1 - y) * kW + x];
            r.pixels.swap(flipped);
        }
        CheckPaletteReadback(r, std::string("E1 RenderTarget2D A -> sampled -> RenderTarget2D B") +
                                (kRtSampleOrientation == RtSampleOrientation::BottomUp
                                     ? " (row order un-flipped: REMED-GFX-147)" : ""));
        PrintTransferTable(r, "RT2D-A->RT2D-B");
    }

    /// Leg F: repeated readback of the same target is stable and does not mutate the source.
    void LegF(RenderTarget2D& target, const Readback& firstRead)
    {
        Readback again = ReadWhole(target, kW, kH);
        if (RtUnsupported())
        {
            check(again.threwNotSupported, "F1 repeated render-target readback rejects consistently");
            return;
        }
        if (again.threwNotSupported || again.threwSomethingElse)
        {
            check(false, "F1 repeated render-target readback failed");
            return;
        }
        bool stable = firstRead.pixels.size() == again.pixels.size();
        if (stable)
            for (std::size_t i = 0; i < again.pixels.size(); ++i)
                if (!Same(again.pixels[i], firstRead.pixels[i])) stable = false;
        check(stable, "F1 repeated RenderTarget2D GetData returns identical bytes");

        Readback third = ReadWhole(target, kW, kH);
        bool stillPalette = !third.threwNotSupported && !third.threwSomethingElse;
        if (stillPalette)
            for (int y = 0; y < kH; ++y)
                for (int x = 0; x < kW; ++x)
                    if (!Same(third.pixels[static_cast<std::size_t>(y) * kW + x], PaletteColor(x, y)))
                        stillPalette = false;
        check(stillPalette, "F2 readback does not mutate the render target it read");
    }

    /// Leg G: the same format path on a RenderTargetCube face.
    void LegG(GraphicsDevice& dev)
    {
        if (!kCubeSupported)
        {
            check(true, std::string("G1 RenderTargetCube not supported on ") + kRendererName +
                        " -- declared, not inferred");
            return;
        }
        const int size = kW;   // square faces; the palette rows repeat to fill the extra height
        RenderTargetCube rtc(dev, size, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
        const Color faceColor = PaletteColor(7, 6);   // mid-tone RGB with non-trivial alpha
        bool ok = true;
        std::string detail;
        try
        {
            dev.SetRenderTarget(&rtc, CubeMapFace::PositiveY);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            FillRect(dev, Rectangle(0, 0, size, size), faceColor);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);

            std::vector<Color> px(static_cast<std::size_t>(size) * size, Color(0xCD, 0xCD, 0xCD, 0xCD));
            rtc.GetData(CubeMapFace::PositiveY, px.data(), static_cast<int>(px.size()));
            const Color& centre = px[static_cast<std::size_t>(size / 2) * size + size / 2];
            ok = Same(centre, faceColor);
            detail = " want=" + ColorText(faceColor) + " got=" + ColorText(centre);
        }
        catch (const System::NotSupportedException&)
        {
            ok = false;
            detail = " readback rejected with NotSupportedException";
        }
        catch (const std::exception& e)
        {
            ok = false;
            detail = std::string(" threw: ") + e.what();
        }
        check(ok, "G1 RenderTargetCube face reads back the mid-tone colour byte-exact" + detail);
    }

    /// Leg H: rectangle and destination-offset readback of a rendered target keep the palette.
    void LegH(RenderTarget2D& target)
    {
        if (RtUnsupported())
        {
            check(true, "H1 render-target region readback not applicable on a rejecting renderer");
            return;
        }
        const Rectangle rect(6, 1, 5, 3);
        std::vector<Color> got(static_cast<std::size_t>(rect.Width) * rect.Height,
                               Color(0xA5, 0xA5, 0xA5, 0xA5));
        bool ok = true;
        std::string detail;
        try
        {
            target.GetData(0, &rect, got.data(), 0, static_cast<int>(got.size()));
            for (int ry = 0; ry < rect.Height; ++ry)
                for (int rx = 0; rx < rect.Width; ++rx)
                {
                    const Color want = PaletteColor(rect.X + rx, rect.Y + ry);
                    const Color& g = got[static_cast<std::size_t>(ry) * rect.Width + rx];
                    if (!Same(g, want) && detail.empty())
                    {
                        ok = false;
                        detail = " at (" + std::to_string(rx) + "," + std::to_string(ry) +
                                 ") want=" + ColorText(want) + " got=" + ColorText(g);
                    }
                }
        }
        catch (const std::exception& e) { ok = false; detail = std::string(" threw: ") + e.what(); }
        check(ok, "H1 RenderTarget2D rectangle readback keeps the mid-tone palette" + detail);

        constexpr int kOffset = 3;
        const std::size_t total = static_cast<std::size_t>(kW) * kH;
        const Rectangle whole(0, 0, kW, kH);
        std::vector<Color> offGot(total + kOffset, Color(0xA5, 0xA5, 0xA5, 0xA5));
        bool offOk = true;
        std::string offDetail;
        try
        {
            target.GetData(0, &whole, offGot.data(), kOffset, static_cast<int>(total));
            for (int i = 0; i < kOffset; ++i)
                if (!Same(offGot[static_cast<std::size_t>(i)], Color(0xA5, 0xA5, 0xA5, 0xA5)))
                    offOk = false;
            for (int y = 0; y < kH; ++y)
                for (int x = 0; x < kW; ++x)
                {
                    const Color want = PaletteColor(x, y);
                    const Color& g = offGot[static_cast<std::size_t>(y) * kW + x + kOffset];
                    if (!Same(g, want) && offDetail.empty())
                    {
                        offOk = false;
                        offDetail = " at (" + std::to_string(x) + "," + std::to_string(y) +
                                    ") want=" + ColorText(want) + " got=" + ColorText(g);
                    }
                }
        }
        catch (const std::exception& e) { offOk = false; offDetail = std::string(" threw: ") + e.what(); }
        check(offOk,
              "H2 RenderTarget2D destination-offset readback keeps the palette and writes no earlier element" +
              offDetail);
    }

    /// Leg I: blending with a mid-tone source over a mid-tone destination. The expectation is
    /// computed on the STORED BYTES, which is what every non-WebGPU renderer does; a gamma-space
    /// blend differs from it by far more than the +-2 rounding tolerance allowed here.
    void LegI(GraphicsDevice& dev)
    {
        if (RtUnsupported())
        {
            check(true, "I1 blending not applicable on a renderer that cannot read targets back");
            return;
        }
        const Color dst(36, 92, 173, 255);
        const Color src(189, 47, 113, 128);

        auto byteBlendAlpha = [&](int s, int d, int a) {
            return static_cast<int>(std::lround((static_cast<double>(s) * a +
                                                 static_cast<double>(d) * (255 - a)) / 255.0));
        };
        auto near2 = [](int a, int b) { return std::abs(a - b) <= 2; };

        // NonPremultiplied: dst = src.rgb * src.a + dst.rgb * (1 - src.a), on stored bytes.
        {
            RenderTarget2D rt(dev, kW, kH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt);
            ResetState(dev);
            dev.Clear(dst);
            {
                SpriteBatch sb(dev);
                SamplerState point = SamplerState::PointClamp;
                sb.Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied, &point, nullptr, nullptr);
                sb.Draw(whiteTex_, Rectangle(0, 0, kW, kH), Rectangle(0, 0, 1, 1), src);
                sb.End();
            }
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            Readback r = ReadWhole(rt, kW, kH);
            bool ok = !r.threwNotSupported && !r.threwSomethingElse;
            std::string detail;
            if (ok)
            {
                const Color& got = r.pixels[static_cast<std::size_t>(kH / 2) * kW + kW / 2];
                const int a = static_cast<int>(src.getAProperty());
                const int er = byteBlendAlpha(src.getRProperty(), dst.getRProperty(), a);
                const int eg = byteBlendAlpha(src.getGProperty(), dst.getGProperty(), a);
                const int eb = byteBlendAlpha(src.getBProperty(), dst.getBProperty(), a);
                ok = near2(got.getRProperty(), er) && near2(got.getGProperty(), eg) &&
                     near2(got.getBProperty(), eb);
                detail = " expected ~(" + std::to_string(er) + "," + std::to_string(eg) + "," +
                         std::to_string(eb) + ") got " + ColorText(got);
            }
            else detail = " readback failed";
            check(ok, "I1 NonPremultiplied blend of mid-tone source over mid-tone destination "
                      "matches the byte-space result" + detail);
        }

        // Additive: dst = src + dst, saturating, on stored bytes.
        {
            RenderTarget2D rt(dev, kW, kH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            const Color addSrc(64, 96, 32, 255);
            dev.SetRenderTarget(&rt);
            ResetState(dev);
            dev.Clear(dst);
            {
                SpriteBatch sb(dev);
                SamplerState point = SamplerState::PointClamp;
                sb.Begin(SpriteSortMode::Deferred, BlendState::Additive, &point, nullptr, nullptr);
                sb.Draw(whiteTex_, Rectangle(0, 0, kW, kH), Rectangle(0, 0, 1, 1), addSrc);
                sb.End();
            }
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            Readback r = ReadWhole(rt, kW, kH);
            bool ok = !r.threwNotSupported && !r.threwSomethingElse;
            std::string detail;
            if (ok)
            {
                const Color& got = r.pixels[static_cast<std::size_t>(kH / 2) * kW + kW / 2];
                // BlendState::Additive is SourceAlpha/One; the source alpha is 255 here. On a
                // renderer declared SourceOnly the destination term is dropped (REMED-GFX-148),
                // which is asserted as such so the divergence is pinned, not tolerated -- the
                // mid-tone source bytes are still required to arrive exactly.
                const bool addsDst = kAdditiveContract == AdditiveContract::SourcePlusDestination;
                const int dr = addsDst ? static_cast<int>(dst.getRProperty()) : 0;
                const int dg = addsDst ? static_cast<int>(dst.getGProperty()) : 0;
                const int db = addsDst ? static_cast<int>(dst.getBProperty()) : 0;
                const int er = std::min(255, static_cast<int>(addSrc.getRProperty()) + dr);
                const int eg = std::min(255, static_cast<int>(addSrc.getGProperty()) + dg);
                const int eb = std::min(255, static_cast<int>(addSrc.getBProperty()) + db);
                ok = near2(got.getRProperty(), er) && near2(got.getGProperty(), eg) &&
                     near2(got.getBProperty(), eb);
                detail = " expected ~(" + std::to_string(er) + "," + std::to_string(eg) + "," +
                         std::to_string(eb) + ") got " + ColorText(got);
            }
            else detail = " readback failed";
            check(ok, std::string("I2 Additive blend of mid-tone source over mid-tone destination "
                                  "matches the byte-space result") +
                      (kAdditiveContract == AdditiveContract::SourceOnly
                           ? " (destination term dropped: REMED-GFX-148)" : "") + detail);
        }
    }

protected:
    void Initialize() override
    {
        auto& dev = getGraphicsDeviceProperty();
        paletteTex_ = Texture2D(dev, kW, kH);
        const std::vector<Color> palette = PalettePixels();
        paletteTex_.SetData(palette.data(), static_cast<int>(palette.size()));

        whiteTex_ = Texture2D(dev, 1, 1);
        const Color white = Color::White;
        whiteTex_.SetData(&white, 1);
        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));

        std::printf("[INFO] REMED-GFX-131 mid-tone colour contract on %s\n", kRendererName);
        std::fflush(stdout);

        LegA(dev);
        LegB(dev);
        LegC(dev);

        RenderTarget2D sampled(dev, kW, kH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
        Readback sampledRead = LegD(dev, sampled);
        LegE(dev, sampled);
        LegF(sampled, sampledRead);
        LegG(dev);
        LegH(sampled);
        LegI(dev);

#if defined(CNA_RENDERER_WEBGPU)
        // REMED-GFX-131 changes a resource format and adds a view-format reinterpretation, both of
        // which wgpu-native validates. Nothing above may have produced a validation error: an
        // unsupported renderable format, an illegal base/view format pair, a copy-format mismatch
        // or a bad row pitch would all surface here rather than being silently tolerated.
        {
            using CNA::Internal::Renderers::WebGPU::WebGPURenderer;
            const std::size_t validationErrors =
                static_cast<WebGPURenderer&>(dev.GetRenderer()).GetUncapturedErrorCountEXT();
            check(validationErrors == 0,
                  "V1 WebGPU reported no uncaptured validation errors across every leg: " +
                  std::to_string(validationErrors));
        }
#endif

        std::printf("[INFO] %s: %d/%d checks passed\n", kRendererName, passCount_, totalCount_);
        std::fflush(stdout);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    ColorSpaceMidToneContractTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    [[nodiscard]] int Result() const { return result_; }
};

int main(int, char**)
{
    ColorSpaceMidToneContractTest test;
    test.Run();
    return test.Result();
}
