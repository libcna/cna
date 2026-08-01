// SPDX-License-Identifier: MS-PL
// Deterministic pixel regression coverage for the Win32 GDI 2D contract.

#include "CNA/GraphicsBackendType.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace CNA::Internal::Backends;

namespace
{
    using Pixel = std::array<std::uint8_t, 4>;

    [[nodiscard]] ImageData MakeImage(int width, int height, std::vector<std::uint8_t> pixels)
    {
        return ImageData{ width, height, std::move(pixels) };
    }

    [[nodiscard]] Pixel ReadPixel(IGraphicsBackend& backend, int x, int y)
    {
        Pixel pixel{};
        backend.ReadBackbuffer(x, y, 1, 1, pixel.data());
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

    void Draw(IGraphicsBackend& backend, const ITextureBackend& texture,
              const Rectangle& destination, const Rectangle& source, const Color& color,
              float rotation = 0.0f, const Vector2& origin = Vector2(0.0f, 0.0f),
              SpriteEffects effects = SpriteEffects::None, int filter = 1,
              int addressU = 1, int addressV = 1, float layerDepth = 0.0f)
    {
        std::unique_ptr<ISpriteBatchBackend> spriteBatch = backend.CreateSpriteBatch();
        spriteBatch->Begin();
        spriteBatch->SetSamplerFilter(filter); // Point for stable byte-exact sampling.
        spriteBatch->SetSamplerAddressMode(addressU, addressV);
        spriteBatch->Draw(texture, destination, source, color, rotation, origin, effects, layerDepth);
        spriteBatch->End();
    }

    void SetOpaque(IGraphicsBackend& backend)
    {
        backend.ApplyBlendState(/*One*/ 0, /*One*/ 0, /*Zero*/ 1, /*Zero*/ 1,
                                /*Add*/ 0, /*Add*/ 0, BlendWriteState{});
    }

    void SetBlend(IGraphicsBackend& backend, int colorSource, int alphaSource,
                  int colorDestination, int alphaDestination,
                  int colorFunction = /*Add*/ 0, int alphaFunction = /*Add*/ 0)
    {
        backend.ApplyBlendState(colorSource, alphaSource, colorDestination, alphaDestination,
                                colorFunction, alphaFunction, BlendWriteState{});
    }

    void SetAlphaBlend(IGraphicsBackend& backend)
    {
        SetBlend(backend, /*One*/ 0, /*One*/ 0,
                 /*InverseSourceAlpha*/ 5, /*InverseSourceAlpha*/ 5);
    }

    void SetNonPremultiplied(IGraphicsBackend& backend)
    {
        SetBlend(backend, /*SourceAlpha*/ 4, /*SourceAlpha*/ 4,
                 /*InverseSourceAlpha*/ 5, /*InverseSourceAlpha*/ 5);
    }

    void SetAdditive(IGraphicsBackend& backend)
    {
        SetBlend(backend, /*SourceAlpha*/ 4, /*SourceAlpha*/ 4,
                 /*One*/ 0, /*One*/ 0);
    }

    bool RunRegression(IGraphicsBackend& backend, SDL_Window* window)
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
        std::unique_ptr<ITextureBackend> atlas = backend.CreateTexture(atlasData);
        const ImageData premultipliedRedData = MakeImage(1, 1, { 128, 0, 0, 128 });
        std::unique_ptr<ITextureBackend> premultipliedRed = backend.CreateTexture(premultipliedRedData);
        const ImageData blendSourceData = MakeImage(1, 1, { 255, 100, 0, 255 });
        std::unique_ptr<ITextureBackend> blendSource = backend.CreateTexture(blendSourceData);

        // GDI is deliberately a fixed-function 2D backend. It must reject a custom shader instead
        // of returning a dummy object whose source/uniforms are silently ignored by the CPU path.
        std::unique_ptr<IEffectBackend> customEffect =
            backend.CreateEffectBackend("void main() {}", "void main() {}");
        ok &= Expect(customEffect == nullptr,
                     "GDI must explicitly reject custom ShaderEffect programs.");
        ok &= Expect(backend.SupportsCapability(CNA::GraphicsCapability::WireFrame),
                     "GDI must report its real CPU SpriteBatch wireframe support.");
        ok &= Expect(backend.SupportsCapability(CNA::GraphicsCapability::MultiSampleAntiAliasing),
                     "GDI must report its optional real CPU 4x MSAA path.");
        ok &= Expect(backend.SupportsCapability(CNA::GraphicsCapability::StencilBuffer),
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
            ok &= Expect(!backend.SupportsCapability(capability),
                         "GDI must not advertise an unsupported graphics capability.");

        // Upload + source rectangle: top-right texel is green.
        SetOpaque(backend);
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(backend, *atlas, Rectangle(1, 1, 1, 1), Rectangle(1, 0, 1, 1), Color::White);
        ok &= ExpectPixel("source rectangle", ReadPixel(backend, 1, 1), green);
        ok &= ExpectPixel("source rectangle does not spill", ReadPixel(backend, 0, 0), black);

        // BlendState::AlphaBlend uses premultiplied source colour: it must NOT multiply the
        // already-premultiplied red by alpha a second time.
        SetAlphaBlend(backend);
        backend.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        Draw(backend, *premultipliedRed, Rectangle(2, 2, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        ok &= ExpectPixel("premultiplied AlphaBlend", ReadPixel(backend, 2, 2),
                          Pixel{ 128, 0, 127, 255 }, 1);

        // The raw red texture reaches the same visual result only through NonPremultiplied,
        // whose source factor is SourceAlpha rather than AlphaBlend's One.
        SetNonPremultiplied(backend);
        backend.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        Draw(backend, *atlas, Rectangle(2, 2, 1, 1), Rectangle(0, 0, 1, 1),
             Color(255, 255, 255, 128));
        ok &= ExpectPixel("straight-alpha NonPremultiplied", ReadPixel(backend, 2, 2),
                          Pixel{ 128, 0, 127, 191 }, 1);

        // Additive must keep the complete destination factor (One) and saturate, unlike alpha
        // compositing which would discard it at source alpha 1.
        SetAdditive(backend);
        backend.Clear(200.0f / 255.0f, 50.0f / 255.0f, 0.0f, 1.0f);
        Draw(backend, *blendSource, Rectangle(2, 2, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        ok &= ExpectPixel("Additive", ReadPixel(backend, 2, 2), Pixel{ 255, 150, 0, 255 }, 1);

        // Colour and alpha equations are independent. All factors are One, so the expected
        // values expose the selected function rather than a factor coincidence.
        SetBlend(backend, /*One*/ 0, /*One*/ 0, /*One*/ 0, /*One*/ 0,
                 /*Subtract*/ 1, /*Add*/ 0);
        backend.Clear(50.0f / 255.0f, 200.0f / 255.0f, 0.0f, 64.0f / 255.0f);
        const ImageData functionSourceData = MakeImage(1, 1, { 200, 50, 0, 128 });
        std::unique_ptr<ITextureBackend> functionSource = backend.CreateTexture(functionSourceData);
        Draw(backend, *functionSource, Rectangle(2, 2, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        ok &= ExpectPixel("separate blend functions subtract/add", ReadPixel(backend, 2, 2),
                          Pixel{ 150, 0, 0, 192 }, 1);

        SetBlend(backend, /*One*/ 0, /*One*/ 0, /*One*/ 0, /*One*/ 0,
                 /*Add*/ 0, /*ReverseSubtract*/ 2);
        backend.Clear(50.0f / 255.0f, 200.0f / 255.0f, 0.0f, 64.0f / 255.0f);
        Draw(backend, *functionSource, Rectangle(2, 2, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        ok &= ExpectPixel("separate blend functions add/reverse subtract", ReadPixel(backend, 2, 2),
                          Pixel{ 250, 250, 0, 0 }, 1);

        // BlendFactor is dynamic state, independent of ApplyBlendState. A white source exposes
        // the per-channel constant exactly; destination is zero for colour and alpha.
        SetBlend(backend, /*BlendFactor*/ 10, /*One*/ 0, /*Zero*/ 1, /*Zero*/ 1);
        backend.SetBlendFactor(0.75f, 0.5f, 0.25f, 1.0f);
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        const ImageData whiteData = MakeImage(1, 1, { 255, 255, 255, 255 });
        std::unique_ptr<ITextureBackend> white = backend.CreateTexture(whiteData);
        Draw(backend, *white, Rectangle(2, 2, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        ok &= ExpectPixel("BlendFactor", ReadPixel(backend, 2, 2), Pixel{ 191, 127, 63, 255 }, 1);

        backend.SetBlendFactor(1.0f, 1.0f, 1.0f, 1.0f);
        SetOpaque(backend);

        // GDI is a 2D-only backend. The Software base owns a 3D depth buffer, but applying a
        // DepthStencilState must never let it accidentally alter SpriteBatch layering. With an
        // active LessEqual test, blue at 0.75 would normally fail behind red at 0.25; GDI instead
        // keeps its declared 2D draw-order contract and shows the later blue draw.
        backend.ApplyDepthStencilState(
            /*depthEnable*/ true, /*depthWriteEnable*/ true, /*LessEqual*/ 3,
            /*stencilEnable*/ false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(backend, *atlas, Rectangle(2, 2, 1, 1), Rectangle(0, 0, 1, 1), Color::White,
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, /*Point*/ 1,
             /*Clamp*/ 1, /*Clamp*/ 1, /*layerDepth*/ 0.25f);
        Draw(backend, *atlas, Rectangle(2, 2, 1, 1), Rectangle(0, 1, 1, 1), Color::White,
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, /*Point*/ 1,
             /*Clamp*/ 1, /*Clamp*/ 1, /*layerDepth*/ 0.75f);
        ok &= ExpectPixel("GDI ignores 2D depth state", ReadPixel(backend, 2, 2), blue);

        // GDI-026: the GDI path deliberately has no depth buffer, but it owns a real independent
        // 8-bit stencil plane for 2D clipping.  First replace stencil with 1 inside a 2x2 mask,
        // then clear only colour and use Equal(1) to reveal the blue sprite through that mask.
        backend.ClearStencil(0);
        backend.ApplyDepthStencilState(
            /*depthEnable*/ false, /*depthWriteEnable*/ false, /*LessEqual*/ 3,
            /*stencilEnable*/ true, /*Always*/ 0, /*Replace*/ 2, /*Keep*/ 0, /*Keep*/ 0,
            /*readMask*/ 0xFF, /*writeMask*/ 0xFF, /*reference*/ 1,
            /*twoSided*/ false, 0, 0, 0, 0);
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(backend, *atlas, Rectangle(2, 2, 2, 2), Rectangle(0, 0, 1, 1), Color::White);
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f); // must preserve the stencil mask
        backend.ApplyDepthStencilState(
            /*depthEnable*/ false, /*depthWriteEnable*/ false, /*LessEqual*/ 3,
            /*stencilEnable*/ true, /*Equal*/ 4, /*Keep*/ 0, /*Keep*/ 0, /*Keep*/ 0,
            /*readMask*/ 0xFF, /*writeMask*/ 0xFF, /*reference*/ 1,
            /*twoSided*/ false, 0, 0, 0, 0);
        Draw(backend, *atlas, Rectangle(1, 1, 4, 4), Rectangle(0, 1, 1, 1), Color::White);
        ok &= ExpectPixel("GDI stencil mask accepts matching pixel", ReadPixel(backend, 2, 2), blue);
        ok &= ExpectPixel("GDI stencil mask rejects outside pixel", ReadPixel(backend, 1, 1), black);
        backend.ClearStencil(0);
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(backend, *atlas, Rectangle(2, 2, 1, 1), Rectangle(0, 1, 1, 1), Color::White);
        ok &= ExpectPixel("GDI ClearStencil clears the 2D mask", ReadPixel(backend, 2, 2), black);

        // Exercise every reachable StencilOperation rather than only the Replace/Keep pair the
        // clipping example needs.  The test makes the resulting stencil value visible by clearing
        // only colour, comparing it with Equal, then drawing blue.  GDI intentionally disables
        // depth, so StencilDepthBufferFail has no reachable 2D fragment path (the depth-order
        // assertion above proves that boundary); all pass and stencil-fail operations are real.
        const auto expectStencilPassOperation = [&](const char* label, int initial, int operation,
                                                    int operationReference, int compareReference,
                                                    int writeMask = 0xFF, int readMask = 0xFF) {
            backend.ClearStencil(initial);
            backend.ApplyDepthStencilState(
                false, false, /*LessEqual*/ 3, true, /*Always*/ 0, operation,
                /*fail Keep*/ 0, /*depth-fail Keep*/ 0, 0xFF, writeMask,
                operationReference, false, 0, 0, 0, 0);
            Draw(backend, *atlas, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
            backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
            backend.ApplyDepthStencilState(
                false, false, /*LessEqual*/ 3, true, /*Equal*/ 4, /*Keep*/ 0,
                /*fail Keep*/ 0, /*depth-fail Keep*/ 0, readMask, 0xFF,
                compareReference, false, 0, 0, 0, 0);
            Draw(backend, *atlas, Rectangle(0, 0, 1, 1), Rectangle(0, 1, 1, 1), Color::White);
            ok &= ExpectPixel(label, ReadPixel(backend, 0, 0), blue);
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
        backend.ClearStencil(0);
        backend.ApplyDepthStencilState(
            false, false, /*LessEqual*/ 3, true, /*Never*/ 1, /*pass Keep*/ 0,
            /*fail Replace*/ 2, /*depth-fail Keep*/ 0, 0xFF, 0xFF, /*reference*/ 7,
            false, 0, 0, 0, 0);
        Draw(backend, *atlas, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        backend.ApplyDepthStencilState(
            false, false, /*LessEqual*/ 3, true, /*Equal*/ 4, /*pass Keep*/ 0,
            /*fail Keep*/ 0, /*depth-fail Keep*/ 0, 0xFF, 0xFF, /*reference*/ 7,
            false, 0, 0, 0, 0);
        Draw(backend, *atlas, Rectangle(0, 0, 1, 1), Rectangle(0, 1, 1, 1), Color::White);
        ok &= ExpectPixel("GDI stencil fail operation", ReadPixel(backend, 0, 0), blue);
        backend.ApplyDepthStencilState(
            /*depthEnable*/ false, /*depthWriteEnable*/ false, /*LessEqual*/ 3,
            /*stencilEnable*/ false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);

        // GDI's one advertised GraphicsCapability is CPU SpriteBatch wireframe. The middle of a
        // 5x5 quad is intentionally off the two triangle edges and must remain untouched, while
        // its outer corner proves edges are actually rasterized instead of merely accepting state.
        backend.ApplyRasterizerState(/*CullMode::None*/ 0, /*FillMode::WireFrame*/ 1,
                                     /*scissorTestEnable*/ false, 0.0f, 0.0f);
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(backend, *atlas, Rectangle(1, 1, 5, 5), Rectangle(0, 0, 1, 1), Color::White);
        ok &= ExpectPixel("GDI wireframe edge", ReadPixel(backend, 1, 1), red);
        ok &= ExpectPixel("GDI wireframe interior", ReadPixel(backend, 2, 4), black);
        backend.ApplyRasterizerState(/*CullMode::CullCounterClockwiseFace*/ 2,
                                     /*FillMode::Solid*/ 0, /*scissorTestEnable*/ false,
                                     0.0f, 0.0f);

        // Horizontal flip maps the 2-pixel source row in reverse order.
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(backend, *atlas, Rectangle(0, 0, 2, 1), Rectangle(0, 0, 2, 1), Color::White,
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::FlipHorizontally);
        ok &= ExpectPixel("horizontal flip left", ReadPixel(backend, 0, 0), green);
        ok &= ExpectPixel("horizontal flip right", ReadPixel(backend, 1, 0), red);

        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(backend, *atlas, Rectangle(0, 0, 1, 2), Rectangle(0, 0, 1, 2), Color::White,
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::FlipVertically);
        ok &= ExpectPixel("vertical flip top", ReadPixel(backend, 0, 0), blue);
        ok &= ExpectPixel("vertical flip bottom", ReadPixel(backend, 0, 1), red);

        // A 90-degree rotation moves this 2x2 red source left of its unrotated position.
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(backend, *atlas, Rectangle(4, 4, 2, 2), Rectangle(0, 0, 1, 1), Color::White,
             1.57079632679f, Vector2(0.0f, 0.0f));
        ok &= ExpectPixel("rotation destination", ReadPixel(backend, 2, 4), red);
        ok &= ExpectPixel("rotation vacates unrotated area", ReadPixel(backend, 5, 4), black);

        // Point Clamp, Wrap and Mirror must sample their different address domains, not merely
        // store sampler state. Source X=-3,width=8 covers UV [-1.5, 2.5].
        const ImageData rowData = MakeImage(2, 1, {
            255, 0, 0, 255,    0, 255, 0, 255,
        });
        std::unique_ptr<ITextureBackend> row = backend.CreateTexture(rowData);
        const Rectangle extendedSource(-3, 0, 8, 1);
        const Rectangle rowDestination(0, 0, 8, 1);

        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(backend, *row, rowDestination, extendedSource, Color::White,
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, /*Point*/ 1,
             /*Clamp*/ 1, /*Clamp*/ 1);
        ok &= ExpectPixel("point clamp left", ReadPixel(backend, 0, 0), red);
        ok &= ExpectPixel("point clamp right", ReadPixel(backend, 7, 0), green);

        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(backend, *row, rowDestination, extendedSource, Color::White,
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, /*Point*/ 1,
             /*Wrap*/ 0, /*Clamp*/ 1);
        ok &= ExpectPixel("point wrap left", ReadPixel(backend, 0, 0), green);
        ok &= ExpectPixel("point wrap right", ReadPixel(backend, 7, 0), red);

        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(backend, *row, rowDestination, extendedSource, Color::White,
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, /*Point*/ 1,
             /*Mirror*/ 2, /*Clamp*/ 1);
        ok &= ExpectPixel("point mirror left", ReadPixel(backend, 0, 0), green);
        ok &= ExpectPixel("point mirror third", ReadPixel(backend, 2, 0), red);

        // Render-target writes are read back from their own CPU surface, then sampled into the
        // default backbuffer through the same SpriteBatch texture path as a regular texture.
        std::unique_ptr<IRenderTargetBackend> target =
            backend.CreateRenderTarget2D(2, 2, /*DepthFormat::None*/ 0,
                                         /*preserveContents*/ false, /*mipMap*/ true);
        ok &= Expect(target != nullptr, "GDI must create a 2D render target.");
        if (target != nullptr)
        {
            backend.SetRenderTarget2D(target.get());
            backend.Clear(0.0f, 1.0f, 0.0f, 1.0f);
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

            backend.SetRenderTarget2D(nullptr);
            std::array<std::uint8_t, 4> uniformMip{};
            ok &= Expect(target->GetData(1, 0, 0, 1, 1, uniformMip.data(),
                                         static_cast<int>(uniformMip.size())),
                         "mipmapped render-target readback must report success after unbind.");
            ok &= ExpectPixel("uniform render-target mip", uniformMip, green);

            backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
            Draw(backend, *target, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 2, 2), Color::White);
            ok &= ExpectPixel("render-target sampling", ReadPixel(backend, 1, 1), green);

            // Regeneration occurs at the next render-pass boundary. The 1x1 mip of the 2x2
            // atlas is a clamped 2x2 box average: (127,127,63,255), and a minified sample must
            // use that generated level rather than stale data from the previous green pass.
            backend.SetRenderTarget2D(target.get());
            backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
            Draw(backend, *atlas, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 2, 2), Color::White);
            backend.SetRenderTarget2D(nullptr);
            std::array<std::uint8_t, 4> atlasMip{};
            ok &= Expect(target->GetData(1, 0, 0, 1, 1, atlasMip.data(),
                                         static_cast<int>(atlasMip.size())),
                         "regenerated render-target mip readback must report success.");
            ok &= ExpectPixel("render-target box-filter mip", atlasMip,
                              Pixel{ 127, 127, 63, 255 }, 1);

            backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
            Draw(backend, *target, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 2, 2), Color::White);
            ok &= ExpectPixel("render-target mip sampling", ReadPixel(backend, 0, 0),
                              Pixel{ 127, 127, 63, 255 }, 1);
        }

        // A non-power-of-two target must use the same floor-halved dimensions as Texture2D:
        // 3x5 -> 1x2 -> 1x1. Its odd source extent is resolved by the clamped 2x2 box filter,
        // not by allocating a differently shaped chain or reading outside its pixels.
        std::unique_ptr<IRenderTargetBackend> oddTarget =
            backend.CreateRenderTarget2D(3, 5, /*DepthFormat::None*/ 0,
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
        backend.SetVirtualResolution(16, 8);
        int viewportWidth = 0;
        int viewportHeight = 0;
        backend.GetViewportSize(viewportWidth, viewportHeight);
        ok &= Expect(viewportWidth == 16 && viewportHeight == 8,
                     "SetVirtualResolution must resize the GDI CPU backbuffer.");
        backend.Clear(1.0f, 0.0f, 0.0f, 1.0f);
        ok &= ExpectPixel("resized backbuffer", ReadPixel(backend, 15, 7), red);

        // Letterbox transforms use the same presentation geometry as Present().
        ok &= Expect(SDL_SetWindowSize(window, 64, 64), "SDL_SetWindowSize failed.");
        ok &= Expect(SDL_SyncWindow(window), "SDL_SyncWindow failed.");
        backend.SetPresentationMode(static_cast<int>(CnaPresentationMode::Letterbox));
        float windowX = 0.0f;
        float windowY = 0.0f;
        float logicalX = 0.0f;
        float logicalY = 0.0f;
        ok &= Expect(backend.TransformLogicalToWindow(8.0f, 4.0f, windowX, windowY) &&
                         std::fabs(windowX - 32.0f) < 0.01f && std::fabs(windowY - 32.0f) < 0.01f,
                     "logical-to-window letterbox transform is wrong.");
        ok &= Expect(backend.TransformWindowToLogical(32.0f, 32.0f, logicalX, logicalY) &&
                         std::fabs(logicalX - 8.0f) < 0.01f && std::fabs(logicalY - 4.0f) < 0.01f,
                     "window-to-logical letterbox transform is wrong.");
        ok &= Expect(!backend.TransformWindowToLogical(1.0f, 1.0f, logicalX, logicalY),
                     "letterbox bar must not map to a logical coordinate.");

        backend.Present();

        // When CNA_GDI_DIRTY_PRESENTATION=1 is set, this is an eligible 1:1 UI update: the
        // initial clear/present must be full, then only the new small sprite can be sent to GDI.
        // The same test deliberately remains valid with the opt-in disabled, where both presents
        // use the conservative full-frame path.
        ok &= Expect(SDL_SetWindowSize(window, 16, 8), "SDL_SetWindowSize for dirty present failed.");
        ok &= Expect(SDL_SyncWindow(window), "SDL_SyncWindow for dirty present failed.");
        backend.SetPresentationMode(static_cast<int>(CnaPresentationMode::Stretch));
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        backend.Present();
        Draw(backend, *atlas, Rectangle(7, 3, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        backend.Present();
        ok &= ExpectPixel("dirty presentation updated sprite", ReadPixel(backend, 7, 3), red);

        // GDI-028: Anisotropic deliberately uses the CPU sampler's documented Linear path. It
        // must be stable and visibly identical to Linear rather than claiming a costly, partial
        // anisotropic approximation. The atlas's non-uniform texels make an accidental Point path
        // immediately detectable.
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(backend, *atlas, Rectangle(0, 0, 3, 3), Rectangle(0, 0, 2, 2), Color::White,
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, /*Linear*/ 0);
        const Pixel linearFilteredPixel = ReadPixel(backend, 1, 1);
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(backend, *atlas, Rectangle(0, 0, 3, 3), Rectangle(0, 0, 2, 2), Color::White,
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, /*Anisotropic*/ 2);
        ok &= ExpectPixel("GDI anisotropic maps to Linear", ReadPixel(backend, 1, 1),
                          linearFilteredPixel);

        // GDI-025: only an explicit request for four samples opts into CPU MSAA.  A rotated
        // opaque sprite leaves a predictable fractional edge coverage after the sample resolve;
        // the 2x2 grid has one covered sample at this chosen edge pixel, so red resolves to 63.
        ok &= Expect(backend.ApplyMultiSampleCount(2) == 0 && backend.GetMultiSampleCount() == 0,
                     "GDI must honestly reject unsupported CPU MSAA sample counts.");
        ok &= Expect(backend.ApplyMultiSampleCount(4) == 4 && backend.GetMultiSampleCount() == 4,
                     "GDI must apply the requested 4x CPU MSAA count.");
        std::unique_ptr<IRenderTargetBackend> msaaTarget =
            backend.CreateRenderTarget2D(2, 2, /*DepthFormat::None*/ 0,
                                         /*preserveContents*/ false, /*mipMap*/ false,
                                         /*requested MSAA*/ 4);
        ok &= Expect(msaaTarget != nullptr && msaaTarget->GetMultiSampleCount() == 0,
                     "GDI render targets must honestly remain single-sampled.");
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(backend, *atlas, Rectangle(6, 2, 4, 4), Rectangle(0, 0, 1, 1), Color::White,
             0.78539816339f, Vector2(0.5f, 0.5f));
        ok &= ExpectPixel("GDI 4x MSAA resolves fractional rotated edge", ReadPixel(backend, 3, 1),
                          Pixel{ 63, 0, 0, 255 }, 1);
        ok &= Expect(backend.ApplyMultiSampleCount(0) == 0 && backend.GetMultiSampleCount() == 0,
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
        GraphicsBackendCreateArgs args;
        args.window = window;
        args.virtualWidth = 32;
        args.virtualHeight = 32;
        args.presentationMode = CnaPresentationMode::Stretch;
        std::unique_ptr<IGraphicsBackend> backend = CreateGraphicsBackend(args);

        if (CNA::getCurrentGraphicsBackendType() != CNA::GraphicsBackendType::Gdi)
        {
            std::fprintf(stderr, "GDI regression target selected a non-GDI backend.\n");
            result = 1;
        }
        else if (!RunRegression(*backend, window))
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
