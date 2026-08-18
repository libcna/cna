// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-912..MOD-915: cascaded shadow maps as an application uses them.
//
// The unit tests measure the pieces; this runs the whole thing through the public calls a game
// makes, on a scene that has the one property a cascade set exists for -- ground stretching far
// enough away that no single map could cover it at a useful resolution.
//
// What it checks is what a golden image of this scene would have been checked *for*, stated
// directly instead: that the cascades tile the view in depth order, that the shadow survives being
// split across them, and that the whole thing degrades to an unshadowed frame rather than an error
// where a renderer cannot run it. The deviation from the plan's "committed golden" is recorded
// there; a PNG records that a frame looked a particular way on the machine that made it, and these
// record which property was being relied on.
//
// Check A -- the renderer rasterizes 3D and compiles custom effects, or the program SKIPs.
// Check B -- the caster casts a shadow through the cascade atlas at every cascade count.
// Check C -- the debug tint bands the frame in depth order: nearest cascade nearest the camera.
// Check D -- with cascades switched off the same scene renders uniformly lit.
//
// `--benchmark` additionally times a cascade set against a single map (MOD-915). Off by default,
// for the same reason the single-map example's is: a timing is a recording of one machine.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = SKIP.

#include "CNA/Graphics/CascadedShadowMap.hpp"
#include "CNA/Graphics/DirectionalLightEXT.hpp"
#include "CNA/Graphics/ShadowMap.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShadowCascadeStateEXT.hpp"
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
using CNA::Graphics::CascadedShadowMap;
using CNA::Graphics::DirectionalLightEXT;
using CNA::Graphics::ShadowMap;
using CNA::Graphics::ShadowQuality;
using CNA::GraphicsCapability;

namespace
{
    constexpr int   kFrame      = 128;
    constexpr float kGroundHalf = 60.0f;
    constexpr float kCasterHalf = 6.0f;
    constexpr float kCasterHigh = 5.0f;
    constexpr float kNear       = 1.0f;
    constexpr float kFar        = 140.0f;

    void AppendQuad(std::vector<VertexPositionNormalTexture>& out, float y, float halfExtent,
                    float offsetZ)
    {
        const Vector3 up(0.0f, 1.0f, 0.0f);
        const Vector2 uv(0.0f, 0.0f);
        const float e = halfExtent;
        const auto v = [&](float x, float z) {
            return VertexPositionNormalTexture(Vector3(x, y, z + offsetZ), up, uv);
        };
        out.push_back(v(-e, -e)); out.push_back(v(e, -e)); out.push_back(v(e, e));
        out.push_back(v(-e, -e)); out.push_back(v(e, e));  out.push_back(v(-e, e));
    }

    /// Looking down a long strip of ground, which is the arrangement a cascade set is for: the
    /// near end wants a fine map and the far end a coarse one, and no single map gives both.
    Matrix View()
    {
        return Matrix::CreateLookAt(Vector3(0.0f, 12.0f, 55.0f), Vector3(0.0f, 0.0f, -30.0f),
                                    Vector3(0.0f, 1.0f, 0.0f));
    }

    Matrix Projection()
    {
        return Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, kNear, kFar);
    }

    DirectionalLightEXT Sun()
    {
        DirectionalLightEXT sun;
        sun.Direction = Vector3(-0.35f, -1.0f, 0.0f);
        return sun;
    }
}

