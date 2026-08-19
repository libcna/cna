// SPDX-License-Identifier: MS-PL
// Pixel-level proof that NANOVG's SpriteBatch blending implements CNA's real BlendState contract
// -- all four RGBA channels, against a CPU reference computed from the SAME factor ordinals
// ApplyBlendState receives, not from a second rendering of the same scene.
//
// The oracle is deliberately built from first principles rather than from "whatever the renderer
// produces", because the previous version of this file encoded a self-consistent but semantically
// WRONG expectation: it drew ONE straight-alpha texture under both AlphaBlend and
// NonPremultiplied and asserted the two produced the same pixel. They must not. CNA's
// BlendState.cpp defines
//
//     AlphaBlend       colorSrc=One         alphaSrc=One         colorDst/alphaDst=InvSrcAlpha
//     NonPremultiplied colorSrc=SourceAlpha alphaSrc=SourceAlpha colorDst/alphaDst=InvSrcAlpha
//
// i.e. AlphaBlend consumes ALREADY-premultiplied source RGB and must not multiply it again --
// exactly the contract modules/graphics/examples/cross_renderer_2d_corpus.cpp's own row 3 states
// by drawing a premultiplied texel under AlphaBlend and its straight twin under NonPremultiplied
// and requiring the same composited colour.
//
// The three layers that must be kept apart to avoid another self-consistent-but-wrong oracle:
//
//   1. stored source texture value  -- the bytes handed to CreateTexture, straight OR
//      premultiplied depending on which convention the BlendState under test names;
//   2. shader output                -- XNA's SpriteBatch pixel shader emits texel * tint,
//      component-wise, with NO alpha premultiplication of its own. NANOVG reproduces that by
//      creating every image with NVG_IMAGE_PREMULTIPLIED (which makes nanovg_gl.h's fragment
//      shader pass the sampled texel through untouched) and pre-dividing the tint's RGB by its
//      own alpha so NanoVG's own glnvg__premulColor puts it back;
//   3. blend-stage source           -- that shader output, multiplied by the blend factor the
//      BlendState names, with RGB and alpha factors applied independently.
//
// Exit code 0 = PASS, 1 = FAIL, 77 = SKIPPED (no GPU/display).

#include "CNA/Internal/Renderers/NanoVg/NanoVgRenderer.hpp"
#include "CNA/Internal/Renderers/NanoVg/NanoVgSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/NanoVg/NanoVgTextureRenderer.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::NanoVg;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Graphics::ImageData;
using CNA::Examples::SdlTestGlContext;
using CNA::Examples::SdlTestRendererArgs;

