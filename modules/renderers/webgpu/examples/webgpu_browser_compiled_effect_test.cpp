// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-203: compiled XNA Effects in a REAL BROWSER, driven as one page
// through scripts/run-webgpu-browser-test.sh.
//
// Why this page has to exist, when the native suites already pass.
//
// The compiled-effect stack is shared between the two targets down to one seam:
// `WebGPURenderer::GetOrCreateCompiledEffectShaderModuleEXT`, where a native build hands WebGPU
// SPIR-V and a browser build hands it WGSL translated from that same SPIR-V. Two things are NOT
// settled by the native run:
//
//   * WHICH SHADER COMPILER accepts the WGSL. Native goes through wgpu-native's Naga; the browser
//     goes through emdawnwebgpu's Tint. A module one accepts is not thereby accepted by the other,
//     which is exactly the lesson `webgpu_browser_coverage_test.cpp` was written for.
//   * WHETHER MOJOSHADER ITSELF RUNS UNDER WASM. The Effect parser, the D3D9 bytecode walk and the
//     SPIR-V emitter are a C library compiled to WebAssembly here for the first time. A parse that
//     works natively is not thereby a parse that works on a 32-bit wasm target.
//
// Everything above that seam -- Effect parsing, reflection, constants, samplers, techniques,
// vertex declarations, parameter snapshots, SpriteBatch, multi-pass -- is the same code the native
// CTests cover, so this page checks the RUNGS OF THE ACCEPTANCE LADDER rather than re-running the
// whole conformance corpus:
//
//   1  a minimal compiled vertex+pixel Effect draws                     (drawable fixture, pass 0)
//   2  uniform scalar/vector/matrix reach the shader                    (Tint, Transform)
//   3  a Texture2D sampler samples                                      (sampling fixture)
//   4  ordinary transformed geometry lands where it should              (orthographic quad)
//   5  SpriteBatch with a custom compiled Effect                        (Begin(..., &effect))
//   6  multi-pass                                                       (two-pass technique)
//   7  cube sampler                                                     (samplerCUBE fixture)
//   8  volume sampler                                                   (sampler3D fixture)
//   9  a render target as the sampled source
//
// Each check reads pixels back rather than trusting an absence of errors, and each runs in its own
// frame: WEBGPU-133 measured that a readback yields to the browser, which presents and invalidates
// the canvas surface texture during the yield.
//
// Exit code is not the signal in a browser, so this prints "=== N/M PASS ===" like every other
// page the harness drives.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"

#include "CNA/TestSupport/CompiledEffectFixtures.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::WebGPU::WebGPURenderer;

namespace
{
    /// The clear colour every check paints over, so "nothing drew" is distinguishable from
    /// "something drew the wrong colour".
    const Color kBackground(9, 19, 29, 255);

    /// The shapes the shared native contracts stream: NDC positions, so nothing here depends on a
    /// projection, and the fixture's `Transform` stays the identity.
    struct QuadVertex
    {
        float x, y, z;
    };

    struct QuadVertexUV
    {
        float x, y, z;
        float u, v;
    };

    struct QuadVertexUVW
    {
        float x, y, z;
        float u, v, w;
    };

    const float kCorners[6][2] = {
        {-1.0f, 1.0f}, {-1.0f, -1.0f}, {1.0f, -1.0f},
        {-1.0f, 1.0f}, {1.0f, -1.0f},  {1.0f, 1.0f},
    };

    VertexDeclaration PositionDeclaration()
    {
        return VertexDeclaration(static_cast<int>(sizeof(QuadVertex)),
                                 {VertexElement(0, VertexElementFormat::Vector3,
                                                VertexElementUsage::Position, 0)});
    }

    VertexDeclaration PositionUvDeclaration()
    {
        return VertexDeclaration(
            static_cast<int>(sizeof(QuadVertexUV)),
            {VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
             VertexElement(12, VertexElementFormat::Vector2,
                           VertexElementUsage::TextureCoordinate, 0)});
    }

