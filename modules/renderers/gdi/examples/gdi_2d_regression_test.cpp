// SPDX-License-Identifier: MS-PL
// Deterministic pixel regression coverage for the Win32 GDI 2D contract.

#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace CNA::Internal::Renderers;

namespace
{
    using Pixel = std::array<std::uint8_t, 4>;

    [[nodiscard]] ImageData MakeImage(int width, int height, std::vector<std::uint8_t> pixels)
    {
        return ImageData{ width, height, std::move(pixels) };
    }

    [[nodiscard]] Pixel ReadPixel(IGraphicsRenderer& renderer, int x, int y)
    {
        Pixel pixel{};
        renderer.ReadBackbuffer(x, y, 1, 1, pixel.data());
        return pixel;
    }

    bool ExpectPixel(const char* label, const Pixel& actual, const Pixel& expected, int tolerance = 0)
    {
        for (std::size_t channel = 0; channel < actual.size(); ++channel)
        {
            const int difference = std::abs(static_cast<int>(actual[channel]) -
                                            static_cast<int>(expected[channel]));
            if (difference > tolerance)
            {
                std::fprintf(stderr,
                             "%s: expected RGBA(%u,%u,%u,%u), got RGBA(%u,%u,%u,%u).\n",
                             label,
                             expected[0], expected[1], expected[2], expected[3],
                             actual[0], actual[1], actual[2], actual[3]);
                return false;
            }
        }
        return true;
    }

    bool Expect(bool value, const char* label)
    {
        if (!value)
            std::fprintf(stderr, "%s\n", label);
        return value;
    }

    void Draw(IGraphicsRenderer& renderer, const ITextureRenderer& texture,
              const Rectangle& destination, const Rectangle& source, const Color& color,
              float rotation = 0.0f, const Vector2& origin = Vector2(0.0f, 0.0f),
              SpriteEffects effects = SpriteEffects::None, int filter = 1,
              int addressU = 1, int addressV = 1, float layerDepth = 0.0f)
    {
        std::unique_ptr<ISpriteBatchRenderer> spriteBatch = renderer.CreateSpriteBatch();
        spriteBatch->Begin();
        spriteBatch->SetSamplerFilter(filter); // Point for stable byte-exact sampling.
        spriteBatch->SetSamplerAddressMode(addressU, addressV);
        spriteBatch->Draw(texture, destination, source, color, rotation, origin, effects, layerDepth);
        spriteBatch->End();
    }

    void SetOpaque(IGraphicsRenderer& renderer)
    {
        renderer.ApplyBlendState(/*One*/ 0, /*One*/ 0, /*Zero*/ 1, /*Zero*/ 1,
                                /*Add*/ 0, /*Add*/ 0, BlendWriteState{});
    }

    void SetBlend(IGraphicsRenderer& renderer, int colorSource, int alphaSource,
                  int colorDestination, int alphaDestination,
                  int colorFunction = /*Add*/ 0, int alphaFunction = /*Add*/ 0)
    {
        renderer.ApplyBlendState(colorSource, alphaSource, colorDestination, alphaDestination,
                                colorFunction, alphaFunction, BlendWriteState{});
    }

    void SetAlphaBlend(IGraphicsRenderer& renderer)
    {
        SetBlend(renderer, /*One*/ 0, /*One*/ 0,
                 /*InverseSourceAlpha*/ 5, /*InverseSourceAlpha*/ 5);
    }

    void SetNonPremultiplied(IGraphicsRenderer& renderer)
    {
        SetBlend(renderer, /*SourceAlpha*/ 4, /*SourceAlpha*/ 4,
                 /*InverseSourceAlpha*/ 5, /*InverseSourceAlpha*/ 5);
    }

    void SetAdditive(IGraphicsRenderer& renderer)
    {
        SetBlend(renderer, /*SourceAlpha*/ 4, /*SourceAlpha*/ 4,
                 /*One*/ 0, /*One*/ 0);
    }