namespace
{
    int pass = 0, fail = 0;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++pass; else ++fail;
    }

    // Raw XNA Blend ordinals (Microsoft/Xna/Framework/Graphics/Blend.hpp) and BlendFunction
    // ordinals (BlendFunction.hpp). Spelled out rather than cast from the enums so this file
    // exercises exactly the wire format ApplyBlendState is given.
    constexpr int kOne = 0, kZero = 1, kSrcColor = 2, kInvSrcColor = 3, kSrcAlpha = 4,
                  kInvSrcAlpha = 5, kDstColor = 6, kInvDstColor = 7, kDstAlpha = 8,
                  kInvDstAlpha = 9, kBlendFactor = 10, kInvBlendFactor = 11, kSrcAlphaSat = 12;
    constexpr int kAdd = 0, kSubtract = 1, kMax = 3;

    /// One BlendState's six factor/function ordinals, in ApplyBlendState's own argument order.
    struct BlendSpec
    {
        int colorSrc, alphaSrc, colorDst, alphaDst;
        int colorFunc = kAdd, alphaFunc = kAdd;
    };

    // CNA's four built-in presets, transcribed from modules/graphics/src/Xna/BlendState.cpp.
    constexpr BlendSpec kOpaque          {kOne,      kOne,      kZero,        kZero};
    constexpr BlendSpec kAlphaBlend      {kOne,      kOne,      kInvSrcAlpha, kInvSrcAlpha};
    constexpr BlendSpec kNonPremultiplied{kSrcAlpha, kSrcAlpha, kInvSrcAlpha, kInvSrcAlpha};
    constexpr BlendSpec kAdditive        {kSrcAlpha, kSrcAlpha, kOne,         kOne};

    struct Rgba
    {
        float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
    };

    Rgba ToRgba(const Color& c)
    {
        return Rgba{c.getRProperty() / 255.0f, c.getGProperty() / 255.0f,
                    c.getBProperty() / 255.0f, c.getAProperty() / 255.0f};
    }

    /// One blend factor's value for a single colour channel. `channelSrc`/`channelDst` are that
    /// channel's own source/destination values (used only by the *Color factors); the alpha
    /// channel passes its own value for both, which is what makes SourceColor/DestinationColor
    /// degenerate into SourceAlpha/DestinationAlpha there, exactly as GL defines them.
    float FactorValue(int blendOrdinal, float channelSrc, float srcAlpha,
                      float channelDst, float dstAlpha, bool isAlphaChannel)
    {
        switch (blendOrdinal)
        {
        case kOne:          return 1.0f;
        case kZero:         return 0.0f;
        case kSrcColor:     return channelSrc;
        case kInvSrcColor:  return 1.0f - channelSrc;
        case kSrcAlpha:     return srcAlpha;
        case kInvSrcAlpha:  return 1.0f - srcAlpha;
        case kDstColor:     return channelDst;
        case kInvDstColor:  return 1.0f - channelDst;
        case kDstAlpha:     return dstAlpha;
        case kInvDstAlpha:  return 1.0f - dstAlpha;
        // XNA: RGB is multiplied by min(As, 1-Ad); alpha is multiplied by 1.
        case kSrcAlphaSat:  return isAlphaChannel ? 1.0f : std::min(srcAlpha, 1.0f - dstAlpha);
        default:            throw std::runtime_error("FactorValue: unmodelled Blend ordinal");
        }
    }

    /// The reference CNA/XNA blend: result = shaderOut * srcFactor + destination * dstFactor, with
    /// RGB and alpha factors applied independently, clamped and quantised the way an RGBA8 target
    /// is. `shaderOut` is layer 2 above -- the component-wise texel * tint product, NOT the stored
    /// texture value, and NOT premultiplied by anything.
    Color ExpectedBlend(const BlendSpec& spec, const Rgba& shaderOut, const Rgba& destination)
    {
        if (spec.colorFunc != kAdd || spec.alphaFunc != kAdd)
            throw std::runtime_error("ExpectedBlend: only BlendFunction.Add is modelled");

        const auto channel = [&](float s, float d, int srcOrdinal, int dstOrdinal, bool alphaChannel)
        {
            const float sf = FactorValue(srcOrdinal, s, shaderOut.a, d, destination.a, alphaChannel);
            const float df = FactorValue(dstOrdinal, s, shaderOut.a, d, destination.a, alphaChannel);
            const float v = s * sf + d * df;
            return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
        };
        return Color(channel(shaderOut.r, destination.r, spec.colorSrc, spec.colorDst, false),
                     channel(shaderOut.g, destination.g, spec.colorSrc, spec.colorDst, false),
                     channel(shaderOut.b, destination.b, spec.colorSrc, spec.colorDst, false),
                     channel(shaderOut.a, destination.a, spec.alphaSrc, spec.alphaDst, true));
    }

    /// XNA's SpriteBatch pixel shader: the component-wise product of the sampled texel and the
    /// per-sprite tint. No alpha premultiplication happens here in XNA, and none may happen here
    /// in NANOVG either -- that is precisely the bug this file guards against.
    Rgba ShaderOutput(const Color& texel, const Color& tint)
    {
        const Rgba t = ToRgba(texel);
        const Rgba c = ToRgba(tint);
        return Rgba{t.r * c.r, t.g * c.g, t.b * c.b, t.a * c.a};
    }

    ImageData Solid(int w, int h, const Color& c)
    {
        ImageData data;
        data.width = w;
        data.height = h;
        data.pixels.resize(static_cast<std::size_t>(w) * h * 4);
        for (std::size_t i = 0; i < data.pixels.size(); i += 4)
        {
            data.pixels[i + 0] = static_cast<uint8_t>(c.getRProperty());
            data.pixels[i + 1] = static_cast<uint8_t>(c.getGProperty());
            data.pixels[i + 2] = static_cast<uint8_t>(c.getBProperty());
            data.pixels[i + 3] = static_cast<uint8_t>(c.getAProperty());
        }
        return data;
    }

    std::string Describe(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
               std::to_string(static_cast<int>(c.getGProperty())) + "," +
               std::to_string(static_cast<int>(c.getBProperty())) + "," +
               std::to_string(static_cast<int>(c.getAProperty())) + ")";
    }

    /// The narrowest tolerance the arithmetic justifies. Every quantity involved is exactly
    /// representable in float (k/255 inputs, a single multiply-add per channel); the only real
    /// error source is the target's own round-to-nearest at write time plus the pre-divided
    /// tint's one round trip through /a then *a, both strictly below half an 8-bit step. Two
    /// steps therefore leaves headroom for a driver that rounds differently without letting any
    /// of the semantic errors under test (which are tens of steps wide) hide inside it.
    constexpr int kTolerance = 2;

    class BlendHarness
    {
    public:
        BlendHarness(NanoVgRenderer& renderer, bool alphaObservable)
            : renderer_(renderer), alphaObservable_(alphaObservable)
        {
            sprites_ = renderer_.CreateSpriteBatch();
        }

        /// Draws one solid `texel` sprite tinted by `tint` over a `destination`-cleared backbuffer
        /// under `spec`, then compares all four channels against the CPU reference.
        void Expect(const std::string& label, const BlendSpec& spec, const Color& texel,
                    const Color& tint, const Color& destination)
        {
            auto texture = renderer_.CreateTexture(Solid(4, 4, texel));

            renderer_.ApplyBlendState(spec.colorSrc, spec.alphaSrc, spec.colorDst, spec.alphaDst,
                                      spec.colorFunc, spec.alphaFunc, BlendWriteState{});
            const Rgba dst = ToRgba(destination);
            renderer_.Clear(dst.r, dst.g, dst.b, dst.a);
            sprites_->Begin();
            sprites_->Draw(*texture, Rectangle(0, 0, 40, 40), Rectangle(0, 0, 4, 4), tint, 0.0f,
                           Vector2(0, 0), SpriteEffects::None, 0.0f);
            sprites_->End();

            const Color actual = ReadPixel(20, 20);
            const Color expected = ExpectedBlend(spec, ShaderOutput(texel, tint), dst);

            const auto delta = [](int x, int y) { return x > y ? x - y : y - x; };
            bool ok = delta(actual.getRProperty(), expected.getRProperty()) <= kTolerance &&
                      delta(actual.getGProperty(), expected.getGProperty()) <= kTolerance &&
                      delta(actual.getBProperty(), expected.getBProperty()) <= kTolerance;
            std::string alphaNote;
            if (alphaObservable_)
            {
                if (delta(actual.getAProperty(), expected.getAProperty()) > kTolerance)
                    ok = false;
            }
            else
            {
                alphaNote = " [alpha channel not asserted: backbuffer has no alpha storage]";
            }

            Check(ok, label + ": texel=" + Describe(texel) + " tint=" + Describe(tint) +
                          " dst=" + Describe(destination) + " -> expected " + Describe(expected) +
                          ", got " + Describe(actual) + alphaNote);
        }

        /// Confirms a BlendState this renderer genuinely cannot express is refused, and that the
        /// refusal names the property responsible.
        void ExpectRejected(const std::string& label, const BlendSpec& spec,
                            const std::string& expectedFragment)
        {
            std::string message;
            bool threw = false;
            try
            {
                renderer_.ApplyBlendState(spec.colorSrc, spec.alphaSrc, spec.colorDst, spec.alphaDst,
                                          spec.colorFunc, spec.alphaFunc, BlendWriteState{});
            }
            catch (const std::runtime_error& ex)
            {
                threw = true;
                message = ex.what();
            }
            const bool actionable = threw && message.find(expectedFragment) != std::string::npos;
            Check(actionable, label + (threw ? (": rejected with \"" + message + "\"")
                                             : ": NOT rejected"));
        }

        [[nodiscard]] Color ReadPixel(int x, int y) const
        {
            uint8_t rgba[4] = {0, 0, 0, 0};
            renderer_.ReadBackbuffer(x, y, 1, 1, rgba);
            return Color(rgba[0], rgba[1], rgba[2], rgba[3]);
        }

    private:
        NanoVgRenderer& renderer_;
        bool alphaObservable_;
        std::unique_ptr<ISpriteBatchRenderer> sprites_;
    };

    /// A premultiplied texel for the conceptual straight colour (r,g,b) at alpha `a`.
    Color Premultiplied(int r, int g, int b, int a)
    {
        const auto scale = [a](int v) { return static_cast<int>(std::lround(v * a / 255.0)); };
        return Color(scale(r), scale(g), scale(b), a);
    }
}