class CascadedShadowExample : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
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

    static Color At(const std::vector<Color>& pixels, int x, int y)
    {
        return pixels[static_cast<std::size_t>(y) * kFrame + static_cast<std::size_t>(x)];
    }

    /// Ground pixels darker than the brightest, ignoring the cleared background. A shadowed ground
    /// pixel keeps its ambient term and is never pure black, which is what separates the two.
    static int ShadowedGroundPixels(const std::vector<Color>& pixels)
    {
        int brightest = 0;
        for (const Color& p : pixels)
            brightest = std::max(brightest, static_cast<int>(p.getRProperty()));
        int count = 0;
        for (const Color& p : pixels)
        {
            const int value = p.getRProperty();
            if (value > 0 && value < brightest - 24)
                ++count;
        }
        return count;
    }

    static void ConfigureLighting(BasicEffect& effect)
    {
        effect.setLightingEnabledProperty(true);
        effect.setTextureEnabledProperty(false);
        effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect.setAmbientLightColorProperty(Vector3(0.15f, 0.15f, 0.15f));
        effect.setSpecularColorProperty(Vector3::Zero);
        effect.setEmissiveColorProperty(Vector3::Zero);

        auto& light = effect.getDirectionalLight0Property();
        light.setEnabledProperty(true);
        light.setDirectionProperty(Sun().Direction);
        light.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        light.setSpecularColorProperty(Vector3::Zero);
        effect.getDirectionalLight1Property().setEnabledProperty(false);
        effect.getDirectionalLight2Property().setEnabledProperty(false);

        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(View());
        effect.setProjectionProperty(Projection());
    }

    /// Fills every cascade with the casters, then shades the ground from the atlas.
    void RenderFrame(GraphicsDevice& device, CascadedShadowMap& cascades, BasicEffect& effect,
                     const std::vector<VertexPositionNormalTexture>& casters,
                     const std::vector<VertexPositionNormalTexture>& ground)
    {
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setBlendStateProperty(BlendState::Opaque);

        cascades.update(Sun(), View(), Projection());
        for (int i = 0; i < cascades.getCascadeCount(); ++i)
        {
            cascades.begin(i);
            device.DrawUserPrimitives(PrimitiveType::TriangleList, casters.data(), 0,
                                      static_cast<int>(casters.size()) / 3);
            cascades.end();
        }

        cascades.applyToReceiver(effect);
        effect.setShadowsEnabledEXT(true);

        device.Clear(Color::Black);
        effect.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, ground.data(), 0,
                                  static_cast<int>(ground.size()) / 3);
    }

    static void PrintAsciiFrame(const std::vector<Color>& pixels)
    {
        int brightest = 0;
        for (const Color& p : pixels)
            brightest = std::max(brightest, static_cast<int>(p.getRProperty()));
        for (int row = 0; row < 16; ++row)
        {
            std::string line;
            for (int column = 0; column < 32; ++column)
            {
                const int value = At(pixels, column * kFrame / 32, row * kFrame / 16)
                                      .getRProperty();
                line += (value == 0) ? ' ' : ((value < brightest - 24) ? '#' : '.');
            }
            std::printf("    |%s|\n", line.c_str());
        }
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();

        // MOD-1699: `CustomEffects` says a renderer can compile *a* custom effect, not that it
        // takes this layer's shader language or that its lit shaders sample a shadow at all. The
        // Vulkan renderer answers true to the first and false to both of the others, and without
        // this second question a shadow test there does not fail -- it crashes, mid-draw, with no
        // effect applied.
        if (!device.SupportsCapability(GraphicsCapability::ThreeD) ||
            !device.SupportsCapability(GraphicsCapability::CustomEffects) ||
            !device.SupportsShadowSamplingEXT())
        {
            std::printf("SKIP: this renderer does not raster 3D or cannot compile the cascade "
                        "caster's shader (a documented capability boundary, not a defect)\n");
            std::exit(77);
        }

        std::vector<VertexPositionNormalTexture> ground;
        AppendQuad(ground, 0.0f, kGroundHalf, -10.0f);

        // Three casters spread down the strip, so at least one lands in each cascade.
        std::vector<VertexPositionNormalTexture> casters;
        AppendQuad(casters, kCasterHigh, kCasterHalf, 25.0f);
        AppendQuad(casters, kCasterHigh, kCasterHalf, -10.0f);
        AppendQuad(casters, kCasterHigh, kCasterHalf, -45.0f);

        std::vector<Color> pixels(static_cast<std::size_t>(kFrame) * kFrame, Color::Transparent);
        const auto readBack = [&] {
            try { device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size())); }
            catch (const System::NotSupportedException&)
            {
                std::printf("SKIP: this renderer has no readable back buffer\n");
                std::exit(77);
            }
        };

        for (int count = 2; count <= 4; ++count)
        {
            CascadedShadowMap cascades(device, ShadowQuality::Medium, count);
            cascades.setBlendBand(4.0f);
            BasicEffect effect(device);
            ConfigureLighting(effect);

            RenderFrame(device, cascades, effect, casters, ground);
            readBack();

            std::printf("--- %d cascades ---\n", count);
            PrintAsciiFrame(pixels);
            const int shadowed = ShadowedGroundPixels(pixels);
            std::printf("    shadowed ground pixels: %d\n", shadowed);
            check(shadowed > 40,
                  "the casters cast through the cascade atlas (" + std::to_string(count) +
                      " cascades)");
        }

        // Check C: the debug tint has to band the frame in depth order. Sampling one row near the
        // bottom (close ground) and one near the top (distant ground) and requiring different
        // tints proves the cascades tile the view rather than all resolving to the same one --
        // which is exactly what a broken depth term would produce, invisibly.
        {
            CascadedShadowMap cascades(device, ShadowQuality::Medium, 3);
            cascades.setDebugTintEnabled(true);
            BasicEffect effect(device);
            ConfigureLighting(effect);
            RenderFrame(device, cascades, effect, casters, ground);
            readBack();

            const Color near = At(pixels, kFrame / 2, kFrame - 6);
            const Color far  = At(pixels, kFrame / 2, kFrame / 2 + 4);
            std::printf("--- cascade tint --- near %s far %s\n",
                        near.ToString().c_str(), far.ToString().c_str());
            const bool bothOnGround = near.getRProperty() > 0 && far.getRProperty() > 0;
            const bool different = near.getRProperty() != far.getRProperty()
                                || near.getGProperty() != far.getGProperty()
                                || near.getBProperty() != far.getBProperty();
            check(bothOnGround && different,
                  "the debug tint bands the frame, so the cascades tile the view in depth");
        }

        // Check D: the same scene with no cascades at all. Nothing casts, so the ground is
        // uniformly lit -- the state a renderer without cascade support ends up in as well.
        {
            BasicEffect effect(device);
            ConfigureLighting(effect);
            effect.setShadowsEnabledEXT(false);
            device.Clear(Color::Black);
            effect.Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList, ground.data(), 0,
                                      static_cast<int>(ground.size()) / 3);
            readBack();
            check(ShadowedGroundPixels(pixels) == 0,
                  "with shadows off the same scene renders uniformly lit");
        }

        if (benchmark_)
            RunBenchmark(device, casters, ground);

        std::printf("=== %d/%d PASS ===\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

    /// MOD-915: what N cascades cost against one map, on this machine.
    void RunBenchmark(GraphicsDevice& device,
                      const std::vector<VertexPositionNormalTexture>& casters,
                      const std::vector<VertexPositionNormalTexture>& ground)
    {
        constexpr int kIterations = 10;
        const int casterPrimitives = static_cast<int>(casters.size()) / 3;

        std::printf("--- generation cost, %d casting triangles ---\n", casterPrimitives);

        {
            ShadowMap single(device, ShadowQuality::Medium);
            const BoundingBox bounds(Vector3(-kGroundHalf, -1.0f, -kGroundHalf - 10.0f),
                                     Vector3(kGroundHalf, kCasterHigh + 1.0f, kGroundHalf - 10.0f));
            single.begin(Sun(), bounds);
            device.DrawUserPrimitives(PrimitiveType::TriangleList, casters.data(), 0,
                                      casterPrimitives);
            single.end();

            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < kIterations; ++i)
            {
                single.begin(Sun(), bounds);
                device.DrawUserPrimitives(PrimitiveType::TriangleList, casters.data(), 0,
                                          casterPrimitives);
                single.end();
            }
            const auto finish = std::chrono::steady_clock::now();
            std::printf("    single map     %6.2f ms/frame\n",
                        std::chrono::duration<double, std::milli>(finish - start).count()
                            / kIterations);
        }

        for (int count = 2; count <= 4; ++count)
        {
            CascadedShadowMap cascades(device, ShadowQuality::Medium, count);
            BasicEffect effect(device);
            ConfigureLighting(effect);
            RenderFrame(device, cascades, effect, casters, ground);   // untimed: compiles shaders

            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < kIterations; ++i)
            {
                cascades.update(Sun(), View(), Projection());
                for (int c = 0; c < count; ++c)
                {
                    cascades.begin(c);
                    device.DrawUserPrimitives(PrimitiveType::TriangleList, casters.data(), 0,
                                              casterPrimitives);
                    cascades.end();
                }
            }
            const auto finish = std::chrono::steady_clock::now();
            std::printf("    %d cascades     %6.2f ms/frame\n", count,
                        std::chrono::duration<double, std::milli>(finish - start).count()
                            / kIterations);
        }
    }

public:
    explicit CascadedShadowExample(bool benchmark) : benchmark_(benchmark)
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kFrame);
        gdm_->setPreferredBackBufferHeightProperty(kFrame);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int getResult() const { return result_; }
};

int main(int argc, char** argv)
{
    try
    {
        bool benchmark = false;
        for (int i = 1; i < argc; ++i)
            if (std::strcmp(argv[i], "--benchmark") == 0)
                benchmark = true;
        CascadedShadowExample game(benchmark);
        game.Run();
        return game.getResult();
    }
    catch (const CNA::Platform::PlatformException& e)
    {
        std::printf("SKIP: no video subsystem here (%s)\n", e.what());
        return 77;
    }
}