    VertexDeclaration PositionUvwDeclaration()
    {
        return VertexDeclaration(
            static_cast<int>(sizeof(QuadVertexUVW)),
            {VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
             VertexElement(12, VertexElementFormat::Vector3,
                           VertexElementUsage::TextureCoordinate, 0)});
    }
}

class WebGpuBrowserCompiledEffectTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int step_ = 0;
    int pass_ = 0;
    int total_ = 0;
    int width_ = 0;
    int height_ = 0;
    Color pass0Pixel_{0, 0, 0, 0};

    void check(bool ok, const std::string& label)
    {
        ++total_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++pass_;
    }

    static bool Near(int value, int expected, int tolerance = 6)
    {
        return value >= expected - tolerance && value <= expected + tolerance;
    }

    /// Every check renders into an OFFSCREEN target and reads that back, which is what the shared
    /// native contracts do -- and here it is load-bearing rather than a convenience. This renderer
    /// REPLAYS its draws at queue time rather than recording them as the game issues them, so a
    /// backbuffer read taken in the middle of a frame reads a surface the frame's own draws have
    /// not reached yet; measured on this page as every check reading the clear colour, with a
    /// "Destroyed texture used in a submit" from the present that followed the read's yield.
    /// A render-target read resolves against the target the draws were aimed at.
    static constexpr int kTargetSize = 8;

    [[nodiscard]] Color CentreOf(RenderTarget2D& target) const
    {
        Color pixel(0, 0, 0, 0);
        const Rectangle probe(kTargetSize / 2, kTargetSize / 2, 1, 1);
        target.GetData(0, &probe, &pixel, 0, 1);
        return pixel;
    }

    void ResetState(GraphicsDevice& device) const
    {
        RasterizerState rasterizer;
        rasterizer.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(rasterizer);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
    }

    /// Applies one pass of @p effect, draws a full-target NDC quad through it into an offscreen
    /// target, and returns that target's centre pixel.
    template <typename Vertex>
    Color DrawQuadAndRead(GraphicsDevice& device, Effect& effect, std::size_t passIndex,
                          const Vertex (&quad)[6], const VertexDeclaration& declaration)
    {
        RenderTarget2D target(device, kTargetSize, kTargetSize);
        device.SetRenderTarget(&target);
        device.Clear(kBackground);
        ResetState(device);
        effect.getTechniquesProperty()[0].getPassesProperty()[passIndex].Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, static_cast<const void*>(quad), 0,
                                  2, declaration);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return CentreOf(target);
    }

    static void FillQuad(QuadVertex (&quad)[6])
    {
        for (int i = 0; i < 6; ++i) quad[i] = QuadVertex{kCorners[i][0], kCorners[i][1], 0.0f};
    }

    static void FillQuad(QuadVertexUV (&quad)[6], float u, float v)
    {
        for (int i = 0; i < 6; ++i)
            quad[i] = QuadVertexUV{kCorners[i][0], kCorners[i][1], 0.0f, u, v};
    }

    static void FillQuad(QuadVertexUVW (&quad)[6], float u, float v, float w)
    {
        for (int i = 0; i < 6; ++i)
            quad[i] = QuadVertexUVW{kCorners[i][0], kCorners[i][1], 0.0f, u, v, w};
    }

    /// plans/plan_webgpu.md WEBGPU-208. A quad whose texture coordinate RAMPS from 0 to 1 across
    /// the target rather than sitting at one value. Every other check here samples at a constant
    /// coordinate, whose screen-space derivative is zero and whose computed level of detail is
    /// therefore implementation-defined -- fine when the level does not matter, useless when it is
    /// the thing being measured. With the target exactly as wide as the texture this is one texel
    /// per pixel, so the computed level is 0 and a +1 bias must select level 1.
    static void FillRampQuad(QuadVertexUV (&quad)[6])
    {
        for (int i = 0; i < 6; ++i)
        {
            quad[i] = QuadVertexUV{kCorners[i][0], kCorners[i][1], 0.0f,
                                   (kCorners[i][0] + 1.0f) * 0.5f,
                                   (1.0f - kCorners[i][1]) * 0.5f};
        }
    }

    /// The drawable fixture with the identity transform and a chosen Tint.
    static void Configure(Effect& effect, const Vector4& tint)
    {
        auto& parameters = effect.getParametersProperty();
        parameters["Transform"]->SetValue(Matrix::getIdentityProperty());
        parameters["Tint"]->SetValue(tint);
    }