int main()
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        std::printf("[SKIP] no GPU/display available: %s\n", SDL_GetError());
        return 77;
    }

    try
    {
        SDL_Window* window = SDL_CreateWindow("nanovg-blend-test", 100, 100, SDL_WINDOW_OPENGL);
        if (!window) throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        {
            SdlTestGlContext glContext(window);
            NanoVgRenderer renderer(SdlTestRendererArgs(
                window, &glContext, nullptr, 0, 0, CnaPresentationMode::NativeBackBuffer));

            // Whether destination alpha can be observed at all is a property of the GL visual the
            // platform granted, not of this renderer: probe it once rather than asserting into a
            // channel that physically does not exist. Reported either way so a run that could not
            // check alpha never reads as one that did.
            renderer.Clear(0.0f, 0.0f, 0.0f, 100.0f / 255.0f);
            uint8_t probe[4] = {0, 0, 0, 0};
            renderer.ReadBackbuffer(1, 1, 1, 1, probe);
            const bool alphaObservable = probe[3] < 250;
            std::printf("[INFO] backbuffer alpha storage: %s (clear a=100 read back as %u)\n",
                        alphaObservable ? "present" : "ABSENT", probe[3]);

            BlendHarness harness(renderer, alphaObservable);

            // A destination that is distinct in all four channels, and NOT opaque, so a renderer
            // that gets destination alpha wrong cannot hide behind a saturated 255.
            const Color kDst(36, 90, 160, 200);
            const Color kWhite(255, 255, 255, 255);

            // ---- AlphaBlend: genuinely premultiplied source data --------------------------
            // The decisive family. A renderer that premultiplies the source RGB a second time
            // (nanovg_gl.h's own `if (texType == 1) color = vec4(color.xyz*color.w,color.w)`)
            // produces visibly darker RGB at every intermediate alpha below.
            harness.Expect("AlphaBlend a=0 (premultiplied zero)", kAlphaBlend,
                           Premultiplied(255, 128, 0, 0), kWhite, kDst);
            harness.Expect("AlphaBlend a=255 (opaque premultiplied source replaces destination)",
                           kAlphaBlend, Premultiplied(255, 128, 0, 255), kWhite, kDst);
            harness.Expect("AlphaBlend a=64 (~0.25) must NOT re-multiply source RGB", kAlphaBlend,
                           Premultiplied(255, 128, 0, 64), kWhite, kDst);
            harness.Expect("AlphaBlend a=192 (~0.75) must NOT re-multiply source RGB", kAlphaBlend,
                           Premultiplied(255, 128, 0, 192), kWhite, kDst);
            // Alpha 0 with non-zero RGB is not a valid premultiplied encoding, but XNA's blend is
            // still fully defined for it (srcRGB factor One, dstRGB factor 1-0 = 1), so it is the
            // sharpest possible probe of "is the source colour reaching the blend stage intact".
            harness.Expect("AlphaBlend a=0 with non-zero RGB still contributes its colour",
                           kAlphaBlend, Color(90, 40, 20, 0), kWhite, kDst);

            // ---- NonPremultiplied: straight source data, same conceptual colours ------------
            // RGB must land on the SAME composite as the AlphaBlend rows above (that equivalence
            // is exactly what cross_renderer_2d_corpus.cpp's row 3 asserts across renderers), but
            // destination ALPHA must differ: NonPremultiplied's alphaSrc is SourceAlpha, so it
            // contributes As*As, not As.
            harness.Expect("NonPremultiplied a=0", kNonPremultiplied, Color(255, 128, 0, 0),
                           kWhite, kDst);
            harness.Expect("NonPremultiplied a=255", kNonPremultiplied, Color(255, 128, 0, 255),
                           kWhite, kDst);
            harness.Expect("NonPremultiplied a=64 (~0.25)", kNonPremultiplied,
                           Color(255, 128, 0, 64), kWhite, kDst);
            harness.Expect("NonPremultiplied a=192 (~0.75)", kNonPremultiplied,
                           Color(255, 128, 0, 192), kWhite, kDst);

            // The two conventions, side by side, on the same conceptual colour: RGB equal, alpha
            // deliberately not. This is the single check that fails if the two states are
            // collapsed onto one composite operation.
            {
                const Color straight(255, 128, 0, 128);
                const Color premultiplied = Premultiplied(255, 128, 0, 128);
                renderer.ApplyBlendState(kAlphaBlend.colorSrc, kAlphaBlend.alphaSrc,
                                         kAlphaBlend.colorDst, kAlphaBlend.alphaDst, kAdd, kAdd,
                                         BlendWriteState{});
                auto premulTex = renderer.CreateTexture(Solid(4, 4, premultiplied));
                auto sprites = renderer.CreateSpriteBatch();
                const Rgba dst = ToRgba(kDst);
                renderer.Clear(dst.r, dst.g, dst.b, dst.a);
                sprites->Begin();
                sprites->Draw(*premulTex, Rectangle(0, 0, 40, 40), Rectangle(0, 0, 4, 4), kWhite,
                              0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
                sprites->End();
                const Color premultipliedResult = harness.ReadPixel(20, 20);

                renderer.ApplyBlendState(kNonPremultiplied.colorSrc, kNonPremultiplied.alphaSrc,
                                         kNonPremultiplied.colorDst, kNonPremultiplied.alphaDst,
                                         kAdd, kAdd, BlendWriteState{});
                auto straightTex = renderer.CreateTexture(Solid(4, 4, straight));
                renderer.Clear(dst.r, dst.g, dst.b, dst.a);
                sprites->Begin();
                sprites->Draw(*straightTex, Rectangle(0, 0, 40, 40), Rectangle(0, 0, 4, 4), kWhite,
                              0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
                sprites->End();
                const Color straightResult = harness.ReadPixel(20, 20);

                const auto delta = [](int x, int y) { return x > y ? x - y : y - x; };
                Check(delta(premultipliedResult.getRProperty(), straightResult.getRProperty()) <= kTolerance &&
                      delta(premultipliedResult.getGProperty(), straightResult.getGProperty()) <= kTolerance &&
                      delta(premultipliedResult.getBProperty(), straightResult.getBProperty()) <= kTolerance,
                      "AlphaBlend(premultiplied texel) and NonPremultiplied(straight twin) agree on RGB: " +
                          Describe(premultipliedResult) + " vs " + Describe(straightResult));
                if (alphaObservable)
                {
                    Check(premultipliedResult.getAProperty() > straightResult.getAProperty() + 10,
                          "AlphaBlend and NonPremultiplied differ in destination ALPHA as their "
                          "own alphaSourceBlend factors require (As vs As*As): " +
                              Describe(premultipliedResult) + " vs " + Describe(straightResult));
                }
            }

            // The shared cross-renderer corpus's own row 3, executed verbatim: the exact texels,
            // tint and background modules/graphics/examples/cross_renderer_2d_corpus.cpp draws
            // there, whose stated contract is that both halves "must produce the same composited
            // colour". That corpus is registered for EasyGL and Direct2D only, because its row 4
            // round-trips through a RenderTarget2D and NANOVG has no render-target storage -- so
            // the contract it states is reproduced here rather than the whole file being run.
            {
                const Color kCorpusBackground(20, 20, 20, 255);
                harness.Expect("cross-renderer corpus row 3 (AlphaBlend half)", kAlphaBlend,
                               Color(128, 64, 0, 128), kWhite, kCorpusBackground);
                harness.Expect("cross-renderer corpus row 3 (NonPremultiplied half)",
                               kNonPremultiplied, Color(255, 128, 0, 128), kWhite,
                               kCorpusBackground);
            }

            // ---- Opaque: source replacement, including translucent sources -----------------
            harness.Expect("Opaque with a fully opaque source", kOpaque, Color(255, 0, 0, 255),
                           kWhite, kDst);
            harness.Expect("Opaque with a TRANSLUCENT source writes un-attenuated colour", kOpaque,
                           Color(255, 0, 0, 128), kWhite, kDst);
            harness.Expect("Opaque with a=0 and non-zero RGB writes that RGB and alpha 0", kOpaque,
                           Color(200, 50, 10, 0), kWhite, kDst);

            // ---- Additive: whatever BlendState.Additive actually defines --------------------
            // Not (One, One): CNA's own BlendState.cpp says (SourceAlpha, One) on both the colour
            // and the alpha channel, so the source is attenuated by its own alpha first.
            harness.Expect("Additive with an opaque source", kAdditive, Color(90, 40, 20, 255),
                           kWhite, kDst);
            harness.Expect("Additive with a translucent source", kAdditive, Color(200, 160, 80, 128),
                           kWhite, kDst);
            harness.Expect("Additive saturates rather than wrapping", kAdditive,
                           Color(255, 255, 255, 255), kWhite, kDst);

            // ---- Tint / colour multiplication ----------------------------------------------
            // Guards the classic failure: premultiplied source RGB x an implicit shader
            // premultiply x a premultiplied tint, which darkens by alpha squared.
            const Color kTexelStraight(200, 120, 60, 200);
            const Color kTexelPremul = Premultiplied(200, 120, 60, 200);
            harness.Expect("tint: opaque white leaves the texel alone", kNonPremultiplied,
                           kTexelStraight, Color(255, 255, 255, 255), kDst);
            harness.Expect("tint: translucent white scales alpha only", kNonPremultiplied,
                           kTexelStraight, Color(255, 255, 255, 128), kDst);
            harness.Expect("tint: opaque coloured multiplies RGB only", kNonPremultiplied,
                           kTexelStraight, Color(128, 64, 255, 255), kDst);
            harness.Expect("tint: translucent coloured multiplies RGB and alpha independently",
                           kNonPremultiplied, kTexelStraight, Color(128, 64, 255, 128), kDst);
            harness.Expect("tint: premultiplied source x translucent tint is multiplied ONCE",
                           kAlphaBlend, kTexelPremul, Color(128, 64, 255, 128), kDst);
            harness.Expect("tint: a fully transparent tint over Opaque still writes its RGB",
                           kOpaque, kTexelStraight, Color(255, 255, 255, 0), kDst);

            // ---- Custom, exactly representable BlendStates ----------------------------------
            // Separate RGB and alpha factors: additive colour that deliberately preserves the
            // destination's own alpha. The previous implementation rejected every state whose
            // colour and alpha factors differed, so this is new ground, not a restatement.
            harness.Expect("custom: additive RGB with destination alpha preserved",
                           BlendSpec{kSrcAlpha, kZero, kOne, kOne}, Color(120, 200, 40, 160),
                           kWhite, kDst);
            harness.Expect("custom: modulate (source x destination colour)",
                           BlendSpec{kDstColor, kDstAlpha, kZero, kZero}, Color(200, 160, 96, 220),
                           kWhite, kDst);
            harness.Expect("custom: inverse-source-colour destination factor",
                           BlendSpec{kOne, kOne, kInvSrcColor, kInvSrcAlpha},
                           Color(64, 32, 200, 128), kWhite, kDst);
            harness.Expect("custom: source-alpha-saturate source factor",
                           BlendSpec{kSrcAlphaSat, kOne, kOne, kOne}, Color(180, 90, 30, 200),
                           kWhite, kDst);

            // ---- Deterministic rejection of what genuinely cannot be represented ------------
            harness.ExpectRejected("BlendFunction.Subtract",
                                   BlendSpec{kOne, kOne, kZero, kZero, kSubtract, kSubtract},
                                   "BlendFunction");
            harness.ExpectRejected("BlendFunction.Max on the alpha channel only",
                                   BlendSpec{kOne, kOne, kZero, kZero, kAdd, kMax}, "BlendFunction");
            harness.ExpectRejected("Blend.BlendFactor (constant colour)",
                                   BlendSpec{kBlendFactor, kOne, kZero, kZero}, "BlendFactor");
            harness.ExpectRejected("Blend.InverseBlendFactor (constant colour)",
                                   BlendSpec{kOne, kOne, kInvBlendFactor, kZero}, "BlendFactor");
            harness.ExpectRejected("Blend.SourceAlphaSaturation as a DESTINATION factor",
                                   BlendSpec{kOne, kOne, kSrcAlphaSat, kZero}, "SourceAlphaSaturation");

            // ColorWriteChannels: still genuinely unhonourable. nanovg_gl.h's own
            // glnvg__renderFlush calls glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE) at the top
            // of EVERY flush, before the first draw call of the batch, so a mask set outside
            // NanoVG cannot survive to the draw that would need it.
            {
                BlendWriteState maskGreenBlueOnly;
                maskGreenBlueOnly.colorWriteChannels[0] = 0x6; // G|B; bit0=R,1=G,2=B,3=A
                bool writeMaskThrew = false;
                std::string message;
                try
                {
                    renderer.ApplyBlendState(kOne, kOne, kZero, kZero, kAdd, kAdd, maskGreenBlueOnly);
                }
                catch (const std::runtime_error& ex) { writeMaskThrew = true; message = ex.what(); }
                Check(writeMaskThrew && message.find("ColorWriteChannels") != std::string::npos,
                      "a non-default ColorWriteChannels mask is rejected deterministically");
            }

            // MultiSampleMask: refused rather than ignored. This renderer never creates a
            // multisample-capable GL context, so a coverage mask has no sample to disable and no
            // observable effect -- but that is an argument for silence, not for acceptance.
            {
                BlendWriteState halfCoverage;
                halfCoverage.multiSampleMask = 0x0000FFFFu;
                bool maskThrew = false;
                std::string message;
                try
                {
                    renderer.ApplyBlendState(kOne, kOne, kZero, kZero, kAdd, kAdd, halfCoverage);
                }
                catch (const std::runtime_error& ex) { maskThrew = true; message = ex.what(); }
                Check(maskThrew && message.find("MultiSampleMask") != std::string::npos,
                      "a non-default MultiSampleMask is rejected deterministically");

                BlendWriteState zeroCoverage;
                zeroCoverage.multiSampleMask = 0u;
                bool zeroThrew = false;
                try
                {
                    renderer.ApplyBlendState(kOne, kOne, kZero, kZero, kAdd, kAdd, zeroCoverage);
                }
                catch (const std::runtime_error&) { zeroThrew = true; }
                Check(zeroThrew, "a MultiSampleMask of 0 is rejected too");

                bool defaultThrew = false;
                try
                {
                    renderer.ApplyBlendState(kOne, kOne, kZero, kZero, kAdd, kAdd, BlendWriteState{});
                }
                catch (const std::runtime_error&) { defaultThrew = true; }
                Check(!defaultThrew, "the default MultiSampleMask (0xFFFFFFFF) is accepted");
            }

            // A rejected state must not have disturbed the last accepted one: the very next draw
            // still has to composite correctly.
            harness.Expect("an accepted BlendState still works after a rejected one", kAlphaBlend,
                           Premultiplied(255, 128, 0, 128), kWhite, kDst);
        }
        SDL_DestroyWindow(window);
    }
    catch (const std::exception& ex)
    {
        std::printf("[FAIL] uncaught exception: %s\n", ex.what());
        ++fail;
    }

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    std::printf("=== %d/%d PASS ===\n", pass, pass + fail);
    return fail == 0 ? 0 : 1;
}