    bool RunRegression(IGraphicsRenderer& renderer, SDL_Window* window)
    {
        bool ok = true;
        const Pixel black{ 0, 0, 0, 255 };
        const Pixel red{ 255, 0, 0, 255 };
        const Pixel green{ 0, 255, 0, 255 };
        const Pixel blue{ 0, 0, 255, 255 };

        const ImageData atlasData = MakeImage(2, 2, {
            255, 0, 0, 255,    0, 255, 0, 255,
            0, 0, 255, 255,    255, 255, 0, 255,
        });
        std::unique_ptr<ITextureRenderer> atlas = renderer.CreateTexture(atlasData);
        const ImageData premultipliedRedData = MakeImage(1, 1, { 128, 0, 0, 128 });
        std::unique_ptr<ITextureRenderer> premultipliedRed = renderer.CreateTexture(premultipliedRedData);
        const ImageData blendSourceData = MakeImage(1, 1, { 255, 100, 0, 255 });
        std::unique_ptr<ITextureRenderer> blendSource = renderer.CreateTexture(blendSourceData);

        // GDI is deliberately a fixed-function 2D renderer. It must reject a custom shader instead
        // of returning a dummy object whose source/uniforms are silently ignored by the CPU path.
        bool customEffectRejected = false;
        try
        {
            (void)renderer.CreateEffectRenderer("void main() {}", "void main() {}");
        }
        catch (const System::NotSupportedException&)
        {
            customEffectRejected = true;
        }
        ok &= Expect(customEffectRejected,
                     "GDI must reject custom ShaderEffect programs during creation.");
        ok &= Expect(renderer.SupportsCapability(CNA::GraphicsCapability::WireFrame),
                     "GDI must report its real CPU SpriteBatch wireframe support.");
        ok &= Expect(renderer.SupportsCapability(CNA::GraphicsCapability::MultiSampleAntiAliasing),
                     "GDI must report its optional real CPU 4x MSAA path.");
        ok &= Expect(renderer.SupportsCapability(CNA::GraphicsCapability::StencilBuffer),
                     "GDI must report its standalone CPU stencil plane.");
        constexpr CNA::GraphicsCapability unsupportedCapabilities[] = {
            CNA::GraphicsCapability::ThreeD,
            CNA::GraphicsCapability::DepthStencilBuffer,
            CNA::GraphicsCapability::MultipleRenderTargets,
            CNA::GraphicsCapability::AnisotropicFiltering,
            CNA::GraphicsCapability::OcclusionQuery,
            CNA::GraphicsCapability::CustomEffects,
            CNA::GraphicsCapability::Texture3D,
        };
        for (const CNA::GraphicsCapability capability : unsupportedCapabilities)
            ok &= Expect(!renderer.SupportsCapability(capability),
                         "GDI must not advertise an unsupported graphics capability.");

        // Upload + source rectangle: top-right texel is green.
        SetOpaque(renderer);
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(renderer, *atlas, Rectangle(1, 1, 1, 1), Rectangle(1, 0, 1, 1), Color::White);
        ok &= ExpectPixel("source rectangle", ReadPixel(renderer, 1, 1), green);
        ok &= ExpectPixel("source rectangle does not spill", ReadPixel(renderer, 0, 0), black);

        // BlendState::AlphaBlend uses premultiplied source colour: it must NOT multiply the
        // already-premultiplied red by alpha a second time.
        SetAlphaBlend(renderer);
        renderer.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        Draw(renderer, *premultipliedRed, Rectangle(2, 2, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        ok &= ExpectPixel("premultiplied AlphaBlend", ReadPixel(renderer, 2, 2),
                          Pixel{ 128, 0, 127, 255 }, 1);

        // The raw red texture reaches the same visual result only through NonPremultiplied,
        // whose source factor is SourceAlpha rather than AlphaBlend's One.
        SetNonPremultiplied(renderer);
        renderer.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        Draw(renderer, *atlas, Rectangle(2, 2, 1, 1), Rectangle(0, 0, 1, 1),
             Color(255, 255, 255, 128));
        ok &= ExpectPixel("straight-alpha NonPremultiplied", ReadPixel(renderer, 2, 2),
                          Pixel{ 128, 0, 127, 191 }, 1);

        // Additive must keep the complete destination factor (One) and saturate, unlike alpha
        // compositing which would discard it at source alpha 1.
        SetAdditive(renderer);
        renderer.Clear(200.0f / 255.0f, 50.0f / 255.0f, 0.0f, 1.0f);
        Draw(renderer, *blendSource, Rectangle(2, 2, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        ok &= ExpectPixel("Additive", ReadPixel(renderer, 2, 2), Pixel{ 255, 150, 0, 255 }, 1);

        // Colour and alpha equations are independent. All factors are One, so the expected
        // values expose the selected function rather than a factor coincidence.
        SetBlend(renderer, /*One*/ 0, /*One*/ 0, /*One*/ 0, /*One*/ 0,
                 /*Subtract*/ 1, /*Add*/ 0);
        renderer.Clear(50.0f / 255.0f, 200.0f / 255.0f, 0.0f, 64.0f / 255.0f);
        const ImageData functionSourceData = MakeImage(1, 1, { 200, 50, 0, 128 });
        std::unique_ptr<ITextureRenderer> functionSource = renderer.CreateTexture(functionSourceData);
        Draw(renderer, *functionSource, Rectangle(2, 2, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        ok &= ExpectPixel("separate blend functions subtract/add", ReadPixel(renderer, 2, 2),
                          Pixel{ 150, 0, 0, 192 }, 1);

        SetBlend(renderer, /*One*/ 0, /*One*/ 0, /*One*/ 0, /*One*/ 0,
                 /*Add*/ 0, /*ReverseSubtract*/ 2);
        renderer.Clear(50.0f / 255.0f, 200.0f / 255.0f, 0.0f, 64.0f / 255.0f);
        Draw(renderer, *functionSource, Rectangle(2, 2, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        ok &= ExpectPixel("separate blend functions add/reverse subtract", ReadPixel(renderer, 2, 2),
                          Pixel{ 250, 250, 0, 0 }, 1);

        // REMED-GFX-231: SourceAlphaSaturation is min(source alpha, 1-destination alpha) for
        // colour and One for alpha. These nontrivial, deliberately different alphas distinguish
        // the destination-alpha term from the former erroneous 1-source-alpha calculation.
        SetBlend(renderer, /*SourceAlphaSaturation*/ 12, /*One*/ 0,
                 /*Zero*/ 1, /*Zero*/ 1);
        renderer.Clear(17.0f / 255.0f, 83.0f / 255.0f, 149.0f / 255.0f,
                      223.0f / 255.0f);
        const ImageData saturationSourceData = MakeImage(1, 1, { 201, 111, 37, 63 });
        std::unique_ptr<ITextureRenderer> saturationSource =
            renderer.CreateTexture(saturationSourceData);
        Draw(renderer, *saturationSource, Rectangle(2, 2, 1, 1),
             Rectangle(0, 0, 1, 1), Color::White);
        ok &= ExpectPixel("SourceAlphaSaturation uses inverse destination alpha",
                          ReadPixel(renderer, 2, 2), Pixel{ 25, 13, 4, 63 }, 1);

        // BlendFactor is dynamic state, independent of ApplyBlendState. A white source exposes
        // the per-channel constant exactly; destination is zero for colour and alpha.
        SetBlend(renderer, /*BlendFactor*/ 10, /*One*/ 0, /*Zero*/ 1, /*Zero*/ 1);
        renderer.SetBlendFactor(0.75f, 0.5f, 0.25f, 1.0f);
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        const ImageData whiteData = MakeImage(1, 1, { 255, 255, 255, 255 });
        std::unique_ptr<ITextureRenderer> white = renderer.CreateTexture(whiteData);
        Draw(renderer, *white, Rectangle(2, 2, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        ok &= ExpectPixel("BlendFactor", ReadPixel(renderer, 2, 2), Pixel{ 191, 127, 63, 255 }, 1);

        renderer.SetBlendFactor(1.0f, 1.0f, 1.0f, 1.0f);
        SetOpaque(renderer);

        // GDI is a 2D-only renderer. Its privately composed CPU core has no depth allocation, and
        // applying a DepthStencilState must never alter SpriteBatch layering. With an active
        // LessEqual test, blue at 0.75 would normally fail behind red at 0.25; GDI instead keeps
        // its declared 2D draw-order contract and shows the later blue draw.
        renderer.ApplyDepthStencilState(
            /*depthEnable*/ true, /*depthWriteEnable*/ true, /*LessEqual*/ 3,
            /*stencilEnable*/ false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(renderer, *atlas, Rectangle(2, 2, 1, 1), Rectangle(0, 0, 1, 1), Color::White,
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, /*Point*/ 1,
             /*Clamp*/ 1, /*Clamp*/ 1, /*layerDepth*/ 0.25f);
        Draw(renderer, *atlas, Rectangle(2, 2, 1, 1), Rectangle(0, 1, 1, 1), Color::White,
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, /*Point*/ 1,
             /*Clamp*/ 1, /*Clamp*/ 1, /*layerDepth*/ 0.75f);
        ok &= ExpectPixel("GDI ignores 2D depth state", ReadPixel(renderer, 2, 2), blue);

        // GDI-026: the GDI path deliberately has no depth buffer, but it owns a real independent
        // 8-bit stencil plane for 2D clipping.  First replace stencil with 1 inside a 2x2 mask,
        // then clear only colour and use Equal(1) to reveal the blue sprite through that mask.
        renderer.ClearStencil(0);
        renderer.ApplyDepthStencilState(
            /*depthEnable*/ false, /*depthWriteEnable*/ false, /*LessEqual*/ 3,
            /*stencilEnable*/ true, /*Always*/ 0, /*Replace*/ 2, /*Keep*/ 0, /*Keep*/ 0,
            /*readMask*/ 0xFF, /*writeMask*/ 0xFF, /*reference*/ 1,
            /*twoSided*/ false, 0, 0, 0, 0);
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(renderer, *atlas, Rectangle(2, 2, 2, 2), Rectangle(0, 0, 1, 1), Color::White);
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f); // must preserve the stencil mask
        renderer.ApplyDepthStencilState(
            /*depthEnable*/ false, /*depthWriteEnable*/ false, /*LessEqual*/ 3,
            /*stencilEnable*/ true, /*Equal*/ 4, /*Keep*/ 0, /*Keep*/ 0, /*Keep*/ 0,
            /*readMask*/ 0xFF, /*writeMask*/ 0xFF, /*reference*/ 1,
            /*twoSided*/ false, 0, 0, 0, 0);
        Draw(renderer, *atlas, Rectangle(1, 1, 4, 4), Rectangle(0, 1, 1, 1), Color::White);
        ok &= ExpectPixel("GDI stencil mask accepts matching pixel", ReadPixel(renderer, 2, 2), blue);
        ok &= ExpectPixel("GDI stencil mask rejects outside pixel", ReadPixel(renderer, 1, 1), black);
        renderer.ClearStencil(0);
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(renderer, *atlas, Rectangle(2, 2, 1, 1), Rectangle(0, 1, 1, 1), Color::White);
        ok &= ExpectPixel("GDI ClearStencil clears the 2D mask", ReadPixel(renderer, 2, 2), black);

        // Exercise every reachable StencilOperation rather than only the Replace/Keep pair the
        // clipping example needs.  The test makes the resulting stencil value visible by clearing
        // only colour, comparing it with Equal, then drawing blue.  GDI intentionally disables
        // depth, so StencilDepthBufferFail has no reachable 2D fragment path (the depth-order
        // assertion above proves that boundary); all pass and stencil-fail operations are real.
        const auto expectStencilPassOperation = [&](const char* label, int initial, int operation,
                                                    int operationReference, int compareReference,
                                                    int writeMask = 0xFF, int readMask = 0xFF) {
            renderer.ClearStencil(initial);
            renderer.ApplyDepthStencilState(
                false, false, /*LessEqual*/ 3, true, /*Always*/ 0, operation,
                /*fail Keep*/ 0, /*depth-fail Keep*/ 0, 0xFF, writeMask,
                operationReference, false, 0, 0, 0, 0);
            Draw(renderer, *atlas, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
            renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
            renderer.ApplyDepthStencilState(
                false, false, /*LessEqual*/ 3, true, /*Equal*/ 4, /*Keep*/ 0,
                /*fail Keep*/ 0, /*depth-fail Keep*/ 0, readMask, 0xFF,
                compareReference, false, 0, 0, 0, 0);
            Draw(renderer, *atlas, Rectangle(0, 0, 1, 1), Rectangle(0, 1, 1, 1), Color::White);
            ok &= ExpectPixel(label, ReadPixel(renderer, 0, 0), blue);
        };
        expectStencilPassOperation("GDI stencil Keep", 0x31, /*Keep*/ 0, 0x80, 0x31);
        expectStencilPassOperation("GDI stencil Zero", 0x31, /*Zero*/ 1, 0x80, 0);
        expectStencilPassOperation("GDI stencil Replace", 0, /*Replace*/ 2, 0x5A, 0x5A);
        expectStencilPassOperation("GDI stencil Increment wraps", 0xFF, /*Increment*/ 3, 0, 0);
        expectStencilPassOperation("GDI stencil Decrement wraps", 0, /*Decrement*/ 4, 0, 0xFF);
        expectStencilPassOperation("GDI stencil IncrementSaturation", 0xFF,
                                   /*IncrementSaturation*/ 5, 0, 0xFF);
        expectStencilPassOperation("GDI stencil DecrementSaturation", 0,
                                   /*DecrementSaturation*/ 6, 0, 0);
        expectStencilPassOperation("GDI stencil Invert", 0x55, /*Invert*/ 7, 0, 0xAA);
        expectStencilPassOperation("GDI stencil write/read mask", 0xF0, /*Replace*/ 2,
                                   0x0A, /*compare low nibble*/ 0x0A, /*write*/ 0x0F,
                                   /*read*/ 0x0F);
        renderer.ClearStencil(0);
        renderer.ApplyDepthStencilState(
            false, false, /*LessEqual*/ 3, true, /*Never*/ 1, /*pass Keep*/ 0,
            /*fail Replace*/ 2, /*depth-fail Keep*/ 0, 0xFF, 0xFF, /*reference*/ 7,
            false, 0, 0, 0, 0);
        Draw(renderer, *atlas, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.ApplyDepthStencilState(
            false, false, /*LessEqual*/ 3, true, /*Equal*/ 4, /*pass Keep*/ 0,
            /*fail Keep*/ 0, /*depth-fail Keep*/ 0, 0xFF, 0xFF, /*reference*/ 7,
            false, 0, 0, 0, 0);
        Draw(renderer, *atlas, Rectangle(0, 0, 1, 1), Rectangle(0, 1, 1, 1), Color::White);
        ok &= ExpectPixel("GDI stencil fail operation", ReadPixel(renderer, 0, 0), blue);
        renderer.ApplyDepthStencilState(
            /*depthEnable*/ false, /*depthWriteEnable*/ false, /*LessEqual*/ 3,
            /*stencilEnable*/ false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);

        // GDI's one advertised GraphicsCapability is CPU SpriteBatch wireframe. The middle of a
        // 5x5 quad is intentionally off the two triangle edges and must remain untouched, while
        // its outer corner proves edges are actually rasterized instead of merely accepting state.
        renderer.ApplyRasterizerState(/*CullMode::None*/ 0, /*FillMode::WireFrame*/ 1,
                                     /*scissorTestEnable*/ false, 0.0f, 0.0f);
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(renderer, *atlas, Rectangle(1, 1, 5, 5), Rectangle(0, 0, 1, 1), Color::White);
        ok &= ExpectPixel("GDI wireframe edge", ReadPixel(renderer, 1, 1), red);
        ok &= ExpectPixel("GDI wireframe interior", ReadPixel(renderer, 2, 4), black);
        renderer.ApplyRasterizerState(/*CullMode::CullCounterClockwiseFace*/ 2,
                                     /*FillMode::Solid*/ 0, /*scissorTestEnable*/ false,
                                     0.0f, 0.0f);

        // Horizontal flip maps the 2-pixel source row in reverse order.
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(renderer, *atlas, Rectangle(0, 0, 2, 1), Rectangle(0, 0, 2, 1), Color::White,
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::FlipHorizontally);
        ok &= ExpectPixel("horizontal flip left", ReadPixel(renderer, 0, 0), green);
        ok &= ExpectPixel("horizontal flip right", ReadPixel(renderer, 1, 0), red);

        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(renderer, *atlas, Rectangle(0, 0, 1, 2), Rectangle(0, 0, 1, 2), Color::White,
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::FlipVertically);
        ok &= ExpectPixel("vertical flip top", ReadPixel(renderer, 0, 0), blue);
        ok &= ExpectPixel("vertical flip bottom", ReadPixel(renderer, 0, 1), red);

        // A 90-degree rotation moves this 2x2 red source left of its unrotated position.
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(renderer, *atlas, Rectangle(4, 4, 2, 2), Rectangle(0, 0, 1, 1), Color::White,
             1.57079632679f, Vector2(0.0f, 0.0f));
        ok &= ExpectPixel("rotation destination", ReadPixel(renderer, 2, 4), red);
        ok &= ExpectPixel("rotation vacates unrotated area", ReadPixel(renderer, 5, 4), black);

        // Point Clamp, Wrap and Mirror must sample their different address domains, not merely
        // store sampler state. Source X=-3,width=8 covers UV [-1.5, 2.5].
        const ImageData rowData = MakeImage(2, 1, {
            255, 0, 0, 255,    0, 255, 0, 255,
        });
        std::unique_ptr<ITextureRenderer> row = renderer.CreateTexture(rowData);
        const Rectangle extendedSource(-3, 0, 8, 1);
        const Rectangle rowDestination(0, 0, 8, 1);

        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(renderer, *row, rowDestination, extendedSource, Color::White,
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, /*Point*/ 1,
             /*Clamp*/ 1, /*Clamp*/ 1);
        ok &= ExpectPixel("point clamp left", ReadPixel(renderer, 0, 0), red);
        ok &= ExpectPixel("point clamp right", ReadPixel(renderer, 7, 0), green);

        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(renderer, *row, rowDestination, extendedSource, Color::White,
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, /*Point*/ 1,
             /*Wrap*/ 0, /*Clamp*/ 1);
        ok &= ExpectPixel("point wrap left", ReadPixel(renderer, 0, 0), green);
        ok &= ExpectPixel("point wrap right", ReadPixel(renderer, 7, 0), red);

        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(renderer, *row, rowDestination, extendedSource, Color::White,
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, /*Point*/ 1,
             /*Mirror*/ 2, /*Clamp*/ 1);
        ok &= ExpectPixel("point mirror left", ReadPixel(renderer, 0, 0), green);
        ok &= ExpectPixel("point mirror third", ReadPixel(renderer, 2, 0), red);

        // Render-target writes are read back from their own CPU surface, then sampled into the
        // default backbuffer through the same SpriteBatch texture path as a regular texture.
        std::unique_ptr<IRenderTargetRenderer> target =
            renderer.CreateRenderTarget2D(2, 2, /*DepthFormat::None*/ 0,
                                         /*preserveContents*/ false, /*mipMap*/ true);
        ok &= Expect(target != nullptr, "GDI must create a 2D render target.");
        if (target != nullptr)
        {
            renderer.SetRenderTarget2D(target.get());
            renderer.Clear(0.0f, 1.0f, 0.0f, 1.0f);
            std::array<std::uint8_t, 4> targetPixel{};
            ok &= Expect(target->GetData(0, 0, 0, 1, 1, targetPixel.data(),
                                         static_cast<int>(targetPixel.size())),
                         "render-target readback must report success.");
            ok &= ExpectPixel("render-target readback", targetPixel, green);

            bool rejectedActiveMip = false;
            try
            {
                std::array<std::uint8_t, 4> ignored{};
                target->GetData(1, 0, 0, 1, 1, ignored.data(),
                                static_cast<int>(ignored.size()));
            }
            catch (const std::exception&)
            {
                rejectedActiveMip = true;
            }
            ok &= Expect(rejectedActiveMip,
                         "generated render-target mips must stay unavailable during an active pass.");

            renderer.SetRenderTarget2D(nullptr);
            std::array<std::uint8_t, 4> uniformMip{};
            ok &= Expect(target->GetData(1, 0, 0, 1, 1, uniformMip.data(),
                                         static_cast<int>(uniformMip.size())),
                         "mipmapped render-target readback must report success after unbind.");
            ok &= ExpectPixel("uniform render-target mip", uniformMip, green);

            renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
            Draw(renderer, *target, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 2, 2), Color::White);
            ok &= ExpectPixel("render-target sampling", ReadPixel(renderer, 1, 1), green);

            // Regeneration occurs at the next render-pass boundary. The 1x1 mip of the 2x2
            // atlas is a clamped 2x2 box average: (127,127,63,255), and a minified sample must
            // use that generated level rather than stale data from the previous green pass.
            renderer.SetRenderTarget2D(target.get());
            renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
            Draw(renderer, *atlas, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 2, 2), Color::White);
            renderer.SetRenderTarget2D(nullptr);
            std::array<std::uint8_t, 4> atlasMip{};
            ok &= Expect(target->GetData(1, 0, 0, 1, 1, atlasMip.data(),
                                         static_cast<int>(atlasMip.size())),
                         "regenerated render-target mip readback must report success.");
            ok &= ExpectPixel("render-target box-filter mip", atlasMip,
                              Pixel{ 127, 127, 63, 255 }, 1);

            renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
            Draw(renderer, *target, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 2, 2), Color::White);
            ok &= ExpectPixel("render-target mip sampling", ReadPixel(renderer, 0, 0),
                              Pixel{ 127, 127, 63, 255 }, 1);
        }

        // REMED-GFX-230: RenderTarget2D must honor a caller's row pitch just like Texture2D. Use
        // an odd width, padding that looks nothing like the pixels, and asymmetric RGBA channels
        // so a tight-row assumption, padding ingestion, row overlap, or channel swap is visible.
        std::unique_ptr<IRenderTargetRenderer> stridedTarget =
            renderer.CreateRenderTarget2D(3, 2, /*DepthFormat::None*/ 0,
                                         /*preserveContents*/ false, /*mipMap*/ false);
        ok &= Expect(stridedTarget != nullptr,
                     "GDI must create an odd-width target for pitched upload coverage.");
        if (stridedTarget != nullptr)
        {
            const std::array<std::uint8_t, 32> paddedUpload{
                3, 21, 39, 57,  75, 93, 111, 129,  147, 165, 183, 201,
                0xDE, 0xAD, 0xBE, 0xEF,
                5, 25, 45, 65,  85, 105, 125, 145,  165, 185, 205, 225,
                0xCA, 0xFE, 0xBA, 0xBE,
            };
            const std::array<std::uint8_t, 24> expectedRows{
                3, 21, 39, 57,  75, 93, 111, 129,  147, 165, 183, 201,
                5, 25, 45, 65,  85, 105, 125, 145,  165, 185, 205, 225,
            };
            stridedTarget->UpdatePixels(paddedUpload.data(), 16);
            std::array<std::uint8_t, 24> readback{};
            ok &= Expect(stridedTarget->GetData(0, 0, 0, 3, 2, readback.data(),
                                                static_cast<int>(readback.size())),
                         "pitched render-target readback must report success.");
            ok &= Expect(readback == expectedRows,
                         "odd-width render-target upload consumes rows without their padding.");

            bool rejectedShortStride = false;
            try
            {
                stridedTarget->UpdatePixels(paddedUpload.data(), 11);
            }
            catch (const System::ArgumentOutOfRangeException&)
            {
                rejectedShortStride = true;
            }
            ok &= Expect(rejectedShortStride,
                         "a positive render-target stride shorter than width*4 must be rejected.");
            readback.fill(0);
            ok &= Expect(stridedTarget->GetData(0, 0, 0, 3, 2, readback.data(),
                                                static_cast<int>(readback.size())) &&
                             readback == expectedRows,
                         "a rejected render-target stride leaves the prior pixels unchanged.");
        }

        // A non-power-of-two target must use the same floor-halved dimensions as Texture2D:
        // 3x5 -> 1x2 -> 1x1. Its odd source extent is resolved by the clamped 2x2 box filter,
        // not by allocating a differently shaped chain or reading outside its pixels.
        std::unique_ptr<IRenderTargetRenderer> oddTarget =
            renderer.CreateRenderTarget2D(3, 5, /*DepthFormat::None*/ 0,
                                         /*preserveContents*/ false, /*mipMap*/ true);
        ok &= Expect(oddTarget != nullptr, "GDI must create a non-power-of-two mipmapped target.");
        if (oddTarget != nullptr)
        {
            std::array<std::uint8_t, 3 * 5 * 4> oddPixels{};
            for (int y = 0; y < 5; ++y)
            {
                for (int x = 0; x < 3; ++x)
                {
                    const std::size_t index = static_cast<std::size_t>(y * 3 + x) * 4u;
                    oddPixels[index + 0] = static_cast<std::uint8_t>(x * 40);
                    oddPixels[index + 1] = static_cast<std::uint8_t>(y * 40);
                    oddPixels[index + 2] = 0;
                    oddPixels[index + 3] = 255;
                }
            }
            oddTarget->UpdatePixels(oddPixels.data(), /*tight stride*/ 3 * 4);

            std::array<std::uint8_t, 2 * 4> oddLevel1{};
            ok &= Expect(oddTarget->GetData(1, 0, 0, 1, 2, oddLevel1.data(),
                                             static_cast<int>(oddLevel1.size())),
                         "3x5 render-target mip level 1 must be 1x2.");
            ok &= ExpectPixel("odd render-target mip level 1 top",
                              Pixel{ oddLevel1[0], oddLevel1[1], oddLevel1[2], oddLevel1[3] },
                              Pixel{ 20, 20, 0, 255 });
            ok &= ExpectPixel("odd render-target mip level 1 bottom",
                              Pixel{ oddLevel1[4], oddLevel1[5], oddLevel1[6], oddLevel1[7] },
                              Pixel{ 20, 100, 0, 255 });

            std::array<std::uint8_t, 4> oddLevel2{};
            ok &= Expect(oddTarget->GetData(2, 0, 0, 1, 1, oddLevel2.data(),
                                             static_cast<int>(oddLevel2.size())),
                         "3x5 render-target mip level 2 must be 1x1.");
            ok &= ExpectPixel("odd render-target final mip", oddLevel2, Pixel{ 20, 60, 0, 255 });

            bool rejectedOutsideLevel1 = false;
            try
            {
                std::array<std::uint8_t, 4> ignored{};
                oddTarget->GetData(1, 1, 0, 1, 1, ignored.data(),
                                   static_cast<int>(ignored.size()));
            }
            catch (const std::exception&)
            {
                rejectedOutsideLevel1 = true;
            }
            ok &= Expect(rejectedOutsideLevel1,
                         "3x5 render-target mip level 1 must reject x=1 outside its 1-pixel width.");
        }

        // Resizing changes the CPU backbuffer itself, not only the final window blit.
        renderer.SetVirtualResolution(16, 8);
        int viewportWidth = 0;
        int viewportHeight = 0;
        renderer.GetViewportSize(viewportWidth, viewportHeight);
        ok &= Expect(viewportWidth == 16 && viewportHeight == 8,
                     "SetVirtualResolution must resize the GDI CPU backbuffer.");
        renderer.Clear(1.0f, 0.0f, 0.0f, 1.0f);
        ok &= ExpectPixel("resized backbuffer", ReadPixel(renderer, 15, 7), red);

        // Letterbox transforms use the same presentation geometry as Present().
        ok &= Expect(SDL_SetWindowSize(window, 64, 64), "SDL_SetWindowSize failed.");
        ok &= Expect(SDL_SyncWindow(window), "SDL_SyncWindow failed.");
        renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::Letterbox));
        float windowX = 0.0f;
        float windowY = 0.0f;
        float logicalX = 0.0f;
        float logicalY = 0.0f;
        ok &= Expect(renderer.TransformLogicalToWindow(8.0f, 4.0f, windowX, windowY) &&
                         std::fabs(windowX - 32.0f) < 0.01f && std::fabs(windowY - 32.0f) < 0.01f,
                     "logical-to-window letterbox transform is wrong.");
        ok &= Expect(renderer.TransformWindowToLogical(32.0f, 32.0f, logicalX, logicalY) &&
                         std::fabs(logicalX - 8.0f) < 0.01f && std::fabs(logicalY - 4.0f) < 0.01f,
                     "window-to-logical letterbox transform is wrong.");
        ok &= Expect(!renderer.TransformWindowToLogical(1.0f, 1.0f, logicalX, logicalY),
                     "letterbox bar must not map to a logical coordinate.");

        bool acceptedEveryPresentationMode = true;
        for (int mode = static_cast<int>(CnaPresentationMode::Letterbox);
             mode <= static_cast<int>(CnaPresentationMode::FixedHeightDynamicWidth); ++mode)
        {
            try
            {
                renderer.SetPresentationMode(mode);
            }
            catch (const std::exception&)
            {
                acceptedEveryPresentationMode = false;
            }
        }
        ok &= Expect(acceptedEveryPresentationMode,
                     "GDI accepts every defined presentation-mode ordinal.");

        // Restore a geometry with observable bars, then prove both invalid boundaries fail before
        // changing it. A blind enum cast used to accept these impossible values and leave later
        // presentation/coordinate switches with undefined policy.
        renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::Letterbox));
        bool rejectedNegativeMode = false;
        bool rejectedPastEndMode = false;
        try
        {
            renderer.SetPresentationMode(-1);
        }
        catch (const System::ArgumentOutOfRangeException&)
        {
            rejectedNegativeMode = true;
        }
        try
        {
            renderer.SetPresentationMode(
                static_cast<int>(CnaPresentationMode::FixedHeightDynamicWidth) + 1);
        }
        catch (const System::ArgumentOutOfRangeException&)
        {
            rejectedPastEndMode = true;
        }
        ok &= Expect(rejectedNegativeMode && rejectedPastEndMode,
                     "GDI rejects presentation-mode ordinals outside [0,4].");
        ok &= Expect(!renderer.TransformWindowToLogical(1.0f, 1.0f, logicalX, logicalY),
                     "rejected presentation modes leave the previous Letterbox state active.");

        renderer.Present();

        // When CNA_GDI_DIRTY_PRESENTATION=1 is set, this is an eligible 1:1 UI update: the
        // initial clear/present must be full, then only the new small sprite can be sent to GDI.
        // The same test deliberately remains valid with the opt-in disabled, where both presents
        // use the conservative full-frame path.
        ok &= Expect(SDL_SetWindowSize(window, 16, 8), "SDL_SetWindowSize for dirty present failed.");
        ok &= Expect(SDL_SyncWindow(window), "SDL_SyncWindow for dirty present failed.");
        renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::Stretch));
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.Present();
        Draw(renderer, *atlas, Rectangle(7, 3, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        renderer.Present();
        ok &= ExpectPixel("dirty presentation updated sprite", ReadPixel(renderer, 7, 3), red);

        // GDI-028: Anisotropic deliberately uses the CPU sampler's documented Linear path. It
        // must be stable and visibly identical to Linear rather than claiming a costly, partial
        // anisotropic approximation. The atlas's non-uniform texels make an accidental Point path
        // immediately detectable.
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(renderer, *atlas, Rectangle(0, 0, 3, 3), Rectangle(0, 0, 2, 2), Color::White,
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, /*Linear*/ 0);
        const Pixel linearFilteredPixel = ReadPixel(renderer, 1, 1);
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(renderer, *atlas, Rectangle(0, 0, 3, 3), Rectangle(0, 0, 2, 2), Color::White,
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, /*Anisotropic*/ 2);
        ok &= ExpectPixel("GDI anisotropic maps to Linear", ReadPixel(renderer, 1, 1),
                          linearFilteredPixel);

        // GDI-025: only an explicit request for four samples opts into CPU MSAA.  A rotated
        // opaque sprite leaves a predictable fractional edge coverage after the sample resolve;
        // the 2x2 grid has one covered sample at this chosen edge pixel, so red resolves to 63.
        ok &= Expect(renderer.ApplyMultiSampleCount(2) == 0 && renderer.GetMultiSampleCount() == 0,
                     "GDI must honestly reject unsupported CPU MSAA sample counts.");
        ok &= Expect(renderer.ApplyMultiSampleCount(4) == 4 && renderer.GetMultiSampleCount() == 4,
                     "GDI must apply the requested 4x CPU MSAA count.");
        std::unique_ptr<IRenderTargetRenderer> msaaTarget =
            renderer.CreateRenderTarget2D(2, 2, /*DepthFormat::None*/ 0,
                                         /*preserveContents*/ false, /*mipMap*/ false,
                                         /*requested MSAA*/ 4);
        ok &= Expect(msaaTarget != nullptr && msaaTarget->GetMultiSampleCount() == 0,
                     "GDI render targets must honestly remain single-sampled.");
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(renderer, *atlas, Rectangle(6, 2, 4, 4), Rectangle(0, 0, 1, 1), Color::White,
             0.78539816339f, Vector2(0.5f, 0.5f));
        ok &= ExpectPixel("GDI 4x MSAA resolves fractional rotated edge", ReadPixel(renderer, 3, 1),
                          Pixel{ 63, 0, 0, 255 }, 1);
        ok &= Expect(renderer.ApplyMultiSampleCount(0) == 0 && renderer.GetMultiSampleCount() == 0,
                     "GDI must release optional CPU MSAA when zero samples are requested.");
        return ok;
    }
} // namespace

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("CNA GDI 2D regression", 32, 32, SDL_WINDOW_HIDDEN);
    if (window == nullptr)
    {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    int result = 0;
    try
    {
        GraphicsRendererCreateArgs args;
        args.surface.windowId = SDL_GetWindowID(window);
        args.virtualWidth = 32;
        args.virtualHeight = 32;
        args.presentationMode = CnaPresentationMode::Stretch;
        std::unique_ptr<IGraphicsRenderer> renderer = CreateGraphicsRenderer(args);

        if (CNA::getCurrentGraphicsRendererType() != CNA::GraphicsRendererType::Gdi)
        {
            std::fprintf(stderr, "GDI regression target selected a non-GDI renderer.\n");
            result = 1;
        }
        else if (!RunRegression(*renderer, window))
        {
            result = 1;
        }
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "GDI 2D regression failed: %s\n", error.what());
        result = 1;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