public:
    WebGpuBrowserCompiledEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(256);
        gdm_->setPreferredBackBufferHeightProperty(256);
    }

    /// Everything happens in Draw(), not Update(), and that is load-bearing: the frame's canvas
    /// surface texture is acquired around Draw, and a backbuffer readback yields to the browser,
    /// which presents and invalidates that texture during the yield (WEBGPU-133). Work done in
    /// Update() submits against a texture the yield already destroyed --  measured here, as
    /// "Destroyed texture used in a submit" on every second check.
    void Draw(const GameTime&) override
    {
        GraphicsDevice& device = getGraphicsDeviceProperty();
        width_ = device.getViewportProperty().getWidthProperty();
        height_ = device.getViewportProperty().getHeightProperty();
        if (width_ <= 8 || height_ <= 8) return;   // wait for the canvas to be sized

        switch (step_)
        {
            case 0:
            {
                // Rung 0: the capability must be TRUE here. If it is not, nothing below can mean
                // anything, so say so rather than reporting a page full of quiet failures.
                check(device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects),
                      "capability CompiledEffects is true in the browser");
                break;
            }
            case 1:
            {
                // Rungs 1, 2 and 4: MojoShader parses D3D9 bytecode under wasm, the WGSL the
                // translator produced compiles under TINT, the uniform arrives, and the geometry
                // lands over the whole surface.
                Effect effect(device, CNA::TestSupport::BuildSyntheticDrawableEffect());
                Configure(effect, Vector4(1.0f, 0.0f, 0.0f, 1.0f));
                QuadVertex quad[6];
                FillQuad(quad);
                const Color pixel =
                    DrawQuadAndRead(device, effect, 1, quad, PositionDeclaration());
                check(Near(pixel.getRProperty(), 255) && Near(pixel.getGProperty(), 0) &&
                          Near(pixel.getBProperty(), 0),
                      "compiled vertex+pixel Effect draws its Tint through a uniform");
                break;
            }
            case 2:
            {
                // The same effect with a different uniform must produce a different pixel: this is
                // what separates "the shader ran" from "something painted".
                Effect effect(device, CNA::TestSupport::BuildSyntheticDrawableEffect());
                Configure(effect, Vector4(0.0f, 0.0f, 1.0f, 1.0f));
                QuadVertex quad[6];
                FillQuad(quad);
                const Color pixel =
                    DrawQuadAndRead(device, effect, 1, quad, PositionDeclaration());
                check(Near(pixel.getBProperty(), 255) && Near(pixel.getRProperty(), 0),
                      "a changed uniform changes the pixel");
                break;
            }
            case 3:
            {
                // Rung 3: a Texture2D sampler, sampled by the compiled pixel shader.
                Effect effect(device, CNA::TestSupport::BuildSyntheticSamplingEffect({}));
                Configure(effect, Vector4(1.0f, 1.0f, 1.0f, 1.0f));
                Texture2D texture(device, 1, 1);
                const Color texel[1] = {Color(20, 200, 90, 255)};
                texture.SetData(texel, 1);
                effect.getParametersProperty()["FxTexture"]->SetValue(&texture);

                QuadVertexUV quad[6];
                FillQuad(quad, 0.5f, 0.5f);
                const Color pixel =
                    DrawQuadAndRead(device, effect, 1, quad, PositionUvDeclaration());
                check(Near(pixel.getRProperty(), 20) && Near(pixel.getGProperty(), 200) &&
                          Near(pixel.getBProperty(), 90),
                      "a compiled pixel shader samples a Texture2D");
                break;
            }
            case 4:
            {
                // Rung 5: SpriteBatch with a custom compiled Effect. SpriteBatch supplies its own
                // vertices and its own orthographic transform, so this is a different route into
                // the same shaders rather than the same draw again.
                Effect effect(device, CNA::TestSupport::BuildSyntheticDrawableEffect());
                auto& parameters = effect.getParametersProperty();
                parameters["Transform"]->SetValue(Matrix::CreateOrthographicOffCenter(
                    0.0f, static_cast<float>(kTargetSize), static_cast<float>(kTargetSize), 0.0f,
                    -1.0f, 1.0f));
                parameters["Tint"]->SetValue(Vector4(0.0f, 1.0f, 0.0f, 1.0f));

                Texture2D sprite(device, 1, 1);
                const Color white[1] = {Color::White};
                sprite.SetData(white, 1);

                RenderTarget2D target(device, kTargetSize, kTargetSize);
                device.SetRenderTarget(&target);
                device.Clear(kBackground);
                ResetState(device);
                {
                    SpriteBatch batch(device);
                    batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr,
                                nullptr, &effect);
                    batch.Draw(sprite, Rectangle(0, 0, kTargetSize, kTargetSize), Color::White);
                    batch.End();
                }
                device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                const Color pixel = CentreOf(target);
                check(Near(pixel.getGProperty(), 255) && Near(pixel.getRProperty(), 0),
                      "SpriteBatch draws through a custom compiled Effect");
                break;
            }
            case 5:
            {
                // Rung 6, first half: pass 0 of the fixture's technique is its ALTERNATE program.
                // Whatever it paints is recorded here and compared in the next frame.
                Effect effect(device, CNA::TestSupport::BuildSyntheticDrawableEffect());
                Configure(effect, Vector4(1.0f, 0.0f, 1.0f, 1.0f));
                const int passes =
                    effect.getTechniquesProperty()[0].getPassesProperty().getCountProperty();
                QuadVertex quad[6];
                FillQuad(quad);
                pass0Pixel_ = DrawQuadAndRead(device, effect, 0, quad, PositionDeclaration());
                check(passes >= 2,
                      "the fixture's technique has two distinguishable passes to select between");
                break;
            }
            case 6:
            {
                // Rung 6, second half: pass 1 is a DIFFERENT program with the same parameters. A
                // route that reused the first pass's modules would produce the same pixel; a route
                // that never applied the second would produce the clear colour.
                Effect effect(device, CNA::TestSupport::BuildSyntheticDrawableEffect());
                Configure(effect, Vector4(1.0f, 0.0f, 1.0f, 1.0f));
                QuadVertex quad[6];
                FillQuad(quad);
                const Color pixel =
                    DrawQuadAndRead(device, effect, 1, quad, PositionDeclaration());
                const bool differs = pixel.getRProperty() != pass0Pixel_.getRProperty() ||
                                     pixel.getGProperty() != pass0Pixel_.getGProperty() ||
                                     pixel.getBProperty() != pass0Pixel_.getBProperty();
                check(differs && Near(pixel.getRProperty(), 255) &&
                          Near(pixel.getGProperty(), 0) && Near(pixel.getBProperty(), 255),
                      "each pass of a multi-pass technique runs its own program");
                break;
            }
            case 7:
            {
                // Rung 7: a cube sampler. +X is the face a (1, 0, 0) direction selects.
                Effect effect(device,
                              CNA::TestSupport::BuildSyntheticSamplingEffect(
                                  {}, 0, CNA::TestSupport::SyntheticSamplerKind::SamplerCube));
                Configure(effect, Vector4(1.0f, 1.0f, 1.0f, 1.0f));

                TextureCube cube(device, 1, false, SurfaceFormat::Color);
                const Color faces[6] = {Color(255, 0, 0, 255),   Color(0, 255, 0, 255),
                                        Color(0, 0, 255, 255),   Color(255, 255, 0, 255),
                                        Color(255, 0, 255, 255), Color(0, 255, 255, 255)};
                for (int face = 0; face < 6; ++face)
                {
                    const Color texel[1] = {faces[face]};
                    cube.SetData(static_cast<CubeMapFace>(face), texel, 1);
                }
                effect.getParametersProperty()["FxTexture"]->SetValue(&cube);

                QuadVertexUVW quad[6];
                FillQuad(quad, 1.0f, 0.0f, 0.0f);
                const Color pixel =
                    DrawQuadAndRead(device, effect, 1, quad, PositionUvwDeclaration());
                check(Near(pixel.getRProperty(), 255) && Near(pixel.getGProperty(), 0) &&
                          Near(pixel.getBProperty(), 0),
                      "a compiled pixel shader samples a TextureCube");
                break;
            }
            case 8:
            {
                // Rung 8, first slice.
                Effect effect(device,
                              CNA::TestSupport::BuildSyntheticSamplingEffect(
                                  {}, 0, CNA::TestSupport::SyntheticSamplerKind::Sampler3D));
                Configure(effect, Vector4(1.0f, 1.0f, 1.0f, 1.0f));
                Texture3D volume(device, 1, 1, 2, false, SurfaceFormat::Color);
                const Color slices[2] = {Color(255, 40, 40, 255), Color(40, 40, 255, 255)};
                volume.SetData(slices, 2);
                effect.getParametersProperty()["FxTexture"]->SetValue(&volume);

                QuadVertexUVW quad[6];
                FillQuad(quad, 0.5f, 0.5f, 0.25f);
                const Color pixel =
                    DrawQuadAndRead(device, effect, 1, quad, PositionUvwDeclaration());
                check(Near(pixel.getRProperty(), 255) && Near(pixel.getBProperty(), 40),
                      "a compiled pixel shader samples a Texture3D (near slice)");
                break;
            }
            case 9:
            {
                // Rung 8, second slice: w must pick a different slice, which is what makes this a
                // volume sampler rather than a 2D one with a spare coordinate.
                Effect effect(device,
                              CNA::TestSupport::BuildSyntheticSamplingEffect(
                                  {}, 0, CNA::TestSupport::SyntheticSamplerKind::Sampler3D));
                Configure(effect, Vector4(1.0f, 1.0f, 1.0f, 1.0f));
                Texture3D volume(device, 1, 1, 2, false, SurfaceFormat::Color);
                const Color slices[2] = {Color(255, 40, 40, 255), Color(40, 40, 255, 255)};
                volume.SetData(slices, 2);
                effect.getParametersProperty()["FxTexture"]->SetValue(&volume);

                QuadVertexUVW quad[6];
                FillQuad(quad, 0.5f, 0.5f, 0.75f);
                const Color pixel =
                    DrawQuadAndRead(device, effect, 1, quad, PositionUvwDeclaration());
                check(Near(pixel.getBProperty(), 255) && Near(pixel.getRProperty(), 40),
                      "a compiled pixel shader samples a Texture3D (far slice)");
                break;
            }
            case 10:
            {
                // Rung 9: a render target as the sampled source, which is where the browser's
                // texture-view lifetime rules differ most from the native ones.
                RenderTarget2D source(device, kTargetSize, kTargetSize);
                device.SetRenderTarget(&source);
                device.Clear(Color(200, 120, 40, 255));
                device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

                Effect effect(device, CNA::TestSupport::BuildSyntheticSamplingEffect({}));
                Configure(effect, Vector4(1.0f, 1.0f, 1.0f, 1.0f));
                effect.getParametersProperty()["FxTexture"]->SetValue(&source);

                QuadVertexUV quad[6];
                FillQuad(quad, 0.5f, 0.5f);
                const Color pixel =
                    DrawQuadAndRead(device, effect, 1, quad, PositionUvDeclaration());
                check(Near(pixel.getRProperty(), 200) && Near(pixel.getGProperty(), 120) &&
                          Near(pixel.getBProperty(), 40),
                      "a compiled pixel shader samples a render target");
                break;
            }
            case 11:
            {
                // plans/plan_webgpu.md WEBGPU-208 -- rung 10: SamplerState.MipMapLevelOfDetailBias
                // on a compiled Effect, IN THE BROWSER and in pixels.
                //
                // This is the rung that cannot be inferred from the native run. The bias reaches
                // the shader as a SPIR-V image operand, and the browser never sees that SPIR-V: it
                // sees the WGSL `SpirvToWgsl` writes, where the operand has become a different
                // builtin (`textureSampleBias`) that TINT -- not Naga -- has to accept and lower.
                // A translator that dropped the operand, or a Tint that refused it, would still
                // render a picture; it would just be the unbiased one.
                //
                // Four mip levels of flat distinct colours, because a real chain is nearly
                // self-similar and could not tell level 1 from level 0.
                const Color levels[4] = {Color(255, 0, 0, 255), Color(0, 255, 0, 255),
                                         Color(0, 0, 255, 255), Color(255, 255, 0, 255)};
                Texture2D mipped(device, kTargetSize, kTargetSize, /*mipMap=*/true,
                                 SurfaceFormat::Color);
                for (int level = 0; level < mipped.getLevelCountProperty(); ++level)
                {
                    const int extent = kTargetSize >> level > 0 ? kTargetSize >> level : 1;
                    const Rectangle whole(0, 0, extent, extent);
                    std::vector<Color> texels(static_cast<std::size_t>(extent * extent),
                                              levels[level < 3 ? level : 3]);
                    mipped.SetData(level, &whole, texels.data(), 0,
                                   static_cast<int>(texels.size()));
                }

                const auto sampleWithBias = [&](float bias) {
                    Effect effect(device, CNA::TestSupport::BuildSyntheticSamplingEffect(
                        {
                            {CNA::TestSupport::EffectFormat::SampMagFilter,
                             CNA::TestSupport::EffectFormat::FilterPoint},
                            {CNA::TestSupport::EffectFormat::SampMinFilter,
                             CNA::TestSupport::EffectFormat::FilterPoint},
                            {CNA::TestSupport::EffectFormat::SampMipFilter,
                             CNA::TestSupport::EffectFormat::FilterPoint},
                            {CNA::TestSupport::EffectFormat::SampAddressU,
                             CNA::TestSupport::EffectFormat::AddressClamp},
                            {CNA::TestSupport::EffectFormat::SampAddressV,
                             CNA::TestSupport::EffectFormat::AddressClamp},
                            {CNA::TestSupport::EffectFormat::SampMipMapLodBias,
                             CNA::TestSupport::FloatBits(bias), true},
                        },
                        0));
                    Configure(effect, Vector4(1.0f, 1.0f, 1.0f, 1.0f));
                    effect.getParametersProperty()["FxTexture"]->SetValue(&mipped);
                    QuadVertexUV quad[6];
                    FillRampQuad(quad);
                    return DrawQuadAndRead(device, effect, 1, quad, PositionUvDeclaration());
                };

                const Color unbiased = sampleWithBias(0.0f);
                check(Near(unbiased.getRProperty(), 255) && Near(unbiased.getGProperty(), 0),
                      "a compiled Effect with no LOD bias samples the base level");
                const Color biased = sampleWithBias(1.0f);
                check(Near(biased.getGProperty(), 255) && Near(biased.getRProperty(), 0),
                      "a compiled Effect's MipMapLevelOfDetailBias of +1 selects the next level");
                break;
            }
            default:
                std::printf("=== %d/%d PASS ===\n", pass_, total_);
                std::fflush(stdout);
                Exit();
                return;
        }
        ++step_;
    }
};

int main(int, char**)
{
    WebGpuBrowserCompiledEffectTest game;
    game.Run();
    return 0;
}
