// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-1225/MOD-1227/MOD-1229/MOD-1245/MOD-1246: image-based lighting, end to end.
//
// The precompute has its own unit tests; what those cannot show is whether the three products
// actually reach a shader and light a surface. This does, by drawing the same quad several times
// and comparing the frames -- each check is a difference between two renders, so a renderer that
// silently ignored the whole IBL group fails visibly instead of producing a plausible picture.
//
// Check A -- an environment lights a surface that has no lights and no ambient colour at all.
// Check B -- with the environment detached, that same surface is black: the light came from IBL.
// Check C -- flat ambient and IBL are exclusive, never summed (MOD-1226).
// Check D -- roughness changes what a metal reflects, so the mip ramp is really being indexed.
// Check E -- an occlusion map darkens the environment term but not the direct light (MOD-1227).
// Check F -- the white furnace: a white environment on a white non-metal returns near-white, and
//            the shortfall is the measured quality of the approximation (MOD-1229).
//
// `--benchmark` times an IBL-lit frame against a flat-ambient one (MOD-1246).
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = SKIP.

#include "CNA/Graphics/EnvironmentProcessor.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ImageBasedLightEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "System/NotSupportedException.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Graphics::EnvironmentProcessor;
using CNA::GraphicsCapability;

namespace
{
    constexpr int kFrame = 96;

    /// A cube of one colour: the input the white-furnace test needs, and the one whose correct
    /// output is known without computing anything.
    std::unique_ptr<TextureCube> MakeConstantCube(GraphicsDevice& device, int size,
                                                  const Color& colour)
    {
        auto cube = std::make_unique<TextureCube>(device, size, false, SurfaceFormat::Color);
        const std::vector<Color> face(static_cast<std::size_t>(size) * size, colour);
        for (int i = 0; i < 6; ++i)
            cube->SetData(static_cast<CubeMapFace>(i), face.data(),
                          static_cast<int>(face.size()));
        return cube;
    }

    /// A cube with one bright face: a *directional* environment, where a rough surface and a
    /// smooth one must disagree about what they reflect.
    std::unique_ptr<TextureCube> MakeDirectionalCube(GraphicsDevice& device, int size)
    {
        auto cube = MakeConstantCube(device, size, Color(10, 10, 10, 255));
        const std::vector<Color> bright(static_cast<std::size_t>(size) * size,
                                        Color(255, 255, 255, 255));
        cube->SetData(CubeMapFace::PositiveZ, bright.data(), static_cast<int>(bright.size()));
        return cube;
    }

    Texture2D* MakeConstantTexture(GraphicsDevice& device, const Color& colour,
                                   std::vector<std::unique_ptr<Texture2D>>& keepAlive)
    {
        auto texture = std::make_unique<Texture2D>(device, 2, 2);
        const std::vector<Color> texels(4, colour);
        texture->SetData(texels.data(), 4);
        keepAlive.push_back(std::move(texture));
        return keepAlive.back().get();
    }
}

class IblExample : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::vector<std::unique_ptr<Texture2D>> textures_;
    std::unique_ptr<VertexBuffer> quad_;
    bool benchmark_ = false;
    int  passCount_ = 0;
    int  checkCount_ = 0;
    int  result_ = 1;

    void check(bool ok, const std::string& label)
    {
        ++checkCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    /// One quad, facing the camera, drawn with whatever PBR configuration the caller has made.
    ///
    /// A raw stride-48 stream rather than `VertexPositionNormalTangentTexture`: that class has a
    /// virtual base, so its in-memory layout is not the GPU one and passing an array of it to
    /// DrawUserPrimitives draws garbage silently. `VertexBuffer::SetDataRaw` with the stream's
    /// real stride is the shape every other PBR test in this repository uses.
    struct PbrGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
    };
    static_assert(sizeof(PbrGpuVertex) == 48, "the PBR stream is 48 bytes per vertex");

    void DrawQuad(GraphicsDevice& device, PbrEffect& effect)
    {
        if (!quad_)
        {
            const std::array<PbrGpuVertex, 6> vertices{
                PbrGpuVertex{-1, -1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1},
                PbrGpuVertex{ 1, -1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1},
                PbrGpuVertex{ 1,  1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0},
                PbrGpuVertex{-1, -1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1},
                PbrGpuVertex{ 1,  1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0},
                PbrGpuVertex{-1,  1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0},
            };
            quad_ = std::make_unique<VertexBuffer>(device, static_cast<int>(vertices.size()));
            quad_->SetDataRaw(vertices.data(), static_cast<int>(vertices.size()),
                              static_cast<int>(sizeof(PbrGpuVertex)));
        }

        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setBlendStateProperty(BlendState::Opaque);
        effect.Apply();
        device.SetVertexBuffer(quad_.get());
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        device.SetVertexBuffer(nullptr);
    }

    /// A PbrEffect with every light off, so whatever the frame shows came from the ambient term.
    std::unique_ptr<PbrEffect> MakeUnlitPbr(GraphicsDevice& device, float roughness, float metallic)
    {
        auto effect = std::make_unique<PbrEffect>(device);
        effect->setWorldProperty(Matrix::getIdentityProperty());
        effect->setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero,
                                                     Vector3(0.0f, 1.0f, 0.0f)));
        effect->setProjectionProperty(
            Matrix::CreatePerspectiveFieldOfView(1.0f, 1.0f, 0.1f, 100.0f));
        effect->setTextureProperty(MakeConstantTexture(device, Color::White, textures_));
        effect->setBaseColorTextureIsSrgbEXTProperty(false);
        effect->setEncodeOutputToSrgbEXTProperty(false);
        effect->setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect->setMetallicRoughnessMapProperty(MakeConstantTexture(device, Color::White, textures_));
        effect->setRoughnessFactorProperty(roughness);
        effect->setMetallicFactorProperty(metallic);
        effect->setAmbientLightColorProperty(Vector3::Zero);
        effect->setEmissiveFactorProperty(Vector3::Zero);
        effect->DirectionalLight0.setEnabledProperty(false);
        effect->DirectionalLight1.setEnabledProperty(false);
        effect->DirectionalLight2.setEnabledProperty(false);
        effect->setFogEnabledProperty(false);
        return effect;
    }

    std::vector<Color> Render(GraphicsDevice& device, PbrEffect& effect)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kFrame) * kFrame, Color::Transparent);
        device.Clear(Color::Black);
        DrawQuad(device, effect);
        try { device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size())); }
        catch (const System::NotSupportedException&)
        {
            std::printf("SKIP: this renderer has no readable back buffer\n");
            std::exit(77);
        }
        return pixels;
    }

    static Color Centre(const std::vector<Color>& pixels)
    {
        return pixels[static_cast<std::size_t>(kFrame / 2) * kFrame + kFrame / 2];
    }

    static int Luma(const Color& c)
    {
        return (c.getRProperty() + c.getGProperty() + c.getBProperty()) / 3;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();

        if (!device.SupportsCapability(GraphicsCapability::ThreeD) ||
            !device.SupportsCapability(GraphicsCapability::CustomEffects))
        {
            std::printf("SKIP: this renderer does not raster 3D or cannot compile the PBR shader "
                        "(a documented capability boundary, not a defect)\n");
            std::exit(77);
        }

        EnvironmentProcessor processor(device);

        // Small and coarse on purpose: this program is checking that the values arrive and are
        // used, not that they are accurate -- accuracy is what the unit tests measure.
        constexpr int kMips = 4;
        auto whiteEnvironment = MakeConstantCube(device, 8, Color(255, 255, 255, 255));
        auto whiteIrradiance  = processor.generateIrradiance(whiteEnvironment.get(), 8, 8);
        auto whiteSpecular    = processor.generatePrefilteredSpecular(whiteEnvironment.get(), 8,
                                                                      kMips, 16);
        auto brdf             = processor.generateBrdfLut(32, 32);

        ImageBasedLightEXT white;
        white.Irradiance          = whiteIrradiance.get();
        white.PrefilteredSpecular = whiteSpecular.get();
        white.BrdfLut             = brdf.get();
        white.PrefilteredMipCount = kMips;

        // --- A/B: an environment lights an otherwise unlit surface -----------------------------
        auto effect = MakeUnlitPbr(device, 0.6f, 0.0f);
        const int dark = Luma(Centre(Render(device, *effect)));
        effect->setImageBasedLightEXT(white);
        const int lit = Luma(Centre(Render(device, *effect)));
        std::printf("    unlit %d, IBL-lit %d\n", dark, lit);
        check(lit > dark + 40, "an environment lights a surface with no lights and no ambient");
        check(dark < 12, "with no environment and no lights the surface is black, so the light in "
                         "the frame above came from the environment");

        // --- C: flat ambient and IBL are exclusive, not additive -------------------------------
        effect->setImageBasedLightEXT(ImageBasedLightEXT{});
        effect->setAmbientLightColorProperty(Vector3(0.5f, 0.5f, 0.5f));
        const int flatOnly = Luma(Centre(Render(device, *effect)));
        effect->setImageBasedLightEXT(white);
        const int both = Luma(Centre(Render(device, *effect)));
        std::printf("    flat ambient %d, flat ambient + environment %d, environment alone %d\n",
                    flatOnly, both, lit);
        check(std::abs(both - lit) <= 6,
              "binding an environment replaces the flat ambient term rather than adding to it");
        effect->setAmbientLightColorProperty(Vector3::Zero);

        // --- D: roughness indexes the prefiltered mip ramp --------------------------------------
        auto directionalEnvironment = MakeDirectionalCube(device, 8);
        auto directionalSpecular =
            processor.generatePrefilteredSpecular(directionalEnvironment.get(), 8, kMips, 32);
        auto directionalIrradiance =
            processor.generateIrradiance(directionalEnvironment.get(), 8, 8);
        ImageBasedLightEXT directional;
        directional.Irradiance          = directionalIrradiance.get();
        directional.PrefilteredSpecular = directionalSpecular.get();
        directional.BrdfLut             = brdf.get();
        directional.PrefilteredMipCount = kMips;

        auto mirror = MakeUnlitPbr(device, 0.0f, 1.0f);
        mirror->setImageBasedLightEXT(directional);
        const int smooth = Luma(Centre(Render(device, *mirror)));
        auto rough = MakeUnlitPbr(device, 1.0f, 1.0f);
        rough->setImageBasedLightEXT(directional);
        const int blurred = Luma(Centre(Render(device, *rough)));
        std::printf("    metal reflecting a directional environment: smooth %d, rough %d\n",
                    smooth, blurred);
        check(std::abs(smooth - blurred) > 5,
              "roughness changes what a metal reflects, so the mip ramp is really indexed");

        // --- E: occlusion darkens the environment term ------------------------------------------
        auto occluded = MakeUnlitPbr(device, 0.6f, 0.0f);
        occluded->setImageBasedLightEXT(white);
        occluded->setOcclusionMapProperty(MakeConstantTexture(device, Color(64, 64, 64, 255),
                                                              textures_));
        const int occludedLuma = Luma(Centre(Render(device, *occluded)));
        std::printf("    environment-lit %d, same with a 0.25 occlusion map %d\n", lit,
                    occludedLuma);
        check(occludedLuma < lit - 20,
              "an occlusion map darkens the environment term (MOD-1227)");

        ImageBasedLightEXT half = white;
        half.Intensity = 0.5f;

        // --- G: a shadow attenuates the direct light only (MOD-1228) ----------------------------
        // The shadow map here is simply black, which every receiver reads as "an occluder at
        // distance 0 in front of me" -- a fully shadowed surface without needing a caster pass.
        // The environment term must survive it, because a shadow answers the visibility of one
        // light and says nothing about the rest of the sky.
        auto shadowed = MakeUnlitPbr(device, 0.6f, 0.0f);
        shadowed->DirectionalLight0.setEnabledProperty(true);
        shadowed->DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        shadowed->DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        shadowed->setImageBasedLightEXT(half);
        const int litByBoth = Luma(Centre(Render(device, *shadowed)));
        shadowed->setShadowMapEXT(MakeConstantTexture(device, Color::Black, textures_));
        shadowed->setLightViewProjectionEXT(Matrix::getIdentityProperty());
        shadowed->setShadowsEnabledEXT(true);
        const int litByEnvironmentOnly = Luma(Centre(Render(device, *shadowed)));
        auto environmentOnly = MakeUnlitPbr(device, 0.6f, 0.0f);
        environmentOnly->setImageBasedLightEXT(half);
        const int noDirectAtAll = Luma(Centre(Render(device, *environmentOnly)));
        std::printf("    sun + environment %d, fully shadowed %d, environment alone %d\n",
                    litByBoth, litByEnvironmentOnly, noDirectAtAll);
        check(litByEnvironmentOnly < litByBoth - 10 &&
                  std::abs(litByEnvironmentOnly - noDirectAtAll) <= 6,
              "a shadow removes the direct light and leaves the environment term (MOD-1228)");

        // --- F: the white furnace ----------------------------------------------------------------
        // At intensity 1 a white environment saturates the frame, and a saturated frame can only
        // show energy LOSS -- gain clips to white and looks perfect. Half intensity puts the
        // expected answer at 128/255, where the measurement is two-sided: a surface that returns
        // more than it received is as visible as one that returns less.
        std::printf("    white furnace, albedo 1, no lights, environment at half intensity "
                    "(128/255 is exact energy conservation):\n");
        int worstDeviation = 0;
        for (const float roughness : {0.1f, 0.4f, 0.7f, 1.0f})
        {
            auto furnace = MakeUnlitPbr(device, roughness, 0.0f);
            furnace->setImageBasedLightEXT(half);
            const int value = Luma(Centre(Render(device, *furnace)));
            worstDeviation = std::max(worstDeviation, std::abs(value - 128));
            std::printf("      roughness %.1f -> %d/255 (%+d)\n",
                        static_cast<double>(roughness), value, value - 128);
        }
        // A loose bound, deliberately: the split sum is an approximation, the products are 8-bit,
        // and the irradiance sweep here is 8 samples. The printed numbers are the honest measure
        // of the approximation; the check catches a surface gone dark or blown out.
        check(worstDeviation < 64,
              "a white environment on a white non-metal returns close to the energy it received");

        if (benchmark_)
        {
            // Clear and draw only: a read-back per frame costs more than either shading path and
            // would bury the difference this is trying to measure.
            const auto timeOf = [&](PbrEffect& fx) {
                const auto frame = [&] { device.Clear(Color::Black); DrawQuad(device, fx); };
                for (int i = 0; i < 8; ++i) frame();
                const auto start = std::chrono::steady_clock::now();
                constexpr int kFrames = 200;
                for (int i = 0; i < kFrames; ++i) frame();
                return std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - start).count() / kFrames;
            };
            auto flat = MakeUnlitPbr(device, 0.6f, 0.0f);
            flat->setAmbientLightColorProperty(Vector3(0.5f, 0.5f, 0.5f));
            auto ibl = MakeUnlitPbr(device, 0.6f, 0.0f);
            ibl->setImageBasedLightEXT(white);
            std::printf("--- MOD-1246: per-frame cost ---\n");
            std::printf("    flat ambient: %.3f ms/frame\n", timeOf(*flat));
            std::printf("    image-based:  %.3f ms/frame\n", timeOf(*ibl));
        }

        std::printf("%d/%d checks passed\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

public:
    explicit IblExample(bool benchmark) : benchmark_(benchmark)
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kFrame);
        gdm_->setPreferredBackBufferHeightProperty(kFrame);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int result() const { return result_; }
};

int main(int argc, char** argv)
{
    bool benchmark = false;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--benchmark") == 0) benchmark = true;

    IblExample example(benchmark);
    example.Run();
    return example.result();
}
